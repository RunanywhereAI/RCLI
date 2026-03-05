#pragma once
// =============================================================================
// RCLI Model Registry
// =============================================================================
//
// Single source of truth for all supported LLM models.
// Used by: upgrade-llm, auto-detect, setup, info, and the interactive CLI.
//
// HOW TO ADD A NEW MODEL (for open-source contributors):
//   1. Add a new entry to the `all_models()` vector below.
//   2. Fill in every field (see LlmModelDef docs).
//   3. Set the `priority` field: higher = preferred when multiple models are
//      installed. The auto-detect logic picks the highest-priority model found.
//   4. That's it! The CLI, upgrade-llm, and auto-detect will pick it up.
//
// =============================================================================

#include <string>
#include <vector>

namespace rcli {

struct LlmModelDef {
    std::string id;            // Unique slug: "qwen3.5-4b"
    std::string name;          // Display name: "Qwen3.5 4B"
    std::string filename;      // Local GGUF filename (stored in models_dir)
    std::string url;           // HuggingFace direct download URL
    std::string family;        // Model family (maps to ModelProfile): "qwen3", "lfm2", etc.
    int         size_mb;       // Approximate Q4_K_M download size in MB
    int         priority;      // Auto-detect priority (higher = preferred). Default model = 0.
    std::string speed_est;     // Speed estimate on Apple Silicon M-series
    std::string tool_calling;  // Tool calling quality: "Basic", "Good", "Excellent"
    std::string description;   // One-line description for UI
    bool        is_default;    // Ships with `rcli setup` (only one should be true)
    bool        is_recommended;// Highlighted as recommended in `upgrade-llm`
};

// ---------------------------------------------------------------------------
// All supported models — the ONLY place model info needs to be defined.
// Sorted by priority (lowest first, highest last).
// ---------------------------------------------------------------------------
inline std::vector<LlmModelDef> all_models() {
    return {
        // =================================================================
        // Default (ships with `rcli setup`)
        // =================================================================
        {
            /* id            */ "lfm2-1.2b",
            /* name          */ "Liquid LFM2 1.2B Tool",
            /* filename      */ "lfm2-1.2b-tool-q4_k_m.gguf",
            /* url           */ "https://huggingface.co/LiquidAI/LFM2-1.2B-Tool-GGUF/resolve/main/LFM2-1.2B-Tool-Q4_K_M.gguf",
            /* family        */ "lfm2",
            /* size_mb       */ 731,
            /* priority      */ 0,
            /* speed_est     */ "~180 t/s",
            /* tool_calling  */ "Excellent",
            /* description   */ "Best for tool calling + conversation. Ships by default.",
            /* is_default    */ true,
            /* is_recommended*/ true,
        },

        // =================================================================
        // Upgrade options (downloaded via `rcli upgrade-llm`)
        // =================================================================
        {
            /* id            */ "qwen3-0.6b",
            /* name          */ "Qwen3 0.6B",
            /* filename      */ "qwen3-0.6b-q4_k_m.gguf",
            /* url           */ "https://huggingface.co/Qwen/Qwen3-0.6B-GGUF/resolve/main/qwen3-0.6b-q4_k_m.gguf",
            /* family        */ "qwen3",
            /* size_mb       */ 456,
            /* priority      */ 5,
            /* speed_est     */ "~250 t/s",
            /* tool_calling  */ "Basic",
            /* description   */ "Ultra-fast, smallest model. Limited tool-calling accuracy.",
            /* is_default    */ false,
            /* is_recommended*/ false,
        },
        {
            /* id            */ "qwen3.5-0.8b",
            /* name          */ "Qwen3.5 0.8B",
            /* filename      */ "qwen3.5-0.8b-q4_k_m.gguf",
            /* url           */ "https://huggingface.co/unsloth/Qwen3.5-0.8B-GGUF/resolve/main/Qwen3.5-0.8B-Q4_K_M.gguf",
            /* family        */ "qwen3",
            /* size_mb       */ 600,
            /* priority      */ 10,
            /* speed_est     */ "~220 t/s",
            /* tool_calling  */ "Basic",
            /* description   */ "Qwen3.5 generation. Slightly better than 0.6B, still very fast.",
            /* is_default    */ false,
            /* is_recommended*/ false,
        },
        {
            /* id            */ "lfm2-350m",
            /* name          */ "Liquid LFM2 350M",
            /* filename      */ "LFM2-350M-Q4_K_M.gguf",
            /* url           */ "https://huggingface.co/LiquidAI/LFM2-350M-GGUF/resolve/main/LFM2-350M-Q4_K_M.gguf",
            /* family        */ "lfm2",
            /* size_mb       */ 219,
            /* priority      */ 3,
            /* speed_est     */ "~350 t/s",
            /* tool_calling  */ "Basic",
            /* description   */ "Ultra-tiny Liquid model. Fastest inference, 128K context.",
            /* is_default    */ false,
            /* is_recommended*/ false,
        },
        {
            /* id            */ "lfm2.5-1.2b",
            /* name          */ "Liquid LFM2.5 1.2B Instruct",
            /* filename      */ "LFM2.5-1.2B-Instruct-Q4_K_M.gguf",
            /* url           */ "https://huggingface.co/LiquidAI/LFM2.5-1.2B-Instruct-GGUF/resolve/main/LFM2.5-1.2B-Instruct-Q4_K_M.gguf",
            /* family        */ "lfm2",
            /* size_mb       */ 731,
            /* priority      */ 25,
            /* speed_est     */ "~180 t/s",
            /* tool_calling  */ "Good",
            /* description   */ "Next-gen Liquid model. Tool calling via chat template, 128K context.",
            /* is_default    */ false,
            /* is_recommended*/ false,
        },
        {
            /* id            */ "lfm2-2.6b",
            /* name          */ "Liquid LFM2 2.6B",
            /* filename      */ "LFM2-2.6B-Q4_K_M.gguf",
            /* url           */ "https://huggingface.co/LiquidAI/LFM2-2.6B-GGUF/resolve/main/LFM2-2.6B-Q4_K_M.gguf",
            /* family        */ "lfm2",
            /* size_mb       */ 1480,
            /* priority      */ 35,
            /* speed_est     */ "~120 t/s",
            /* tool_calling  */ "Good",
            /* description   */ "Larger Liquid model. Strong conversational + 128K context.",
            /* is_default    */ false,
            /* is_recommended*/ false,
        },
        {
            /* id            */ "qwen3.5-2b",
            /* name          */ "Qwen3.5 2B",
            /* filename      */ "qwen3.5-2b-q4_k_m.gguf",
            /* url           */ "https://huggingface.co/unsloth/Qwen3.5-2B-GGUF/resolve/main/Qwen3.5-2B-Q4_K_M.gguf",
            /* family        */ "qwen3",
            /* size_mb       */ 1200,
            /* priority      */ 30,
            /* speed_est     */ "~150 t/s",
            /* tool_calling  */ "Good",
            /* description   */ "Good all-rounder. Solid tool calling + conversations.",
            /* is_default    */ false,
            /* is_recommended*/ false,
        },
        {
            /* id            */ "qwen3-4b",
            /* name          */ "Qwen3 4B",
            /* filename      */ "qwen3-4b-q4_k_m.gguf",
            /* url           */ "https://huggingface.co/Qwen/Qwen3-4B-GGUF/resolve/main/Qwen3-4B-Q4_K_M.gguf",
            /* family        */ "qwen3",
            /* size_mb       */ 2500,
            /* priority      */ 40,
            /* speed_est     */ "~80 t/s",
            /* tool_calling  */ "Good",
            /* description   */ "Smart conversations and reasoning. Needs more RAM.",
            /* is_default    */ false,
            /* is_recommended*/ false,
        },
        {
            /* id            */ "qwen3.5-4b",
            /* name          */ "Qwen3.5 4B",
            /* filename      */ "qwen3.5-4b-q4_k_m.gguf",
            /* url           */ "https://huggingface.co/unsloth/Qwen3.5-4B-GGUF/resolve/main/Qwen3.5-4B-Q4_K_M.gguf",
            /* family        */ "qwen3",
            /* size_mb       */ 2700,
            /* priority      */ 50,
            /* speed_est     */ "~75 t/s",
            /* tool_calling  */ "Excellent",
            /* description   */ "Best small model. Native tool calling, 262K context. Recommended.",
            /* is_default    */ false,
            /* is_recommended*/ true,
        },
    };
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Find the highest-priority model that exists in models_dir
inline const LlmModelDef* find_best_installed(const std::string& models_dir,
                                                const std::vector<LlmModelDef>& models) {
    const LlmModelDef* best = nullptr;
    for (auto& m : models) {
        std::string path = models_dir + "/" + m.filename;
        FILE* f = fopen(path.c_str(), "r");
        if (f) {
            fclose(f);
            if (!best || m.priority > best->priority)
                best = &m;
        }
    }
    return best;
}

// Find model by id
inline const LlmModelDef* find_model_by_id(const std::string& id,
                                             const std::vector<LlmModelDef>& models) {
    for (auto& m : models) {
        if (m.id == id) return &m;
    }
    return nullptr;
}

// Find model by filename
inline const LlmModelDef* find_model_by_filename(const std::string& filename,
                                                   const std::vector<LlmModelDef>& models) {
    for (auto& m : models) {
        if (m.filename == filename) return &m;
    }
    return nullptr;
}

// Get the default model (the one installed by `rcli setup`)
inline const LlmModelDef* get_default_model(const std::vector<LlmModelDef>& models) {
    for (auto& m : models) {
        if (m.is_default) return &m;
    }
    return nullptr;
}

// Get all upgrade options (non-default models, sorted by priority)
inline std::vector<const LlmModelDef*> get_upgrade_options(const std::vector<LlmModelDef>& models) {
    std::vector<const LlmModelDef*> opts;
    for (auto& m : models) {
        if (!m.is_default) opts.push_back(&m);
    }
    return opts;
}

// Format size for display: 456 -> "456MB", 2500 -> "2.5GB"
inline std::string format_size(int mb) {
    if (mb >= 1000) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.1fGB", mb / 1000.0);
        return buf;
    }
    return std::to_string(mb) + "MB";
}

// ---------------------------------------------------------------------------
// Config persistence — stores user's model selection
// Config file: ~/Library/RCLI/config
// Format: simple key=value, one per line. Currently only "model=<id>".
// ---------------------------------------------------------------------------

inline std::string config_path() {
    const char* home = getenv("HOME");
    if (!home) return "";
    return std::string(home) + "/Library/RCLI/config";
}

// Read the user-selected model id from config (empty string if not set)
inline std::string read_selected_model_id() {
    std::string path = config_path();
    if (path.empty()) return "";
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return "";
    char line[256];
    std::string result;
    while (fgets(line, sizeof(line), f)) {
        std::string s(line);
        if (s.find("model=") == 0) {
            result = s.substr(6);
            while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
                result.pop_back();
            break;
        }
    }
    fclose(f);
    return result;
}

// Write the user-selected model id to config (preserves other keys like tts_model=, stt_model=)
inline bool write_selected_model_id(const std::string& model_id) {
    std::string path = config_path();
    if (path.empty()) return false;

    std::string dir = path.substr(0, path.rfind('/'));
    std::string cmd = "mkdir -p '" + dir + "'";
    (void)system(cmd.c_str());

    std::vector<std::string> lines;
    bool found = false;
    FILE* f = fopen(path.c_str(), "r");
    if (f) {
        char buf[512];
        while (fgets(buf, sizeof(buf), f)) {
            std::string s(buf);
            if (s.find("model=") == 0) {
                lines.push_back("model=" + model_id + "\n");
                found = true;
            } else {
                lines.push_back(s);
            }
        }
        fclose(f);
    }
    if (!found) lines.push_back("model=" + model_id + "\n");

    f = fopen(path.c_str(), "w");
    if (!f) return false;
    for (auto& l : lines) fputs(l.c_str(), f);
    fclose(f);
    return true;
}

// Clear the user's model selection (preserves other keys)
inline bool clear_selected_model() {
    std::string path = config_path();
    if (path.empty()) return false;

    std::vector<std::string> lines;
    FILE* f = fopen(path.c_str(), "r");
    if (f) {
        char buf[512];
        while (fgets(buf, sizeof(buf), f)) {
            std::string s(buf);
            if (s.find("model=") != 0) lines.push_back(s);
        }
        fclose(f);
    }

    f = fopen(path.c_str(), "w");
    if (!f) return false;
    for (auto& l : lines) fputs(l.c_str(), f);
    fclose(f);
    return true;
}

// ---------------------------------------------------------------------------
// Resolve which model to use: user preference > auto-detect (highest priority)
// ---------------------------------------------------------------------------
inline const LlmModelDef* resolve_active_model(const std::string& models_dir,
                                                 const std::vector<LlmModelDef>& models) {
    // 1. Check user preference
    std::string selected = read_selected_model_id();
    if (!selected.empty()) {
        const auto* m = find_model_by_id(selected, models);
        if (m) {
            std::string path = models_dir + "/" + m->filename;
            FILE* f = fopen(path.c_str(), "r");
            if (f) { fclose(f); return m; }
            // Selected model not installed — fall through to auto-detect
        }
    }

    // 2. Auto-detect: highest-priority installed model
    return find_best_installed(models_dir, models);
}

} // namespace rcli
