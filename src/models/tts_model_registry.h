#pragma once
// =============================================================================
// RCLI TTS Model Registry
// =============================================================================
//
// Single source of truth for all supported TTS (text-to-speech) voices.
// Used by: voices command, auto-detect, setup, info, and the interactive CLI.
//
// HOW TO ADD A NEW TTS MODEL (for open-source contributors):
//   1. Add a new entry to the `all_tts_models()` vector below.
//   2. Fill in every field (see TtsModelDef docs).
//   3. Set the `priority` field: higher = preferred when multiple are installed.
//   4. Make sure the `architecture` matches one of: "vits", "kokoro", "kitten", "matcha"
//   5. That's it! The CLI, voices command, and auto-detect will pick it up.
//
// All models run through sherpa-onnx which natively supports each architecture.
// No code changes to sherpa-onnx are needed — just populate the right config fields.
//
// =============================================================================

#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>

namespace rcli {

struct TtsModelDef {
    std::string id;            // Unique slug: "kokoro-en-v0.19"
    std::string name;          // Display name: "Kokoro English v0.19"
    std::string architecture;  // sherpa-onnx architecture: "vits", "kokoro", "kitten", "matcha"
    std::string dir_name;      // Local directory name under models/ (also used for install check)
    std::string download_url;  // URL (tar.bz2 archive from sherpa-onnx releases)
    int         size_mb;       // Approximate download size in MB
    int         num_speakers;  // Number of speaker/voice options
    int         priority;      // Auto-detect priority (higher = preferred). Default = 0.
    std::string quality;       // Voice quality: "Good", "Great", "Excellent"
    std::string description;   // One-line description for UI
    bool        is_default;    // Ships with `rcli setup`
    bool        is_recommended;// Highlighted as recommended in `rcli voices`

    // Architecture-specific file paths (relative to dir_name/)
    std::string model_file;    // ONNX model filename
    std::string tokens_file;   // tokens.txt
    std::string config_file;   // .onnx.json (VITS/Piper only, empty for others)
    std::string voices_file;   // voices.bin (Kokoro/Kitten only, empty for VITS)
    std::string vocoder_file;  // vocoder ONNX (Matcha only, empty for others)
    std::string lexicon_file;  // lexicon file (Kokoro multi-lang only)
    std::string lang;          // language code (Kokoro multi-lang only, e.g. "en-us")
    bool        needs_espeak;  // Whether espeak-ng-data/ is required as phonemizer
    std::string archive_dir;   // Directory name inside tar.bz2 (empty = same as dir_name)
};

// ---------------------------------------------------------------------------
// All supported TTS models — the ONLY place TTS model info needs to be defined.
// Sorted by priority (lowest first, highest last).
// ---------------------------------------------------------------------------
inline std::vector<TtsModelDef> all_tts_models() {
    return {
        // =================================================================
        // Default (ships with `rcli setup`)
        // =================================================================
        {
            /* id            */ "piper-lessac",
            /* name          */ "Piper Lessac (English)",
            /* architecture  */ "vits",
            /* dir_name      */ "piper-voice",
            /* download_url  */ "",  // Downloaded by setup inline (not a tar.bz2)
            /* size_mb       */ 60,
            /* num_speakers  */ 1,
            /* priority      */ 0,
            /* quality       */ "Good",
            /* description   */ "Fastest TTS. Low latency, good quality. Recommended.",
            /* is_default    */ true,
            /* is_recommended*/ true,
            /* model_file    */ "en_US-lessac-medium.onnx",
            /* tokens_file   */ "tokens.txt",
            /* config_file   */ "en_US-lessac-medium.onnx.json",
            /* voices_file   */ "",
            /* vocoder_file  */ "",
            /* lexicon_file  */ "",
            /* lang          */ "",
            /* needs_espeak  */ true,
            /* archive_dir   */ "vits-piper-en_US-lessac-medium",
        },

        // =================================================================
        // Upgrade options (downloaded via `rcli voices`)
        // =================================================================
        {
            /* id            */ "piper-amy",
            /* name          */ "Piper Amy (English)",
            /* architecture  */ "vits",
            /* dir_name      */ "piper-amy",
            /* download_url  */ "https://github.com/k2-fsa/sherpa-onnx/releases/download/tts-models/vits-piper-en_US-amy-medium.tar.bz2",
            /* size_mb       */ 60,
            /* num_speakers  */ 1,
            /* priority      */ 5,
            /* quality       */ "Good",
            /* description   */ "Warm female voice. Alternative Piper tone.",
            /* is_default    */ false,
            /* is_recommended*/ false,
            /* model_file    */ "en_US-amy-medium.onnx",
            /* tokens_file   */ "tokens.txt",
            /* config_file   */ "en_US-amy-medium.onnx.json",
            /* voices_file   */ "",
            /* vocoder_file  */ "",
            /* lexicon_file  */ "",
            /* lang          */ "",
            /* needs_espeak  */ true,
            /* archive_dir   */ "vits-piper-en_US-amy-medium",
        },
        {
            /* id            */ "kitten-nano",
            /* name          */ "KittenTTS Nano (English)",
            /* architecture  */ "kitten",
            /* dir_name      */ "kitten-nano-en-v0_1-fp16",
            /* download_url  */ "https://github.com/k2-fsa/sherpa-onnx/releases/download/tts-models/kitten-nano-en-v0_1-fp16.tar.bz2",
            /* size_mb       */ 90,
            /* num_speakers  */ 8,
            /* priority      */ 20,
            /* quality       */ "Great",
            /* description   */ "Lightweight, 8 voices (4M/4F). Good quality for size.",
            /* is_default    */ false,
            /* is_recommended*/ false,
            /* model_file    */ "model.fp16.onnx",
            /* tokens_file   */ "tokens.txt",
            /* config_file   */ "",
            /* voices_file   */ "voices.bin",
            /* vocoder_file  */ "",
            /* lexicon_file  */ "",
            /* lang          */ "",
            /* needs_espeak  */ true,
            /* archive_dir   */ "",
        },
        {
            /* id            */ "matcha-ljspeech",
            /* name          */ "Matcha LJSpeech (English)",
            /* architecture  */ "matcha",
            /* dir_name      */ "matcha-icefall-en_US-ljspeech",
            /* download_url  */ "https://github.com/k2-fsa/sherpa-onnx/releases/download/tts-models/matcha-icefall-en_US-ljspeech.tar.bz2",
            /* size_mb       */ 100,
            /* num_speakers  */ 1,
            /* priority      */ 15,
            /* quality       */ "Great",
            /* description   */ "Fast synthesis, clear female voice. Matcha architecture.",
            /* is_default    */ false,
            /* is_recommended*/ false,
            /* model_file    */ "model-steps-3.onnx",
            /* tokens_file   */ "tokens.txt",
            /* config_file   */ "",
            /* voices_file   */ "",
            /* vocoder_file  */ "hifigan_v2.onnx",
            /* lexicon_file  */ "",
            /* lang          */ "",
            /* needs_espeak  */ true,
            /* archive_dir   */ "",
        },
        {
            /* id            */ "kokoro-en",
            /* name          */ "Kokoro English v0.19",
            /* architecture  */ "kokoro",
            /* dir_name      */ "kokoro-en-v0_19",
            /* download_url  */ "https://github.com/k2-fsa/sherpa-onnx/releases/download/tts-models/kokoro-en-v0_19.tar.bz2",
            /* size_mb       */ 310,
            /* num_speakers  */ 11,
            /* priority      */ 40,
            /* quality       */ "Excellent",
            /* description   */ "Best English quality. 11 natural-sounding voices.",
            /* is_default    */ false,
            /* is_recommended*/ true,
            /* model_file    */ "model.onnx",
            /* tokens_file   */ "tokens.txt",
            /* config_file   */ "",
            /* voices_file   */ "voices.bin",
            /* vocoder_file  */ "",
            /* lexicon_file  */ "",
            /* lang          */ "",
            /* needs_espeak  */ true,
            /* archive_dir   */ "",
        },
        {
            /* id            */ "kokoro-multi",
            /* name          */ "Kokoro Multi-lang v1.1",
            /* architecture  */ "kokoro",
            /* dir_name      */ "kokoro-multi-lang-v1_1",
            /* download_url  */ "https://github.com/k2-fsa/sherpa-onnx/releases/download/tts-models/kokoro-multi-lang-v1_1.tar.bz2",
            /* size_mb       */ 500,
            /* num_speakers  */ 103,
            /* priority      */ 50,
            /* quality       */ "Excellent",
            /* description   */ "103 speakers, Chinese + English. Best multi-language voice.",
            /* is_default    */ false,
            /* is_recommended*/ false,
            /* model_file    */ "model.onnx",
            /* tokens_file   */ "tokens.txt",
            /* config_file   */ "",
            /* voices_file   */ "voices.bin",
            /* vocoder_file  */ "",
            /* lexicon_file  */ "lexicon-us-en.txt",
            /* lang          */ "en-us",
            /* needs_espeak  */ true,
            /* archive_dir   */ "",
        },
    };
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Check if a TTS model is fully installed (all required files exist)
inline bool is_tts_installed(const std::string& models_dir, const TtsModelDef& m) {
    auto check_file = [&](const std::string& rel) -> bool {
        if (rel.empty()) return true;
        std::string path = models_dir + "/" + m.dir_name + "/" + rel;
        FILE* f = fopen(path.c_str(), "r");
        if (f) { fclose(f); return true; }
        return false;
    };
    if (!check_file(m.model_file))  return false;
    if (!check_file(m.tokens_file)) return false;
    if (!check_file(m.config_file)) return false;
    if (!check_file(m.voices_file)) return false;
    if (!check_file(m.vocoder_file)) return false;
    if (m.needs_espeak) {
        std::string espeak = models_dir + "/espeak-ng-data";
        struct stat st;
        if (stat(espeak.c_str(), &st) != 0 || !S_ISDIR(st.st_mode))
            return false;
    }
    return true;
}

// Find the highest-priority TTS model that exists in models_dir
inline const TtsModelDef* find_best_installed_tts(const std::string& models_dir,
                                                    const std::vector<TtsModelDef>& models) {
    const TtsModelDef* best = nullptr;
    for (auto& m : models) {
        if (is_tts_installed(models_dir, m)) {
            if (!best || m.priority > best->priority)
                best = &m;
        }
    }
    return best;
}

// Find TTS model by id
inline const TtsModelDef* find_tts_by_id(const std::string& id,
                                           const std::vector<TtsModelDef>& models) {
    for (auto& m : models) {
        if (m.id == id) return &m;
    }
    return nullptr;
}

// Get the default TTS model
inline const TtsModelDef* get_default_tts(const std::vector<TtsModelDef>& models) {
    for (auto& m : models) {
        if (m.is_default) return &m;
    }
    return nullptr;
}

// Get all TTS upgrade options (non-default models)
inline std::vector<const TtsModelDef*> get_tts_upgrade_options(const std::vector<TtsModelDef>& models) {
    std::vector<const TtsModelDef*> opts;
    for (auto& m : models) {
        if (!m.is_default) opts.push_back(&m);
    }
    return opts;
}

// ---------------------------------------------------------------------------
// Config persistence — stores user's TTS voice selection
// Uses same config file as LLM: ~/Library/RCLI/config
// Key: tts_model=<id>
// ---------------------------------------------------------------------------

inline std::string tts_config_path() {
    const char* home = getenv("HOME");
    if (!home) return "";
    return std::string(home) + "/Library/RCLI/config";
}

// Read the user-selected TTS model id from config
inline std::string read_selected_tts_id() {
    std::string path = tts_config_path();
    if (path.empty()) return "";
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return "";
    char line[256];
    std::string result;
    while (fgets(line, sizeof(line), f)) {
        std::string s(line);
        if (s.find("tts_model=") == 0) {
            result = s.substr(10);
            while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
                result.pop_back();
            break;
        }
    }
    fclose(f);
    return result;
}

// Write TTS selection to config (preserves other keys like model=)
inline bool write_selected_tts_id(const std::string& tts_id) {
    std::string path = tts_config_path();
    if (path.empty()) return false;

    std::string dir = path.substr(0, path.rfind('/'));
    std::string cmd = "mkdir -p '" + dir + "'";
    (void)system(cmd.c_str());

    // Read existing lines, replace/add tts_model=
    std::vector<std::string> lines;
    bool found = false;
    FILE* f = fopen(path.c_str(), "r");
    if (f) {
        char buf[512];
        while (fgets(buf, sizeof(buf), f)) {
            std::string s(buf);
            if (s.find("tts_model=") == 0) {
                lines.push_back("tts_model=" + tts_id + "\n");
                found = true;
            } else {
                lines.push_back(s);
            }
        }
        fclose(f);
    }
    if (!found) lines.push_back("tts_model=" + tts_id + "\n");

    f = fopen(path.c_str(), "w");
    if (!f) return false;
    for (auto& l : lines) fputs(l.c_str(), f);
    fclose(f);
    return true;
}

// Clear TTS selection (preserves other keys)
inline bool clear_selected_tts() {
    std::string path = tts_config_path();
    if (path.empty()) return false;

    std::vector<std::string> lines;
    FILE* f = fopen(path.c_str(), "r");
    if (f) {
        char buf[512];
        while (fgets(buf, sizeof(buf), f)) {
            std::string s(buf);
            if (s.find("tts_model=") != 0) lines.push_back(s);
        }
        fclose(f);
    }

    f = fopen(path.c_str(), "w");
    if (!f) return false;
    for (auto& l : lines) fputs(l.c_str(), f);
    fclose(f);
    return true;
}

// Resolve which TTS model to use: user preference > auto-detect
inline const TtsModelDef* resolve_active_tts(const std::string& models_dir,
                                              const std::vector<TtsModelDef>& models) {
    std::string selected = read_selected_tts_id();
    if (!selected.empty()) {
        const auto* m = find_tts_by_id(selected, models);
        if (m && is_tts_installed(models_dir, *m)) return m;
    }
    return find_best_installed_tts(models_dir, models);
}

} // namespace rcli
