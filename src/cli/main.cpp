// =============================================================================
// RCLI CLI — Entry point and interactive mode
// =============================================================================
//
// This file is kept lean: just the interactive loop, listen/ask/bench/rag
// commands, and the main() dispatch table. All reusable UI is in headers:
//
//   cli_common.h      — colors, Args, terminal utilities, global state
//   help.h            — help / usage / banner strings
//   actions_cli.h     — actions display + execution
//   model_pickers.h   — LLM / STT / TTS pickers, dashboard, info
//   setup_cmds.h      — setup, upgrade-llm, upgrade-stt
//   visualizer.h      — push-to-talk waveform animation
//
// =============================================================================

#include "cli/cli_common.h"
#include "cli/help.h"
#include "cli/actions_cli.h"
#include "cli/model_pickers.h"
#include "cli/setup_cmds.h"
#include "cli/visualizer.h"
#include "cli/tui_app.h"
#include "pipeline/orchestrator.h"
#include "engines/metalrt_loader.h"
#include "audio/audio_io.h"
#include "audio/mic_permission.h"
#include "core/personality.h"
#include "llama.h"

// Defined in cli_common.h as a forward declaration; implemented here because
// it depends on the Objective-C mic_permission bridge compiled into this TU.
inline bool ensure_mic_permission() {
    MicPermissionStatus status = check_mic_permission();
    if (status == MIC_PERM_AUTHORIZED) return true;

    if (status == MIC_PERM_NOT_DETERMINED) {
        fprintf(stderr, "\n  %s%sMicrophone access required%s\n", color::bold, color::yellow, color::reset);
        fprintf(stderr, "  RCLI needs your microphone for voice commands.\n");
        fprintf(stderr, "  A macOS permission dialog should appear now...\n\n");
        fflush(stderr);
        int granted = request_mic_permission();
        if (granted) {
            fprintf(stderr, "  %s%sMicrophone access granted!%s\n\n", color::bold, color::green, color::reset);
            return true;
        }
    }

    fprintf(stderr,
        "\n  %s%sMicrophone access denied%s\n\n"
        "  RCLI cannot listen without microphone access.\n\n"
        "  To fix this:\n"
        "    1. Open %sSystem Settings%s > %sPrivacy & Security%s > %sMicrophone%s\n"
        "    2. Toggle %sON%s for your terminal app (Terminal, iTerm, or Cursor)\n"
        "    3. Re-run rcli\n\n"
        "  %sTip:%s You can still use text commands without the mic:\n"
        "    rcli ask \"open Safari\"\n\n",
        color::bold, color::red, color::reset,
        color::bold, color::reset, color::bold, color::reset, color::bold, color::reset,
        color::bold, color::reset,
        color::dim, color::reset);
    return false;
}

// =============================================================================
// Interactive mode — main loop (TUI dashboard)
// =============================================================================

static int cmd_interactive(const Args& args) {
    print_banner(args);

    if (!ensure_mic_permission()) return 1;

    if (!models_exist(args.models_dir)) {
        print_missing_models(args.models_dir);
        return 1;
    }

    fprintf(stderr, "\n  %sLoading AI models...%s ", color::dim, color::reset);
    fflush(stderr);

    // Pass engine override via config JSON if specified (e.g. "rcli metalrt", "rcli llamacpp")
    const char* config_json = nullptr;
    std::string config_buf;
    if (!args.engine_override.empty()) {
        config_buf = "{\"engine\": \"" + args.engine_override + "\"}";
        config_json = config_buf.c_str();
    }
    g_engine = rcli_create(config_json);
    if (!g_engine) {
        fprintf(stderr, "\n  %s%sError: Failed to create engine%s\n", color::bold, color::red, color::reset);
        return 1;
    }

    signal(SIGINT, signal_handler);

    rcli_set_transcript_callback(g_engine,
        [](const char* text, int, void*) { g_current_transcript = text; }, nullptr);

    rcli_set_state_callback(g_engine,
        [](int, int new_s, void*) { g_state.store(new_s); }, nullptr);

    rcli_set_action_callback(g_engine,
        [](const char* name, const char* result, int success, void*) {
            print_action_result(name, result, success != 0);
        }, nullptr);

    if (rcli_init(g_engine, args.models_dir.c_str(), args.gpu_layers) != 0) {
        fprintf(stderr, "\n  %s%sError: Failed to initialize (check models dir: %s)%s\n",
                color::bold, color::red, args.models_dir.c_str(), color::reset);
        rcli_destroy(g_engine);
        return 1;
    }

    rcli_process_command(g_engine, "hi"); // warm up LLM
    double warmup_tps = 0;
    rcli_get_last_llm_perf(g_engine, nullptr, &warmup_tps, nullptr, nullptr);

    bool rag_loaded = false;
    if (!args.rag_index.empty()) {
        if (rcli_rag_load_index(g_engine, args.rag_index.c_str()) == 0) {
            rag_loaded = true;
        } else {
            fprintf(stderr, "\n  %s%sWarning: Could not load RAG index at %s%s\n",
                    color::bold, color::yellow, args.rag_index.c_str(), color::reset);
        }
    }

    fprintf(stderr, "%s%sdone!%s\n", color::bold, color::green, color::reset);

    ActionRegistry count_reg;
    count_reg.register_defaults();
    const char* stt_name = rcli_get_stt_model(g_engine);
    const char* llm_name = rcli_get_llm_model(g_engine);
    const char* tts_name_live = rcli_get_tts_model(g_engine);

    fprintf(stderr, "  %s%sRCLI%s %s%s%s  %s·%s  %s%s%s  %s·%s  %s%d actions%s",
            color::bold, color::orange, color::reset,
            color::dim, RA_VERSION, color::reset,
            color::dim, color::reset,
            color::dim, stt_name, color::reset,
            color::dim, color::reset,
            color::dim, count_reg.num_actions(), color::reset);
    if (rag_loaded)
        fprintf(stderr, "  %s·%s  %s%sRAG active%s", color::dim, color::reset, color::bold, color::green, color::reset);
    fprintf(stderr, "\n");

    fprintf(stderr, "  %s%s \xc2\xb7 %s \xc2\xb7 %s \xc2\xb7 Apple Silicon",
            color::dim, llm_name, tts_name_live,
            args.rag_index.empty() ? "RAG off" : "RAG on");
    if (warmup_tps > 0)
        fprintf(stderr, " \xc2\xb7 %.0f tok/s", warmup_tps);
    fprintf(stderr, "%s\n", color::reset);

    // Launch full-screen TUI dashboard
    {
        rcli_tui::TuiApp tui(g_engine, args);
        tui.set_rag_loaded(rag_loaded);
        tui.run();
    }

    fprintf(stderr, "\n  %s%sRCLI%s %s\xe2\x80\x94 See you next time!%s\n\n",
            color::bold, color::orange, color::reset, color::dim, color::reset);
    rcli_stop_listening(g_engine);
    rcli_destroy(g_engine);
    return 0;
}

// =============================================================================
// Listen mode (push-to-talk voice loop)
// =============================================================================

static int cmd_listen(const Args& args) {
    if (args.help) { print_help_listen(); return 0; }
    if (!ensure_mic_permission()) return 1;
    if (!models_exist(args.models_dir)) { print_missing_models(args.models_dir); return 1; }

    fprintf(stderr, "\n%s%s  RCLI — Voice Mode (Push-to-Talk)%s\n", color::bold, color::orange, color::reset);
    fprintf(stderr, "  Press SPACE to start talking, SPACE again to stop.\n");
    fprintf(stderr, "  Press Ctrl+C to quit.\n\n");

    g_engine = rcli_create(nullptr);
    if (!g_engine) return 1;

    signal(SIGINT, signal_handler);

    rcli_set_action_callback(g_engine,
        [](const char* name, const char* result, int success, void*) {
            print_action_result(name, result, success != 0);
        }, nullptr);

    if (rcli_init(g_engine, args.models_dir.c_str(), args.gpu_layers) != 0) {
        rcli_destroy(g_engine);
        return 1;
    }

    fprintf(stderr, "  %s%sReady.%s Press SPACE to talk.\n\n", color::bold, color::green, color::reset);

    // Open /dev/tty directly for keyboard input — isolated from anything
    // else that might touch STDIN_FILENO (engine init, audio threads, etc.)
    int tty_fd = open("/dev/tty", O_RDONLY);
    if (tty_fd < 0) {
        fprintf(stderr, "  %s%sError: Cannot open /dev/tty%s\n", color::bold, color::red, color::reset);
        rcli_destroy(g_engine);
        return 1;
    }

    struct termios tty_orig;
    tcgetattr(tty_fd, &tty_orig);
    struct termios tty_raw = tty_orig;
    tty_raw.c_lflag &= ~(ECHO | ICANON | ISIG);
    tty_raw.c_cc[VMIN] = 0;
    tty_raw.c_cc[VTIME] = 1;
    tcsetattr(tty_fd, TCSAFLUSH, &tty_raw);

    auto read_key = [tty_fd]() -> char {
        char c = 0;
        read(tty_fd, &c, 1);
        return c;
    };

    while (g_running) {
        fprintf(stderr, "  %s[SPACE] to record  [q] to quit%s\r",
                color::dim, color::reset);
        fflush(stderr);

        while (g_running) {
            char c = read_key();
            if (c == ' ') break;
            if (c == 'q' || c == 'Q' || c == 3 || c == 27) { g_running = false; break; }
        }
        if (!g_running) break;

        fprintf(stderr, "\r%s", visualizer::ESC_CLEAR_LINE);

        rcli_stop_speaking(g_engine);
        g_peak_rms.store(0.0f, std::memory_order_relaxed);
        rcli_start_capture(g_engine);

        fprintf(stderr, "  %s%s● Recording...%s speak now! Press SPACE to stop.\r",
                color::bold, color::green, color::reset);
        fflush(stderr);

        while (g_running) {
            char c = read_key();
            if (c == ' ') break;
            if (c == 'q' || c == 'Q' || c == 3 || c == 27) { g_running = false; break; }

            float level = rcli_get_audio_level(g_engine);
            float prev = g_peak_rms.load(std::memory_order_relaxed);
            if (level > prev) g_peak_rms.store(level, std::memory_order_relaxed);
        }

        fprintf(stderr, "\r%s", visualizer::ESC_CLEAR_LINE);

        if (!g_running) break;

        bool below_noise_gate = (g_peak_rms.load(std::memory_order_relaxed) < NOISE_GATE_THRESHOLD);

        if (below_noise_gate) {
            rcli_stop_capture_and_transcribe(g_engine);
            fprintf(stderr, "  %s(no speech detected)%s\n\n", color::dim, color::reset);
            continue;
        }

        fprintf(stderr, "  %sTranscribing...%s", color::dim, color::reset);
        fflush(stderr);
        const char* transcript = rcli_stop_capture_and_transcribe(g_engine);
        fprintf(stderr, "\r%s", visualizer::ESC_CLEAR_LINE);

        if (transcript && transcript[0]) {
            fprintf(stderr, "  %s%sYou:%s %s\n", color::bold, color::blue, color::reset, transcript);
            print_stt_perf(g_engine);
            fprintf(stderr, "  %sThinking...%s", color::dim, color::reset);
            fflush(stderr);

            const char* response = rcli_process_command(g_engine, transcript);
            fprintf(stderr, "\r%s", visualizer::ESC_CLEAR_LINE);

            if (response && response[0]) {
                print_response(response);
                print_llm_perf(g_engine);
                if (!args.no_speak) {
                    std::atomic<bool> tts_done{false};
                    std::thread tts_thread([&]() {
                        rcli_speak(g_engine, response);
                        tts_done.store(true, std::memory_order_release);
                    });
                    tts_thread.detach();

                    fprintf(stderr, "  %s%s♪ Speaking...%s press SPACE to interrupt\r",
                            color::bold, color::cyan, color::reset);
                    fflush(stderr);

                    while (g_running && !tts_done.load(std::memory_order_acquire)) {
                        char c = read_key();
                        if (c == ' ') {
                            rcli_stop_speaking(g_engine);
                            fprintf(stderr, "\r%s", visualizer::ESC_CLEAR_LINE);
                            break;
                        }
                        if (c == 'q' || c == 'Q' || c == 3 || c == 27) {
                            rcli_stop_speaking(g_engine);
                            g_running = false;
                            break;
                        }
                    }
                    fprintf(stderr, "\r%s", visualizer::ESC_CLEAR_LINE);
                    print_tts_perf(g_engine);
                }
            }
        } else {
            fprintf(stderr, "  %s(no speech detected)%s\n", color::dim, color::reset);
        }
        fprintf(stderr, "\n");
    }

    tcsetattr(tty_fd, TCSAFLUSH, &tty_orig);
    close(tty_fd);
    rcli_destroy(g_engine);
    fprintf(stderr, "\n  %s%sRCLI%s %s\xe2\x80\x94 See you next time!%s\n\n",
            color::bold, color::orange, color::reset, color::dim, color::reset);
    return 0;
}

// =============================================================================
// Mic test
// =============================================================================

static int cmd_mic_test(const Args& args) {
    if (!ensure_mic_permission()) return 1;

    fprintf(stderr, "\n%s%s  Microphone Test%s\n", color::bold, color::orange, color::reset);
    fprintf(stderr, "  Speak into your MacBook mic for 5 seconds.\n");
    fprintf(stderr, "  You should see the bars move when you speak.\n\n");

    g_engine = rcli_create(nullptr);
    if (!g_engine) { fprintf(stderr, "  Failed to create engine\n"); return 1; }

    std::string mdir = default_models_dir();
    if (rcli_init(g_engine, mdir.c_str(), 0) != 0) {
        fprintf(stderr, "  Failed to init (need models? Run: rcli setup)\n");
        rcli_destroy(g_engine);
        return 1;
    }

    rcli_set_transcript_callback(g_engine,
        [](const char* text, int is_final, void*) {
            if (is_final && text[0])
                fprintf(stderr, "  %s%sHeard:%s %s\n", color::bold, color::green, color::reset, text);
        }, nullptr);

    rcli_start_listening(g_engine);

    float max_level = 0;
    for (int i = 0; i < 50; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        float level = rcli_get_audio_level(g_engine);
        if (level > max_level) max_level = level;
        float norm = std::min(1.0f, level * 10.0f);
        int filled = (int)(norm * 30);
        fprintf(stderr, "  [");
        for (int j = 0; j < 30; j++)
            fprintf(stderr, "%s", j < filled ? "\xe2\x96\x88" : "\xc2\xb7");
        fprintf(stderr, "] %.4f\r", level);
        fflush(stderr);
    }

    rcli_stop_listening(g_engine);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    fprintf(stderr, "\n\n  Max level: %.4f\n", max_level);
    if (max_level > 0.01f)
        fprintf(stderr, "  %s%sMic is working well!%s\n\n", color::bold, color::green, color::reset);
    else if (max_level > 0.003f)
        fprintf(stderr, "  %s%sMic is working but quiet. Try speaking louder or closer.%s\n\n",
                color::bold, color::yellow, color::reset);
    else
        fprintf(stderr, "  %s%sMic levels very low. Check System Settings > Sound > Input.%s\n\n",
                color::bold, color::red, color::reset);

    rcli_destroy(g_engine);
    return 0;
}

// =============================================================================
// Ask (one-shot text command)
// =============================================================================

static int cmd_ask(const Args& args) {
    if (args.help) { print_help_ask(); return 0; }

    if (args.arg1.empty()) {
        fprintf(stderr, "\n  Usage: rcli ask \"your question or command\"\n\n");
        fprintf(stderr, "  Examples:\n");
        fprintf(stderr, "    rcli ask \"open Safari\"\n");
        fprintf(stderr, "    rcli ask \"create a note called Ideas\"\n");
        fprintf(stderr, "    rcli ask \"what is the capital of France?\"\n\n");
        return 1;
    }

    if (!models_exist(args.models_dir)) { print_missing_models(args.models_dir); return 1; }

    g_engine = rcli_create(nullptr);
    if (!g_engine) return 1;

    rcli_set_action_callback(g_engine,
        [](const char* name, const char* result, int success, void*) {
            print_action_result(name, result, success != 0);
        }, nullptr);

    fprintf(stderr, "%sInitializing...%s\n", color::dim, color::reset);
    if (rcli_init(g_engine, args.models_dir.c_str(), args.gpu_layers) != 0) {
        rcli_destroy(g_engine);
        return 1;
    }

    if (!args.rag_index.empty()) {
        if (rcli_rag_load_index(g_engine, args.rag_index.c_str()) != 0) {
            fprintf(stderr, "%s%sWarning: Could not load RAG index%s\n", color::bold, color::yellow, color::reset);
        }
    }

    const char* response = nullptr;
    if (!args.rag_index.empty()) {
        response = rcli_rag_query(g_engine, args.arg1.c_str());
    } else {
        response = rcli_process_command(g_engine, args.arg1.c_str());
    }
    if (response && response[0]) {
        fprintf(stdout, "%s\n", response);
        print_llm_perf(g_engine);
        if (!args.no_speak) {
            rcli_speak(g_engine, response);
            print_tts_perf(g_engine);
        }
    }

    rcli_destroy(g_engine);
    return 0;
}

// =============================================================================
// Benchmark
// =============================================================================

static int cmd_bench(const Args& args) {
    if (args.help) { print_help_bench(); return 0; }
    if (!models_exist(args.models_dir)) { print_missing_models(args.models_dir); return 1; }

    fprintf(stderr, "\n");
    fprintf(stderr, "  %s\xe2\x94\x8c\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x90%s\n",
            color::dim, color::reset);
    fprintf(stderr, "  %s\xe2\x94\x82%s %s%s RCLI Benchmark%s                    %s\xe2\x94\x82%s\n",
            color::dim, color::reset, color::bold, color::orange, color::reset, color::dim, color::reset);
    fprintf(stderr, "  %s\xe2\x94\x82%s %sSuite: %-10s  Runs: %-3d%s              %s\xe2\x94\x82%s\n",
            color::dim, color::reset, color::dim,
            args.bench_suite.c_str(), args.bench_runs, color::reset,
            color::dim, color::reset);
    fprintf(stderr, "  %s\xe2\x94\x94\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x98%s\n",
            color::dim, color::reset);

    fprintf(stderr, "\n  %sInitializing engine...%s\n", color::dim, color::reset);

    g_engine = rcli_create(nullptr);
    if (!g_engine) {
        fprintf(stderr, "  %s%sError: Failed to create engine%s\n", color::bold, color::red, color::reset);
        return 1;
    }

    if (rcli_init(g_engine, args.models_dir.c_str(), args.gpu_layers) != 0) {
        fprintf(stderr, "  %s%sError: Failed to initialize%s\n", color::bold, color::red, color::reset);
        rcli_destroy(g_engine);
        return 1;
    }

    // Display active models being benchmarked
    const char* llm_name = rcli_get_llm_model(g_engine);
    const char* stt_name = rcli_get_stt_model(g_engine);
    const char* tts_name = rcli_get_tts_model(g_engine);
    fprintf(stderr, "\n  %s%sBenchmarking:%s\n", color::bold, color::orange, color::reset);
    fprintf(stderr, "    LLM = %s%s%s\n", color::green, llm_name ? llm_name : "N/A", color::reset);
    fprintf(stderr, "    STT = %s%s%s\n", color::green, stt_name ? stt_name : "N/A", color::reset);
    fprintf(stderr, "    TTS = %s%s%s\n\n", color::green, tts_name ? tts_name : "N/A", color::reset);

    if (!args.rag_index.empty()) {
        if (rcli_rag_load_index(g_engine, args.rag_index.c_str()) != 0) {
            fprintf(stderr, "  %s%sWarning: Could not load RAG index%s\n",
                    color::bold, color::yellow, color::reset);
        }
    }

    const char* output = args.bench_output.empty() ? nullptr : args.bench_output.c_str();
    int rc = rcli_run_full_benchmark(g_engine, args.bench_suite.c_str(),
                                           args.bench_runs, output);

    rcli_destroy(g_engine);

    fprintf(stderr, "  %s%sRCLI%s %s\xe2\x80\x94 Benchmark complete.%s\n\n",
            color::bold, color::orange, color::reset, color::dim, color::reset);
    return rc;
}

// =============================================================================
// RAG subcommands
// =============================================================================

static int cmd_rag(const Args& args) {
    if (args.help || args.arg1.empty()) {
        print_help_rag();
        return args.help ? 0 : 1;
    }

    if (args.arg1 == "status") {
        std::string index_dir;
        if (const char* home = getenv("HOME"))
            index_dir = std::string(home) + "/Library/RCLI/index";
        struct stat st;
        if (stat(index_dir.c_str(), &st) == 0) {
            fprintf(stdout, "\n  %s%sRAG Index:%s %s\n", color::bold, color::green, color::reset, index_dir.c_str());
            fprintf(stdout, "  Status: indexed\n\n");
        } else {
            fprintf(stdout, "\n  %s%sNo RAG index found.%s\n", color::bold, color::yellow, color::reset);
            fprintf(stdout, "  Run: rcli rag ingest <directory>\n\n");
        }
        return 0;
    }

    if (!models_exist(args.models_dir)) { print_missing_models(args.models_dir); return 1; }

    g_engine = rcli_create(nullptr);
    if (!g_engine) return 1;

    fprintf(stderr, "%sInitializing engine...%s\n", color::dim, color::reset);
    if (rcli_init(g_engine, args.models_dir.c_str(), args.gpu_layers) != 0) {
        rcli_destroy(g_engine);
        return 1;
    }

    if (args.arg1 == "ingest") {
        if (args.arg2.empty()) {
            fprintf(stderr, "\n  Usage: rcli rag ingest <directory>\n\n");
            rcli_destroy(g_engine);
            return 1;
        }
        if (!ensure_embedding_model(args.models_dir)) {
            rcli_destroy(g_engine);
            return 1;
        }
        fprintf(stderr, "\n%s%s  RAG Ingest%s\n", color::bold, color::orange, color::reset);
        fprintf(stderr, "  Indexing documents from: %s\n\n", args.arg2.c_str());
        int rc = rcli_rag_ingest(g_engine, args.arg2.c_str());
        if (rc == 0) {
            fprintf(stderr, "\n  %s%sIndexing complete!%s\n\n", color::bold, color::green, color::reset);
            fprintf(stderr, "  Query your docs:\n");
            fprintf(stderr, "    rcli rag query \"your question here\"\n");
            fprintf(stderr, "    rcli ask --rag ~/Library/RCLI/index \"your question\"\n\n");
        } else {
            fprintf(stderr, "\n  %s%sIndexing failed.%s\n\n", color::bold, color::red, color::reset);
        }
        rcli_destroy(g_engine);
        return rc;
    }

    if (args.arg1 == "query") {
        if (args.arg2.empty()) {
            fprintf(stderr, "\n  Usage: rcli rag query \"your question\"\n\n");
            rcli_destroy(g_engine);
            return 1;
        }
        std::string index_dir;
        if (!args.rag_index.empty()) index_dir = args.rag_index;
        else if (const char* home = getenv("HOME"))
            index_dir = std::string(home) + "/Library/RCLI/index";
        if (rcli_rag_load_index(g_engine, index_dir.c_str()) != 0) {
            fprintf(stderr, "  %s%sCould not load RAG index at %s%s\n",
                    color::bold, color::red, index_dir.c_str(), color::reset);
            fprintf(stderr, "  Run: rcli rag ingest <directory>\n\n");
            rcli_destroy(g_engine);
            return 1;
        }
        const char* result = rcli_rag_query(g_engine, args.arg2.c_str());
        if (result && result[0]) fprintf(stdout, "%s\n", result);
        rcli_destroy(g_engine);
        return 0;
    }

    fprintf(stderr, "%sUnknown rag subcommand: %s%s\n", color::red, args.arg1.c_str(), color::reset);
    print_help_rag();
    rcli_destroy(g_engine);
    return 1;
}

// =============================================================================
// MetalRT management
// =============================================================================

static int cmd_metalrt(const Args& args) {
    if (args.arg1 == "install") {
        if (!rastack::MetalRTLoader::gpu_supported()) {
            fprintf(stderr, "\n  %s%sMetalRT requires Apple M3 or later.%s\n"
                    "  Your Mac uses an M1/M2 chip which doesn't support Metal 3.1 shaders.\n"
                    "  Please use llama.cpp instead: %srcli engine llamacpp%s\n\n",
                    color::bold, color::red, color::reset, color::bold, color::reset);
            return 1;
        }
        auto& loader = rastack::MetalRTLoader::instance();
        if (loader.is_available()) {
            std::string ver = rastack::MetalRTLoader::installed_version();
            fprintf(stderr, "\n  %s%sMetalRT already installed%s", color::bold, color::green, color::reset);
            if (!ver.empty()) fprintf(stderr, " (%s)", ver.c_str());
            fprintf(stderr, "\n\n");
            return 0;
        }
        fprintf(stderr, "\n%s%s  MetalRT Install%s\n\n", color::bold, color::orange, color::reset);
        if (rastack::MetalRTLoader::install()) {
            fprintf(stderr, "\n  %s%sMetalRT installed successfully!%s\n", color::bold, color::green, color::reset);
            fprintf(stderr, "  Run %srcli engine metalrt%s to activate.\n\n", color::bold, color::reset);
            return 0;
        }
        fprintf(stderr, "\n  %s%sInstallation failed.%s\n", color::bold, color::red, color::reset);
        fprintf(stderr, "  Tip: set METALRT_REPO=/path/to/metalrt-binaries for local install,\n");
        fprintf(stderr, "       or check your internet connection for remote install.\n\n");
        return 1;
    }

    if (args.arg1 == "remove") {
        if (!rastack::MetalRTLoader::instance().is_available()) {
            fprintf(stderr, "\n  MetalRT is not installed.\n\n");
            return 0;
        }
        rastack::MetalRTLoader::instance().unload();
        if (rastack::MetalRTLoader::remove()) {
            rcli::write_engine_preference("llamacpp");
            fprintf(stderr, "\n  %s%sMetalRT removed.%s Engine reverted to llama.cpp.\n\n",
                    color::bold, color::green, color::reset);
            return 0;
        }
        fprintf(stderr, "\n  %s%sFailed to remove MetalRT.%s\n\n", color::bold, color::red, color::reset);
        return 1;
    }

    if (args.arg1 == "status") {
        fprintf(stderr, "\n%s%s  MetalRT Status%s\n\n", color::bold, color::orange, color::reset);
        auto& loader = rastack::MetalRTLoader::instance();
        if (!loader.is_available()) {
            fprintf(stderr, "  %sInstalled:%s  no\n", color::bold, color::reset);
            fprintf(stderr, "  Run %srcli metalrt install%s to download.\n\n", color::bold, color::reset);
            return 0;
        }
        std::string ver = rastack::MetalRTLoader::installed_version();
        fprintf(stderr, "  %sInstalled:%s  %s%syes%s", color::bold, color::reset,
                color::bold, color::green, color::reset);
        if (!ver.empty()) fprintf(stderr, " (%s)", ver.c_str());
        fprintf(stderr, "\n");

        if (loader.load()) {
            fprintf(stderr, "  %sABI:%s       v%u\n", color::bold, color::reset, loader.abi_version());
        }
        loader.unload();

        // Show install mode
        bool local = rastack::MetalRTLoader::is_local_mode();
        std::string local_repo = rastack::MetalRTLoader::local_repo_path();
        if (local) {
            fprintf(stderr, "  %sMode:%s      %sLOCAL%s%s%s%s\n", color::bold, color::reset,
                    color::cyan, color::reset,
                    local_repo.empty() ? "" : " (",
                    local_repo.c_str(),
                    local_repo.empty() ? "" : ")");
        } else {
            fprintf(stderr, "  %sMode:%s      %sREMOTE%s (GitHub releases)\n",
                    color::bold, color::reset, color::green, color::reset);
        }

        // Show supported models
        auto models = rcli::all_models();
        fprintf(stderr, "  %sModels:%s    ", color::bold, color::reset);
        bool first = true;
        for (auto& m : models) {
            if (m.metalrt_supported) {
                if (!first) fprintf(stderr, ", ");
                bool inst = rcli::is_metalrt_model_installed(m);
                fprintf(stderr, "%s%s%s", inst ? color::green : "", m.name.c_str(),
                        inst ? color::reset : "");
                first = false;
            }
        }
        fprintf(stderr, "\n");

        // Codesign check
        std::string dylib = rastack::MetalRTLoader::dylib_path();
        std::string verify_cmd = "codesign --verify --deep --strict '" + dylib + "' 2>/dev/null";
        bool sig_ok = (system(verify_cmd.c_str()) == 0);
        fprintf(stderr, "  %sSignature:%s %s%s%s\n\n",
                color::bold, color::reset,
                sig_ok ? color::green : color::red,
                sig_ok ? "valid" : "INVALID",
                color::reset);
        return 0;
    }

    if (args.arg1 == "download") {
        auto models = rcli::all_models();
        std::vector<const rcli::LlmModelDef*> mrt_models;
        for (auto& m : models) {
            if (m.metalrt_supported) mrt_models.push_back(&m);
        }
        auto comp_models = rcli::metalrt_component_models();

        fprintf(stderr, "\n%s%s  MetalRT Model Download%s\n\n", color::bold, color::orange, color::reset);

        // LLM models
        fprintf(stderr, "  %s— LLM Models —%s\n", color::bold, color::reset);
        fprintf(stderr, "  %s#  %-28s  %-8s  %-12s  Status%s\n",
                color::bold, "Model", "Size", "Speed", color::reset);

        for (size_t i = 0; i < mrt_models.size(); i++) {
            auto* m = mrt_models[i];
            bool inst = rcli::is_metalrt_model_installed(*m);
            fprintf(stderr, "  %s%zu%s  %-28s  %-8s  %-12s  %s%s%s\n",
                    color::bold, i + 1, color::reset,
                    m->name.c_str(),
                    rcli::format_size(m->metalrt_size_mb).c_str(),
                    m->metalrt_speed_est.c_str(),
                    inst ? color::green : "",
                    inst ? "installed" : "-",
                    inst ? color::reset : "");
        }

        // STT/TTS component models
        size_t offset = mrt_models.size();
        fprintf(stderr, "\n  %s— STT/TTS Components —%s\n", color::bold, color::reset);
        fprintf(stderr, "  %s#  %-28s  %-8s  %-5s  Status%s\n",
                color::bold, "Model", "Size", "Type", color::reset);

        for (size_t i = 0; i < comp_models.size(); i++) {
            auto& cm = comp_models[i];
            bool inst = rcli::is_metalrt_component_installed(cm);
            std::string type_label = (cm.component == "stt") ? "STT" : "TTS";
            fprintf(stderr, "  %s%zu%s  %-28s  %-8s  %-5s  %s%s%s\n",
                    color::bold, offset + i + 1, color::reset,
                    cm.name.c_str(),
                    rcli::format_size(cm.size_mb).c_str(),
                    type_label.c_str(),
                    inst ? color::green : "",
                    inst ? "installed" : "-",
                    inst ? color::reset : "");
        }

        size_t total = offset + comp_models.size();
        fprintf(stderr, "\n  Enter model number [1-%zu/q]: ", total);
        fflush(stderr);

        char buf[16] = {};
        if (read(STDIN_FILENO, buf, sizeof(buf) - 1) <= 0 || buf[0] == '\n' || buf[0] == 'q') {
            fprintf(stderr, "\n  Cancelled.\n\n");
            return 0;
        }

        int choice = atoi(buf);
        if (choice < 1 || choice > (int)total) {
            fprintf(stderr, "\n  Invalid choice.\n\n");
            return 1;
        }

        if (choice <= (int)offset) {
            // LLM model selected
            auto* sel = mrt_models[choice - 1];
            if (rcli::is_metalrt_model_installed(*sel)) {
                fprintf(stderr, "\n  %s%s%s already installed.%s\n\n",
                        color::bold, color::green, sel->name.c_str(), color::reset);
                return 0;
            }

            std::string mrt_dir = rcli::metalrt_models_dir() + "/" + sel->metalrt_dir_name;
            fprintf(stderr, "\n  Downloading %s (%s)...\n\n",
                    sel->name.c_str(), rcli::format_size(sel->metalrt_size_mb).c_str());

            std::string config_url = sel->metalrt_url;
            auto cpos = config_url.rfind("model.safetensors");
            if (cpos != std::string::npos) config_url.replace(cpos, 17, "config.json");
            std::string dl_cmd = "bash -c '"
                "set -e; mkdir -p \"" + mrt_dir + "\"; "
                "curl -fL -# -o \"" + mrt_dir + "/model.safetensors\" \"" + sel->metalrt_url + "\"; "
                "curl -fL -# -o \"" + mrt_dir + "/tokenizer.json\" \"" + sel->metalrt_tokenizer_url + "\"; "
                "curl -fL -# -o \"" + mrt_dir + "/config.json\" \"" + config_url + "\"; "
                "'";

            if (system(dl_cmd.c_str()) != 0) {
                fprintf(stderr, "\n  %s%sDownload failed.%s Check your internet connection.\n\n",
                        color::bold, color::red, color::reset);
                return 1;
            }

            fprintf(stderr, "\n  %s%s%s downloaded!%s\n", color::bold, color::green, sel->name.c_str(), color::reset);
            fprintf(stderr, "  Location: %s\n\n", mrt_dir.c_str());
        } else {
            // STT/TTS component model selected
            auto& sel = comp_models[choice - 1 - offset];
            if (rcli::is_metalrt_component_installed(sel)) {
                fprintf(stderr, "\n  %s%s%s already installed.%s\n\n",
                        color::bold, color::green, sel.name.c_str(), color::reset);
                return 0;
            }

            std::string mrt_dir = rcli::metalrt_models_dir() + "/" + sel.dir_name;
            fprintf(stderr, "\n  Downloading %s (%s)...\n\n",
                    sel.name.c_str(), rcli::format_size(sel.size_mb).c_str());

            std::string hf_base = "https://huggingface.co/" + sel.hf_repo + "/resolve/main/";
            std::string subdir = sel.hf_subdir.empty() ? "" : sel.hf_subdir + "/";
            std::string dl_cmd;
            if (sel.component == "tts") {
                dl_cmd = "bash -c '"
                    "set -e; mkdir -p \"" + mrt_dir + "/voices\"; "
                    "curl -fL -# -o \"" + mrt_dir + "/config.json\" \"" + hf_base + subdir + "config.json\"; "
                    "curl -fL -# -o \"" + mrt_dir + "/kokoro-v1_0.safetensors\" \"" + hf_base + subdir + "kokoro-v1_0.safetensors\"; "
                    "for v in af_heart af_alloy af_aoede af_bella af_jessica af_kore af_nicole af_nova af_river af_sarah af_sky "
                    "am_adam am_echo am_eric am_fenrir am_liam am_michael am_onyx am_puck am_santa "
                    "bf_alice bf_emma bf_isabella bf_lily bm_daniel bm_fable bm_george bm_lewis; do "
                    "curl -fL -s -o \"" + mrt_dir + "/voices/${v}.safetensors\" \"" + hf_base + subdir + "voices/${v}.safetensors\"; "
                    "done; "
                    "'";
            } else {
                dl_cmd = "bash -c '"
                    "set -e; mkdir -p \"" + mrt_dir + "\"; "
                    "curl -fL -# -o \"" + mrt_dir + "/config.json\" \"" + hf_base + subdir + "config.json\"; "
                    "curl -fL -# -o \"" + mrt_dir + "/model.safetensors\" \"" + hf_base + subdir + "model.safetensors\"; "
                    "curl -fL -# -o \"" + mrt_dir + "/tokenizer.json\" \"" + hf_base + subdir + "tokenizer.json\"; "
                    "'";
            }

            if (system(dl_cmd.c_str()) != 0) {
                fprintf(stderr, "\n  %s%sDownload failed.%s Check your internet connection.\n\n",
                        color::bold, color::red, color::reset);
                return 1;
            }

            fprintf(stderr, "\n  %s%s%s downloaded!%s\n", color::bold, color::green, sel.name.c_str(), color::reset);
            fprintf(stderr, "  Location: %s\n\n", mrt_dir.c_str());
        }
        return 0;
    }

    if (args.arg1 == "bench") {
        if (!rastack::MetalRTLoader::instance().is_available()) {
            fprintf(stderr, "\n  MetalRT is not installed. Run %srcli metalrt install%s first.\n\n",
                    color::bold, color::reset);
            return 1;
        }

        auto models = rcli::all_models();
        const rcli::LlmModelDef* bench_model = nullptr;
        for (auto& m : models) {
            if (m.metalrt_supported && rcli::is_metalrt_model_installed(m)) {
                if (!bench_model || m.metalrt_size_mb > bench_model->metalrt_size_mb)
                    bench_model = &m;
            }
        }
        if (!bench_model) {
            fprintf(stderr, "\n  No MetalRT models installed. Run %srcli metalrt download%s first.\n\n",
                    color::bold, color::reset);
            return 1;
        }

        std::string model_dir = rcli::metalrt_models_dir() + "/" + bench_model->metalrt_dir_name;
        fprintf(stderr, "\n%s%s  MetalRT Benchmark%s\n\n", color::bold, color::orange, color::reset);
        fprintf(stderr, "  Model: %s\n", bench_model->name.c_str());
        fprintf(stderr, "  Path:  %s\n\n", model_dir.c_str());

        rastack::MetalRTEngineConfig cfg;
        cfg.model_dir = model_dir;
        cfg.max_tokens = 128;
        cfg.temperature = 0.0f;

        rastack::MetalRTEngine engine;
        if (!engine.init(cfg)) {
            fprintf(stderr, "  %s%sFailed to init MetalRT engine.%s\n\n",
                    color::bold, color::red, color::reset);
            return 1;
        }

        fprintf(stderr, "  Device: %s\n\n", engine.device_name().c_str());

        const char* prompts[] = {
            "Hello, how are you?",
            "Explain quantum computing in one sentence.",
            "Write a haiku about the ocean.",
        };

        for (int i = 0; i < 3; i++) {
            fprintf(stderr, "  Run %d: \"%s\"\n", i + 1, prompts[i]);
            engine.reset_conversation();
            std::string result = engine.generate(prompts[i]);

            const auto& stats = engine.last_stats();
            fprintf(stderr, "    Prefill: %6.1f ms (%d tokens, %.0f tok/s)\n",
                    stats.prompt_eval_us / 1000.0, stats.prompt_tokens, stats.prompt_tps());
            fprintf(stderr, "    Decode:  %6.1f ms (%d tokens, %.0f tok/s)\n",
                    stats.generation_us / 1000.0, stats.generated_tokens, stats.gen_tps());
            fprintf(stderr, "    Output:  \"%.*s%s\"\n\n",
                    (int)std::min(result.size(), (size_t)80), result.c_str(),
                    result.size() > 80 ? "..." : "");
        }

        engine.shutdown();
        return 0;
    }

    fprintf(stderr,
        "\n%s%s  rcli metalrt%s  —  Manage MetalRT engine\n\n"
        "  Commands:\n"
        "    rcli metalrt install    Download and install MetalRT binary\n"
        "    rcli metalrt remove     Uninstall MetalRT binary\n"
        "    rcli metalrt status     Show version, models, signature status\n"
        "    rcli metalrt download   Download MetalRT model weights\n"
        "    rcli metalrt bench      Benchmark MetalRT inference speed\n\n",
        color::bold, color::orange, color::reset);
    return args.arg1.empty() ? 0 : 1;
}

static int cmd_engine(const Args& args) {
    std::string target = args.arg1;

    if (target == "metalrt") {
        if (!rastack::MetalRTLoader::gpu_supported()) {
            fprintf(stderr, "\n  %s%sMetalRT requires Apple M3 or later.%s\n"
                    "  Your Mac uses an M1/M2 chip which doesn't support Metal 3.1 shaders.\n"
                    "  Please use llama.cpp instead: %srcli engine llamacpp%s\n\n",
                    color::bold, color::red, color::reset, color::bold, color::reset);
            return 1;
        }
        if (!rastack::MetalRTLoader::instance().is_available()) {
            fprintf(stderr, "\n  MetalRT not found. Installing automatically...\n\n");
            if (!rastack::MetalRTLoader::install()) {
                fprintf(stderr, "  %s%sMetalRT install failed.%s Check internet and try: %srcli metalrt install%s\n\n",
                        color::bold, color::red, color::reset, color::bold, color::reset);
                return 1;
            }
            fprintf(stderr, "  %s%sMetalRT engine installed!%s\n\n", color::bold, color::green, color::reset);

            auto models = rcli::all_models();
            for (auto& m : models) {
                if (m.metalrt_id == "metalrt-lfm2.5-1.2b" && !rcli::is_metalrt_model_installed(m)) {
                    fprintf(stderr, "  Downloading MetalRT LLM: %s...\n", m.name.c_str());
                    std::string mrt_dir = rcli::metalrt_models_dir() + "/" + m.metalrt_dir_name;
                    std::string cfg_url = m.metalrt_url;
                    auto pos = cfg_url.rfind("model.safetensors");
                    if (pos != std::string::npos) cfg_url.replace(pos, 17, "config.json");
                    std::string dl = "bash -c 'set -e; mkdir -p \"" + mrt_dir + "\"; "
                        "curl -fL -# -o \"" + mrt_dir + "/model.safetensors\" \"" + m.metalrt_url + "\"; "
                        "curl -fL -# -o \"" + mrt_dir + "/tokenizer.json\" \"" + m.metalrt_tokenizer_url + "\"; "
                        "curl -fL -# -o \"" + mrt_dir + "/config.json\" \"" + cfg_url + "\"; '";
                    if (system(dl.c_str()) != 0)
                        fprintf(stderr, "  %sLLM download failed.%s\n", color::yellow, color::reset);
                    break;
                }
            }
            auto comps = rcli::metalrt_component_models();
            for (auto& cm : comps) {
                if (!cm.default_install || rcli::is_metalrt_component_installed(cm)) continue;
                std::string label = (cm.component == "stt") ? "STT" : "TTS";
                fprintf(stderr, "  Downloading MetalRT %s: %s...\n", label.c_str(), cm.name.c_str());
                std::string cm_dir = rcli::metalrt_models_dir() + "/" + cm.dir_name;
                std::string hf = "https://huggingface.co/" + cm.hf_repo + "/resolve/main/";
                std::string sub = cm.hf_subdir.empty() ? "" : cm.hf_subdir + "/";
                if (cm.component == "tts") {
                    std::string dl = "bash -c 'set -e; mkdir -p \"" + cm_dir + "/voices\"; "
                        "curl -fL -# -o \"" + cm_dir + "/config.json\" \"" + hf + sub + "config.json\"; "
                        "curl -fL -# -o \"" + cm_dir + "/kokoro-v1_0.safetensors\" \"" + hf + sub + "kokoro-v1_0.safetensors\"; "
                        "for v in af_heart af_alloy af_aoede af_bella af_jessica af_kore af_nicole af_nova af_river af_sarah af_sky "
                        "am_adam am_echo am_eric am_fenrir am_liam am_michael am_onyx am_puck am_santa "
                        "bf_alice bf_emma bf_isabella bf_lily bm_daniel bm_fable bm_george bm_lewis; do "
                        "curl -fL -s -o \"" + cm_dir + "/voices/${v}.safetensors\" \"" + hf + sub + "voices/${v}.safetensors\"; done; '";
                    if (system(dl.c_str()) != 0)
                        fprintf(stderr, "  %s%s download failed.%s\n", color::yellow, label.c_str(), color::reset);
                } else {
                    std::string dl = "bash -c 'set -e; mkdir -p \"" + cm_dir + "\"; "
                        "curl -fL -# -o \"" + cm_dir + "/config.json\" \"" + hf + sub + "config.json\"; "
                        "curl -fL -# -o \"" + cm_dir + "/model.safetensors\" \"" + hf + sub + "model.safetensors\"; "
                        "curl -fL -# -o \"" + cm_dir + "/tokenizer.json\" \"" + hf + sub + "tokenizer.json\"; '";
                    if (system(dl.c_str()) != 0)
                        fprintf(stderr, "  %s%s download failed.%s\n", color::yellow, label.c_str(), color::reset);
                }
            }
        }
        rcli::write_engine_preference("metalrt");
        fprintf(stderr, "\n  %s%sEngine set to MetalRT.%s Restart RCLI to apply.\n\n",
                color::bold, color::green, color::reset);
        return 0;
    }

    if (target == "llamacpp") {
        rcli::write_engine_preference("llamacpp");
        fprintf(stderr, "\n  %s%sEngine set to llama.cpp.%s Restart RCLI to apply.\n\n",
                color::bold, color::green, color::reset);
        return 0;
    }

    if (target == "auto") {
        rcli::write_engine_preference("auto");
        fprintf(stderr, "\n  %s%sEngine set to Auto.%s MetalRT when available, llama.cpp fallback.\n\n",
                color::bold, color::green, color::reset);
        return 0;
    }

    std::string current = rcli::read_engine_preference();
    if (current.empty()) current = "auto";
    fprintf(stderr,
        "\n%s%s  rcli engine%s  —  Switch LLM inference backend\n\n"
        "  Current: %s%s%s\n\n"
        "  Commands:\n"
        "    rcli engine metalrt    Use MetalRT (Apple Silicon GPU)\n"
        "    rcli engine llamacpp   Use llama.cpp (open source)\n"
        "    rcli engine auto       Auto-select best available\n\n",
        color::bold, color::orange, color::reset,
        color::bold, current.c_str(), color::reset);
    return 0;
}

static int cmd_personality(const Args& args) {
    std::string target = args.arg1;

    if (!target.empty()) {
        auto* info = rastack::find_personality(target);
        if (!info) {
            fprintf(stderr, "\n  %s%sUnknown personality: %s%s\n\n", color::bold, color::red, target.c_str(), color::reset);
            fprintf(stderr, "  Available: ");
            for (auto& p : rastack::all_personalities()) fprintf(stderr, "%s ", p.key);
            fprintf(stderr, "\n\n");
            return 1;
        }
        rcli::write_personality_preference(target);
        fprintf(stderr, "\n  %s%sPersonality set to %s.%s %s\n",
                color::bold, color::green, info->name, color::reset, info->tagline);
        fprintf(stderr, "  Restart RCLI to apply.\n\n");
        return 0;
    }

    // Show current + list
    std::string current = rcli::read_personality_preference();
    if (current.empty()) current = "default";

    fprintf(stderr, "\n%s%s  rcli personality%s  —  Change assistant personality\n\n",
            color::bold, color::orange, color::reset);
    fprintf(stderr, "  Current: %s%s%s\n\n", color::bold, current.c_str(), color::reset);
    fprintf(stderr, "  Available:\n");
    for (auto& p : rastack::all_personalities()) {
        const char* marker = (current == p.key) ? " *" : "  ";
        fprintf(stderr, "   %s %s%-14s%s  %s\n", marker, color::green, p.key, color::reset, p.tagline);
    }
    fprintf(stderr, "\n  Usage: rcli personality <name>\n\n");
    return 0;
}

// =============================================================================
// Main — command dispatch
// =============================================================================

int main(int argc, char** argv) {
    Args args = parse_args(argc, argv);

    rastack::set_log_level(args.verbose ? rastack::LogLevel::DEBUG : rastack::LogLevel::ERROR);

    if (!args.verbose) {
        llama_log_set([](enum ggml_log_level, const char*, void*) {}, nullptr);
    }

    if (args.command.empty()) {
        if (args.help) { print_usage(argv[0]); return 0; }
        return cmd_interactive(args);
    }

    if (args.command == "listen")      return cmd_listen(args);
    if (args.command == "ask")         return cmd_ask(args);
    if (args.command == "mic-test")    return cmd_mic_test(args);
    if (args.command == "actions")     return cmd_actions(args);
    if (args.command == "action")      return cmd_action(args);
    if (args.command == "rag")         return cmd_rag(args);
    if (args.command == "setup")       return cmd_setup(args);
    if (args.command == "models")      return cmd_models(args);
    if (args.command == "voices")      return cmd_voices(args);
    if (args.command == "stt")         return cmd_stt_picker(args);
    if (args.command == "upgrade-stt") return cmd_upgrade_stt(args);
    if (args.command == "upgrade-llm") return cmd_upgrade_llm(args);
    if (args.command == "cleanup")     return cmd_cleanup(args);
    if (args.command == "bench")       return cmd_bench(args);
    if (args.command == "info")        return cmd_info();
    if (args.command == "metalrt") {
        // Subcommands: install, remove, status, download, bench → management
        if (!args.arg1.empty() && (args.arg1 == "install" || args.arg1 == "remove" ||
            args.arg1 == "status" || args.arg1 == "download" || args.arg1 == "bench"))
            return cmd_metalrt(args);
        // Bare "rcli metalrt" → launch interactive TUI with MetalRT engine
        Args override_args = args;
        override_args.engine_override = "metalrt";
        override_args.command.clear();
        return cmd_interactive(override_args);
    }
    if (args.command == "llamacpp") {
        // "rcli llamacpp" → launch interactive TUI with llama.cpp engine
        Args override_args = args;
        override_args.engine_override = "llamacpp";
        override_args.command.clear();
        return cmd_interactive(override_args);
    }
    if (args.command == "engine")      return cmd_engine(args);
    if (args.command == "personality") return cmd_personality(args);

    // Hidden test command: exercises streaming LLM → TTS pipeline
    if (args.command == "stream-test") {
        if (!models_exist(args.models_dir)) { print_missing_models(args.models_dir); return 1; }
        g_engine = rcli_create(nullptr);
        if (!g_engine) return 1;
        fprintf(stderr, "%sInitializing...%s\n", color::dim, color::reset);
        if (rcli_init(g_engine, args.models_dir.c_str(), args.gpu_layers) != 0) {
            rcli_destroy(g_engine); return 1;
        }
        std::string query = args.arg1.empty() ? "hello, how are you?" : args.arg1;
        auto cb = [](const char* event, const char* data, void*) {
            fprintf(stderr, "  [STREAM] event=%-12s data=%.80s\n", event, data ? data : "");
        };
        fprintf(stderr, "\nTesting streaming pipeline with: \"%s\"\n", query.c_str());
        const char* resp = rcli_process_and_speak(g_engine, query.c_str(), cb, nullptr);
        fprintf(stderr, "\nResponse: %s\n", resp ? resp : "(null)");
        rcli_destroy(g_engine);
        return resp && resp[0] ? 0 : 1;
    }

    if (args.command == "--help" || args.command == "-h") {
        print_usage(argv[0]);
        return 0;
    }

    fprintf(stderr, "%sUnknown command: %s%s\n", color::red, args.command.c_str(), color::reset);
    print_usage(argv[0]);
    return 1;
}
