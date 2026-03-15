#pragma once
// =============================================================================
// RCLI VLM Model Registry
// =============================================================================
//
// Registry of supported VLM (Vision Language Model) models.
// Each model consists of a language model GGUF + an mmproj (vision projector) GGUF.
//
// =============================================================================

#include <string>
#include <vector>
#include <unistd.h>

namespace rcli {

struct VlmModelDef {
    std::string id;               // Unique slug: "smolvlm-500m"
    std::string name;             // Display name: "SmolVLM 500M Instruct"
    std::string model_filename;   // Language model GGUF filename
    std::string mmproj_filename;  // Vision projector GGUF filename
    std::string model_url;        // HuggingFace download URL for language model
    std::string mmproj_url;       // HuggingFace download URL for mmproj
    int         model_size_mb;    // Approximate model download size
    int         mmproj_size_mb;   // Approximate mmproj download size
    std::string description;      // One-line description
    bool        is_default;       // Default model for `rcli vlm`
};

inline std::vector<VlmModelDef> all_vlm_models() {
    return {
        {
            /* id              */ "qwen3-vl-2b",
            /* name            */ "Qwen3 VL 2B Instruct",
            /* model_filename  */ "Qwen3-VL-2B-Instruct-Q8_0.gguf",
            /* mmproj_filename */ "mmproj-Qwen3-VL-2B-Instruct-Q8_0.gguf",
            /* model_url       */ "https://huggingface.co/ggml-org/Qwen3-VL-2B-Instruct-GGUF/resolve/main/Qwen3-VL-2B-Instruct-Q8_0.gguf",
            /* mmproj_url      */ "https://huggingface.co/ggml-org/Qwen3-VL-2B-Instruct-GGUF/resolve/main/mmproj-Qwen3-VL-2B-Instruct-Q8_0.gguf",
            /* model_size_mb   */ 1830,
            /* mmproj_size_mb  */ 445,
            /* description     */ "Qwen3 Vision-Language model. High quality image analysis.",
            /* is_default      */ false,
        },
        {
            /* id              */ "lfm2-vl-1.6b",
            /* name            */ "Liquid LFM2 VL 1.6B",
            /* model_filename  */ "LFM2-VL-1.6B-Q8_0.gguf",
            /* mmproj_filename */ "mmproj-LFM2-VL-1.6B-Q8_0.gguf",
            /* model_url       */ "https://huggingface.co/LiquidAI/LFM2-VL-1.6B-GGUF/resolve/main/LFM2-VL-1.6B-Q8_0.gguf",
            /* mmproj_url      */ "https://huggingface.co/LiquidAI/LFM2-VL-1.6B-GGUF/resolve/main/mmproj-LFM2-VL-1.6B-Q8_0.gguf",
            /* model_size_mb   */ 1250,
            /* mmproj_size_mb  */ 210,
            /* description     */ "Liquid Foundation Model for vision. Fast, 128K context.",
            /* is_default      */ false,
        },
        {
            /* id              */ "smolvlm-500m",
            /* name            */ "SmolVLM 500M Instruct",
            /* model_filename  */ "SmolVLM-500M-Instruct-Q8_0.gguf",
            /* mmproj_filename */ "mmproj-SmolVLM-500M-Instruct-Q8_0.gguf",
            /* model_url       */ "https://huggingface.co/ggml-org/SmolVLM-500M-Instruct-GGUF/resolve/main/SmolVLM-500M-Instruct-Q8_0.gguf",
            /* mmproj_url      */ "https://huggingface.co/ggml-org/SmolVLM-500M-Instruct-GGUF/resolve/main/mmproj-SmolVLM-500M-Instruct-Q8_0.gguf",
            /* model_size_mb   */ 437,
            /* mmproj_size_mb  */ 109,
            /* description     */ "Smallest VLM. Fast image analysis, lower quality.",
            /* is_default      */ false,
        },
    };
}

inline std::pair<bool, VlmModelDef> get_default_vlm_model() {
    auto models = all_vlm_models();
    for (auto& m : models) {
        if (m.is_default) return {true, m};
    }
    return {false, {}};
}

inline bool is_vlm_model_installed(const std::string& models_dir, const VlmModelDef& m) {
    std::string model_path = models_dir + "/" + m.model_filename;
    std::string mmproj_path = models_dir + "/" + m.mmproj_filename;
    return access(model_path.c_str(), R_OK) == 0 &&
           access(mmproj_path.c_str(), R_OK) == 0;
}

inline std::pair<bool, VlmModelDef> find_installed_vlm(const std::string& models_dir) {
    auto models = all_vlm_models();
    for (auto& m : models) {
        if (is_vlm_model_installed(models_dir, m)) return {true, m};
    }
    return {false, {}};
}

} // namespace rcli
