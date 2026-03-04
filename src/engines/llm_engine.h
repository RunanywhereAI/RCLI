#pragma once

#include "core/types.h"
#include "engines/model_profile.h"
#include <string>
#include <vector>
#include <atomic>

// Forward declare llama types
struct llama_model;
struct llama_context;
struct llama_sampler;
struct llama_vocab;

namespace rastack {

struct LlmConfig {
    std::string model_path;
    int         n_gpu_layers = 99;   // offload everything to Metal
    int         n_ctx        = 2048; // context window
    int         n_batch      = 512;  // batch size for prompt processing
    int         n_threads       = 1;   // CPU threads for decode (GPU-bound, 1 is optimal)
    int         n_threads_batch = 8;   // CPU threads for prompt eval
    float       temperature  = 0.7f;
    float       top_p        = 0.9f;
    int         top_k        = 40;
    int         max_tokens   = 512;  // max response tokens
    bool        use_mmap     = true;
    bool        use_mlock    = false;
    bool        flash_attn   = true;   // enable Flash Attention (Metal)
    bool        ignore_eos   = false;  // force generation to max_tokens (benchmarking)
    int         type_k       = 1;     // KV cache K type: 1=F16, 8=Q8_0, 2=Q4_0
    int         type_v       = 1;     // KV cache V type: 1=F16, 8=Q8_0, 2=Q4_0
};

struct LlmStats {
    int64_t  prompt_tokens     = 0;
    int64_t  generated_tokens  = 0;
    int64_t  prompt_eval_us    = 0;
    int64_t  generation_us     = 0;
    double   prompt_tps()  const { return prompt_tokens > 0 ? prompt_tokens * 1e6 / prompt_eval_us : 0; }
    double   gen_tps()     const { return generated_tokens > 0 ? generated_tokens * 1e6 / generation_us : 0; }
    int64_t  first_token_us    = 0;
    std::vector<int64_t> token_timestamps_us;  // per-token absolute timestamps
};

class LlmEngine {
public:
    LlmEngine();
    ~LlmEngine();

    // Initialize model
    bool init(const LlmConfig& config);

    // Generate response with streaming callback
    // Returns full response text
    std::string generate(const std::string& prompt, TokenCallback on_token = nullptr);

    // Generate with tool definitions injected into system prompt
    std::string generate_with_tools(const std::string& user_message,
                                     const std::string& tool_defs_json,
                                     const std::string& system_prompt,
                                     TokenCallback on_token = nullptr);

    // Build chat prompt using model's template
    std::string build_chat_prompt(const std::string& system_prompt,
                                   const std::vector<std::pair<std::string, std::string>>& history,
                                   const std::string& user_message);

    // Build continuation prompt for second LLM pass after tool execution
    std::string build_tool_continuation_prompt(
        const std::string& system_prompt,
        const std::string& user_message,
        const std::string& assistant_tool_call_text,
        const std::string& tool_results_text);

    // Cancel ongoing generation
    void cancel() { cancelled_.store(true, std::memory_order_release); }

    // Clear KV cache (e.g., after barge-in)
    void clear_kv_cache();

    // Cache system prompt KV state for reuse across queries
    void cache_system_prompt(const std::string& system_prompt);

    // Generate using cached system prompt (only evals user portion)
    std::string generate_with_cached_prompt(const std::string& user_portion,
                                             TokenCallback on_token = nullptr);

    bool has_prompt_cache() const { return has_cached_prompt_; }

    // Get stats from last generation
    const LlmStats& last_stats() const { return stats_; }

    bool is_initialized() const { return initialized_; }

    // Model info
    int context_size() const;
    int n_params() const;

    // Tokenize / detokenize (public for benchmarking)
    std::vector<int32_t> tokenize(const std::string& text, bool add_special);
    std::string detokenize(const std::vector<int32_t>& tokens);
    std::string token_to_text(int32_t token);

    // Count tokens in a string (for history budget management)
    int count_tokens(const std::string& text);

    // Runtime config overrides for benchmarking
    void set_max_tokens(int n) { config_.max_tokens = n; }
    void set_ignore_eos(bool v) { config_.ignore_eos = v; }

    // Access the auto-detected model profile
    const ModelProfile& profile() const { return profile_; }
    ModelProfile& profile() { return profile_; }

private:

    llama_model*   model_   = nullptr;
    llama_context* ctx_     = nullptr;
    llama_sampler* sampler_ = nullptr;
    const llama_vocab* vocab_ = nullptr;

    LlmConfig       config_;
    LlmStats         stats_;
    ModelProfile     profile_;
    bool             initialized_ = false;
    std::atomic<bool> cancelled_{false};

    // Prompt cache state
    std::string cached_system_prompt_;
    int         cached_prompt_n_tokens_ = 0;
    bool        has_cached_prompt_ = false;
};

} // namespace rastack
