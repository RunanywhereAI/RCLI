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
#include <unistd.h>

namespace rcli {

enum class LlmBackendType { LLAMACPP, METALRT, BOTH };

struct LlmModelDef {
    std::string id;            // Unique slug: "qwen3.5-4b"
    std::string name;          // Display name: "Qwen3.5 4B"
    std::string filename;      // Local GGUF filename (stored in models_dir)
    std::string url;           // HuggingFace direct download URL
    std::string family;        // Model family (maps to ModelProfile): "qwen3", "lfm2", etc.
    int         size_mb;       // Approximate Q4_K_M download size in MB
    int         priority;      // Auto-detect priority (higher = preferred). Default model = 0.
    std::string speed_est;     // Speed estimate on Apple Silicon M-series (llama.cpp)
    std::string tool_calling;  // Tool calling quality: "Basic", "Good", "Excellent"
    std::string description;   // One-line description for UI
    bool        is_default;    // Ships with `rcli setup` (only one should be true)
    bool        is_recommended;// Highlighted as recommended in `upgrade-llm`

    // MetalRT fields (empty/false if not supported)
    bool        metalrt_supported = false;
    std::string metalrt_id;             // e.g. "metalrt-qwen3-0.6b"
    std::string metalrt_url;            // HuggingFace MLX safetensors URL
    std::string metalrt_tokenizer_url;  // HuggingFace tokenizer.json URL
    std::string metalrt_dir_name;       // local dir under ~/Library/RCLI/models/metalrt/
    int         metalrt_size_mb = 0;    // MLX weight size
    std::string metalrt_speed_est;      // e.g. "~486 tok/s"
    std::string metalrt_family;         // for config.json: "Qwen3", "Llama-3.2", "LFM2.5"
    std::string metalrt_size;           // for config.json: "0.6B", "4B", "3B", "1.2B"
    std::string metalrt_name;           // for config.json: "Qwen3-0.6B-MLX-4bit"
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
            /* metalrt_supported   */ true,
            /* metalrt_id          */ "metalrt-qwen3-0.6b",
            /* metalrt_url         */ "https://huggingface.co/runanywhere/qwen3_0.6B_MLX_4bit/resolve/main/Qwen3-0.6B-MLX-4bit/model.safetensors",
            /* metalrt_tokenizer   */ "https://huggingface.co/runanywhere/qwen3_0.6B_MLX_4bit/resolve/main/Qwen3-0.6B-MLX-4bit/tokenizer.json",
            /* metalrt_dir_name    */ "Qwen3-0.6B-MLX-4bit",
            /* metalrt_size_mb     */ 300,
            /* metalrt_speed_est   */ "~486 t/s",
            /* metalrt_family      */ "Qwen3",
            /* metalrt_size        */ "0.6B",
            /* metalrt_name        */ "Qwen3-0.6B-MLX-4bit",
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
            /* metalrt_supported   */ true,
            /* metalrt_id          */ "metalrt-lfm2.5-1.2b",
            /* metalrt_url         */ "https://huggingface.co/LiquidAI/LFM2.5-1.2B-Instruct-MLX-4bit/resolve/main/model.safetensors",
            /* metalrt_tokenizer   */ "https://huggingface.co/LiquidAI/LFM2.5-1.2B-Instruct-MLX-4bit/resolve/main/tokenizer.json",
            /* metalrt_dir_name    */ "LFM2.5-1.2B-MLX-4bit",
            /* metalrt_size_mb     */ 800,
            /* metalrt_speed_est   */ "~250 t/s",
            /* metalrt_family      */ "LFM2.5",
            /* metalrt_size        */ "1.2B",
            /* metalrt_name        */ "LFM2.5-1.2B-MLX-4bit",
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
            /* metalrt_supported   */ true,
            /* metalrt_id          */ "metalrt-qwen3-4b",
            /* metalrt_url         */ "https://huggingface.co/runanywhere/qwen3_4B_mlx_4bit/resolve/main/Qwen3-4B-MLX-4bit/model.safetensors",
            /* metalrt_tokenizer   */ "https://huggingface.co/runanywhere/qwen3_4B_mlx_4bit/resolve/main/Qwen3-4B-MLX-4bit/tokenizer.json",
            /* metalrt_dir_name    */ "Qwen3-4B-MLX-4bit",
            /* metalrt_size_mb     */ 2500,
            /* metalrt_speed_est   */ "~180 t/s",
            /* metalrt_family      */ "Qwen3",
            /* metalrt_size        */ "4B",
            /* metalrt_name        */ "Qwen3-4B-MLX-4bit",
        },
        {
            /* id            */ "llama3.2-3b",
            /* name          */ "Llama 3.2 3B Instruct",
            /* filename      */ "llama-3.2-3b-instruct-q4_k_m.gguf",
            /* url           */ "https://huggingface.co/bartowski/Llama-3.2-3B-Instruct-GGUF/resolve/main/Llama-3.2-3B-Instruct-Q4_K_M.gguf",
            /* family        */ "llama3",
            /* size_mb       */ 1800,
            /* priority      */ 45,
            /* speed_est     */ "~100 t/s",
            /* tool_calling  */ "Good",
            /* description   */ "Meta's Llama 3.2. Strong reasoning and tool calling.",
            /* is_default    */ false,
            /* is_recommended*/ false,
            /* metalrt_supported   */ true,
            /* metalrt_id          */ "metalrt-llama3.2-3b",
            /* metalrt_url         */ "https://huggingface.co/runanywhere/Llama_32_3B_4bit/resolve/main/Llama-3.2-3B-Instruct-4bit/model.safetensors",
            /* metalrt_tokenizer   */ "https://huggingface.co/runanywhere/Llama_32_3B_4bit/resolve/main/Llama-3.2-3B-Instruct-4bit/tokenizer.json",
            /* metalrt_dir_name    */ "Llama-3.2-3B-Instruct-MLX-4bit",
            /* metalrt_size_mb     */ 1800,
            /* metalrt_speed_est   */ "~150 t/s",
            /* metalrt_family      */ "Llama-3.2",
            /* metalrt_size        */ "3B",
            /* metalrt_name        */ "Llama-3.2-3B-Instruct-MLX-4bit",
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
// Engine-scoped model filtering
// ---------------------------------------------------------------------------

inline std::vector<LlmModelDef> models_for_engine(LlmBackendType engine) {
    auto all = all_models();
    if (engine == LlmBackendType::LLAMACPP) return all;
    if (engine == LlmBackendType::METALRT) {
        std::vector<LlmModelDef> result;
        for (auto& m : all)
            if (m.metalrt_supported) result.push_back(m);
        return result;
    }
    return all;  // BOTH: everything
}

inline std::string engine_label(const LlmModelDef& m) {
    if (m.metalrt_supported && !m.filename.empty()) return "both";
    if (m.metalrt_supported) return "MetalRT only";
    return "llama.cpp";
}

inline std::string metalrt_models_dir() {
    const char* home = getenv("HOME");
    if (!home) return "";
    return std::string(home) + "/Library/RCLI/models/metalrt";
}

inline bool is_metalrt_model_installed(const LlmModelDef& m) {
    if (!m.metalrt_supported) return false;
    std::string dir = metalrt_models_dir() + "/" + m.metalrt_dir_name;
    std::string safetensors = dir + "/model.safetensors";
    return access(safetensors.c_str(), R_OK) == 0;
}

// ---------------------------------------------------------------------------
// MetalRT STT/TTS component models
// ---------------------------------------------------------------------------

struct MetalRTComponentModel {
    std::string id;
    std::string name;
    std::string component;       // "stt" or "tts"
    std::string hf_repo;         // HuggingFace repo path (org/repo)
    std::string hf_subdir;       // subdirectory within repo (empty for flat repos)
    std::string dir_name;        // local dir under metalrt_models_dir()
    int         size_mb;
    std::string description;
    std::string tokenizer_hf_repo;  // Separate repo for tokenizer.json (if not in hf_repo)
};

inline std::vector<MetalRTComponentModel> metalrt_component_models() {
    return {
        {
            "metalrt-whisper-tiny",
            "Whisper Tiny (MLX 4-bit)",
            "stt",
            "runanywhere/whisper_tiny_4bit",
            "whisper-tiny-mlx-4bit",
            "whisper-tiny-mlx-4bit",
            40,
            "Fastest transcription, lower accuracy (~10% WER)",
            "",
        },
        {
            "metalrt-whisper-small",
            "Whisper Small (MLX 4-bit)",
            "stt",
            "runanywhere/whisper_small_4bit",
            "whisper-small-mlx-4bit",
            "whisper-small-mlx-4bit",
            375,
            "Good balance of speed and accuracy (~5% WER)",
            "",
        },
        {
            "metalrt-whisper-medium",
            "Whisper Medium (MLX 4-bit)",
            "stt",
            "runanywhere/whisper_medium_4bit",
            "whisper-medium-mlx-4bit",
            "whisper-medium-mlx-4bit",
            980,
            "Best accuracy, slower (~3% WER)",
            "",
        },
        {
            "metalrt-kokoro-82m",
            "Kokoro 82M (bf16)",
            "tts",
            "runanywhere/kokoro_bf16",
            "Kokoro-82M-bf16",
            "Kokoro-82M-bf16",
            82,
            "Kokoro TTS for GPU-accelerated speech synthesis",
            "",
        },
    };
}

inline bool is_metalrt_component_installed(const MetalRTComponentModel& m) {
    std::string dir = metalrt_models_dir() + "/" + m.dir_name;
    if (access(dir.c_str(), R_OK) != 0) return false;

    if (m.component == "tts") {
        std::string model_weights = dir + "/kokoro-v1_0.safetensors";
        std::string voice_file    = dir + "/voices/af_heart.safetensors";
        return access(model_weights.c_str(), R_OK) == 0 &&
               access(voice_file.c_str(), R_OK) == 0;
    }

    std::string safetensors = dir + "/model.safetensors";
    std::string weights_npz = dir + "/weights.npz";
    std::string tokenizer   = dir + "/tokenizer.json";
    bool has_weights = access(safetensors.c_str(), R_OK) == 0 ||
                       access(weights_npz.c_str(), R_OK) == 0;
    bool has_tokenizer = access(tokenizer.c_str(), R_OK) == 0;
    return has_weights && has_tokenizer;
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

// Read the engine preference from config (empty string if not set)
inline std::string read_engine_preference() {
    std::string path = config_path();
    if (path.empty()) return "";
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return "";
    char line[256];
    std::string result;
    while (fgets(line, sizeof(line), f)) {
        std::string s(line);
        if (s.find("engine=") == 0) {
            result = s.substr(7);
            while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
                result.pop_back();
            break;
        }
    }
    fclose(f);
    return result;
}

// Write engine preference to config (preserves other keys)
inline bool write_engine_preference(const std::string& engine) {
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
            if (s.find("engine=") == 0) {
                lines.push_back("engine=" + engine + "\n");
                found = true;
            } else {
                lines.push_back(s);
            }
        }
        fclose(f);
    }
    if (!found) lines.push_back("engine=" + engine + "\n");

    f = fopen(path.c_str(), "w");
    if (!f) return false;
    for (auto& l : lines) fputs(l.c_str(), f);
    fclose(f);
    return true;
}

// Read personality preference from config
inline std::string read_personality_preference() {
    std::string path = config_path();
    if (path.empty()) return "";
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return "";
    char line[256];
    std::string result;
    while (fgets(line, sizeof(line), f)) {
        std::string s(line);
        if (s.find("personality=") == 0) {
            result = s.substr(12);
            while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
                result.pop_back();
            break;
        }
    }
    fclose(f);
    return result;
}

// Write personality preference to config (preserves other keys)
inline bool write_personality_preference(const std::string& personality) {
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
            if (s.find("personality=") == 0) {
                lines.push_back("personality=" + personality + "\n");
                found = true;
            } else {
                lines.push_back(s);
            }
        }
        fclose(f);
    }
    if (!found) lines.push_back("personality=" + personality + "\n");

    f = fopen(path.c_str(), "w");
    if (!f) return false;
    for (auto& l : lines) fputs(l.c_str(), f);
    fclose(f);
    return true;
}

// Read the user-selected MetalRT STT model id from config
inline std::string read_selected_metalrt_stt_id() {
    std::string path = config_path();
    if (path.empty()) return "";
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return "";
    char line[256];
    std::string result;
    while (fgets(line, sizeof(line), f)) {
        std::string s(line);
        if (s.find("metalrt_stt=") == 0) {
            result = s.substr(12);
            while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
                result.pop_back();
            break;
        }
    }
    fclose(f);
    return result;
}

// Write the user-selected MetalRT STT model id to config
inline bool write_selected_metalrt_stt_id(const std::string& id) {
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
            if (s.find("metalrt_stt=") == 0) {
                lines.push_back("metalrt_stt=" + id + "\n");
                found = true;
            } else {
                lines.push_back(s);
            }
        }
        fclose(f);
    }
    if (!found) lines.push_back("metalrt_stt=" + id + "\n");

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
