#include "sdk/download.h"

#include <utility>

#include "download_service.pb.h"
#include "model_types.pb.h"

#include "rac/core/rac_core.h"
#include "rac/foundation/rac_proto_buffer.h"
#include "rac/infrastructure/download/rac_download_orchestrator.h"
#include "rac/infrastructure/model_management/rac_model_registry.h"

#include "sdk/install.h"
#include "sdk/llm.h"

namespace rcli::sdk {
namespace {

namespace v1 = runanywhere::v1;

Phase PhaseOf(v1::DownloadState state) {
    switch (state) {
        case v1::DOWNLOAD_STATE_PENDING: return Phase::Pending;
        case v1::DOWNLOAD_STATE_DOWNLOADING:
        case v1::DOWNLOAD_STATE_RETRYING: return Phase::Running;
        case v1::DOWNLOAD_STATE_EXTRACTING: return Phase::Extracting;
        case v1::DOWNLOAD_STATE_COMPLETED: return Phase::Done;
        case v1::DOWNLOAD_STATE_CANCELLED: return Phase::Cancelled;
        case v1::DOWNLOAD_STATE_FAILED: return Phase::Failed;
        default: return Phase::Pending;
    }
}

/// Serialize into a string the orchestrator can read, then run `call`.
template <typename Request, typename Result, typename Fn>
bool Roundtrip(const Request& request, Result* result, Fn call) {
    std::string bytes;
    if (!request.SerializeToString(&bytes)) {
        return false;
    }
    rac_proto_buffer_t out;
    rac_proto_buffer_init(&out);
    const rac_result_t rc =
        call(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size(), &out);
    const bool ok =
        rc == RAC_SUCCESS && result->ParseFromArray(out.data, static_cast<int>(out.size));
    rac_proto_buffer_free(&out);
    return ok;
}

}  // namespace

void OnProgress(const std::uint8_t* bytes, std::size_t size, void* /*user_data*/) {
    v1::DownloadProgress progress;
    if (!progress.ParseFromArray(bytes, static_cast<int>(size)) || progress.model_id().empty()) {
        return;
    }
    Downloads& downloads = Downloads::Instance();
    std::vector<std::function<void()>> notify;
    std::string failed;
    {
        const std::lock_guard<std::mutex> lock(downloads.mutex_);
        Download& entry = downloads.state_[progress.model_id()];
        entry.phase = PhaseOf(progress.state());
        entry.fraction = progress.overall_progress();
        entry.bytes = progress.bytes_downloaded();
        entry.total = progress.total_bytes();
        entry.bytes_per_second = progress.bytes_per_second();
        if (entry.phase == Phase::Failed) {
            entry.detail = progress.error().message().empty() ? "download failed"
                                                              : progress.error().message();
            // The partial is cleared below, so the retry is the useful half of
            // the message: without it this reads as a permanent condition.
            entry.detail += " — the partial file was removed, try again";
            // Whatever landed is partial or corrupt, and leaving it means the
            // next attempt resumes onto it and fails the same way — which is
            // exactly how "archive extraction failed" becomes permanent.
            failed = progress.model_id();
        } else {
            entry.detail = progress.current_file_name();
        }
        notify = downloads.listeners_;
        downloads.settled_.notify_all();
    }
    if (!failed.empty()) {
        std::int64_t freed = 0;
        Remove(failed, &freed, nullptr);
    }
    for (const std::function<void()>& listener : notify) {
        listener();
    }
}

Downloads::Downloads() {
    rac_download_set_progress_proto_callback(OnProgress, nullptr);
}

Downloads& Downloads::Instance() {
    static Downloads downloads;
    return downloads;
}

bool Downloads::Start(const catalog::Model& model, std::string* error) {
    const std::string id(model.id);
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        const auto it = state_.find(id);
        if (it != state_.end() &&
            (it->second.phase == Phase::Pending || it->second.phase == Phase::Running ||
             it->second.phase == Phase::Extracting)) {
            return true;
        }
        state_[id] = Download{Phase::Pending, 0.0F, 0, model.bytes, 0.0F, ""};
    }

    // The plan is what validates the entry and reports a refusal with a reason;
    // starting without one would turn "no space" into a generic failure.
    if (!Register(model, error)) {
        const std::lock_guard<std::mutex> lock(mutex_);
        settled_.notify_all();
        state_[id] = Download{Phase::Failed, 0.0F, 0, 0, 0.0F, error != nullptr ? *error : ""};
        return false;
    }

    // The orchestrator plans from the metadata carried in the request, not from
    // the registry, so the saved entry has to be read back and attached.
    v1::DownloadPlanRequest plan_request;
    plan_request.set_model_id(id);
    {
        rac_proto_buffer_t saved;
        rac_proto_buffer_init(&saved);
        const rac_result_t rc =
            rac_model_registry_get_proto_buffer(rac_get_model_registry(), id.c_str(), &saved);
        const bool ok = rc == RAC_SUCCESS && saved.status == RAC_SUCCESS &&
                        plan_request.mutable_model()->ParseFromArray(
                            saved.data, static_cast<int>(saved.size));
        rac_proto_buffer_free(&saved);
        if (!ok) {
            const std::string message = "the registered entry for " + id + " could not be read";
            if (error != nullptr) {
                *error = message;
            }
            const std::lock_guard<std::mutex> lock(mutex_);
            state_[id] = Download{Phase::Failed, 0.0F, 0, 0, 0.0F, message};
            return false;
        }
    }
    v1::DownloadPlanResult plan;
    const bool planned = Roundtrip(plan_request, &plan, rac_download_plan_proto);
    // A URL that points at a web page instead of an artifact answers 200 with a
    // few hundred KB of HTML, and every layer below treats that as a completed
    // download: the bytes land, a manifest is written, and the model reports
    // itself installed. Catching it here is the difference between a clear
    // failure and a model that is silently 164 KB of markup.
    if (planned && plan.can_start() && model.bytes > 0 && plan.total_bytes() > 0 &&
        plan.total_bytes() < model.bytes / 2) {
        const std::string message = "the server offered " + catalog::HumanSize(plan.total_bytes()) +
                                    " but the catalog says " + catalog::HumanSize(model.bytes) +
                                    " — the download URL looks wrong";
        if (error != nullptr) {
            *error = message;
        }
        const std::lock_guard<std::mutex> lock(mutex_);
        settled_.notify_all();
        state_[id] = Download{Phase::Failed, 0.0F, 0, 0, 0.0F, message};
        return false;
    }
    if (!planned || !plan.can_start()) {
        std::string message = "download plan rejected";
        if (planned && !plan.error().message().empty()) {
            message = plan.error().message();
        }
        if (error != nullptr) {
            *error = message;
        }
        const std::lock_guard<std::mutex> lock(mutex_);
        settled_.notify_all();
        state_[id] = Download{Phase::Failed, 0.0F, 0, 0, 0.0F, message};
        return false;
    }

    v1::DownloadStartRequest start_request;
    start_request.set_model_id(id);
    *start_request.mutable_plan() = plan;
    v1::DownloadStartResult start;
    if (!Roundtrip(start_request, &start, rac_download_start_proto) || !start.accepted()) {
        std::string message =
            start.error().message().empty() ? "download refused" : start.error().message();
        if (error != nullptr) {
            *error = message;
        }
        const std::lock_guard<std::mutex> lock(mutex_);
        settled_.notify_all();
        state_[id] = Download{Phase::Failed, 0.0F, 0, 0, 0.0F, std::move(message)};
        return false;
    }
    return true;
}

void Downloads::Cancel(const std::string& id) {
    v1::DownloadCancelRequest request;
    request.set_model_id(id);
    v1::DownloadCancelResult result;
    Roundtrip(request, &result, rac_download_cancel_proto);
}

Download Downloads::Get(const std::string& id) const {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto it = state_.find(id);
    return it == state_.end() ? Download{} : it->second;
}

bool Downloads::Busy() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [id, entry] : state_) {
        if (entry.phase == Phase::Pending || entry.phase == Phase::Running ||
            entry.phase == Phase::Extracting) {
            return true;
        }
    }
    return false;
}

void Downloads::Listen(std::function<void()> handler) {
    const std::lock_guard<std::mutex> lock(mutex_);
    listeners_.push_back(std::move(handler));
}

std::vector<Downloads::Active> Downloads::InFlight() const {
    std::vector<Active> active;
    const std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [id, entry] : state_) {
        if (entry.phase == Phase::Pending || entry.phase == Phase::Running ||
            entry.phase == Phase::Extracting) {
            active.push_back({id, entry.phase, entry.fraction});
        }
    }
    return active;
}

Phase Downloads::Await(const std::string& id) {
    std::unique_lock<std::mutex> lock(mutex_);
    settled_.wait(lock, [this, &id] {
        const auto it = state_.find(id);
        return it == state_.end() || (it->second.phase != Phase::Pending &&
                                      it->second.phase != Phase::Running &&
                                      it->second.phase != Phase::Extracting);
    });
    const auto it = state_.find(id);
    return it == state_.end() ? Phase::Idle : it->second.phase;
}

}  // namespace rcli::sdk
