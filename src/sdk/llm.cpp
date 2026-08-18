#include "sdk/llm.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <thread>
#include <utility>

#include "rac/features/llm/rac_llm_component.h"
#include "rac/features/llm/rac_llm_types.h"

#include "sdk/session.h"

namespace rcli::sdk {
namespace {

namespace fs = std::filesystem;

/// Weights, as opposed to the tokenizer and config files sitting beside them.
bool IsWeightFile(const fs::path& path) {
    const std::string ext = path.extension().string();
    return ext == ".gguf" || ext == ".safetensors" || ext == ".bin";
}

/// One user_data is shared by all three callbacks, so it carries everything
/// they need rather than each pointing at its own object.
struct Streaming {
    std::function<void(std::string)> on_token;
    std::atomic<bool> cancelled{false};
    std::string failure;
};

rac_bool_t OnToken(const char* token, void* user_data) {
    auto* streaming = static_cast<Streaming*>(user_data);
    if (streaming->cancelled.load()) {
        return RAC_FALSE;  // stops generation
    }
    if (token != nullptr && streaming->on_token) {
        streaming->on_token(token);
    }
    return RAC_TRUE;
}

}  // namespace

std::vector<LocalModel> LocalModels() {
    std::vector<LocalModel> models;
    const std::string home(Session::Instance().home());
    if (home.empty()) {
        return models;
    }
    // Both layouts are real: the SDK's path docs describe
    // {base}/RunAnywhere/Models, and the desktop default base dir already ends
    // in "runanywhere" so models land directly under {base}/Models. Checking
    // both means a home written by any RunAnywhere app is readable here.
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
            // The first weight file in the model's own directory. A model whose
            // download was interrupted has none, and is correctly not listed.
            for (const auto& file : fs::directory_iterator(entry.path(), ec)) {
                if (file.is_regular_file() && IsWeightFile(file.path())) {
                    models.push_back({entry.path().filename().string(),
                                      framework.path().filename().string(),
                                      file.path().string()});
                    break;
                }
            }
        }
    }
    std::sort(models.begin(), models.end(),
              [](const LocalModel& a, const LocalModel& b) { return a.id < b.id; });
    return models;
}

Llm::~Llm() {
    if (handle_ != nullptr) {
        rac_llm_component_destroy(static_cast<rac_handle_t>(handle_));
    }
}

bool Llm::Load(const LocalModel& model, std::string* error) {
    if (handle_ == nullptr) {
        rac_handle_t handle = nullptr;
        if (rac_llm_component_create(&handle) != RAC_SUCCESS || handle == nullptr) {
            if (error != nullptr) {
                *error = "could not create the LLM component";
            }
            return false;
        }
        handle_ = handle;
    }
    const rac_result_t rc = rac_llm_component_load_model(
        static_cast<rac_handle_t>(handle_), model.path.c_str(), model.id.c_str(), nullptr);
    if (rc != RAC_SUCCESS) {
        if (error != nullptr) {
            *error = "failed to load " + model.id + " (" + std::to_string(rc) + ")";
        }
        return false;
    }
    model_id_ = model.id;
    return true;
}

bool Llm::Generate(std::string prompt, std::function<void(std::string)> on_token,
                   std::function<void(std::string)> on_done) {
    if (handle_ == nullptr || busy_->load()) {
        return false;
    }
    busy_->store(true);

    // The worker captures handles by value, never `this`: a detached thread can
    // outlive the screen that started it, and reaching back into a destroyed
    // object is the crash that shows up as an intermittent segfault.
    auto work = [handle = handle_, busy = busy_, prompt = std::move(prompt),
                 on_token = std::move(on_token), on_done = std::move(on_done)]() mutable {
        auto streaming = std::make_shared<Streaming>();
        streaming->on_token = std::move(on_token);

        rac_llm_options_t options = RAC_LLM_OPTIONS_DEFAULT;
        const rac_result_t rc = rac_llm_component_generate_stream(
            static_cast<rac_handle_t>(handle), prompt.c_str(), &options, OnToken, nullptr,
            [](rac_result_t code, const char* message, void* user_data) {
                auto* state = static_cast<Streaming*>(user_data);
                state->failure = message != nullptr
                                     ? message
                                     : "generation failed (" + std::to_string(code) + ")";
            },
            streaming.get());
        if (rc != RAC_SUCCESS && streaming->failure.empty()) {
            streaming->failure = "generation failed (" + std::to_string(rc) + ")";
        }
        busy->store(false);
        if (on_done) {
            on_done(streaming->failure);
        }
    };
    // RCLI_SYNC_GEN runs generation on the calling thread. Only for isolating
    // whether a fault belongs to the worker thread or to the SDK call.
    if (std::getenv("RCLI_SYNC_GEN") != nullptr) {
        work();
    } else {
        std::thread(std::move(work)).detach();
    }
    return true;
}

void Llm::Cancel() {
    rac_llm_component_cancel(static_cast<rac_handle_t>(handle_));
}

}  // namespace rcli::sdk
