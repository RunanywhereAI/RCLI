#include "engines/metalrt_vlm_engine.h"
#include "core/log.h"
#include <chrono>
#include <mutex>

namespace rastack {

bool MetalRTVlmEngine::init(const MetalRTVlmConfig& config) {
    auto& loader = MetalRTLoader::instance();
    if (!loader.is_loaded() && !loader.load()) {
        LOG_ERROR("MetalRT-VLM", "dylib not loaded");
        return false;
    }

    if (!loader.has_vision()) {
        LOG_WARN("MetalRT-VLM", "Vision symbols not available in dylib — "
                 "create=%p analyze=%p",
                 (void*)loader.vision_create, (void*)loader.vision_analyze);
        return false;
    }

    LOG_DEBUG("MetalRT-VLM", "Creating VLM instance via Metal GPU...");
    auto t_start = std::chrono::high_resolution_clock::now();

    handle_ = loader.vision_create();
    if (!handle_) {
        LOG_ERROR("MetalRT-VLM", "Failed to create VLM instance");
        return false;
    }

    LOG_DEBUG("MetalRT-VLM", "Loading model from %s ...", config.model_dir.c_str());
    if (!loader.vision_load(handle_, config.model_dir.c_str())) {
        LOG_ERROR("MetalRT-VLM", "Failed to load model from %s", config.model_dir.c_str());
        loader.vision_destroy(handle_);
        handle_ = nullptr;
        return false;
    }

    config_ = config;

    auto t_end = std::chrono::high_resolution_clock::now();
    double init_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();

    initialized_ = true;

    const char* mname = loader.vision_model_name ? loader.vision_model_name(handle_) : "";
    const char* dname = loader.vision_device_name ? loader.vision_device_name(handle_) : "";

    LOG_DEBUG("MetalRT-VLM", "=== MetalRT VLM GPU VERIFICATION ===");
    LOG_DEBUG("MetalRT-VLM", "  Engine:    VLM via libmetalrt.dylib (Metal GPU)");
    LOG_DEBUG("MetalRT-VLM", "  Model dir: %s", config.model_dir.c_str());
    LOG_DEBUG("MetalRT-VLM", "  Model:     %s", mname);
    LOG_DEBUG("MetalRT-VLM", "  Device:    %s", dname);
    LOG_DEBUG("MetalRT-VLM", "  Init time: %.1f ms", init_ms);
    return true;
}

void MetalRTVlmEngine::shutdown() {
    if (handle_) {
        auto& loader = MetalRTLoader::instance();
        if (loader.vision_destroy) {
            loader.vision_destroy(handle_);
        }
        handle_ = nullptr;
    }
    initialized_ = false;
    stats_ = {};
}

void MetalRTVlmEngine::reset() {
    if (!initialized_ || !handle_) return;
    auto& loader = MetalRTLoader::instance();
    if (loader.vision_reset) {
        std::lock_guard<std::mutex> gpu_lock(loader.gpu_mutex());
        loader.vision_reset(handle_);
    }
}

std::string MetalRTVlmEngine::analyze_image(const std::string& image_path,
                                              const std::string& prompt) {
    if (!initialized_ || !handle_) return "";

    auto& loader = MetalRTLoader::instance();

    LOG_DEBUG("MetalRT-VLM", "analyze_image() → Metal GPU | image=%s prompt=%zu chars",
              image_path.c_str(), prompt.size());

    MetalRTLoader::MetalRTVisionOptions opts = {};
    opts.max_tokens = config_.max_tokens;
    opts.top_k = config_.top_k;
    opts.temperature = config_.temperature;
    opts.think = false;

    auto wall_start = std::chrono::high_resolution_clock::now();
    MetalRTLoader::MetalRTVisionResult result;
    {
        std::lock_guard<std::mutex> gpu_lock(loader.gpu_mutex());
        result = loader.vision_analyze(handle_, image_path.c_str(), prompt.c_str(), &opts);
    }
    auto wall_end = std::chrono::high_resolution_clock::now();
    double wall_ms = std::chrono::duration<double, std::milli>(wall_end - wall_start).count();

    // Store stats
    stats_.vision_encode_ms = result.vision_encode_ms;
    stats_.prefill_ms = result.prefill_ms;
    stats_.decode_ms = result.decode_ms;
    stats_.tps = result.tps;
    stats_.prompt_tokens = result.prompt_tokens;
    stats_.generated_tokens = result.generated_tokens;

    std::string text;
    if (result.response && result.response[0]) {
        text = result.response;
    } else if (result.text && result.text[0]) {
        text = result.text;
    }

    LOG_DEBUG("MetalRT-VLM", "=== VLM ANALYSIS [Metal GPU] ===");
    LOG_DEBUG("MetalRT-VLM", "  Vision encode: %.1f ms", result.vision_encode_ms);
    LOG_DEBUG("MetalRT-VLM", "  Prefill:       %.1f ms (%d tokens)", result.prefill_ms, result.prompt_tokens);
    LOG_DEBUG("MetalRT-VLM", "  Decode:        %.1f ms (%d tokens)", result.decode_ms, result.generated_tokens);
    LOG_DEBUG("MetalRT-VLM", "  TPS:           %.1f tok/s", result.tps);
    LOG_DEBUG("MetalRT-VLM", "  Wall time:     %.1f ms", wall_ms);

    if (loader.vision_free_result)
        loader.vision_free_result(result);

    return text;
}

std::string MetalRTVlmEngine::analyze_image_stream(const std::string& image_path,
                                                     const std::string& prompt,
                                                     TokenCallback on_token) {
    if (!initialized_ || !handle_) return "";

    auto& loader = MetalRTLoader::instance();
    if (!loader.vision_analyze_stream) {
        // Fall back to non-streaming
        return analyze_image(image_path, prompt);
    }

    LOG_DEBUG("MetalRT-VLM", "analyze_image_stream() → Metal GPU | image=%s", image_path.c_str());

    MetalRTLoader::MetalRTVisionOptions opts = {};
    opts.max_tokens = config_.max_tokens;
    opts.top_k = config_.top_k;
    opts.temperature = config_.temperature;
    opts.think = false;

    // Bridge TokenCallback to MetalRTStreamCb
    struct StreamCtx {
        TokenCallback cb;
    };
    StreamCtx ctx{on_token};

    MetalRTStreamCb stream_cb = nullptr;
    if (on_token) {
        stream_cb = [](const char* piece, void* user_data) -> bool {
            auto* sctx = static_cast<StreamCtx*>(user_data);
            if (sctx->cb) {
                TokenOutput tok;
                tok.text = piece;
                sctx->cb(tok);
            }
            return true;  // continue generation
        };
    }

    auto wall_start = std::chrono::high_resolution_clock::now();
    MetalRTLoader::MetalRTVisionResult result;
    {
        std::lock_guard<std::mutex> gpu_lock(loader.gpu_mutex());
        result = loader.vision_analyze_stream(handle_, image_path.c_str(), prompt.c_str(),
                                               stream_cb, &ctx, &opts);
    }
    auto wall_end = std::chrono::high_resolution_clock::now();
    double wall_ms = std::chrono::duration<double, std::milli>(wall_end - wall_start).count();

    stats_.vision_encode_ms = result.vision_encode_ms;
    stats_.prefill_ms = result.prefill_ms;
    stats_.decode_ms = result.decode_ms;
    stats_.tps = result.tps;
    stats_.prompt_tokens = result.prompt_tokens;
    stats_.generated_tokens = result.generated_tokens;

    std::string text;
    if (result.response && result.response[0]) {
        text = result.response;
    } else if (result.text && result.text[0]) {
        text = result.text;
    }

    LOG_DEBUG("MetalRT-VLM", "  Stream complete: %.1f ms, %d tokens, %.1f tok/s",
              wall_ms, result.generated_tokens, result.tps);

    if (loader.vision_free_result)
        loader.vision_free_result(result);

    return text;
}

std::string MetalRTVlmEngine::generate(const std::string& prompt) {
    if (!initialized_ || !handle_) return "";

    auto& loader = MetalRTLoader::instance();
    if (!loader.vision_generate) return "";

    MetalRTLoader::MetalRTVisionOptions opts = {};
    opts.max_tokens = config_.max_tokens;
    opts.top_k = config_.top_k;
    opts.temperature = config_.temperature;
    opts.think = false;

    MetalRTLoader::MetalRTVisionResult result;
    {
        std::lock_guard<std::mutex> gpu_lock(loader.gpu_mutex());
        result = loader.vision_generate(handle_, prompt.c_str(), &opts);
    }

    stats_.vision_encode_ms = result.vision_encode_ms;
    stats_.prefill_ms = result.prefill_ms;
    stats_.decode_ms = result.decode_ms;
    stats_.tps = result.tps;
    stats_.prompt_tokens = result.prompt_tokens;
    stats_.generated_tokens = result.generated_tokens;

    std::string text;
    if (result.response && result.response[0]) {
        text = result.response;
    } else if (result.text && result.text[0]) {
        text = result.text;
    }

    if (loader.vision_free_result)
        loader.vision_free_result(result);

    return text;
}

std::string MetalRTVlmEngine::model_name() const {
    if (!initialized_ || !handle_) return "";
    auto& loader = MetalRTLoader::instance();
    if (!loader.vision_model_name) return "";
    const char* name = loader.vision_model_name(handle_);
    return name ? name : "";
}

std::string MetalRTVlmEngine::device_name() const {
    if (!initialized_ || !handle_) return "";
    auto& loader = MetalRTLoader::instance();
    if (!loader.vision_device_name) return "";
    const char* name = loader.vision_device_name(handle_);
    return name ? name : "";
}

} // namespace rastack