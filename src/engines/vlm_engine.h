#pragma once

#include "core/types.h"
#include <string>
#include <vector>
#include <atomic>

// Forward declare llama types
struct llama_model;
struct llama_context;
struct llama_sampler;
struct llama_vocab;

// Forward declare mtmd types
struct mtmd_context;

namespace rastack {

struct VlmConfig {
    std::string model_path;       // Path to VLM language model GGUF
    std::string mmproj_path;      // Path to vision projector (mmproj) GGUF
    int         n_gpu_layers = 99;
    int         n_ctx        = 4096;  // VLM needs larger context for image tokens
    int         n_batch      = 512;
    int         n_threads       = 1;
    int         n_threads_batch = 8;
    float       temperature  = 0.7f;
    float       top_p        = 0.9f;
    int         top_k        = 40;
    int         max_tokens   = 512;
    bool        use_mmap     = true;
    bool        use_mlock    = false;
    bool        flash_attn   = true;
};

struct VlmStats {
    int64_t  prompt_tokens     = 0;
    int64_t  generated_tokens  = 0;
    int64_t  prompt_eval_us    = 0;
    int64_t  generation_us     = 0;
    int64_t  image_encode_us   = 0;   // Time spent encoding the image
    double   prompt_tps()  const { return prompt_tokens > 0 ? prompt_tokens * 1e6 / prompt_eval_us : 0; }
    double   gen_tps()     const { return generated_tokens > 0 ? generated_tokens * 1e6 / generation_us : 0; }
    int64_t  first_token_us    = 0;
};

class VlmEngine {
public:
    VlmEngine();
    ~VlmEngine();

    // Initialize model + vision projector
    bool init(const VlmConfig& config);

    // Release all resources
    void shutdown();

    // Analyze an image with a text prompt
    // Returns the generated description/analysis text
    std::string analyze_image(const std::string& image_path,
                              const std::string& prompt,
                              TokenCallback on_token = nullptr);

    // Cancel ongoing generation
    void cancel() { cancelled_.store(true, std::memory_order_release); }

    // Get stats from last generation
    const VlmStats& last_stats() const { return stats_; }

    bool is_initialized() const { return initialized_; }

    // Check if an image file is a supported format
    static bool is_supported_image(const std::string& path);

private:
    llama_model*    model_      = nullptr;
    llama_context*  ctx_        = nullptr;
    llama_sampler*  sampler_    = nullptr;
    const llama_vocab* vocab_   = nullptr;
    mtmd_context*   ctx_mtmd_   = nullptr;

    VlmConfig        config_;
    VlmStats         stats_;
    bool             initialized_ = false;
    std::atomic<bool> cancelled_{false};
};

} // namespace rastack
