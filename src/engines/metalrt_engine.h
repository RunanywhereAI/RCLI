#pragma once
// =============================================================================
// MetalRT LLM Engine — wraps the dynamically-loaded MetalRT C API
// =============================================================================
//
// Provides a LlmEngine-compatible interface backed by MetalRT function pointers
// resolved at runtime via MetalRTLoader. If MetalRT is not installed, all
// operations gracefully fail.
//
// =============================================================================

#include "core/types.h"
#include "engines/metalrt_loader.h"
#include "engines/llm_engine.h"
#include "engines/model_profile.h"
#include <string>
#include <atomic>

namespace rastack {

struct MetalRTEngineConfig {
    std::string model_dir;
    int         max_tokens   = 2048;
    int         top_k        = 40;
    float       temperature  = 0.0f;
    bool        think        = false;
};

class MetalRTEngine {
public:
    MetalRTEngine() = default;
    ~MetalRTEngine();

    MetalRTEngine(const MetalRTEngine&) = delete;
    MetalRTEngine& operator=(const MetalRTEngine&) = delete;

    bool init(const MetalRTEngineConfig& config);
    void shutdown();

    std::string generate(const std::string& prompt, TokenCallback on_token = nullptr);

    // Raw generate: accepts a fully-formatted prompt (chat template already applied by caller).
    // Uses metalrt_generate_raw if available, falls back to regular generate otherwise.
    std::string generate_raw(const std::string& formatted_prompt, TokenCallback on_token = nullptr);

    // Cache a pre-formatted system prompt into MetalRT's KV cache.
    // Subsequent generate_raw_continue() calls only prefill the user turn.
    void cache_system_prompt(const std::string& formatted_system_prompt);

    // Generate from a continuation, reusing cached system prompt in KV.
    // When reset_cache=true (default), KV is truncated back to cache_prompt() position.
    // When reset_cache=false, KV is preserved from previous generation (multi-turn reuse).
    // Falls back to generate_raw() if cache_prompt isn't available in the dylib.
    std::string generate_raw_continue(const std::string& formatted_continuation,
                                      TokenCallback on_token = nullptr,
                                      bool reset_cache = true);

    bool has_prompt_cache() const { return prompt_cached_; }
    const std::string& cached_prompt() const { return cached_system_prompt_; }

    int count_tokens(const std::string& text);
    int context_size() const;
    void clear_kv();

    void set_system_prompt(const std::string& prompt);

    void cancel() { cancelled_.store(true, std::memory_order_release); }
    void reset_conversation();

    bool is_initialized() const { return initialized_; }
    const LlmStats& last_stats() const { return stats_; }

    std::string model_name() const;
    std::string device_name() const;
    bool supports_thinking() const;

    const ModelProfile& profile() const { return profile_; }

    void set_max_tokens(int n) { config_.max_tokens = n; }
    void set_temperature(float t) { config_.temperature = t; }
    void set_ignore_eos(bool v) { ignore_eos_ = v; }

private:
    void record_stats(const MetalRTResult& result, int64_t elapsed_us);
    std::string extract_and_clean(MetalRTResult& result);

    void* handle_ = nullptr;
    MetalRTEngineConfig config_;
    LlmStats stats_;
    ModelProfile profile_;
    bool initialized_ = false;
    bool prompt_cached_ = false;
    std::string cached_system_prompt_;
    std::atomic<bool> cancelled_{false};
    bool ignore_eos_ = false;
};

} // namespace rastack
