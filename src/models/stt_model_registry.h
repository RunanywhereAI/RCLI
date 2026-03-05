#pragma once
// =============================================================================
// RCLI STT Model Registry
// =============================================================================
//
// Single source of truth for all supported STT (speech-to-text) models.
// Used by: models command, auto-detect, setup, info, upgrade-stt, and CLI.
//
// HOW TO ADD A NEW STT MODEL (for open-source contributors):
//   1. Add a new entry to the `all_stt_models()` vector below.
//   2. Fill in every field (see SttModelDef docs).
//   3. Set the `priority` field for offline models (higher = preferred).
//   4. That's it! The CLI, models command, and auto-detect will pick it up.
//
// STT has two categories:
//   - STREAMING: real-time mic input (e.g., Zipformer). Always active in live mode.
//   - OFFLINE: batch file transcription (e.g., Whisper, Parakeet). User-switchable.
//
// =============================================================================

#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>

namespace rcli {

struct SttModelDef {
    std::string id;            // Unique slug: "parakeet-tdt"
    std::string name;          // Display name: "Parakeet TDT 0.6B v3"
    std::string backend;       // "zipformer", "whisper", "nemo_transducer"
    std::string category;      // "streaming" or "offline"
    std::string dir_name;      // Local directory name under models/
    std::string download_url;  // tar.bz2 URL (empty if bundled with setup)
    int         size_mb;       // Approximate download size in MB
    int         priority;      // Auto-detect priority for offline models (higher = preferred)
    std::string accuracy;      // Accuracy info: "Good", "~5% WER", "~1.9% WER"
    std::string description;   // One-line description for UI
    bool        is_default;    // Ships with `rcli setup`
    bool        is_recommended;// Highlighted as recommended

    // Architecture-specific file paths (relative to dir_name/)
    std::string encoder_file;
    std::string decoder_file;
    std::string joiner_file;   // Zipformer/Parakeet transducer only
    std::string tokens_file;
    std::string archive_dir;   // Directory name inside tar.bz2 (empty = same as dir_name)
};

// ---------------------------------------------------------------------------
// All supported STT models
// ---------------------------------------------------------------------------
inline std::vector<SttModelDef> all_stt_models() {
    return {
        // =================================================================
        // Streaming (always active for live mic input)
        // =================================================================
        {
            /* id            */ "zipformer",
            /* name          */ "Zipformer (Streaming)",
            /* backend       */ "zipformer",
            /* category      */ "streaming",
            /* dir_name      */ "zipformer",
            /* download_url  */ "",  // Bundled with setup
            /* size_mb       */ 50,
            /* priority      */ 0,
            /* accuracy      */ "Good",
            /* description   */ "Real-time streaming STT. Always active for live mic.",
            /* is_default    */ true,
            /* is_recommended*/ false,
            /* encoder_file  */ "encoder-epoch-99-avg-1.int8.onnx",
            /* decoder_file  */ "decoder-epoch-99-avg-1.int8.onnx",
            /* joiner_file   */ "joiner-epoch-99-avg-1.int8.onnx",
            /* tokens_file   */ "tokens.txt",
            /* archive_dir   */ "sherpa-onnx-streaming-zipformer-en-20M-2023-02-17",
        },

        // =================================================================
        // Offline (user-switchable for batch/file transcription)
        // =================================================================
        {
            /* id            */ "whisper-base",
            /* name          */ "Whisper base.en",
            /* backend       */ "whisper",
            /* category      */ "offline",
            /* dir_name      */ "whisper-base.en",
            /* download_url  */ "",  // Bundled with setup
            /* size_mb       */ 140,
            /* priority      */ 0,
            /* accuracy      */ "~5% WER",
            /* description   */ "Small and fast. Good accuracy (~5% WER). Recommended for most users.",
            /* is_default    */ true,
            /* is_recommended*/ true,
            /* encoder_file  */ "base.en-encoder.int8.onnx",
            /* decoder_file  */ "base.en-decoder.int8.onnx",
            /* joiner_file   */ "",
            /* tokens_file   */ "base.en-tokens.txt",
            /* archive_dir   */ "sherpa-onnx-whisper-base.en",
        },
        {
            /* id            */ "parakeet-tdt",
            /* name          */ "Parakeet TDT 0.6B v3",
            /* backend       */ "nemo_transducer",
            /* category      */ "offline",
            /* dir_name      */ "parakeet-tdt",
            /* download_url  */ "https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/sherpa-onnx-nemo-parakeet-tdt-0.6b-v3-int8.tar.bz2",
            /* size_mb       */ 640,
            /* priority      */ 50,
            /* accuracy      */ "~1.9% WER",
            /* description   */ "Best accuracy if you need low WER. Auto-punctuation, 25 languages.",
            /* is_default    */ false,
            /* is_recommended*/ true,
            /* encoder_file  */ "encoder.int8.onnx",
            /* decoder_file  */ "decoder.int8.onnx",
            /* joiner_file   */ "joiner.int8.onnx",
            /* tokens_file   */ "tokens.txt",
            /* archive_dir   */ "sherpa-onnx-nemo-parakeet-tdt-0.6b-v3-int8",
        },
    };
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

inline bool is_stt_installed(const std::string& models_dir, const SttModelDef& m) {
    std::string path = models_dir + "/" + m.dir_name + "/" + m.encoder_file;
    FILE* f = fopen(path.c_str(), "r");
    if (f) { fclose(f); return true; }
    return false;
}

// Find the highest-priority offline STT model that is installed
inline const SttModelDef* find_best_installed_stt(const std::string& models_dir,
                                                    const std::vector<SttModelDef>& models) {
    const SttModelDef* best = nullptr;
    for (auto& m : models) {
        if (m.category != "offline") continue;
        if (is_stt_installed(models_dir, m)) {
            if (!best || m.priority > best->priority)
                best = &m;
        }
    }
    return best;
}

inline const SttModelDef* find_stt_by_id(const std::string& id,
                                           const std::vector<SttModelDef>& models) {
    for (auto& m : models) {
        if (m.id == id) return &m;
    }
    return nullptr;
}

inline const SttModelDef* get_default_offline_stt(const std::vector<SttModelDef>& models) {
    for (auto& m : models) {
        if (m.is_default && m.category == "offline") return &m;
    }
    return nullptr;
}

inline std::vector<const SttModelDef*> get_offline_stt_models(const std::vector<SttModelDef>& models) {
    std::vector<const SttModelDef*> result;
    for (auto& m : models) {
        if (m.category == "offline") result.push_back(&m);
    }
    return result;
}

// ---------------------------------------------------------------------------
// Config persistence — key: stt_model=<id> in ~/Library/RCLI/config
// ---------------------------------------------------------------------------

inline std::string stt_config_path() {
    const char* home = getenv("HOME");
    if (!home) return "";
    return std::string(home) + "/Library/RCLI/config";
}

inline std::string read_selected_stt_id() {
    std::string path = stt_config_path();
    if (path.empty()) return "";
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return "";
    char line[256];
    std::string result;
    while (fgets(line, sizeof(line), f)) {
        std::string s(line);
        if (s.find("stt_model=") == 0) {
            result = s.substr(10);
            while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
                result.pop_back();
            break;
        }
    }
    fclose(f);
    return result;
}

inline bool write_selected_stt_id(const std::string& stt_id) {
    std::string path = stt_config_path();
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
            if (s.find("stt_model=") == 0) {
                lines.push_back("stt_model=" + stt_id + "\n");
                found = true;
            } else {
                lines.push_back(s);
            }
        }
        fclose(f);
    }
    if (!found) lines.push_back("stt_model=" + stt_id + "\n");

    f = fopen(path.c_str(), "w");
    if (!f) return false;
    for (auto& l : lines) fputs(l.c_str(), f);
    fclose(f);
    return true;
}

inline bool clear_selected_stt() {
    std::string path = stt_config_path();
    if (path.empty()) return false;

    std::vector<std::string> lines;
    FILE* f = fopen(path.c_str(), "r");
    if (f) {
        char buf[512];
        while (fgets(buf, sizeof(buf), f)) {
            std::string s(buf);
            if (s.find("stt_model=") != 0) lines.push_back(s);
        }
        fclose(f);
    }

    f = fopen(path.c_str(), "w");
    if (!f) return false;
    for (auto& l : lines) fputs(l.c_str(), f);
    fclose(f);
    return true;
}

// Resolve which offline STT to use: user preference > auto-detect (highest priority)
inline const SttModelDef* resolve_active_stt(const std::string& models_dir,
                                              const std::vector<SttModelDef>& models) {
    std::string selected = read_selected_stt_id();
    if (!selected.empty()) {
        const auto* m = find_stt_by_id(selected, models);
        if (m && m->category == "offline" && is_stt_installed(models_dir, *m))
            return m;
    }
    return find_best_installed_stt(models_dir, models);
}

} // namespace rcli
