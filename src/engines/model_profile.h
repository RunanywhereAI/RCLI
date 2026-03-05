#pragma once

#include "core/types.h"
#include <string>
#include <vector>
#include <functional>

struct llama_model;

namespace rastack {

// Identifies the model family for format-specific behavior
enum class ModelFamily {
    QWEN3,      // Qwen3 (ChatML + <tool_call> JSON)
    LFM2,       // Liquid LFM2 (ChatML variant + <|tool_call_start|>[func()] format)
    LLAMA3,     // Llama 3.x (uses <|begin_of_text|> / <|eot_id|>)
    GEMMA,      // Google Gemma (<start_of_turn> / <end_of_turn>)
    MISTRAL,    // Mistral/Mixtral ([INST] format)
    PHI,        // Microsoft Phi (<|system|> / <|end|>)
    CHATML,     // Generic ChatML (Hermes, OpenChat, etc.)
    UNKNOWN,    // Fallback: use llama_chat_apply_template or ChatML
};

// Per-model format configuration. Auto-detected from GGUF metadata or filename.
// Eliminates hardcoded template strings throughout the codebase.
struct ModelProfile {
    ModelFamily family = ModelFamily::UNKNOWN;
    std::string family_name;             // Human-readable: "Qwen3", "LFM2", etc.

    // Chat template tokens
    std::string msg_start;               // e.g. "<|im_start|>" or "<|start_header_id|>"
    std::string msg_end;                 // e.g. "<|im_end|>" or "<|eot_id|>"
    std::string role_separator;          // usually "\n"

    // Think-mode control
    std::string no_think_suffix;         // "/no_think" for Qwen3, "" for others
    std::string think_start;             // "<think>" for Qwen3, "" if unsupported
    std::string think_end;               // "</think>"

    // Tool-call output format (what the model generates)
    std::string tool_call_start;         // "<tool_call>" or "<|tool_call_start|>"
    std::string tool_call_end;           // "</tool_call>" or "<|tool_call_end|>"
    bool tool_call_json_format = true;   // true = {"name":..,"arguments":..}, false = func(k="v")

    // Tool-response tags (for wrapping results fed back to the model)
    std::string tool_response_start;     // "<tool_response>" or "<|tool_response_start|>"
    std::string tool_response_end;       // "</tool_response>" or "<|tool_response_end|>"

    // GGUF-embedded chat template string (from model metadata)
    std::string gguf_template;
    bool has_gguf_template = false;

    // ----- Factory -----

    // Auto-detect from a loaded llama_model (reads GGUF metadata + filename)
    static ModelProfile detect(const llama_model* model, const std::string& model_path);

    // Manual override by family name (for testing or user config)
    static ModelProfile from_family(ModelFamily family);

    // ----- Prompt building -----

    // Build a complete chat prompt from system prompt, history, and user message
    std::string build_chat_prompt(
        const std::string& system_prompt,
        const std::vector<std::pair<std::string, std::string>>& history,
        const std::string& user_message) const;

    // Build just the system portion (for KV caching)
    std::string build_system_prefix(const std::string& system_prompt) const;

    // Build the user turn + assistant start (for appending after cached system)
    std::string build_user_turn(const std::string& user_message) const;

    // Build tool continuation prompt (after tool execution)
    std::string build_tool_continuation(
        const std::string& system_prompt,
        const std::string& user_message,
        const std::string& assistant_tool_call_text,
        const std::string& tool_results_text) const;

    // Build system prompt with tool definitions injected
    std::string build_tool_system_prompt(
        const std::string& base_system_prompt,
        const std::string& tool_defs_json) const;

    // ----- Tool call parsing -----

    // Parse tool calls from this model's output
    std::vector<ToolCall> parse_tool_calls(const std::string& llm_output) const;

    // ----- Output cleaning -----

    // Strip model-specific artifacts (think blocks, tool call tokens, junk suffixes)
    std::string clean_output(const std::string& raw) const;

    // Check if a token should be suppressed from streaming (think blocks, tool calls)
    bool should_suppress_token(const std::string& accumulated, bool& in_think, bool& in_tool) const;
};

} // namespace rastack
