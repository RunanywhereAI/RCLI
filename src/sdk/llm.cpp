#include "sdk/llm.h"

#include <algorithm>
#include <filesystem>
#include <set>
#include <thread>
#include <utility>

#include "chat.pb.h"
#include "llm_service.pb.h"
#include "model_types.pb.h"
#include "vlm_options.pb.h"

#include "rac/core/rac_core.h"
#include "rac/core/rac_model_lifecycle.h"
#include "rac/features/llm/rac_llm_service.h"
#include "rac/features/vlm/rac_vlm_service.h"
#include "rac/foundation/rac_proto_buffer.h"

#include "catalog/catalog.h"
#include "sdk/install.h"
#include "sdk/session.h"
#include "settings/settings.h"

namespace rcli::sdk {
namespace {

namespace fs = std::filesystem;
namespace v1 = runanywhere::v1;

bool IsWeightFile(const fs::path& path) {
    const std::string ext = path.extension().string();
    return ext == ".gguf" || ext == ".safetensors" || ext == ".onnx" || ext == ".bin";
}

/// Written by the download orchestrator as files land rather than once they
/// all have, so a partial carries one too. Presence means "this came from a
/// download", not "the download finished".
constexpr std::string_view kManifest = ".rac-manifest.binpb";

bool IsArchive(std::string_view name) {
    for (std::string_view suffix : {".zip", ".tar", ".gz", ".tgz", ".bz2", ".xz"}) {
        if (name.size() > suffix.size() &&
            name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
            return true;
        }
    }
    return false;
}

/// Whether every file the catalog lists for this model is on disk.
///
/// Not a size comparison: the catalog's byte count is documented as
/// approximate, so a correct install can sit well under it and a half-finished
/// one can sit near it. Filenames are exact. Archives are exempt because what
/// lands is the extracted tree, which shares no name with the download.
bool IsComplete(const std::string& id, const fs::path& dir) {
    const catalog::Model* entry = nullptr;
    for (const catalog::Model& model : catalog::All()) {
        if (model.id == id) {
            entry = &model;
            break;
        }
    }
    if (entry == nullptr || entry->files.empty()) {
        return true;
    }
    std::error_code ec;
    std::set<std::string> present;
    for (const auto& file : fs::recursive_directory_iterator(dir, ec)) {
        if (file.is_regular_file(ec)) {
            present.insert(file.path().filename().string());
        }
    }
    for (const catalog::File& wanted : entry->files) {
        if (!wanted.required || IsArchive(wanted.filename)) {
            continue;
        }
        if (present.count(std::string(wanted.filename)) == 0) {
            return false;
        }
    }
    return true;
}

/// The stream callback is a plain C function pointer with one void*, so the
/// per-generation state travels through it.
struct Stream {
    std::function<void(Piece, std::string)> on_piece;
    std::string failure;
    Metrics metrics;
};

Stream* g_stream = nullptr;

void OnEvent(const uint8_t* bytes, std::size_t size, void* /*user_data*/) {
    Stream* stream = g_stream;
    if (stream == nullptr) {
        return;
    }
    v1::LLMStreamEvent event;
    if (!event.ParseFromArray(bytes, static_cast<int>(size))) {
        return;
    }
    if (!event.token().empty() && stream->on_piece) {
        stream->on_piece(event.event_kind() == v1::LLM_STREAM_EVENT_KIND_THINKING ? Piece::Thinking
                                                                                  : Piece::Answer,
                         event.token());
    }
    if (event.has_result() && event.result().has_usage()) {
        const v1::TokenUsage& usage = event.result().usage();
        stream->metrics = Metrics{usage.ttft_ms(),
                                  usage.prefill_ms(),
                                  usage.decode_tokens_per_second(),
                                  usage.input_tokens(),
                                  usage.output_tokens(),
                                  usage.counts_estimated()};
    }
    if (event.has_error() && !event.error().message().empty()) {
        stream->failure = event.error().message();
    }
}

/// The vision stream has its own event type and its callback returns a bool to
/// keep going, so it cannot share OnEvent.
rac_bool_t OnVisionEvent(const uint8_t* bytes, std::size_t size, void* /*user_data*/) {
    Stream* stream = g_stream;
    if (stream == nullptr) {
        return RAC_FALSE;
    }
    v1::VLMStreamEvent event;
    if (!event.ParseFromArray(bytes, static_cast<int>(size))) {
        return RAC_TRUE;
    }
    if (!event.token().empty() && stream->on_piece) {
        stream->on_piece(Piece::Answer, event.token());
    }
    if (event.has_error() && !event.error().message().empty()) {
        stream->failure = event.error().message();
    }
    return RAC_TRUE;
}

/// Single-turn on purpose: commons' VLM module reads `prompt` and never looks
/// at `messages`, so filling the conversation in would be a request field that
/// nothing reads. An image question stands on its own until that changes.
rac_result_t StreamVision(const std::string& model, const std::string& prompt,
                          const std::string& image_path) {
    v1::VLMGenerationRequest request;
    request.set_model_id(model);
    request.add_images()->set_file_path(image_path);
    request.set_prompt(prompt);

    v1::LLMGenerationOptions* options = request.mutable_options();
    options->set_temperature(settings::Temperature());
    options->set_max_output_tokens(settings::MaxTokens());

    std::string bytes;
    if (!request.SerializeToString(&bytes)) {
        return RAC_ERROR_INVALID_PARAMETER;
    }
    const rac_result_t rc = rac_vlm_stream_proto(reinterpret_cast<const uint8_t*>(bytes.data()),
                                                 bytes.size(), OnVisionEvent, nullptr);
    // The dispatcher can still be inside the callback when the call returns;
    // g_stream must outlive that, so wait for the drain before clearing it.
    rac_vlm_proto_quiesce();
    return rc;
}

}  // namespace

std::vector<LocalModel> LocalModels() {
    std::vector<LocalModel> models;
    const std::string home(Session::Instance().home());
    if (home.empty()) {
        return models;
    }
    // Both layouts are real: the SDK's path docs describe {base}/RunAnywhere/
    // Models, and the desktop default base dir already ends in "runanywhere" so
    // models land directly under {base}/Models.
    std::error_code ec;
    fs::path root = fs::path(home) / "RunAnywhere" / "Models";
    if (!fs::is_directory(root, ec)) {
        root = fs::path(home) / "Models";
    }
    if (!fs::is_directory(root, ec)) {
        return models;
    }
    for (const auto& framework : fs::directory_iterator(root, ec)) {
        if (!framework.is_directory()) {
            continue;
        }
        for (const auto& entry : fs::directory_iterator(framework.path(), ec)) {
            if (!entry.is_directory()) {
                continue;
            }
            std::string weights;
            bool manifest = false;
            std::int64_t bytes = 0;
            for (const auto& file : fs::recursive_directory_iterator(entry.path(), ec)) {
                if (!file.is_regular_file()) {
                    continue;
                }
                bytes += static_cast<std::int64_t>(file.file_size(ec));
                if (file.path().filename() == kManifest) {
                    manifest = true;
                } else if (weights.empty() && IsWeightFile(file.path())) {
                    weights = file.path().string();
                }
            }
            // A directory with weights but no manifest was placed by hand; it
            // is still loadable, so it still counts.
            if (!manifest && weights.empty()) {
                continue;
            }
            const std::string id = entry.path().filename().string();
            models.push_back({id, framework.path().filename().string(), entry.path().string(),
                              weights, bytes, IsComplete(id, entry.path())});
        }
    }
    std::sort(models.begin(), models.end(),
              [](const LocalModel& a, const LocalModel& b) { return a.id < b.id; });
    return models;
}

bool Remove(const std::string& id, std::int64_t* freed, std::string* error) {
    auto fail = [error](std::string message) {
        if (error != nullptr) {
            *error = std::move(message);
        }
        return false;
    };
    if (id.empty() || id.find('/') != std::string::npos || id == "." || id == "..") {
        return fail("not a model id: " + id);
    }
    const std::string home(Session::Instance().home());
    if (home.empty()) {
        return fail("no storage root");
    }
    std::error_code ec;
    fs::path root = fs::path(home) / "RunAnywhere" / "Models";
    if (!fs::is_directory(root, ec)) {
        root = fs::path(home) / "Models";
    }
    for (const auto& framework : fs::directory_iterator(root, ec)) {
        if (!framework.is_directory()) {
            continue;
        }
        const fs::path target = framework.path() / id;
        if (!fs::is_directory(target, ec)) {
            continue;
        }
        // The path was built from the root down rather than taken from a
        // caller, and the id was checked for separators above; this is the
        // belt to that braces.
        if (target.parent_path().parent_path() != root) {
            return fail("refusing to delete outside the models folder");
        }
        std::int64_t bytes = 0;
        for (const auto& file : fs::recursive_directory_iterator(target, ec)) {
            if (file.is_regular_file(ec)) {
                bytes += static_cast<std::int64_t>(file.file_size(ec));
            }
        }
        fs::remove_all(target, ec);
        if (ec) {
            return fail("could not delete " + target.string() + ": " + ec.message());
        }
        if (freed != nullptr) {
            *freed = bytes;
        }
        return true;
    }
    return fail(id + " is not downloaded");
}

bool Llm::Load(const std::string& id, std::string* error) {
    // Registering first is what lets the lifecycle resolve the id; it is
    // idempotent, and for a model already on disk it costs nothing.
    for (const catalog::Model& model : catalog::All()) {
        if (model.id == id && catalog::Installable(model)) {
            Register(model, nullptr);
            break;
        }
    }

    v1::ModelLoadRequest request;
    request.set_model_id(id);
    request.set_validate_availability(true);
    if (const int context = settings::ContextLength(); context > 0) {
        request.set_context_length(context);
    }
    const std::string accelerator = settings::Accelerator();
    if (accelerator == "cpu") {
        request.set_accelerator_policy(v1::ACCELERATOR_POLICY_CPU);
    } else if (accelerator == "gpu") {
        request.set_accelerator_policy(v1::ACCELERATOR_POLICY_GPU);
    } else if (accelerator == "npu") {
        request.set_accelerator_policy(v1::ACCELERATOR_POLICY_NPU);
    } else if (accelerator == "auto") {
        request.set_accelerator_policy(v1::ACCELERATOR_POLICY_AUTO);
    }
    const std::string engine = settings::Engine();
    if (engine == "llamacpp") {
        request.set_framework(v1::INFERENCE_FRAMEWORK_LLAMA_CPP);
    } else if (engine == "mlx") {
        request.set_framework(v1::INFERENCE_FRAMEWORK_MLX);
    } else if (engine == "neurt") {
        request.set_framework(v1::INFERENCE_FRAMEWORK_COREML);
    } else if (engine == "onnx") {
        request.set_framework(v1::INFERENCE_FRAMEWORK_ONNX);
    } else if (engine == "sherpa") {
        request.set_framework(v1::INFERENCE_FRAMEWORK_SHERPA);
    }

    std::string bytes;
    if (!request.SerializeToString(&bytes)) {
        if (error != nullptr) {
            *error = "could not serialize the load request";
        }
        return false;
    }
    rac_proto_buffer_t out;
    rac_proto_buffer_init(&out);
    const rac_result_t rc = rac_model_lifecycle_load_proto(
        rac_get_model_registry(), reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(),
        &out);

    bool ok = rc == RAC_SUCCESS;
    std::string message;
    if (ok) {
        v1::ModelLoadResult result;
        if (result.ParseFromArray(out.data, static_cast<int>(out.size))) {
            if (result.has_error()) {
                ok = false;
                message = result.error().message();
            }
            // Commons reports here when a load knob went to an engine that may
            // not honour it. Worth surfacing rather than dropping.
            for (const std::string& warning : result.warnings()) {
                message = warning;
            }
        }
    } else {
        message = "load failed (" + std::to_string(rc) + ")";
    }
    rac_proto_buffer_free(&out);

    if (error != nullptr) {
        *error = message;
    }
    if (ok) {
        model_id_ = id;
        multimodal_ = false;
        for (const catalog::Model& model : catalog::All()) {
            if (model.id == id) {
                multimodal_ = model.category == catalog::Category::Multimodal;
                break;
            }
        }
    }
    return ok;
}

bool Llm::Generate(std::string prompt, std::vector<Turn> history, std::string image_path,
                   std::function<void(Piece, std::string)> on_piece,
                   std::function<void(std::string, Metrics)> on_done) {
    if (model_id_.empty() || busy_->load()) {
        return false;
    }
    busy_->store(true);

    // Never captures `this`: a detached worker can outlive the screen.
    std::thread([busy = busy_, model = model_id_, prompt = std::move(prompt),
                 history = std::move(history), image = std::move(image_path),
                 on_piece = std::move(on_piece), on_done = std::move(on_done)]() mutable {
        if (!image.empty()) {
            Stream stream;
            stream.on_piece = std::move(on_piece);
            g_stream = &stream;
            const rac_result_t rc = StreamVision(model, prompt, image);
            g_stream = nullptr;
            if (rc != RAC_SUCCESS && stream.failure.empty()) {
                stream.failure = "the vision path failed (" + std::to_string(rc) + ")";
            }
            busy->store(false);
            if (on_done) {
                on_done(stream.failure, stream.metrics);
            }
            return;
        }

        v1::LLMGenerateRequest request;
        request.set_model_id(model);
        for (const Turn& turn : history) {
            v1::ChatMessage* message = request.add_messages();
            message->set_role(turn.from_user ? v1::MESSAGE_ROLE_USER : v1::MESSAGE_ROLE_ASSISTANT);
            message->set_content(turn.text);
        }
        v1::ChatMessage* current = request.add_messages();
        current->set_role(v1::MESSAGE_ROLE_USER);
        current->set_content(prompt);

        v1::LLMGenerationOptions* options = request.mutable_options();
        options->set_temperature(settings::Temperature());
        options->set_max_output_tokens(settings::MaxTokens());
        v1::ReasoningOptions* reasoning = options->mutable_reasoning();
        const std::string mode = settings::Reasoning();
        reasoning->set_mode(mode == "on"    ? v1::REASONING_MODE_ON
                            : mode == "off" ? v1::REASONING_MODE_OFF
                                            : v1::REASONING_MODE_UNSPECIFIED);
        // Without this the reasoning is stripped before it reaches us, and the
        // collapsed thought block in the transcript would never have content.
        reasoning->set_include_in_output(true);

        std::string bytes;
        if (!request.SerializeToString(&bytes)) {
            busy->store(false);
            if (on_done) {
                on_done("could not serialize the request", Metrics{});
            }
            return;
        }

        Stream stream;
        stream.on_piece = std::move(on_piece);
        g_stream = &stream;
        const rac_result_t rc = rac_llm_generate_stream_proto(
            reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(), OnEvent, nullptr);
        g_stream = nullptr;

        if (rc != RAC_SUCCESS && stream.failure.empty()) {
            stream.failure = "generation failed (" + std::to_string(rc) + ")";
        }
        busy->store(false);
        if (on_done) {
            on_done(stream.failure, stream.metrics);
        }
    }).detach();
    return true;
}

void Llm::Cancel() {
    rac_proto_buffer_t out;
    rac_proto_buffer_init(&out);
    rac_llm_cancel_proto(&out);
    rac_proto_buffer_free(&out);
}

}  // namespace rcli::sdk
