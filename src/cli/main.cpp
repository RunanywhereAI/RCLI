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
#include "audio/audio_io.h"
#include "audio/mic_permission.h"
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
// Interactive mode — prompt reset helper
// =============================================================================

static void reset_prompt(std::string& input_buf, bool& typing, size_t& cursor_pos) {
    input_buf.clear();
    typing = false;
    cursor_pos = 0;
    fprintf(stderr, "  %s%s>%s ", color::orange, color::bold, color::reset);
    fflush(stderr);
}

static void reset_prompt_nl(std::string& input_buf, bool& typing, size_t& cursor_pos) {
    input_buf.clear();
    typing = false;
    cursor_pos = 0;
    fprintf(stderr, "\n  %s%s>%s ", color::orange, color::bold, color::reset);
    fflush(stderr);
}

// =============================================================================
// Interactive mode — command dispatch (extracted from the main loop)
// =============================================================================

// Returns true if the input was handled as a command, false to fall through to LLM.
static bool dispatch_command(const std::string& cmd_input,
                             const std::string& input_buf,
                             const Args& args,
                             bool& rag_loaded,
                             std::string& ibuf, bool& typing, size_t& cursor_pos) {
    if (cmd_input == "quit" || cmd_input == "q" || cmd_input == "exit") {
        g_running = false;
        return true;
    }

    if (cmd_input == "help" || cmd_input == "h" || cmd_input == "?") {
        print_help_interactive();
        reset_prompt(ibuf, typing, cursor_pos);
        return true;
    }

    if (cmd_input == "models") {
        fprintf(stderr, "\n  %s%s  Active Models%s\n\n", color::bold, color::orange, color::reset);
        fprintf(stderr, "    %sLLM:%s  %s%s%s\n",
                color::bold, color::reset,
                color::green, rcli_get_llm_model(g_engine), color::reset);
        fprintf(stderr, "    %sSTT:%s  %s%s%s (offline)  |  Zipformer (streaming)\n",
                color::bold, color::reset,
                color::green, rcli_get_stt_model(g_engine), color::reset);
        fprintf(stderr, "    %sTTS:%s  %s%s%s\n\n",
                color::bold, color::reset,
                color::green, rcli_get_tts_model(g_engine), color::reset);
        fprintf(stderr, "    %sTip:%s Run %srcli models%s to switch (LLM, STT, or TTS).\n\n",
                color::dim, color::reset, color::bold, color::reset);
        reset_prompt(ibuf, typing, cursor_pos);
        return true;
    }

    if (cmd_input == "voices") {
        auto tts_all = rcli::all_tts_models();
        std::string mdir = default_models_dir();
        const auto* active_tts = rcli::resolve_active_tts(mdir, tts_all);
        std::string user_tts = rcli::read_selected_tts_id();

        fprintf(stderr, "\n  %s%s  Voices%s", color::bold, color::orange, color::reset);
        fprintf(stderr, !user_tts.empty() ? "  (pinned: %s)" : "  (auto-detect)", user_tts.c_str());
        fprintf(stderr, "\n\n");

        fprintf(stderr, "    %s#  %-30s  %-8s  %-8s  %-10s  %s%s\n",
                color::bold, "Voice", "Size", "Arch", "Speakers", "Status", color::reset);
        for (size_t i = 0; i < tts_all.size(); i++) {
            auto& v = tts_all[i];
            bool inst = rcli::is_tts_installed(mdir, v);
            bool is_act = active_tts && active_tts->id == v.id;
            std::string label = v.name;
            if (v.is_default) label += " (default)";
            char spk[16]; snprintf(spk, sizeof(spk), "%d", v.num_speakers);
            fprintf(stderr, "    %s%-2zu%s %-30s  %-8s  %-8s  %-10s  %s%s%s\n",
                    is_act ? "\033[32m" : "", i + 1, is_act ? "\033[0m" : "",
                    label.c_str(), rcli::format_size(v.size_mb).c_str(),
                    v.architecture.c_str(), spk,
                    is_act ? "\033[32m\xe2\x97\x8f active" :
                        (inst ? "installed" : "\033[2mnot installed"),
                    is_act || !inst ? "\033[0m" : "", "");
        }
        fprintf(stderr, "\n    %sTip:%s Run %srcli voices%s to switch voices.\n\n",
                color::dim, color::reset, color::bold, color::reset);
        reset_prompt(ibuf, typing, cursor_pos);
        return true;
    }

    if (cmd_input == "stt") {
        auto stt_all = rcli::all_stt_models();
        std::string mdir = default_models_dir();
        const auto* active_stt_m = rcli::resolve_active_stt(mdir, stt_all);
        std::string user_stt = rcli::read_selected_stt_id();

        fprintf(stderr, "\n  %s%s  STT Models%s", color::bold, color::orange, color::reset);
        fprintf(stderr, !user_stt.empty() ? "  (pinned: %s)" : "  (auto-detect)", user_stt.c_str());
        fprintf(stderr, "\n\n");
        fprintf(stderr, "    %sStreaming:%s  Zipformer (always active for live mic)\n",
                color::bold, color::reset);
        fprintf(stderr, "    %sOffline:%s   %s%s%s (active)\n\n",
                color::bold, color::reset, color::green,
                active_stt_m ? active_stt_m->name.c_str() : rcli_get_stt_model(g_engine),
                color::reset);

        auto offline = rcli::get_offline_stt_models(stt_all);
        fprintf(stderr, "    %s#  %-28s  %-7s  %-12s  %s%s\n",
                color::bold, "Offline Model", "Size", "Accuracy", "Status", color::reset);
        for (size_t i = 0; i < offline.size(); i++) {
            auto& m = *offline[i];
            bool inst = rcli::is_stt_installed(mdir, m);
            bool is_act = active_stt_m && active_stt_m->id == m.id;
            std::string label = m.name;
            if (m.is_default) label += " (default)";
            fprintf(stderr, "    %s%-2zu%s %-28s  %-7s  %-12s  %s%s%s\n",
                    is_act ? "\033[32m" : "", i + 1, is_act ? "\033[0m" : "",
                    label.c_str(), rcli::format_size(m.size_mb).c_str(),
                    m.accuracy.c_str(),
                    is_act ? "\033[32m\xe2\x97\x8f active" :
                        (inst ? "installed" : "\033[2mnot installed"),
                    is_act || !inst ? "\033[0m" : "", "");
        }
        fprintf(stderr, "\n    %sTip:%s Run %srcli stt%s to switch STT models.\n\n",
                color::dim, color::reset, color::bold, color::reset);
        reset_prompt(ibuf, typing, cursor_pos);
        return true;
    }

    if (cmd_input == "actions" || cmd_input.substr(0, 8) == "actions ") {
        std::string detail_name;
        if (cmd_input.size() > 8) detail_name = cmd_input.substr(8);

        ActionRegistry inline_reg;
        inline_reg.register_defaults();

        if (!detail_name.empty()) {
            const ActionDef* def = inline_reg.get_def(detail_name);
            if (def) {
                disable_raw_mode();
                print_action_detail(*def);
                enable_raw_mode();
            } else {
                fprintf(stderr, "  %s%sUnknown action: %s%s  (type %sactions%s to list all)\n",
                        color::bold, color::red, detail_name.c_str(), color::reset,
                        color::bold, color::reset);
            }
        } else {
            disable_raw_mode();
            print_actions_interactive();
            enable_raw_mode();
        }
        reset_prompt(ibuf, typing, cursor_pos);
        return true;
    }

    if (cmd_input.substr(0, 3) == "do " ||
        (input_buf[0] == '/' && input_buf.substr(0, 4) == "/do ")) {
        std::string action_str = (cmd_input.substr(0, 3) == "do ")
            ? cmd_input.substr(3) : input_buf.substr(4);
        std::string action_name, action_text;
        auto space = action_str.find(' ');
        if (space != std::string::npos) {
            action_name = action_str.substr(0, space);
            action_text = action_str.substr(space + 1);
        } else {
            action_name = action_str;
        }

        ActionRegistry do_reg;
        do_reg.register_defaults();
        const ActionDef* def = do_reg.get_def(action_name);
        if (!def) {
            fprintf(stderr, "  %s%sUnknown action: %s%s  (type %sactions%s to list all)\n",
                    color::bold, color::red, action_name.c_str(), color::reset,
                    color::bold, color::reset);
            reset_prompt(ibuf, typing, cursor_pos);
            return true;
        }

        std::string action_args = "{}";
        if (!action_text.empty()) {
            if (action_text.front() == '{') {
                action_args = action_text;
            } else {
                std::string first_key;
                const auto& schema = def->parameters_json;
                auto qpos = schema.find('"');
                if (qpos != std::string::npos) {
                    auto qend = schema.find('"', qpos + 1);
                    if (qend != std::string::npos)
                        first_key = schema.substr(qpos + 1, qend - qpos - 1);
                }
                if (!first_key.empty()) {
                    std::string escaped;
                    for (char ch : action_text) {
                        if (ch == '"') escaped += "\\\"";
                        else if (ch == '\\') escaped += "\\\\";
                        else if (ch == '\n') escaped += "\\n";
                        else escaped += ch;
                    }
                    action_args = "{\"" + first_key + "\": \"" + escaped + "\"}";
                }
            }
        }

        auto result = do_reg.execute(action_name, action_args);
        if (result.success)
            fprintf(stderr, "  %s%s%s%s\n", color::green, color::bold, result.output.c_str(), color::reset);
        else
            fprintf(stderr, "  %s%s[failed]%s %s  %s\n",
                    color::bold, color::red, color::reset, action_name.c_str(), result.error.c_str());

        reset_prompt_nl(ibuf, typing, cursor_pos);
        return true;
    }

    if (cmd_input == "rag status") {
        if (rag_loaded) {
            fprintf(stderr, "  %s%sRAG:%s active (index: %s%s%s)\n",
                    color::bold, color::green, color::reset,
                    color::dim, args.rag_index.c_str(), color::reset);
        } else {
            fprintf(stderr, "  %s%sRAG:%s not loaded\n", color::bold, color::yellow, color::reset);
            fprintf(stderr, "  %sStart with: rcli --rag <index_dir>%s\n", color::dim, color::reset);
        }
        reset_prompt_nl(ibuf, typing, cursor_pos);
        return true;
    }

    if (cmd_input.substr(0, 11) == "rag ingest " || cmd_input == "rag ingest") {
        std::string docs_dir;
        if (cmd_input.size() > 11) docs_dir = cmd_input.substr(11);
        while (!docs_dir.empty() && docs_dir.back() == ' ') docs_dir.pop_back();
        while (!docs_dir.empty() && docs_dir.front() == ' ') docs_dir.erase(docs_dir.begin());
        if (!docs_dir.empty()) docs_dir = sanitize_path(docs_dir);

        if (docs_dir.empty()) {
            fprintf(stderr, "  %sUsage: rag ingest <directory>%s\n", color::dim, color::reset);
        } else {
            if (!ensure_embedding_model(args.models_dir)) {
                reset_prompt_nl(ibuf, typing, cursor_pos);
                return true;
            }
            fprintf(stderr, "  Indexing %s ...\n", docs_dir.c_str());
            fflush(stderr);
            int rc = rcli_rag_ingest(g_engine, docs_dir.c_str());
            if (rc == 0) {
                rag_loaded = true;
                fprintf(stderr, "  %s%sIndexing complete!%s RAG is now active.\n",
                        color::bold, color::green, color::reset);
            } else {
                fprintf(stderr, "  %s%sIndexing failed.%s\n", color::bold, color::red, color::reset);
            }
        }
        reset_prompt_nl(ibuf, typing, cursor_pos);
        return true;
    }

    return false;  // Not a command — fall through to LLM
}

// =============================================================================
// Interactive mode — push-to-talk voice recording
// =============================================================================

static void handle_voice_recording(const Args& args, bool rag_loaded) {
    rcli_stop_speaking(g_engine);

    g_recording = true;
    g_vis_active = true;
    g_peak_rms.store(0.0f, std::memory_order_relaxed);

    int saved_stderr = visualizer::suppress_stderr();
    rcli_start_capture(g_engine);

    visualizer::begin();
    fprintf(visualizer::vis_out, "\n");
    fflush(visualizer::vis_out);
    visualizer::draw(visualizer::DogState::LISTENING, 0.0f, 0, true);

    std::atomic<bool> vis_running{true};
    std::thread vis_thread([&vis_running]() {
        int tick = 0;
        while (vis_running.load(std::memory_order_relaxed)) {
            float level = rcli_get_audio_level(g_engine);
            float prev = g_peak_rms.load(std::memory_order_relaxed);
            if (level > prev) g_peak_rms.store(level, std::memory_order_relaxed);
            visualizer::draw(visualizer::DogState::LISTENING, level, tick++);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });

    while (g_running) {
        char stop = 0;
        ssize_t r = read(STDIN_FILENO, &stop, 1);
        if (r <= 0) continue;
        if (stop != ' ') break;
    }

    vis_running = false;
    vis_thread.join();
    g_recording = false;

    bool below_noise_gate = (g_peak_rms.load(std::memory_order_relaxed) < NOISE_GATE_THRESHOLD);

    if (below_noise_gate) {
        rcli_stop_capture_and_transcribe(g_engine);
        visualizer::clear();
        g_vis_active = false;
        visualizer::restore_stderr(saved_stderr);
        fprintf(stderr, "%s", visualizer::ESC_SHOW_CURSOR);
        fprintf(stderr, "  %s(no speech detected — speak louder or closer to the mic)%s\n",
                color::dim, color::reset);
    } else {
        visualizer::draw(visualizer::DogState::THINKING, 0.0f, 0);
        const char* transcript = rcli_stop_capture_and_transcribe(g_engine);

        visualizer::clear();
        g_vis_active = false;
        visualizer::restore_stderr(saved_stderr);
        fprintf(stderr, "%s", visualizer::ESC_SHOW_CURSOR);

        if (transcript && transcript[0]) {
            fprintf(stderr, "  %s%sYou:%s %s\n", color::bold, color::blue, color::reset, transcript);
            print_stt_perf(g_engine);
            fprintf(stderr, "  %sThinking...%s", color::dim, color::reset);
            fflush(stderr);

            int llm_saved = visualizer::suppress_stderr();
            const char* response = nullptr;
            if (rag_loaded) {
                response = rcli_rag_query(g_engine, transcript);
            } else {
                response = rcli_process_command(g_engine, transcript);
            }
            visualizer::restore_stderr(llm_saved);

            fprintf(stderr, "\r%s", visualizer::ESC_CLEAR_LINE);

            if (response && response[0]) {
                print_response(response);
                print_llm_perf(g_engine);
                int speak_saved = visualizer::suppress_stderr();
                if (!args.no_speak) rcli_speak(g_engine, response);
                visualizer::restore_stderr(speak_saved);
                print_tts_perf(g_engine);
            }
        } else {
            fprintf(stderr, "  %s(no speech detected — try speaking louder)%s\n",
                    color::dim, color::reset);
        }
    }
    g_current_transcript.clear();
    fprintf(stderr, "\n%s%s  >%s ", color::orange, color::bold, color::reset);
    fflush(stderr);
}

// =============================================================================
// Interactive mode — main loop
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

    g_engine = rcli_create(nullptr);
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
        fprintf(stderr, " \xc2\xb7 \xe2\x9a\xa1 %.0f tok/s", warmup_tps);
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
// Listen mode (continuous voice-to-action)
// =============================================================================

static int cmd_listen(const Args& args) {
    if (args.help) { print_help_listen(); return 0; }
    if (!ensure_mic_permission()) return 1;
    if (!models_exist(args.models_dir)) { print_missing_models(args.models_dir); return 1; }

    fprintf(stderr, "\n%s%s  RCLI — Continuous Voice Mode%s\n", color::bold, color::orange, color::reset);
    fprintf(stderr, "  Speak naturally. RA listens, acts, and responds.\n");
    fprintf(stderr, "  Press Ctrl+C to stop.\n\n");

    g_engine = rcli_create(nullptr);
    if (!g_engine) return 1;

    signal(SIGINT, signal_handler);

    static std::atomic<int> listen_vis_state{0};

    rcli_set_transcript_callback(g_engine,
        [](const char* text, int is_final, void* ud) {
            auto* a = static_cast<const Args*>(ud);
            if (is_final) {
                listen_vis_state.store(1);
                visualizer::clear();
                fprintf(stderr, "  %s%sYou:%s %s\n\n", color::bold, color::blue, color::reset, text);
                visualizer::begin();
                visualizer::draw(visualizer::DogState::THINKING, 0.0f, 0, true);

                const char* response = rcli_process_command(g_engine, text);
                visualizer::clear();

                if (response && response[0]) {
                    print_response(response);
                    print_llm_perf(g_engine);
                    if (!a->no_speak) {
                        listen_vis_state.store(2);
                        fprintf(stderr, "\n");
                        visualizer::begin();
                        visualizer::draw(visualizer::DogState::SPEAKING, 0.3f, 0, true);
                        rcli_speak(g_engine, response);
                        visualizer::clear();
                        print_tts_perf(g_engine);
                    }
                }

                listen_vis_state.store(0);
                fprintf(stderr, "\n");
                visualizer::begin();
                visualizer::draw(visualizer::DogState::LISTENING, 0.0f, 0, true);
            }
        }, const_cast<Args*>(&args));

    rcli_set_action_callback(g_engine,
        [](const char* name, const char* result, int success, void*) {
            print_action_result(name, result, success != 0);
        }, nullptr);

    if (rcli_init(g_engine, args.models_dir.c_str(), args.gpu_layers) != 0) {
        rcli_destroy(g_engine);
        return 1;
    }

    fprintf(stderr, "  %s%sReady.%s\n\n", color::bold, color::green, color::reset);
    visualizer::set_output(visualizer::open_tty());
    rcli_start_listening(g_engine);

    visualizer::begin();
    fprintf(visualizer::vis_out, "\n");
    fflush(visualizer::vis_out);
    visualizer::draw(visualizer::DogState::LISTENING, 0.0f, 0, true);

    int tick = 0;
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        int vs = listen_vis_state.load();
        if (vs == 0)      visualizer::draw(visualizer::DogState::LISTENING, rcli_get_audio_level(g_engine), tick++);
        else if (vs == 1)  visualizer::draw(visualizer::DogState::THINKING, 0.0f, tick++);
        else               visualizer::draw(visualizer::DogState::SPEAKING, 0.3f, tick++);
    }

    visualizer::clear();
    rcli_stop_listening(g_engine);
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

    if (args.command == "--help" || args.command == "-h") {
        print_usage(argv[0]);
        return 0;
    }

    fprintf(stderr, "%sUnknown command: %s%s\n", color::red, args.command.c_str(), color::reset);
    print_usage(argv[0]);
    return 1;
}
