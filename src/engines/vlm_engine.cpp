#include "engines/vlm_engine.h"
#include "core/log.h"
#include "llama.h"
#include "ggml.h"
#include "ggml-backend.h"
#include "mtmd.h"
#include "mtmd-helper.h"
#include <cstring>
#include <algorithm>

namespace rastack {

VlmEngine::VlmEngine() = default;

VlmEngine::~VlmEngine() {
    shutdown();
}

void VlmEngine::shutdown() {
    if (ctx_mtmd_) { mtmd_free(ctx_mtmd_);           ctx_mtmd_ = nullptr; }
    if (sampler_)  { llama_sampler_free(sampler_);    sampler_  = nullptr; }
    if (ctx_)      { llama_free(ctx_);                ctx_      = nullptr; }
    if (model_)    { llama_model_free(model_);        model_    = nullptr; }
    vocab_       = nullptr;
    initialized_ = false;
    stats_       = VlmStats{};
    LOG_DEBUG("VLM", "Shutdown complete");
}

bool VlmEngine::init(const VlmConfig& config) {
    if (initialized_) shutdown();

    config_ = config;

    // Initialize backend (loads Metal, etc.)
    ggml_backend_load_all();

    // Load language model
    llama_model_params model_params = llama_model_default_params();
    model_params.n_gpu_layers = config.n_gpu_layers;
    model_params.use_mmap     = config.use_mmap;
    model_params.use_mlock    = config.use_mlock;

    LOG_DEBUG("VLM", "Loading VLM model: %s", config.model_path.c_str());
    model_ = llama_model_load_from_file(config.model_path.c_str(), model_params);
    if (!model_) {
        LOG_ERROR("VLM", "Failed to load VLM model");
        return false;
    }

    vocab_ = llama_model_get_vocab(model_);

    // Create inference context
    llama_context_params ctx_params = llama_context_default_params();
    ctx_params.n_ctx           = config.n_ctx;
    ctx_params.n_batch         = config.n_batch;
    ctx_params.n_threads       = config.n_threads;
    ctx_params.n_threads_batch = config.n_threads_batch;
    ctx_params.no_perf         = false;
    ctx_params.flash_attn_type = config.flash_attn ? LLAMA_FLASH_ATTN_TYPE_AUTO : LLAMA_FLASH_ATTN_TYPE_DISABLED;

    ctx_ = llama_init_from_model(model_, ctx_params);
    if (!ctx_) {
        LOG_ERROR("VLM", "Failed to create VLM context");
        llama_model_free(model_);
        model_ = nullptr;
        return false;
    }

    // Initialize mtmd (vision projector)
    LOG_DEBUG("VLM", "Loading vision projector: %s", config.mmproj_path.c_str());
    mtmd_context_params mtmd_params = mtmd_context_params_default();
    mtmd_params.use_gpu    = (config.n_gpu_layers > 0);
    mtmd_params.n_threads  = config.n_threads_batch;
    mtmd_params.flash_attn_type = config.flash_attn ? LLAMA_FLASH_ATTN_TYPE_AUTO : LLAMA_FLASH_ATTN_TYPE_DISABLED;

    ctx_mtmd_ = mtmd_init_from_file(config.mmproj_path.c_str(), model_, mtmd_params);
    if (!ctx_mtmd_) {
        LOG_ERROR("VLM", "Failed to load vision projector (mmproj)");
        llama_free(ctx_);
        llama_model_free(model_);
        ctx_   = nullptr;
        model_ = nullptr;
        return false;
    }

    if (!mtmd_support_vision(ctx_mtmd_)) {
        LOG_ERROR("VLM", "Model does not support vision input");
        mtmd_free(ctx_mtmd_);
        llama_free(ctx_);
        llama_model_free(model_);
        ctx_mtmd_ = nullptr;
        ctx_      = nullptr;
        model_    = nullptr;
        return false;
    }

    // Setup sampler chain
    auto sparams = llama_sampler_chain_default_params();
    sampler_ = llama_sampler_chain_init(sparams);
    if (config.temperature > 0.0f) {
        llama_sampler_chain_add(sampler_, llama_sampler_init_temp(config.temperature));
        llama_sampler_chain_add(sampler_, llama_sampler_init_top_k(config.top_k));
        llama_sampler_chain_add(sampler_, llama_sampler_init_top_p(config.top_p, 1));
        llama_sampler_chain_add(sampler_, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));
    } else {
        llama_sampler_chain_add(sampler_, llama_sampler_init_greedy());
    }

    initialized_ = true;
    LOG_INFO("VLM", "Initialized (vision support: yes)");
    return true;
}

std::string VlmEngine::analyze_image(const std::string& image_path,
                                      const std::string& prompt,
                                      TokenCallback on_token) {
    if (!initialized_) return "";

    cancelled_.store(false, std::memory_order_relaxed);
    stats_ = VlmStats{};

    // Clear KV cache
    llama_memory_clear(llama_get_memory(ctx_), true);
    if (sampler_) llama_sampler_reset(sampler_);

    // 1. Load image
    LOG_DEBUG("VLM", "Loading image: %s", image_path.c_str());
    mtmd_bitmap* bitmap = mtmd_helper_bitmap_init_from_file(ctx_mtmd_, image_path.c_str());
    if (!bitmap) {
        LOG_ERROR("VLM", "Failed to load image: %s", image_path.c_str());
        return "";
    }

    // 2. Build prompt with media marker using ChatML template (Qwen3-VL format)
    // The model expects: <|im_start|>system\n...<|im_end|>\n<|im_start|>user\n<marker>\nprompt<|im_end|>\n<|im_start|>assistant\n
    std::string marker = mtmd_default_marker();
    std::string full_prompt =
        "<|im_start|>system\nYou are a helpful assistant.<|im_end|>\n"
        "<|im_start|>user\n" + marker + "\n" + prompt + "<|im_end|>\n"
        "<|im_start|>assistant\n";

    mtmd_input_text input_text;
    input_text.text          = full_prompt.c_str();
    input_text.add_special   = true;
    input_text.parse_special = true;

    // 3. Tokenize (combines text tokens + image tokens)
    mtmd_input_chunks* chunks = mtmd_input_chunks_init();
    const mtmd_bitmap* bitmap_ptr = bitmap;

    int64_t t_encode_start = now_us();
    int32_t tokenize_result = mtmd_tokenize(ctx_mtmd_, chunks, &input_text, &bitmap_ptr, 1);
    if (tokenize_result != 0) {
        LOG_ERROR("VLM", "Failed to tokenize image+text (error=%d)", tokenize_result);
        mtmd_input_chunks_free(chunks);
        mtmd_bitmap_free(bitmap);
        return "";
    }

    size_t n_tokens = mtmd_helper_get_n_tokens(chunks);
    stats_.prompt_tokens = n_tokens;
    LOG_DEBUG("VLM", "Tokenized: %zu total tokens (text + image)", n_tokens);

    // 4. Evaluate all chunks (text + image encoding + decoding)
    int64_t t_prompt_start = now_us();
    llama_pos n_past = 0;
    int32_t eval_result = mtmd_helper_eval_chunks(
        ctx_mtmd_, ctx_, chunks,
        n_past,           // n_past
        0,                // seq_id
        config_.n_batch,  // n_batch
        true,             // logits_last
        &n_past           // updated n_past
    );

    stats_.image_encode_us = now_us() - t_encode_start;
    stats_.prompt_eval_us  = now_us() - t_prompt_start;

    // Clean up image resources
    mtmd_input_chunks_free(chunks);
    mtmd_bitmap_free(bitmap);

    if (eval_result != 0) {
        LOG_ERROR("VLM", "Failed to evaluate image+text chunks (error=%d)", eval_result);
        return "";
    }

    LOG_DEBUG("VLM", "Image encoded in %.1fms, prompt eval in %.1fms",
            stats_.image_encode_us / 1000.0, stats_.prompt_eval_us / 1000.0);

    // 5. Generate tokens (same pattern as LlmEngine::generate)
    std::string result;
    int64_t t_gen_start = now_us();
    bool first_token = true;

    for (int i = 0; i < config_.max_tokens; i++) {
        if (cancelled_.load(std::memory_order_relaxed)) {
            LOG_DEBUG("VLM", "Generation cancelled");
            break;
        }

        int32_t new_token = llama_sampler_sample(sampler_, ctx_, -1);

        if (first_token) {
            stats_.first_token_us = now_us() - t_prompt_start;
            first_token = false;
        }

        if (llama_vocab_is_eog(vocab_, new_token)) {
            break;
        }

        // Decode token to text
        char buf[256];
        int n = llama_token_to_piece(vocab_, new_token, buf, sizeof(buf), 0, true);
        if (n < 0) continue;
        std::string piece(buf, n);

        result += piece;
        stats_.generated_tokens++;

        if (on_token) {
            TokenOutput tok;
            tok.text      = piece;
            tok.token_id  = new_token;
            tok.is_eos    = false;
            tok.is_tool_call = false;
            on_token(tok);
        }

        // Feed token back for next iteration
        llama_batch batch = llama_batch_get_one(&new_token, 1);
        if (llama_decode(ctx_, batch) != 0) {
            LOG_ERROR("VLM", "Failed to decode token");
            break;
        }
    }

    stats_.generation_us = now_us() - t_gen_start;

    LOG_DEBUG("VLM", "Generated %lld tokens (%.1f tok/s), first token: %.1fms",
            stats_.generated_tokens, stats_.gen_tps(),
            stats_.first_token_us / 1000.0);

    return result;
}

bool VlmEngine::is_supported_image(const std::string& path) {
    // Get extension (case-insensitive)
    auto dot = path.rfind('.');
    if (dot == std::string::npos) return false;

    std::string ext = path.substr(dot);
    // Convert to lowercase
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    return ext == ".jpg"  || ext == ".jpeg" ||
           ext == ".png"  || ext == ".bmp"  ||
           ext == ".gif"  || ext == ".webp" ||
           ext == ".tga";
}

} // namespace rastack
