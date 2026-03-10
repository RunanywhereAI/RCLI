#pragma once
// =============================================================================
// RCLI CLI — Shared types, colors, terminal utilities, and global state
// =============================================================================
//
// Included by all CLI module headers. Provides the color namespace, Args struct,
// terminal raw-mode helpers, and the handful of global atoms that coordinate
// the interactive loop, voice recording, and visualization.
//
// =============================================================================

#include "api/rcli_api.h"
#include "actions/action_registry.h"
#include "actions/macos_actions.h"
#include "core/log.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <csignal>
#include <thread>
#include <chrono>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <atomic>
#include <map>
#include <vector>
#include <dirent.h>

using namespace rastack;
using namespace rcli;

// =============================================================================
// ANSI colors
// =============================================================================
namespace color {
    inline const char* reset      = "\033[0m";
    inline const char* bold       = "\033[1m";
    inline const char* dim        = "\033[2m";
    inline const char* cyan       = "\033[36m";
    inline const char* green      = "\033[32m";
    inline const char* yellow     = "\033[33m";
    inline const char* red        = "\033[31m";
    inline const char* magenta    = "\033[35m";
    inline const char* blue       = "\033[34m";
    inline const char* white      = "\033[97m";
    inline const char* orange     = "\033[38;5;208m";
    inline const char* clear_line = "\033[2K\r";
}

#ifndef RCLI_VERSION
#define RCLI_VERSION "0.0.0"
#endif
inline const char* RA_VERSION = "v" RCLI_VERSION;

// =============================================================================
// Terminal utilities
// =============================================================================

inline struct termios g_orig_termios;
inline bool g_raw_mode = false;

inline void disable_raw_mode() {
    if (g_raw_mode) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_orig_termios);
        g_raw_mode = false;
    }
}

inline void enable_raw_mode() {
    tcgetattr(STDIN_FILENO, &g_orig_termios);
    atexit(disable_raw_mode);
    struct termios raw = g_orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
    g_raw_mode = true;
}

inline int get_terminal_width() {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) return ws.ws_col;
    return 80;
}

// =============================================================================
// Global state (shared across all CLI modules)
// =============================================================================

inline RCLIHandle g_engine = nullptr;
inline std::atomic<bool> g_running{true};
inline std::atomic<bool> g_recording{false};
inline std::atomic<bool> g_vis_active{false};
inline std::atomic<int> g_state{0};
inline std::string g_current_transcript;
inline std::atomic<float> g_peak_rms{0.0f};

inline constexpr float NOISE_GATE_THRESHOLD = 0.015f;

// =============================================================================
// Args — parsed from command line
// =============================================================================

struct Args {
    std::string command;
    std::string arg1;
    std::string arg2;
    std::string models_dir;
    std::string rag_index;
    std::string bench_suite = "all";
    std::string bench_output;
    std::string bench_llm;
    std::string bench_tts;
    std::string bench_stt;
    int bench_runs = 3;
    int gpu_layers = 99;
    int ctx_size = 4096;
    bool no_speak = false;
    bool verbose = false;
    bool help = false;
    bool bench_all_llm = false;
    bool bench_all_tts = false;
};

inline std::string default_models_dir() {
    if (const char* home = getenv("HOME"))
        return std::string(home) + "/Library/RCLI/models";
    return "./models";
}

inline Args parse_args(int argc, char** argv) {
    Args args;
    args.models_dir = default_models_dir();
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--models" && i + 1 < argc)     { args.models_dir = argv[++i]; continue; }
        if (a == "--gpu-layers" && i + 1 < argc)  { args.gpu_layers = std::atoi(argv[++i]); continue; }
        if (a == "--ctx-size" && i + 1 < argc)     { args.ctx_size = std::atoi(argv[++i]); continue; }
        if (a == "--rag" && i + 1 < argc)          { args.rag_index = argv[++i]; continue; }
        if (a == "--suite" && i + 1 < argc)        { args.bench_suite = argv[++i]; continue; }
        if (a == "--runs" && i + 1 < argc)         { args.bench_runs = std::atoi(argv[++i]); continue; }
        if (a == "--output" && i + 1 < argc)       { args.bench_output = argv[++i]; continue; }
        if (a == "--llm" && i + 1 < argc)          { args.bench_llm = argv[++i]; continue; }
        if (a == "--tts" && i + 1 < argc)          { args.bench_tts = argv[++i]; continue; }
        if (a == "--stt" && i + 1 < argc)          { args.bench_stt = argv[++i]; continue; }
        if (a == "--all-llm")                       { args.bench_all_llm = true; continue; }
        if (a == "--all-tts")                       { args.bench_all_tts = true; continue; }
        if (a == "--no-speak")                     { args.no_speak = true; continue; }
        if (a == "--verbose" || a == "-v")         { args.verbose = true; continue; }
        if (a == "--help" || a == "-h")            { args.help = true; continue; }

        if (args.command.empty()) args.command = a;
        else if (args.arg1.empty()) args.arg1 = a;
        else if (args.arg2.empty()) args.arg2 = a;
    }
    return args;
}

// =============================================================================
// Shared helpers
// =============================================================================

inline void signal_handler(int) { g_running = false; }

inline bool models_exist(const std::string& dir) {
    struct stat st;
    // Check for any known LLM model (default is LFM2, fallback to Qwen3)
    std::string check_lfm2  = dir + "/lfm2-1.2b-tool-q4_k_m.gguf";
    std::string check_qwen3 = dir + "/qwen3-0.6b-q4_k_m.gguf";
    return stat(check_lfm2.c_str(), &st) == 0 || stat(check_qwen3.c_str(), &st) == 0;
}

inline void print_missing_models(const std::string& dir) {
    fprintf(stderr,
        "\n%s%s  Models not found%s in %s\n\n"
        "  Run: %srcli setup%s\n\n"
        "  This downloads ~1GB of AI models (LFM2 1.2B LLM, Zipformer STT,\n"
        "  Whisper STT, Piper TTS, Silero VAD) to ~/Library/RCLI/models/\n\n",
        color::bold, color::red, color::reset, dir.c_str(),
        color::bold, color::reset);
}

inline std::string strip_think_tags(const std::string& text) {
    std::string result = text;
    while (true) {
        auto start = result.find("<think>");
        if (start == std::string::npos) break;
        auto end = result.find("</think>", start);
        if (end == std::string::npos) {
            result.erase(start);
            break;
        }
        result.erase(start, end + 8 - start);
    }
    size_t first = result.find_first_not_of(" \t\n\r");
    if (first == std::string::npos) return "";
    return result.substr(first);
}

inline void print_response(const char* text) {
    std::string clean = strip_think_tags(text);
    if (clean.empty()) return;
    fprintf(stderr, "\n  %s%sRA:%s %s\n", color::bold, color::orange, color::reset, clean.c_str());
}

inline void print_action_result(const char* name, const char* result, bool success) {
    if (success) {
        fprintf(stderr, "  %s%s[action]%s %s%s%s  %s\n",
                color::bold, color::green, color::reset,
                color::bold, name, color::reset, result);
    } else {
        fprintf(stderr, "  %s%s[failed]%s %s  %s\n",
                color::bold, color::red, color::reset, name, result);
    }
}

inline void draw_status_bar() {
    const char* state_labels[] = {"IDLE", "LISTENING", "PROCESSING", "SPEAKING", "INTERRUPTED"};
    const char* state_colors[] = {color::dim, color::green, color::yellow, color::cyan, color::red};
    int s = g_state.load();
    if (s < 0 || s > 4) s = 0;

    fprintf(stderr, "%s", color::clear_line);
    if (g_recording.load()) {
        fprintf(stderr, "  %s%s REC %s%s  ", color::bold, color::red, state_labels[s], color::reset);
        if (!g_current_transcript.empty()) {
            fprintf(stderr, "%s%s\"%s\"%s", color::dim, color::cyan,
                    g_current_transcript.c_str(), color::reset);
        }
    } else {
        fprintf(stderr, "  %s%s%s%s  ", state_colors[s], color::bold, state_labels[s], color::reset);
    }
    fflush(stderr);
}

// Sanitize a user-typed path: remove shell escapes, quotes, expand ~,
// and fuzzy-match in the parent directory if the exact path doesn't exist.
inline std::string sanitize_path(const std::string& raw) {
    std::string p = raw;
    if (p.size() >= 2 &&
        ((p.front() == '"' && p.back() == '"') ||
         (p.front() == '\'' && p.back() == '\''))) {
        p = p.substr(1, p.size() - 2);
    }
    std::string cleaned;
    for (size_t i = 0; i < p.size(); i++) {
        if (p[i] == '\\' && i + 1 < p.size()) {
            cleaned += p[i + 1];
            i++;
        } else {
            cleaned += p[i];
        }
    }
    if (!cleaned.empty() && cleaned[0] == '~') {
        const char* home = getenv("HOME");
        if (home) cleaned = std::string(home) + cleaned.substr(1);
    }
    struct stat st;
    if (stat(cleaned.c_str(), &st) == 0) return cleaned;

    auto slash_pos = cleaned.rfind('/');
    if (slash_pos == std::string::npos) return cleaned;

    std::string dir  = cleaned.substr(0, slash_pos);
    std::string name = cleaned.substr(slash_pos + 1);

    auto normalize = [](const std::string& s) {
        std::string n;
        for (char c : s) {
            if (c != ' ' && c != '-' && c != '_')
                n += std::tolower(static_cast<unsigned char>(c));
        }
        return n;
    };
    std::string target = normalize(name);

    DIR* d = opendir(dir.c_str());
    if (!d) return cleaned;

    std::string best_match;
    struct dirent* entry;
    while ((entry = readdir(d)) != nullptr) {
        if (normalize(entry->d_name) == target) {
            best_match = dir + "/" + entry->d_name;
            break;
        }
    }
    closedir(d);

    if (!best_match.empty()) {
        fprintf(stderr, "  %sResolved path:%s %s\n", color::dim, color::reset, best_match.c_str());
        return best_match;
    }
    return cleaned;
}

// Ensure embedding model exists, download if not. Returns true on success.
inline bool ensure_embedding_model(const std::string& models_dir) {
    std::string emb_path = models_dir + "/snowflake-arctic-embed-s-q8_0.gguf";
    struct stat emb_st;
    if (stat(emb_path.c_str(), &emb_st) == 0 && emb_st.st_size >= 1000)
        return true;
    fprintf(stderr, "  %sEmbedding model not found. Downloading (~35MB)...%s\n",
            color::dim, color::reset);
    fflush(stderr);
    std::string dl = "curl -L -# -o '" + emb_path + "' "
        "'https://huggingface.co/ChristianAzinn/snowflake-arctic-embed-s-gguf"
        "/resolve/main/snowflake-arctic-embed-s-Q8_0.GGUF'";
    if (system(dl.c_str()) != 0) {
        fprintf(stderr, "  %s%sDownload failed.%s\n", color::bold, color::red, color::reset);
        return false;
    }
    fprintf(stderr, "  %s%sEmbedding model ready.%s\n", color::bold, color::green, color::reset);
    return true;
}

// Create and init engine with standard error handling. Returns nullptr on failure.
inline RCLIHandle create_and_init_engine(const Args& args) {
    auto engine = rcli_create(nullptr);
    if (!engine) {
        fprintf(stderr, "  %s%sError: Failed to create engine%s\n",
                color::bold, color::red, color::reset);
        return nullptr;
    }
    if (rcli_init(engine, args.models_dir.c_str(), args.gpu_layers) != 0) {
        fprintf(stderr, "  %s%sError: Failed to initialize (check models dir: %s)%s\n",
                color::bold, color::red, args.models_dir.c_str(), color::reset);
        rcli_destroy(engine);
        return nullptr;
    }
    return engine;
}

// Print a one-liner with LLM perf stats after a response.
// Shows: engine · tokens · decode tok/s · TTFT · prefill ms · decode ms
inline void print_llm_perf(RCLIHandle engine) {
    int tokens = 0;
    double tps = 0, ttft = 0, total = 0;
    rcli_get_last_llm_perf(engine, &tokens, &tps, &ttft, &total);
    if (tokens <= 0 || tps <= 0) return;

    double pfill_tps = 0, dec_tps = 0, pfill_ms = 0, dec_ms = 0;
    const char* eng_name = nullptr;
    rcli_get_last_llm_perf_extended(engine,
        &pfill_tps, &dec_tps, &pfill_ms, &dec_ms, nullptr, &eng_name);

    fflush(stdout);
    bool is_metalrt = (eng_name && std::string(eng_name) == "MetalRT");
    const char* eng_color = is_metalrt ? color::green : color::dim;

    fprintf(stderr, "  %s> %s%s%s \xc2\xb7 %d tok \xc2\xb7 %.0f tok/s \xc2\xb7 TTFT %.0fms",
            color::dim, eng_color, eng_name ? eng_name : "LLM", color::dim,
            tokens, tps, ttft);
    if (pfill_ms > 0)
        fprintf(stderr, " \xc2\xb7 prefill %.0fms", pfill_ms);
    if (dec_ms > 0)
        fprintf(stderr, " \xc2\xb7 decode %.0fms", dec_ms);
    fprintf(stderr, "%s\n", color::reset);
}

// Print TTS perf after speaking.
inline void print_tts_perf(RCLIHandle engine) {
    double synth_ms = 0, rtf = 0;
    rcli_get_last_tts_perf(engine, nullptr, &synth_ms, &rtf);
    if (synth_ms > 0) {
        fprintf(stderr, "  %sTTS %.0fms \xc2\xb7 %.2fx realtime%s\n",
                color::dim, synth_ms, rtf, color::reset);
    }
}

// Print STT perf after transcription.
inline void print_stt_perf(RCLIHandle engine) {
    double audio_ms = 0, transcribe_ms = 0;
    rcli_get_last_stt_perf(engine, &audio_ms, &transcribe_ms);
    if (transcribe_ms > 0) {
        fflush(stdout);
        if (audio_ms > 0) {
            double rtf = transcribe_ms / audio_ms;
            fprintf(stderr, "  %sSTT %.0fms \xc2\xb7 %.1fs audio \xc2\xb7 %.2fx RT%s\n",
                    color::dim, transcribe_ms, audio_ms / 1000.0, rtf, color::reset);
        } else {
            fprintf(stderr, "  %sSTT %.0fms%s\n",
                    color::dim, transcribe_ms, color::reset);
        }
    }
}

inline bool ensure_mic_permission();  // Defined in main.cpp (uses Objective-C bridge)
