#include "engines/llm_engine.h"
#include "engines/model_profile.h"
#include "core/log.h"
#include "llama.h"
#include "llama-cpp.h"
#include "ggml.h"
#include "ggml-backend.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace rastack {

LlmEngine::LlmEngine() = default;

LlmEngine::~LlmEngine() {
    shutdown();
}

void LlmEngine::shutdown() {
    if (sampler_) { llama_sampler_free(sampler_);  sampler_ = nullptr; }
    if (ctx_)     { llama_free(ctx_);              ctx_     = nullptr; }
    if (model_)   { llama_model_free(model_);      model_   = nullptr; }
    vocab_                  = nullptr;
    initialized_            = false;
    has_cached_prompt_      = false;
    cached_prompt_n_tokens_ = 0;
    cached_system_prompt_.clear();
    stats_   = LlmStats{};
    profile_ = ModelProfile{};
    LOG_DEBUG("LLM", "Shutdown complete");
}

bool LlmEngine::init(const LlmConfig& config) {
    if (initialized_) shutdown();

    config_ = config;

    // Initialize backend (loads Metal, etc.)
    ggml_backend_load_all();

    // Load model
    llama_model_params model_params = llama_model_default_params();
    model_params.n_gpu_layers = config.n_gpu_layers;
    model_params.use_mmap     = config.use_mmap;
    model_params.use_mlock    = config.use_mlock;

    LOG_DEBUG("LLM", "Loading model: %s", config.model_path.c_str());
    LOG_DEBUG("LLM", "GPU layers: %d, mmap: %s",
            config.n_gpu_layers, config.use_mmap ? "yes" : "no");

    model_ = llama_model_load_from_file(config.model_path.c_str(), model_params);
    if (!model_) {
        LOG_ERROR("LLM", "Failed to load model");
        return false;
    }

    vocab_ = llama_model_get_vocab(model_);

    // Create context
    llama_context_params ctx_params = llama_context_default_params();
    ctx_params.n_ctx           = config.n_ctx;
    ctx_params.n_batch         = config.n_batch;
    ctx_params.n_threads       = config.n_threads;
    ctx_params.n_threads_batch = config.n_threads_batch;
    ctx_params.no_perf         = false; // enable performance tracking
    ctx_params.flash_attn_type = config.flash_attn ? LLAMA_FLASH_ATTN_TYPE_AUTO : LLAMA_FLASH_ATTN_TYPE_DISABLED;
    ctx_params.type_k = static_cast<ggml_type>(config.type_k);
    ctx_params.type_v = static_cast<ggml_type>(config.type_v);

    LOG_DEBUG("LLM", "Flash attention: %s", config.flash_attn ? "enabled" : "disabled");
    LOG_DEBUG("LLM", "KV cache type_k: %d, type_v: %d", config.type_k, config.type_v);

    ctx_ = llama_init_from_model(model_, ctx_params);
    if (!ctx_) {
        LOG_ERROR("LLM", "Failed to create context");
        llama_model_free(model_);
        model_ = nullptr;
        return false;
    }

    // Setup sampler chain
    auto sparams = llama_sampler_chain_default_params();
    sampler_ = llama_sampler_chain_init(sparams);

    // Add sampling stages: temp -> top_k -> top_p -> dist
    if (config.temperature > 0.0f) {
        llama_sampler_chain_add(sampler_, llama_sampler_init_temp(config.temperature));
        llama_sampler_chain_add(sampler_, llama_sampler_init_top_k(config.top_k));
        llama_sampler_chain_add(sampler_, llama_sampler_init_top_p(config.top_p, 1));
        llama_sampler_chain_add(sampler_, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));
    } else {
        llama_sampler_chain_add(sampler_, llama_sampler_init_greedy());
    }

    initialized_ = true;

    // Auto-detect model family for format-aware prompt building
    profile_ = ModelProfile::detect(model_, config_.model_path);

    // Print model info
    char desc[256];
    llama_model_desc(model_, desc, sizeof(desc));
    LOG_DEBUG("LLM", "Model: %s", desc);
    LOG_DEBUG("LLM", "Context: %d tokens, %d layers",
            llama_n_ctx(ctx_), llama_model_n_layer(model_));
    LOG_INFO("LLM", "Initialized (%s profile)", profile_.family_name.c_str());

    return true;
}

std::vector<int32_t> LlmEngine::tokenize(const std::string& text, bool add_special) {
    // First call to get required count
    int n = -llama_tokenize(vocab_, text.c_str(), text.size(), nullptr, 0, add_special, true);
    std::vector<int32_t> tokens(n);
    llama_tokenize(vocab_, text.c_str(), text.size(), tokens.data(), n, add_special, true);
    return tokens;
}

std::string LlmEngine::token_to_text(int32_t token) {
    char buf[256];
    int n = llama_token_to_piece(vocab_, token, buf, sizeof(buf), 0, true);
    if (n < 0) return "";
    return std::string(buf, n);
}

std::string LlmEngine::detokenize(const std::vector<int32_t>& tokens) {
    std::string result;
    for (auto id : tokens) {
        result += token_to_text(id);
    }
    return result;
}

int LlmEngine::count_tokens(const std::string& text) {
    if (!initialized_) return 0;
    return static_cast<int>(tokenize(text, false).size());
}

std::string LlmEngine::generate(const std::string& prompt, TokenCallback on_token) {
    if (!initialized_) return "";

    cancelled_.store(false, std::memory_order_relaxed);
    stats_ = LlmStats{};

    // Clear KV cache so each generation starts fresh
    clear_kv_cache();

    // Tokenize prompt
    auto prompt_tokens = tokenize(prompt, true);
    stats_.prompt_tokens = prompt_tokens.size();

    LOG_DEBUG("LLM", "Prompt tokens: %zu", prompt_tokens.size());

    // Process prompt (prefill) — batch in chunks if needed
    int64_t t_prompt_start = now_us();

    int n_prompt = prompt_tokens.size();
    int n_batch  = config_.n_batch;
    for (int i = 0; i < n_prompt; i += n_batch) {
        int chunk_size = std::min(n_batch, n_prompt - i);
        llama_batch batch = llama_batch_get_one(prompt_tokens.data() + i, chunk_size);
        if (llama_decode(ctx_, batch) != 0) {
            LOG_ERROR("LLM", "Failed to decode prompt chunk at offset %d", i);
            return "";
        }
    }

    stats_.prompt_eval_us = now_us() - t_prompt_start;

    // Collect stop strings: tool_call_end terminates generation after a tool call
    std::vector<std::string> stop_strings;
    if (!profile_.tool_call_end.empty())
        stop_strings.push_back(profile_.tool_call_end);
    if (profile_.tool_call_end != "</tool_call>" && profile_.tool_call_end != "<|tool_call_end|>") {
        stop_strings.push_back("</tool_call>");
        stop_strings.push_back("<|tool_call_end|>");
    } else if (profile_.tool_call_end == "</tool_call>") {
        stop_strings.push_back("<|tool_call_end|>");
    } else if (profile_.tool_call_end == "<|tool_call_end|>") {
        stop_strings.push_back("</tool_call>");
    }

    // Generate tokens
    std::string result;
    int64_t t_gen_start = now_us();
    bool first_token = true;
    bool in_think_block = false;
    bool in_tool_call_block = false;

    stats_.token_timestamps_us.clear();
    stats_.token_timestamps_us.reserve(config_.max_tokens);

    for (int i = 0; i < config_.max_tokens; i++) {
        if (cancelled_.load(std::memory_order_relaxed)) {
            LOG_DEBUG("LLM", "Generation cancelled");
            break;
        }

        int32_t new_token = llama_sampler_sample(sampler_, ctx_, -1);

        if (first_token) {
            stats_.first_token_us = now_us() - t_prompt_start;
            first_token = false;
        }

        if (llama_vocab_is_eog(vocab_, new_token) && !config_.ignore_eos) {
            break;
        }

        stats_.token_timestamps_us.push_back(now_us());

        std::string piece = token_to_text(new_token);
        result += piece;
        stats_.generated_tokens++;

        // Stop early if we've completed a tool call (saves latency)
        bool hit_stop = false;
        if (in_tool_call_block) {
            for (auto& stop : stop_strings) {
                if (result.size() >= stop.size() &&
                    result.compare(result.size() - stop.size(), stop.size(), stop) == 0) {
                    hit_stop = true;
                    break;
                }
            }
        }
        if (hit_stop) {
            LOG_DEBUG("LLM", "Stopped at tool_call_end after %lld tokens", stats_.generated_tokens);
            break;
        }

        bool suppress = profile_.should_suppress_token(result, in_think_block, in_tool_call_block);

        if (on_token && (!suppress || in_tool_call_block)) {
            TokenOutput tok;
            tok.text = piece;
            tok.token_id = new_token;
            tok.is_eos = false;
            tok.is_tool_call = in_tool_call_block;
            on_token(tok);
        }

        llama_batch batch = llama_batch_get_one(&new_token, 1);
        if (llama_decode(ctx_, batch) != 0) {
            LOG_ERROR("LLM", "Failed to decode token");
            break;
        }
    }

    stats_.generation_us = now_us() - t_gen_start;

    LOG_DEBUG("LLM", "Generated %lld tokens (%.1f tok/s), first token: %.1fms",
            stats_.generated_tokens, stats_.gen_tps(),
            stats_.first_token_us / 1000.0);

    return result;
}

std::string LlmEngine::generate_with_tools(
    const std::string& user_message,
    const std::string& tool_defs_json,
    const std::string& system_prompt,
    TokenCallback on_token)
{
    std::string augmented_system = profile_.build_tool_system_prompt(system_prompt, tool_defs_json);
    std::string prompt = profile_.build_chat_prompt(augmented_system, {}, user_message);
    return generate(prompt, on_token);
}

std::string LlmEngine::build_chat_prompt(
    const std::string& system_prompt,
    const std::vector<std::pair<std::string, std::string>>& history,
    const std::string& user_message)
{
    return profile_.build_chat_prompt(system_prompt, history, user_message);
}

std::string LlmEngine::build_tool_continuation_prompt(
    const std::string& system_prompt,
    const std::string& user_message,
    const std::string& assistant_tool_call_text,
    const std::string& tool_results_text)
{
    return profile_.build_tool_continuation(system_prompt, user_message,
                                            assistant_tool_call_text, tool_results_text);
}

void LlmEngine::clear_kv_cache() {
    if (!ctx_) return;
    llama_memory_clear(llama_get_memory(ctx_), true);
    if (sampler_) {
        llama_sampler_reset(sampler_);
    }
}

void LlmEngine::cache_system_prompt(const std::string& system_prompt) {
    if (!initialized_) return;

    // Build just the system portion using the model's template
    std::string sys_text = profile_.build_system_prefix(system_prompt);

    // Clear any existing KV cache
    clear_kv_cache();

    // Tokenize
    auto tokens = tokenize(sys_text, true);
    cached_prompt_n_tokens_ = tokens.size();

    // Prefill: decode system prompt tokens into KV cache
    int n_batch = config_.n_batch;
    for (int i = 0; i < (int)tokens.size(); i += n_batch) {
        int chunk = std::min(n_batch, (int)tokens.size() - i);
        llama_batch batch = llama_batch_get_one(tokens.data() + i, chunk);
        if (llama_decode(ctx_, batch) != 0) {
            LOG_ERROR("LLM", "Failed to cache system prompt");
            has_cached_prompt_ = false;
            return;
        }
    }

    cached_system_prompt_ = system_prompt;
    has_cached_prompt_ = true;
    LOG_DEBUG("LLM", "Cached system prompt: %d tokens", cached_prompt_n_tokens_);
}

std::string LlmEngine::generate_with_cached_prompt(const std::string& user_portion,
                                                      TokenCallback on_token) {
    if (!initialized_ || !has_cached_prompt_) {
        // Fallback to full generate
        return generate(user_portion, on_token);
    }

    cancelled_.store(false, std::memory_order_relaxed);
    stats_ = LlmStats{};

    // Trim KV cache: keep only the cached system prompt tokens
    llama_memory_seq_rm(llama_get_memory(ctx_), 0, cached_prompt_n_tokens_, -1);
    if (sampler_) {
        llama_sampler_reset(sampler_);
    }

    // Tokenize just the user portion (no add_special — system tokens already have BOS)
    auto user_tokens = tokenize(user_portion, false);
    stats_.prompt_tokens = cached_prompt_n_tokens_ + user_tokens.size();

    LOG_DEBUG("LLM", "Using cached prompt (%d cached + %zu new = %lld total tokens)",
            cached_prompt_n_tokens_, user_tokens.size(), stats_.prompt_tokens);

    // Prefill user portion
    int64_t t_prompt_start = now_us();
    int n_batch = config_.n_batch;
    for (int i = 0; i < (int)user_tokens.size(); i += n_batch) {
        int chunk = std::min(n_batch, (int)user_tokens.size() - i);
        llama_batch batch = llama_batch_get_one(user_tokens.data() + i, chunk);
        if (llama_decode(ctx_, batch) != 0) {
            LOG_ERROR("LLM", "Failed to decode user prompt chunk");
            return "";
        }
    }
    stats_.prompt_eval_us = now_us() - t_prompt_start;

    // Collect stop strings for tool call termination
    std::vector<std::string> stop_strings;
    if (!profile_.tool_call_end.empty())
        stop_strings.push_back(profile_.tool_call_end);
    if (profile_.tool_call_end != "</tool_call>" && profile_.tool_call_end != "<|tool_call_end|>") {
        stop_strings.push_back("</tool_call>");
        stop_strings.push_back("<|tool_call_end|>");
    } else if (profile_.tool_call_end == "</tool_call>") {
        stop_strings.push_back("<|tool_call_end|>");
    } else if (profile_.tool_call_end == "<|tool_call_end|>") {
        stop_strings.push_back("</tool_call>");
    }

    // Generate tokens (same logic as generate())
    std::string result;
    int64_t t_gen_start = now_us();
    bool first_token = true;
    bool in_think_block = false;
    bool in_tool_call_block = false;

    for (int i = 0; i < config_.max_tokens; i++) {
        if (cancelled_.load(std::memory_order_relaxed)) {
            LOG_DEBUG("LLM", "Generation cancelled");
            break;
        }

        int32_t new_token = llama_sampler_sample(sampler_, ctx_, -1);

        if (first_token) {
            stats_.first_token_us = now_us() - t_prompt_start;
            first_token = false;
        }

        if (llama_vocab_is_eog(vocab_, new_token)) break;

        std::string piece = token_to_text(new_token);
        result += piece;
        stats_.generated_tokens++;

        // Stop early if we've completed a tool call
        bool hit_stop = false;
        if (in_tool_call_block) {
            for (auto& stop : stop_strings) {
                if (result.size() >= stop.size() &&
                    result.compare(result.size() - stop.size(), stop.size(), stop) == 0) {
                    hit_stop = true;
                    break;
                }
            }
        }
        if (hit_stop) {
            LOG_DEBUG("LLM", "Stopped at tool_call_end after %lld tokens", stats_.generated_tokens);
            break;
        }

        bool suppress = profile_.should_suppress_token(result, in_think_block, in_tool_call_block);
        if (on_token && (!suppress || in_tool_call_block)) {
            TokenOutput tok;
            tok.text = piece;
            tok.token_id = new_token;
            tok.is_eos = false;
            tok.is_tool_call = in_tool_call_block;
            on_token(tok);
        }

        llama_batch batch = llama_batch_get_one(&new_token, 1);
        if (llama_decode(ctx_, batch) != 0) {
            LOG_ERROR("LLM", "Failed to decode token");
            break;
        }
    }

    stats_.generation_us = now_us() - t_gen_start;
    LOG_DEBUG("LLM", "Generated %lld tokens (%.1f tok/s), first token: %.1fms",
            stats_.generated_tokens, stats_.gen_tps(), stats_.first_token_us / 1000.0);

    return result;
}

int LlmEngine::context_size() const {
    return ctx_ ? llama_n_ctx(ctx_) : 0;
}

int LlmEngine::n_params() const {
    return model_ ? static_cast<int>(llama_model_n_params(model_)) : 0;
}

} // namespace rastack
