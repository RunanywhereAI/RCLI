#include "engines/embedding_engine.h"
#include "llama.h"
#include "llama-cpp.h"
#include "ggml.h"
#include "ggml-backend.h"
#include <cstdio>
#include <cmath>
#include <cstring>

namespace rastack {

EmbeddingEngine::EmbeddingEngine() = default;

EmbeddingEngine::~EmbeddingEngine() {
    if (ctx_)   { llama_free(ctx_);          ctx_   = nullptr; }
    if (model_) { llama_model_free(model_);  model_ = nullptr; }
}

bool EmbeddingEngine::init(const EmbeddingConfig& config) {
    config_ = config;

    // Initialize backend (Metal, etc.)
    ggml_backend_load_all();

    // Load model
    llama_model_params model_params = llama_model_default_params();
    model_params.n_gpu_layers = config.n_gpu_layers;
    model_params.use_mmap     = config.use_mmap;
    model_params.use_mlock    = false;

    fprintf(stderr, "[EMBED] Loading model: %s\n", config.model_path.c_str());
    fprintf(stderr, "[EMBED] GPU layers: %d, dim: %d\n",
            config.n_gpu_layers, config.embedding_dim);

    model_ = llama_model_load_from_file(config.model_path.c_str(), model_params);
    if (!model_) {
        fprintf(stderr, "[EMBED] Failed to load model\n");
        return false;
    }

    vocab_ = llama_model_get_vocab(model_);
    n_embd_ = llama_model_n_embd(model_);

    fprintf(stderr, "[EMBED] Model embedding dim: %d\n", n_embd_);

    // Create context with embedding mode enabled
    llama_context_params ctx_params = llama_context_default_params();
    ctx_params.n_ctx           = config.n_batch;
    ctx_params.n_batch         = config.n_batch;
    ctx_params.n_ubatch        = config.n_batch;  // encoder requires n_ubatch >= n_tokens
    ctx_params.n_threads       = config.n_threads;
    ctx_params.n_threads_batch = config.n_threads;
    ctx_params.embeddings      = true;
    ctx_params.no_perf         = true;

    ctx_ = llama_init_from_model(model_, ctx_params);
    if (!ctx_) {
        fprintf(stderr, "[EMBED] Failed to create context\n");
        llama_model_free(model_);
        model_ = nullptr;
        return false;
    }

    // Pre-allocate token buffer (512 tokens is plenty for queries)
    token_buf_.resize(config.n_batch);

    initialized_ = true;

    char desc[256];
    llama_model_desc(model_, desc, sizeof(desc));
    fprintf(stderr, "[EMBED] Model: %s\n", desc);
    fprintf(stderr, "[EMBED] Initialized successfully (dim=%d)\n", n_embd_);

    return true;
}

int EmbeddingEngine::tokenize_into(const std::string& text) {
    int n = -llama_tokenize(vocab_, text.c_str(), text.size(), nullptr, 0, true, true);
    if (n > static_cast<int>(token_buf_.size())) {
        token_buf_.resize(n);
    }
    llama_tokenize(vocab_, text.c_str(), text.size(), token_buf_.data(), n, true, true);
    return n;
}

void EmbeddingEngine::normalize(float* vec, int n) {
    float sum = 0.0f;
    for (int i = 0; i < n; i++) {
        sum += vec[i] * vec[i];
    }
    float norm = std::sqrt(sum);
    if (norm > 0.0f) {
        float inv_norm = 1.0f / norm;
        for (int i = 0; i < n; i++) {
            vec[i] *= inv_norm;
        }
    }
}

bool EmbeddingEngine::embed_into(const std::string& text, float* output, int dim) {
    if (!initialized_ || !output) return false;
    if (dim != n_embd_) return false;

    int64_t t_start = now_us();

    // Clear KV cache for fresh embedding
    auto mem = llama_get_memory(ctx_);
    if (mem) {
        llama_memory_clear(mem, false);
    }

    // Tokenize into pre-allocated buffer
    int n_tokens = tokenize_into(text);
    if (n_tokens <= 0) return false;

    // Truncate to batch size (embedding models have fixed max sequence length)
    if (n_tokens > config_.n_batch) {
        n_tokens = config_.n_batch;
    }

    // Decode tokens
    llama_batch batch = llama_batch_get_one(token_buf_.data(), n_tokens);
    if (llama_decode(ctx_, batch) != 0) {
        fprintf(stderr, "[EMBED] Failed to decode\n");
        return false;
    }

    // Get embeddings
    const float* embd = llama_get_embeddings_seq(ctx_, 0);
    if (!embd) {
        embd = llama_get_embeddings(ctx_);
        if (!embd) {
            fprintf(stderr, "[EMBED] Failed to get embeddings\n");
            return false;
        }
    }

    // Copy and normalize into caller's buffer
    std::memcpy(output, embd, n_embd_ * sizeof(float));
    normalize(output, n_embd_);

    last_latency_us_ = now_us() - t_start;
    return true;
}

std::vector<float> EmbeddingEngine::embed(const std::string& text) {
    std::vector<float> result(n_embd_);
    if (!embed_into(text, result.data(), n_embd_)) {
        return {};
    }
    return result;
}

std::vector<std::vector<float>> EmbeddingEngine::embed_batch(
    const std::vector<std::string>& texts, int batch_size)
{
    std::vector<std::vector<float>> results;
    results.reserve(texts.size());

    for (size_t i = 0; i < texts.size(); i++) {
        auto emb = embed(texts[i]);
        results.push_back(std::move(emb));

        if ((i + 1) % 100 == 0) {
            fprintf(stderr, "[EMBED] Embedded %zu/%zu texts\n", i + 1, texts.size());
        }
    }

    return results;
}

} // namespace rastack
