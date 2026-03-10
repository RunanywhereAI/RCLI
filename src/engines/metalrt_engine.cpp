#include "engines/metalrt_engine.h"
#include "core/log.h"
#include <chrono>
#include <fstream>

static rastack::ModelFamily detect_metalrt_family(const std::string& model_dir) {
    std::string config_path = model_dir + "/config.json";
    std::ifstream f(config_path);
    if (!f.is_open()) return rastack::ModelFamily::QWEN3;

    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());

    auto find_value = [&](const std::string& key) -> std::string {
        auto pos = content.find("\"" + key + "\"");
        if (pos == std::string::npos) return "";
        pos = content.find(':', pos);
        if (pos == std::string::npos) return "";
        auto start = content.find('"', pos + 1);
        if (start == std::string::npos) return "";
        auto end = content.find('"', start + 1);
        if (end == std::string::npos) return "";
        return content.substr(start + 1, end - start - 1);
    };

    // Try "family" field first (MetalRT native format)
    std::string family = find_value("family");
    if (family == "Qwen3")    return rastack::ModelFamily::QWEN3;
    if (family == "Llama-3.2") return rastack::ModelFamily::LLAMA3;
    if (family == "LFM2.5")   return rastack::ModelFamily::LFM2;

    // Fall back to HuggingFace "model_type" field
    std::string model_type = find_value("model_type");
    if (model_type == "lfm2")  return rastack::ModelFamily::LFM2;
    if (model_type == "qwen3") return rastack::ModelFamily::QWEN3;
    if (model_type == "llama") return rastack::ModelFamily::LLAMA3;

    return rastack::ModelFamily::QWEN3;
}

namespace rastack {

void MetalRTEngine::record_stats(const MetalRTResult& result, int64_t elapsed_us) {
    stats_.prompt_tokens = result.prompt_tokens;
    stats_.generated_tokens = result.generated_tokens;
    stats_.prompt_eval_us = (int64_t)(result.prefill_ms * 1000.0);
    stats_.generation_us = (int64_t)(result.decode_ms * 1000.0);
    if (stats_.generation_us == 0) stats_.generation_us = elapsed_us;
    stats_.first_token_us = stats_.prompt_eval_us;
}

std::string MetalRTEngine::extract_and_clean(MetalRTResult& result) {
    auto& loader = MetalRTLoader::instance();

    std::string text;
    if (result.response && result.response[0]) {
        text = result.response;
    } else if (result.text) {
        text = result.text;
    }

    if (loader.free_result) loader.free_result(result);

    static const char* eos_markers[] = {
        "<|im_end|>", "<|eot_id|>", "<|end|>", "</s>", nullptr
    };
    for (const char** m = eos_markers; *m; ++m) {
        auto pos = text.rfind(*m);
        if (pos != std::string::npos) {
            text.erase(pos);
        }
    }
    while (!text.empty() && (text.back() == '\n' || text.back() == ' '))
        text.pop_back();

    return text;
}

MetalRTEngine::~MetalRTEngine() {
    shutdown();
}

bool MetalRTEngine::init(const MetalRTEngineConfig& config) {
    if (initialized_) shutdown();

    auto& loader = MetalRTLoader::instance();
    if (!loader.is_loaded() && !loader.load()) {
        LOG_ERROR("MetalRT", "engine not available%s", "");
        return false;
    }

    handle_ = loader.create();
    if (!handle_) {
        LOG_ERROR("MetalRT", "metalrt_create() failed%s", "");
        return false;
    }

    if (!loader.load_model(handle_, config.model_dir.c_str())) {
        LOG_ERROR("MetalRT", "metalrt_load() failed for %s", config.model_dir.c_str());
        loader.destroy(handle_);
        handle_ = nullptr;
        return false;
    }

    config_ = config;
    profile_ = ModelProfile::from_family(detect_metalrt_family(config.model_dir));
    initialized_ = true;
    cancelled_.store(false);

    std::string dev = device_name();
    LOG_DEBUG("MetalRT", "=== MetalRT LLM GPU VERIFICATION ===");
    LOG_DEBUG("MetalRT", "  Model:       %s", model_name().c_str());
    LOG_DEBUG("MetalRT", "  Device:      %s", dev.c_str());
    LOG_DEBUG("MetalRT", "  Profile:     %s", profile_.family_name.c_str());
    LOG_DEBUG("MetalRT", "  Model dir:   %s", config.model_dir.c_str());
    LOG_DEBUG("MetalRT", "  Thinking:    %s", supports_thinking() ? "yes" : "no");
    LOG_DEBUG("MetalRT", "  Backend:     libmetalrt.dylib (Metal GPU)");
    if (dev.empty() || dev.find("CPU") != std::string::npos) {
        LOG_WARN("MetalRT", "WARNING: device_name() returned '%s' — GPU may NOT be active!", dev.c_str());
    }
    return true;
}

void MetalRTEngine::shutdown() {
    if (handle_) {
        auto& loader = MetalRTLoader::instance();
        if (loader.destroy) loader.destroy(handle_);
        handle_ = nullptr;
    }
    initialized_ = false;
    prompt_cached_ = false;
    cached_system_prompt_.clear();
    stats_ = {};
}

std::string MetalRTEngine::generate(const std::string& prompt, TokenCallback on_token) {
    if (!initialized_ || !handle_) return "";
    cancelled_.store(false);

    auto& loader = MetalRTLoader::instance();

    MetalRTOptions opts{};
    opts.max_tokens = config_.max_tokens;
    opts.top_k = config_.top_k;
    opts.temperature = config_.temperature;
    opts.think = config_.think;
    opts.ignore_eos = ignore_eos_;

    LOG_DEBUG("MetalRT", "generate() called — dispatching to Metal GPU (stream=%s, max_tokens=%d, temp=%.2f)",
              (on_token && loader.generate_stream) ? "yes" : "no", opts.max_tokens, opts.temperature);

    auto t0 = std::chrono::high_resolution_clock::now();

    MetalRTResult result;

    {
        std::lock_guard<std::mutex> gpu_lock(loader.gpu_mutex());

        if (on_token && loader.generate_stream) {
            struct StreamCtx {
                TokenCallback* cb;
                std::atomic<bool>* cancelled;
            };
            StreamCtx ctx{&on_token, &cancelled_};

            result = loader.generate_stream(
                handle_, prompt.c_str(),
                [](const char* piece, void* ud) -> bool {
                    auto* c = static_cast<StreamCtx*>(ud);
                    if (c->cancelled->load(std::memory_order_acquire)) return false;
                    if (c->cb && *c->cb) {
                        TokenOutput tok;
                        tok.text = piece ? piece : "";
                        tok.token_id = -1;
                        tok.is_eos = false;
                        tok.is_tool_call = false;
                        (*c->cb)(tok);
                    }
                    return true;
                },
                &ctx,
                reinterpret_cast<const MetalRTOptions*>(&opts));
        } else if (loader.generate) {
            result = loader.generate(
                handle_, prompt.c_str(),
                reinterpret_cast<const MetalRTOptions*>(&opts));
        } else {
            return "";
        }
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();

    record_stats(result, elapsed_us);

    double total_ms = elapsed_us / 1000.0;
    double tps = (stats_.generated_tokens > 0 && total_ms > 0)
        ? (stats_.generated_tokens * 1000.0 / total_ms) : 0;
    LOG_DEBUG("MetalRT", "generate() complete — %d prompt tok, %d gen tok, "
              "prefill=%.1fms, decode=%.1fms, total=%.1fms, %.1f tok/s [Metal GPU]",
              result.prompt_tokens, result.generated_tokens,
              result.prefill_ms, result.decode_ms, total_ms, tps);

    return extract_and_clean(result);
}

std::string MetalRTEngine::generate_raw(const std::string& formatted_prompt, TokenCallback on_token) {
    if (!initialized_ || !handle_) return "";

    auto& loader = MetalRTLoader::instance();

    // Fall back to regular generate if raw symbols aren't available
    if (!loader.generate_raw) {
        LOG_WARN("MetalRT", "generate_raw not available in dylib, falling back to generate()");
        return generate(formatted_prompt, on_token);
    }

    cancelled_.store(false);

    MetalRTOptions opts{};
    opts.max_tokens = config_.max_tokens;
    opts.top_k = config_.top_k;
    opts.temperature = config_.temperature;
    opts.think = config_.think;
    opts.ignore_eos = ignore_eos_;

    LOG_DEBUG("MetalRT", "generate_raw() called — dispatching pre-formatted prompt to Metal GPU (stream=%s, max_tokens=%d)",
              (on_token && loader.generate_raw_stream) ? "yes" : "no", opts.max_tokens);

    auto t0 = std::chrono::high_resolution_clock::now();

    MetalRTResult result;

    {
        std::lock_guard<std::mutex> gpu_lock(loader.gpu_mutex());

        if (on_token && loader.generate_raw_stream) {
            struct StreamCtx {
                TokenCallback* cb;
                std::atomic<bool>* cancelled;
            };
            StreamCtx ctx{&on_token, &cancelled_};

            result = loader.generate_raw_stream(
                handle_, formatted_prompt.c_str(),
                [](const char* piece, void* ud) -> bool {
                    auto* c = static_cast<StreamCtx*>(ud);
                    if (c->cancelled->load(std::memory_order_acquire)) return false;
                    if (c->cb && *c->cb) {
                        TokenOutput tok;
                        tok.text = piece ? piece : "";
                        tok.token_id = -1;
                        tok.is_eos = false;
                        tok.is_tool_call = false;
                        (*c->cb)(tok);
                    }
                    return true;
                },
                &ctx,
                reinterpret_cast<const MetalRTOptions*>(&opts));
        } else {
            result = loader.generate_raw(
                handle_, formatted_prompt.c_str(),
                reinterpret_cast<const MetalRTOptions*>(&opts));
        }
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();

    record_stats(result, elapsed_us);

    double total_ms = elapsed_us / 1000.0;
    double tps = (stats_.generated_tokens > 0 && total_ms > 0)
        ? (stats_.generated_tokens * 1000.0 / total_ms) : 0;
    LOG_DEBUG("MetalRT", "generate_raw() complete — %d prompt tok, %d gen tok, "
              "prefill=%.1fms, decode=%.1fms, total=%.1fms, %.1f tok/s [Metal GPU]",
              result.prompt_tokens, result.generated_tokens,
              result.prefill_ms, result.decode_ms, total_ms, tps);

    return extract_and_clean(result);
}

void MetalRTEngine::cache_system_prompt(const std::string& formatted_system_prompt) {
    if (!initialized_ || !handle_) return;

    auto& loader = MetalRTLoader::instance();

    if (!loader.cache_prompt) {
        LOG_WARN("MetalRT", "cache_prompt not available in dylib — KV cache prefill disabled");
        prompt_cached_ = false;
        return;
    }

    {
        std::lock_guard<std::mutex> gpu_lock(loader.gpu_mutex());
        loader.cache_prompt(handle_, formatted_system_prompt.c_str());
    }

    cached_system_prompt_ = formatted_system_prompt;
    prompt_cached_ = true;
    LOG_DEBUG("MetalRT", "Cached system prompt (%zu chars) into KV cache", formatted_system_prompt.size());
}

std::string MetalRTEngine::generate_raw_continue(const std::string& formatted_continuation,
                                                   TokenCallback on_token,
                                                   bool reset_cache) {
    if (!initialized_ || !handle_) return "";

    auto& loader = MetalRTLoader::instance();

    if (!prompt_cached_ || !loader.generate_raw_continue) {
        LOG_DEBUG("MetalRT", "generate_raw_continue: no cache, falling back to generate_raw");
        std::string full_prompt = cached_system_prompt_ + formatted_continuation;
        return generate_raw(full_prompt, on_token);
    }

    cancelled_.store(false);

    MetalRTOptions opts{};
    opts.max_tokens = config_.max_tokens;
    opts.top_k = config_.top_k;
    opts.temperature = config_.temperature;
    opts.think = config_.think;
    opts.reset_cache = reset_cache;
    opts.ignore_eos = ignore_eos_;

    LOG_DEBUG("MetalRT", "generate_raw_continue() — %s, continuation=%zu chars (stream=%s)",
              reset_cache ? "reusing cached KV" : "appending to KV (multi-turn)",
              formatted_continuation.size(),
              (on_token && loader.generate_raw_continue_stream) ? "yes" : "no");

    auto t0 = std::chrono::high_resolution_clock::now();

    MetalRTResult result;

    {
        std::lock_guard<std::mutex> gpu_lock(loader.gpu_mutex());

        if (on_token && loader.generate_raw_continue_stream) {
            struct StreamCtx {
                TokenCallback* cb;
                std::atomic<bool>* cancelled;
            };
            StreamCtx ctx{&on_token, &cancelled_};

            result = loader.generate_raw_continue_stream(
                handle_, formatted_continuation.c_str(),
                [](const char* piece, void* ud) -> bool {
                    auto* c = static_cast<StreamCtx*>(ud);
                    if (c->cancelled->load(std::memory_order_acquire)) return false;
                    if (c->cb && *c->cb) {
                        TokenOutput tok;
                        tok.text = piece ? piece : "";
                        tok.token_id = -1;
                        tok.is_eos = false;
                        tok.is_tool_call = false;
                        (*c->cb)(tok);
                    }
                    return true;
                },
                &ctx,
                reinterpret_cast<const MetalRTOptions*>(&opts));
        } else {
            result = loader.generate_raw_continue(
                handle_, formatted_continuation.c_str(),
                reinterpret_cast<const MetalRTOptions*>(&opts));
        }
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();

    record_stats(result, elapsed_us);

    double total_ms = elapsed_us / 1000.0;
    double tps = (stats_.generated_tokens > 0 && total_ms > 0)
        ? (stats_.generated_tokens * 1000.0 / total_ms) : 0;
    LOG_DEBUG("MetalRT", "generate_raw_continue() complete — %d prompt tok, %d gen tok, "
              "prefill=%.1fms, decode=%.1fms, total=%.1fms, %.1f tok/s [KV-cached]",
              result.prompt_tokens, result.generated_tokens,
              result.prefill_ms, result.decode_ms, total_ms, tps);

    return extract_and_clean(result);
}

int MetalRTEngine::count_tokens(const std::string& text) {
    if (!initialized_ || !handle_) return 0;
    auto& loader = MetalRTLoader::instance();
    if (!loader.count_tokens) return 0;
    return loader.count_tokens(handle_, text.c_str());
}

int MetalRTEngine::context_size() const {
    if (!initialized_ || !handle_) return 0;
    auto& loader = MetalRTLoader::instance();
    if (!loader.context_size) return 4096;
    return loader.context_size(handle_);
}

void MetalRTEngine::clear_kv() {
    if (!initialized_ || !handle_) return;
    auto& loader = MetalRTLoader::instance();
    if (loader.clear_kv) {
        std::lock_guard<std::mutex> gpu_lock(loader.gpu_mutex());
        loader.clear_kv(handle_);
    }
    prompt_cached_ = false;
}

void MetalRTEngine::set_system_prompt(const std::string& prompt) {
    if (!handle_) return;
    auto& loader = MetalRTLoader::instance();
    if (loader.set_system_prompt) {
        loader.set_system_prompt(handle_, prompt.c_str());
    }
}

void MetalRTEngine::reset_conversation() {
    if (handle_) {
        auto& loader = MetalRTLoader::instance();
        if (loader.reset) loader.reset(handle_);
    }
    prompt_cached_ = false;
    cached_system_prompt_.clear();
}

std::string MetalRTEngine::model_name() const {
    if (!handle_) return "";
    auto& loader = MetalRTLoader::instance();
    if (loader.model_name) {
        const char* n = loader.model_name(handle_);
        return n ? n : "";
    }
    return "";
}

std::string MetalRTEngine::device_name() const {
    if (!handle_) return "";
    auto& loader = MetalRTLoader::instance();
    if (loader.device_name) {
        const char* n = loader.device_name(handle_);
        return n ? n : "";
    }
    return "";
}

bool MetalRTEngine::supports_thinking() const {
    if (!handle_) return false;
    auto& loader = MetalRTLoader::instance();
    if (loader.supports_thinking) return loader.supports_thinking(handle_);
    return false;
}

} // namespace rastack
