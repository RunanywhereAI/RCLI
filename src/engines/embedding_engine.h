#pragma once

#include "core/types.h"
#include "core/hardware_profile.h"
#include <string>
#include <vector>

// Forward declare llama types
struct llama_model;
struct llama_context;
struct llama_vocab;

namespace rastack {

struct EmbeddingConfig {
    std::string model_path;
    // Safe cross-platform defaults. Call from_hw() for device-optimal settings.
    int         n_gpu_layers  = 0;   // 0 = CPU-only; from_hw() sets 99 for Metal
    int         n_threads     = 4;
    int         n_batch       = 512;
    int         embedding_dim = 384;
    bool        use_mmap      = true;

    // Build optimal config for current device
    static EmbeddingConfig from_hw(const std::string& path,
                                    const HardwareProfile& hw = global_hw()) {
        EmbeddingConfig c;
        c.model_path   = path;
        c.n_gpu_layers = hw.emb_gpu_layers;
        c.n_threads    = hw.emb_n_threads;
        c.n_batch      = hw.emb_n_batch;
        return c;
    }
};

class EmbeddingEngine {
public:
    EmbeddingEngine();
    ~EmbeddingEngine();

    bool init(const EmbeddingConfig& config);

    // Single text → embedding (query-time, ~3-5ms)
    std::vector<float> embed(const std::string& text);

    // Write embedding into caller-provided buffer (zero-alloc query path)
    bool embed_into(const std::string& text, float* output, int dim);

    // Batch embedding (index-time)
    std::vector<std::vector<float>> embed_batch(
        const std::vector<std::string>& texts, int batch_size = 32);

    int embedding_dim() const { return n_embd_; }
    int64_t last_latency_us() const { return last_latency_us_; }
    bool is_initialized() const { return initialized_; }

private:
    // Tokenize into pre-allocated buffer, returns token count
    int tokenize_into(const std::string& text);

    // L2-normalize embedding vector
    void normalize(float* vec, int n);

    llama_model*       model_ = nullptr;
    llama_context*     ctx_   = nullptr;
    const llama_vocab* vocab_ = nullptr;

    EmbeddingConfig config_;
    int             n_embd_ = 0;
    int64_t         last_latency_us_ = 0;
    bool            initialized_ = false;

    // Pre-allocated token buffer (reused per call)
    std::vector<int32_t> token_buf_;
};

} // namespace rastack
