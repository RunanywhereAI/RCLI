#pragma once
// =============================================================================
// RCLI TUI App — Full-screen interactive terminal UI (mactop-style)
// =============================================================================

#include "cli/tui_dashboard.h"
#include "cli/cli_common.h"
#include "api/rcli_api.h"
#include "models/model_registry.h"
#include "models/tts_model_registry.h"
#include "models/stt_model_registry.h"
#include "actions/action_registry.h"

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/component/loop.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/table.hpp>
#include <ftxui/screen/color.hpp>

#include <string>
#include <vector>
#include <deque>
#include <mutex>
#include <thread>
#include <functional>
#include <chrono>
#include <atomic>
#include <sstream>
#include <cmath>
#include <algorithm>

// Implemented in rcli_api.cpp — structured action definitions for TUI
std::vector<rcli::ActionDef> rcli_get_all_action_defs(RCLIHandle handle);

namespace rcli_tui {

using namespace ftxui;

// =============================================================================
// Theme — adapts to dark or light terminal background
// =============================================================================

struct TuiTheme {
    ftxui::Color accent;         // primary brand color (orange)
    ftxui::Color accent2;        // secondary brand color
    ftxui::Color text_normal;    // normal text
    ftxui::Color text_muted;     // dim / secondary text
    ftxui::Color text_selected;  // highlighted item in lists
    ftxui::Color success;        // green
    ftxui::Color warning;        // yellow
    ftxui::Color error;          // red
    ftxui::Color info;           // cyan
    ftxui::Color gauge_cpu;
    ftxui::Color gauge_mem;
    ftxui::Color gauge_gpu;
    ftxui::Color user_msg;       // user message prefix
    ftxui::Color assistant_msg;  // assistant message prefix
};

inline bool detect_light_terminal() {
#ifdef __APPLE__
    if (const char* bg = getenv("COLORFGBG")) {
        std::string s(bg);
        auto pos = s.rfind(';');
        if (pos != std::string::npos) {
            int bg_val = std::atoi(s.c_str() + pos + 1);
            return bg_val >= 8;
        }
    }
    FILE* p = popen("defaults read -g AppleInterfaceStyle 2>/dev/null", "r");
    if (p) {
        char buf[64] = {};
        fgets(buf, sizeof(buf), p);
        int rc = pclose(p);
        if (rc != 0) return true;
    }
#endif
    return false;
}

inline TuiTheme make_theme(bool light) {
    if (light) {
        return {
            ftxui::Color::RGB(200, 120, 0),     // accent (darker orange)
            ftxui::Color::RGB(180, 100, 0),      // accent2
            ftxui::Color::RGB(30, 30, 30),        // text_normal
            ftxui::Color::RGB(100, 100, 100),     // text_muted
            ftxui::Color::RGB(0, 0, 0),           // text_selected
            ftxui::Color::RGB(0, 140, 0),         // success (darker green)
            ftxui::Color::RGB(180, 140, 0),       // warning (darker yellow)
            ftxui::Color::RGB(200, 0, 0),         // error
            ftxui::Color::RGB(0, 130, 160),       // info (darker cyan)
            ftxui::Color::RGB(0, 140, 0),         // gauge_cpu
            ftxui::Color::RGB(180, 140, 0),       // gauge_mem
            ftxui::Color::RGB(0, 130, 160),       // gauge_gpu
            ftxui::Color::RGB(0, 120, 0),         // user_msg
            ftxui::Color::RGB(200, 120, 0),       // assistant_msg
        };
    }
    return {
        ftxui::Color::RGB(255, 165, 0),     // accent (orange)
        ftxui::Color::RGB(255, 165, 0),     // accent2
        ftxui::Color::White,                 // text_normal
        ftxui::Color::GrayDark,              // text_muted
        ftxui::Color::White,                 // text_selected
        ftxui::Color::Green,                 // success
        ftxui::Color::Yellow,                // warning
        ftxui::Color::Red,                   // error
        ftxui::Color::Cyan,                  // info
        ftxui::Color::Green,                 // gauge_cpu
        ftxui::Color::Yellow,                // gauge_mem
        ftxui::Color::Cyan,                  // gauge_gpu
        ftxui::Color::Green,                 // user_msg
        ftxui::Color::RGB(255, 165, 0),     // assistant_msg
    };
}

struct ChatMessage {
    std::string prefix;
    std::string text;
    std::string perf;
    bool is_user = false;
};

enum class VoiceState { IDLE, RECORDING, TRANSCRIBING, THINKING, SPEAKING };

inline Element gauge_with_label(const std::string& label, float value, ftxui::Color c) {
    int pct = (int)(value * 100);
    std::string pct_str = std::to_string(pct) + "%";
    return hbox({
        text(label) | ftxui::bold | size(WIDTH, EQUAL, 5),
        gauge(value) | flex | ftxui::color(c),
        text(" " + pct_str) | size(WIDTH, EQUAL, 5),
    });
}

inline std::string format_bytes_rate(float kbs) {
    if (kbs > 1024.0f) return std::to_string((int)(kbs / 1024.0f)) + " MB/s";
    return std::to_string((int)kbs) + " KB/s";
}

// ASCII dog frames for FTXUI rendering
struct DogFrame {
    std::string l1, l2, l3, l4;
    std::string label;
    ftxui::Color clr;
};

inline DogFrame get_dog_frame(VoiceState state, int tick) {
    auto cyan   = ftxui::Color::Cyan;
    auto yellow = ftxui::Color::Yellow;
    auto green  = ftxui::Color::Green;
    auto dim_c  = ftxui::Color::GrayDark;

    switch (state) {
    case VoiceState::RECORDING: {
        int f = tick % 3;
        if (f == 0) return {"  /^ ^\\   ", " ( o.o ) ))", "  > ^ <   ", "   |_|    ",
                            "Listening... speak now! Press ENTER or SPACE to stop.", cyan};
        if (f == 1) return {"  /^ ^\\    ", " ( O.O ) )))", "  > ^ <    ", "   |_|     ",
                            "Listening... speak now! Press ENTER or SPACE to stop.", cyan};
        return                {"  /^ ^\\  ", " ( o.o ) )", "  > ^ <  ", "   |_|   ",
                            "Listening... speak now! Press ENTER or SPACE to stop.", cyan};
    }
    case VoiceState::TRANSCRIBING:
        return {"  /^ ^\\  ", " ( -.- ) ", "  > ^ <  ", "   |_|   ", "Transcribing...", yellow};
    case VoiceState::THINKING:
        return (tick % 2 == 0)
            ? DogFrame{"  /^ ^\\  ", " ( -.- ) ", "  > ^ <  ", "   |_|   ", "Thinking...", yellow}
            : DogFrame{"  /^ ^\\  ", " ( -.o ) ", "  > ^ <  ", "   |_|   ", "Thinking...", yellow};
    case VoiceState::SPEAKING:
        return (tick % 2 == 0)
            ? DogFrame{"  /^ ^\\  ", " ( >o< ) ", "  > ^ <  ", "   |_|   ", "Speaking...", green}
            : DogFrame{"  /^ ^\\  ", " ( >O< ) ", "  > ^ <  ", "  _|_|_  ", "Speaking...", green};
    default:
        return {"  /^ ^\\  ", " ( o.o )  < Press SPACE to talk", "  > ^ <  ", "   |_|   ",
                "--- RCLI ---", dim_c};
    }
}

class TuiApp {
public:
    TuiApp(RCLIHandle engine, const Args& args)
        : engine_(engine), args_(args),
          theme_(make_theme(detect_light_terminal())) {
        chip_ = detect_chip();
        monitor_.start(800);
    }

    ~TuiApp() {
        monitor_.stop();
    }

    void set_rag_loaded(bool loaded) { rag_loaded_ = loaded; }

    void run() {
        auto screen_obj = ftxui::ScreenInteractive::Fullscreen();
        screen_ = &screen_obj;

        std::string input_text;
        auto input_box = Input(&input_text, "Type a command or question...");

        auto component = Container::Vertical({input_box});

        std::atomic<bool> should_quit{false};

        auto renderer = Renderer(component, [&]() {
            auto stats = monitor_.snapshot();
            return build_ui(stats, input_text);
        });

        auto handler = CatchEvent(renderer, [&](Event event) -> bool {
            // --- Universal stop: P always interrupts processing regardless of context ---
            if ((event == Event::Character('p') || event == Event::Character('P')) &&
                (voice_state_ != VoiceState::IDLE || rcli_is_speaking(engine_))) {
                rcli_stop_processing(engine_);
                voice_state_ = VoiceState::IDLE;
                add_system_message("Stopped.");
                screen_->Post(Event::Custom);
                return true;
            }

            // --- Panel mode guards: each panel intercepts all input ---
            if (models_mode_) {
                if (event == Event::Escape ||
                    event == Event::Character('m') || event == Event::Character('M')) {
                    models_mode_ = false; return true;
                }
                if (event == Event::ArrowUp) { models_cursor_up(); return true; }
                if (event == Event::ArrowDown) { models_cursor_down(); return true; }
                if (event == Event::Return) { models_select_or_download(); return true; }
                return true;
            }
            if (actions_mode_) {
                if (event == Event::Escape ||
                    event == Event::Character('a') || event == Event::Character('A')) {
                    actions_mode_ = false; return true;
                }
                if (event == Event::ArrowUp) { actions_cursor_up(); return true; }
                if (event == Event::ArrowDown) { actions_cursor_down(); return true; }
                if (event == Event::Return) { actions_execute_selected(); return true; }
                return true;
            }
            if (bench_mode_) {
                if (event == Event::Escape ||
                    event == Event::Character('b') || event == Event::Character('B')) {
                    bench_mode_ = false; return true;
                }
                if (event == Event::ArrowUp) {
                    if (bench_cursor_ > 0) bench_cursor_--;
                    return true;
                }
                if (event == Event::ArrowDown) {
                    if (bench_cursor_ < (int)bench_entries_.size() - 1) bench_cursor_++;
                    return true;
                }
                if (event == Event::Return) { bench_run_selected(); return true; }
                return true;
            }
            if (rag_mode_) {
                if (rag_input_active_) {
                    if (event == Event::Escape) {
                        rag_input_active_ = false;
                        rag_input_path_.clear();
                        rag_panel_message_.clear();
                        return true;
                    }
                    if (event == Event::Return) {
                        if (!rag_input_path_.empty()) {
                            rag_start_ingest(rag_input_path_);
                            rag_input_active_ = false;
                        }
                        return true;
                    }
                    if (event == Event::Backspace) {
                        if (!rag_input_path_.empty()) rag_input_path_.pop_back();
                        return true;
                    }
                    if (event.is_character()) {
                        rag_input_path_ += event.character();
                        return true;
                    }
                    return true;
                }
                if (event == Event::Escape ||
                    event == Event::Character('r') || event == Event::Character('R')) {
                    rag_mode_ = false; return true;
                }
                if (event == Event::ArrowUp) {
                    if (rag_panel_cursor_ > 0) rag_panel_cursor_--;
                    return true;
                }
                if (event == Event::ArrowDown) {
                    if (rag_panel_cursor_ < (int)rag_panel_options_.size() - 1) rag_panel_cursor_++;
                    return true;
                }
                if (event == Event::Return) { rag_execute_selected(); return true; }
                return true;
            }
            if (cleanup_mode_) {
                if (event == Event::Escape ||
                    event == Event::Character('d') || event == Event::Character('D')) {
                    cleanup_mode_ = false; return true;
                }
                if (event == Event::ArrowUp) {
                    if (cleanup_cursor_ > 0) cleanup_cursor_--;
                    cleanup_message_.clear();
                    return true;
                }
                if (event == Event::ArrowDown) {
                    if (cleanup_cursor_ < (int)cleanup_entries_.size() - 1) cleanup_cursor_++;
                    cleanup_message_.clear();
                    return true;
                }
                if (event == Event::Return || event == Event::Delete ||
                    event == Event::Backspace) {
                    cleanup_delete_selected();
                    if (cleanup_entries_.empty()) {
                        cleanup_message_ = "All done! No models remaining.";
                        cleanup_msg_color_ = theme_.success;
                    }
                    return true;
                }
                return true;
            }

            // --- Shortcut keys (no panel open) ---

            // ESC while idle + empty input → quit
            if (event == Event::Escape && input_text.empty() &&
                voice_state_ == VoiceState::IDLE) {
                rcli_stop_processing(engine_);
                should_quit = true;
                screen_->Exit();
                return true;
            }

            // Q = quit
            if (event == Event::Character('q')) {
                if (input_text.empty() && voice_state_ == VoiceState::IDLE) {
                    rcli_stop_processing(engine_);
                    should_quit = true;
                    screen_->Exit();
                    return true;
                }
            }

            // M = models panel
            if (event == Event::Character('m') || event == Event::Character('M')) {
                if (input_text.empty() && voice_state_ == VoiceState::IDLE) {
                    enter_models_mode();
                    return true;
                }
            }

            // A = actions panel
            if (event == Event::Character('a') || event == Event::Character('A')) {
                if (input_text.empty() && voice_state_ == VoiceState::IDLE) {
                    enter_actions_mode();
                    return true;
                }
            }

            // B = benchmark panel
            if (event == Event::Character('b') || event == Event::Character('B')) {
                if (input_text.empty() && voice_state_ == VoiceState::IDLE) {
                    enter_bench_mode();
                    return true;
                }
            }

            // R = RAG panel
            if (event == Event::Character('r') || event == Event::Character('R')) {
                if (input_text.empty() && voice_state_ == VoiceState::IDLE) {
                    enter_rag_mode();
                    return true;
                }
            }

            // D = cleanup panel
            if (event == Event::Character('d') || event == Event::Character('D')) {
                if (input_text.empty() && voice_state_ == VoiceState::IDLE) {
                    close_all_panels();
                    enter_cleanup_mode();
                    return true;
                }
            }

            // SPACE with empty input → toggle push-to-talk
            if (event == Event::Character(' ')) {
                if (input_text.empty()) {
                    if (voice_state_ == VoiceState::IDLE) {
                        start_recording();
                    } else if (voice_state_ == VoiceState::RECORDING) {
                        stop_recording();
                    }
                    return true;
                }
            }

            // ENTER
            if (event == Event::Return) {
                if (voice_state_ == VoiceState::RECORDING) {
                    stop_recording();
                    return true;
                }
                if (!input_text.empty() && voice_state_ == VoiceState::IDLE) {
                    process_input(input_text);
                    input_text.clear();
                    return true;
                }
            }

            return false;
        });

        // Refresh loop — fast during recording (10Hz for waveform), slow otherwise (2Hz)
        std::thread refresh([&]() {
            while (!should_quit) {
                int interval = (voice_state_ != VoiceState::IDLE) ? 100 : 500;
                std::this_thread::sleep_for(std::chrono::milliseconds(interval));
                if (voice_state_ == VoiceState::RECORDING && engine_) {
                    float level = rcli_get_audio_level(engine_);
                    audio_level_.store(level, std::memory_order_relaxed);
                    float prev = peak_rms_.load(std::memory_order_relaxed);
                    if (level > prev) peak_rms_.store(level, std::memory_order_relaxed);
                }
                anim_tick_++;
                screen_->Post(Event::Custom);
            }
        });

        screen_obj.Loop(handler);

        should_quit = true;
        if (refresh.joinable()) refresh.join();
    }

private:
    void start_recording() {
        if (!engine_) return;
        rcli_stop_speaking(engine_);
        voice_state_ = VoiceState::RECORDING;
        audio_level_ = 0.0f;
        peak_rms_ = 0.0f;
        anim_tick_ = 0;
        rcli_start_capture(engine_);
        add_system_message("Recording... speak now! Press SPACE or ENTER to stop.");
    }

    void stop_recording() {
        if (!engine_) return;
        voice_state_ = VoiceState::TRANSCRIBING;

        float peak = peak_rms_.load(std::memory_order_relaxed);
        constexpr float NOISE_GATE = 0.003f;

        if (peak < NOISE_GATE) {
            rcli_stop_capture_and_transcribe(engine_);
            voice_state_ = VoiceState::IDLE;
            add_system_message("(no speech detected - speak louder or closer to mic)");
            return;
        }

        // Run transcription + LLM + TTS in background thread
        std::thread([this]() {
            auto t_start = std::chrono::steady_clock::now();
            const char* transcript = rcli_stop_capture_and_transcribe(engine_);

            if (!transcript || !transcript[0]) {
                voice_state_ = VoiceState::IDLE;
                add_system_message("(no speech detected - try speaking louder)");
                screen_->Post(Event::Custom);
                return;
            }

            std::string user_text = transcript;
            add_user_message(user_text);

            // STT perf
            std::string stt_info;
            double audio_ms = 0, trans_ms = 0;
            rcli_get_last_stt_perf(engine_, &audio_ms, &trans_ms);
            if (trans_ms > 0) {
                std::ostringstream os;
                os << std::fixed;
                os.precision(0);
                os << "STT: " << trans_ms << "ms";
                stt_info = os.str();
            }
            if (!stt_info.empty()) {
                add_system_message(stt_info);
            }

            voice_state_ = VoiceState::THINKING;
            screen_->Post(Event::Custom);

            const char* response = rag_loaded_
                ? rcli_rag_query(engine_, user_text.c_str())
                : rcli_process_command(engine_, user_text.c_str());

            if (response && response[0]) {
                std::string perf;
                int tok = 0; double tps = 0, ttft = 0, total = 0;
                rcli_get_last_llm_perf(engine_, &tok, &tps, &ttft, &total);
                if (tok > 0) {
                    std::ostringstream os;
                    os << std::fixed;
                    os.precision(0);
                    os << (rag_loaded_ ? "RAG+LLM: " : "LLM: ")
                       << tok << " tok " << tps << " tok/s TTFT " << ttft << "ms";
                    perf = os.str();
                }

                add_response(response, perf);
                screen_->Post(Event::Custom);

                if (!args_.no_speak) {
                    auto t_audio = std::chrono::steady_clock::now();
                    double ttfa = std::chrono::duration<double, std::milli>(t_audio - t_start).count();
                    last_ttfa_ms_.store(ttfa, std::memory_order_relaxed);

                    voice_state_ = VoiceState::SPEAKING;
                    screen_->Post(Event::Custom);
                    rcli_speak(engine_, response);

                    int samples = 0; double synth_ms = 0, rtf = 0;
                    rcli_get_last_tts_perf(engine_, &samples, &synth_ms, &rtf);
                    if (samples > 0) {
                        std::ostringstream ts;
                        ts << std::fixed;
                        ts.precision(0);
                        ts << "TTFA: " << ttfa << "ms  TTS: ";
                        ts.precision(1);
                        ts << synth_ms << "ms " << rtf << "x RT";
                        add_system_message(ts.str());
                    }
                    wait_for_speech();
                }
            } else {
                add_response("(no response)", "");
            }

            voice_state_ = VoiceState::IDLE;
            screen_->Post(Event::Custom);
        }).detach();
    }

    Element build_ui(const SystemStats& stats, const std::string& input_text) {
        auto chip_bar = build_chip_bar();
        auto gauges = build_gauges(stats);
        auto models = build_models_panel();
        auto chat = build_chat_panel();
        auto voice_panel = build_voice_panel();
        auto input_bar = build_input_bar(input_text);
        auto status = build_status_bar();

        Elements layout;
        layout.push_back(chip_bar);
        layout.push_back(separator());
        layout.push_back(gauges);
        layout.push_back(separator());
        layout.push_back(models);
        layout.push_back(separator());

        if (cleanup_mode_)
            layout.push_back(build_cleanup_panel() | flex);
        else if (models_mode_)
            layout.push_back(build_models_panel_interactive() | flex);
        else if (actions_mode_)
            layout.push_back(build_actions_panel_interactive() | flex);
        else if (bench_mode_)
            layout.push_back(build_bench_panel() | flex);
        else if (rag_mode_)
            layout.push_back(build_rag_panel() | flex);
        else
            layout.push_back(chat | flex);

        if (voice_state_ != VoiceState::IDLE) {
            layout.push_back(separator());
            layout.push_back(voice_panel);
        }

        layout.push_back(separator());
        layout.push_back(input_bar);
        layout.push_back(status);

        return vbox(std::move(layout)) | border;
    }

    Element build_voice_panel() {
        int tick = anim_tick_.load();
        auto frame = get_dog_frame(voice_state_, tick);
        float level = audio_level_.load(std::memory_order_relaxed);
        float norm = std::min(1.0f, level * 5.0f);
        norm = std::sqrtf(norm);

        Elements rows;
        rows.push_back(text(frame.l1) | ftxui::bold | ftxui::color(frame.clr));
        rows.push_back(text(frame.l2) | ftxui::bold | ftxui::color(frame.clr));
        rows.push_back(text(frame.l3) | ftxui::bold | ftxui::color(frame.clr));
        rows.push_back(text(frame.l4) | ftxui::bold | ftxui::color(frame.clr));

        if (voice_state_ == VoiceState::RECORDING) {
            rows.push_back(hbox({
                text(" Audio: ") | dim,
                gauge(norm) | flex | ftxui::color(theme_.info),
            }));
        }

        rows.push_back(text(" " + frame.label) | dim | ftxui::color(frame.clr));

        return vbox(std::move(rows)) | center;
    }

    Element build_chip_bar() {
        std::string cores_str = std::to_string(chip_.total_cores) + " Cores";
        if (chip_.p_cores > 0 && chip_.e_cores > 0)
            cores_str += " (" + std::to_string(chip_.p_cores) + "P+" +
                        std::to_string(chip_.e_cores) + "E)";

        return hbox({
            text(" RCLI v0.1.0 ") | ftxui::bold | ftxui::color(theme_.accent),
            text(" | "),
            text(chip_.chip_name) | ftxui::bold,
            text(" | "),
            text(cores_str),
            text(" | "),
            text(std::to_string(chip_.gpu_cores) + " GPU") | ftxui::color(theme_.success),
            text(" | "),
            text(std::to_string(chip_.ane_cores) + "-core ANE") | ftxui::color(theme_.info),
            text(" | "),
            text(std::to_string(chip_.ram_gb) + "GB RAM") | ftxui::color(theme_.warning),
            text(" "),
        }) | center;
    }

    Element build_gauges(const SystemStats& stats) {
        // RCLI process-specific values
        float rcli_cpu_frac = std::min(stats.proc_cpu_percent / 100.0f, 1.0f);
        float rcli_mem_gb = stats.proc_mem_mb / 1024.0f;
        float rcli_mem_frac = stats.mem_total_gb > 0 ? rcli_mem_gb / stats.mem_total_gb : 0.0f;

        std::ostringstream rcli_mem_label;
        rcli_mem_label << std::fixed;
        rcli_mem_label.precision(1);
        if (stats.proc_mem_mb >= 1024.0f)
            rcli_mem_label << rcli_mem_gb << "GB";
        else
            rcli_mem_label << (int)stats.proc_mem_mb << "MB";

        std::string bat_str = stats.battery_percent >= 0
            ? "Bat: " + std::to_string(stats.battery_percent) + "%" + (stats.battery_charging ? "+" : "")
            : "";

        std::string net_str = "Net: rx " + format_bytes_rate(stats.net_rx_kbs) +
                              " tx " + format_bytes_rate(stats.net_tx_kbs);

        return vbox({
            hbox({
                text(" System ") | ftxui::bold | dim | size(WIDTH, EQUAL, 9),
                gauge_with_label("CPU ", stats.cpu_percent / 100.0f, theme_.gauge_cpu) | flex,
                text(" "),
                gauge_with_label("Mem ", stats.mem_percent / 100.0f, theme_.gauge_mem) | flex,
                text(" "),
                gauge_with_label("GPU ", stats.gpu_percent / 100.0f, theme_.gauge_gpu) | flex,
                text(" "),
                text(bat_str) | dim,
            }),
            hbox({
                text(" RCLI   ") | ftxui::bold | ftxui::color(theme_.accent) | size(WIDTH, EQUAL, 9),
                gauge_with_label("CPU ", rcli_cpu_frac, theme_.accent) | flex,
                text(" "),
                gauge_with_label("Mem ", rcli_mem_frac, theme_.accent) | flex,
                text(" " + rcli_mem_label.str()) | ftxui::color(theme_.accent),
                text("  "),
                text(net_str) | dim,
            }),
        });
    }

    Element build_models_panel() {
        std::string llm_name = "N/A", stt_name = "N/A", tts_name = "N/A";
        std::string llm_perf, tts_perf, stt_perf;

        if (engine_) {
            const char* n = rcli_get_llm_model(engine_);
            if (n) llm_name = n;
            n = rcli_get_stt_model(engine_);
            if (n) stt_name = n;
            n = rcli_get_tts_model(engine_);
            if (n) tts_name = n;

            int tok = 0; double tps = 0, ttft = 0, total = 0;
            rcli_get_last_llm_perf(engine_, &tok, &tps, &ttft, &total);
            if (tok > 0) {
                std::ostringstream os;
                os << tok << " tok " << std::fixed;
                os.precision(0);
                os << tps << " tok/s TTFT " << ttft << "ms";
                llm_perf = os.str();
            }

            int samples = 0; double synth = 0, rtf = 0;
            rcli_get_last_tts_perf(engine_, &samples, &synth, &rtf);
            if (samples > 0) {
                std::ostringstream os;
                os.precision(1);
                os << std::fixed << synth << "ms " << rtf << "x RT";
                tts_perf = os.str();
            }

            double audio_ms = 0, trans_ms = 0;
            rcli_get_last_stt_perf(engine_, &audio_ms, &trans_ms);
            if (trans_ms > 0) {
                std::ostringstream os;
                os.precision(0);
                os << std::fixed << trans_ms << "ms";
                stt_perf = os.str();
            }
        }

        auto rag_indicator = rag_loaded_.load()
            ? hbox({text("RAG: ") | ftxui::bold, text("active") | ftxui::bold | ftxui::color(theme_.success)})
            : hbox({text("RAG: ") | ftxui::bold, text("off") | dim});

        double ttfa = last_ttfa_ms_.load(std::memory_order_relaxed);
        auto ttfa_color = (ttfa > 0 && ttfa < 500)
            ? theme_.success
            : (ttfa >= 500 ? theme_.warning : theme_.text_muted);

        // Row 1: Model names + RAG
        auto row1 = hbox({
            hbox({text("LLM: ") | ftxui::bold, text(llm_name)}) | flex,
            text(" │ "),
            hbox({text("STT: ") | ftxui::bold, text(stt_name)}) | flex,
            text(" │ "),
            hbox({text("TTS: ") | ftxui::bold, text(tts_name)}) | flex,
            text(" │ "),
            rag_indicator,
        });

        // Row 2: Live metrics — always visible, updates after each interaction
        Elements metrics;
        if (ttfa > 0) {
            std::ostringstream os;
            os << std::fixed;
            os.precision(0);
            os << "TTFA " << ttfa << "ms";
            metrics.push_back(text(os.str()) | ftxui::bold | ftxui::color(ttfa_color));
            metrics.push_back(text("  "));
        }
        if (!llm_perf.empty()) {
            metrics.push_back(text(llm_perf) | dim);
            metrics.push_back(text("  "));
        }
        if (!stt_perf.empty()) {
            metrics.push_back(text("STT " + stt_perf) | dim);
            metrics.push_back(text("  "));
        }
        if (!tts_perf.empty()) {
            metrics.push_back(text("TTS " + tts_perf) | dim);
        }
        if (metrics.empty()) {
            metrics.push_back(text("awaiting first interaction...") | dim);
        }
        metrics.push_back(filler());
        metrics.push_back(text("[M] models [A] actions [B] bench") | dim);

        return vbox({row1, hbox(std::move(metrics))});
    }

    Element build_chat_panel() {
        std::lock_guard<std::mutex> lock(chat_mu_);
        Elements lines;

        if (chat_history_.empty()) {
            lines.push_back(text("") | dim);
            lines.push_back(
                text("  Welcome to RCLI") | ftxui::bold | ftxui::color(theme_.accent));
            lines.push_back(text("") | dim);
            lines.push_back(text("  SPACE       Start/stop voice recording") | dim);
            lines.push_back(text("  Type + ENTER   Send a text command or question") | dim);
            lines.push_back(text("  help        Show all commands") | dim);
            lines.push_back(text("") | dim);
            lines.push_back(
                text("  RAG:  Drag a file or folder here to index it, then ask questions about it.") | ftxui::bold);
            lines.push_back(text("") | dim);
            lines.push_back(text("  Try: \"open Safari\"   \"what's the weather?\"   \"set volume to 50\"") | dim);
        } else {
            for (auto& msg : chat_history_) {
                auto prefix_color = msg.is_user ? theme_.user_msg : theme_.accent;
                Elements row;
                row.push_back(text("  " + msg.prefix + " ") | ftxui::bold | ftxui::color(prefix_color));
                row.push_back(text(msg.text));
                lines.push_back(hbox(std::move(row)));
                if (!msg.perf.empty()) {
                    lines.push_back(text("    " + msg.perf) | dim);
                }
            }
        }

        return vbox(std::move(lines));
    }

    Element build_input_bar(const std::string& input_text) {
        std::string hint;
        if (voice_state_ == VoiceState::RECORDING) {
            hint = "[SPACE/ENTER] stop recording ";
        } else if (voice_state_ != VoiceState::IDLE) {
            hint = "Processing... ";
        } else {
            hint = "[SPACE] talk  [Q] quit ";
        }

        auto prompt_color = (voice_state_ == VoiceState::RECORDING)
            ? theme_.info : theme_.success;

        return hbox({
            text(" > ") | ftxui::bold | ftxui::color(prompt_color),
            text(input_text) | flex,
            text(hint) | dim | align_right,
        });
    }

    Element build_status_bar() {
        if (cleanup_mode_) {
            return hbox({
                text(" Model Cleanup ") | ftxui::bold | ftxui::color(theme_.accent),
                filler(),
                text("[Up/Down] navigate  [Enter] delete  [ESC/D] close ") | dim,
            });
        }
        if (models_mode_) {
            return hbox({
                text(" Models ") | ftxui::bold | ftxui::color(theme_.accent),
                filler(),
                text("[Up/Down] navigate  [Enter] select/download  [ESC/M] close ") | dim,
            });
        }
        if (actions_mode_) {
            return hbox({
                text(" Actions ") | ftxui::bold | ftxui::color(theme_.accent),
                filler(),
                text("[Up/Down] navigate  [Enter] run  [ESC/A] close ") | dim,
            });
        }
        if (bench_mode_) {
            return hbox({
                text(" Benchmarks ") | ftxui::bold | ftxui::color(theme_.accent),
                filler(),
                text("[Up/Down] navigate  [Enter] run  [ESC/B] close ") | dim,
            });
        }
        if (rag_mode_) {
            return hbox({
                text(" RAG ") | ftxui::bold | ftxui::color(theme_.accent),
                filler(),
                text("[Up/Down] navigate  [Enter] select  [ESC/R] close ") | dim,
            });
        }

        bool processing = voice_state_ != VoiceState::IDLE;

        Elements right_items;
        if (processing)
            right_items.push_back(text("[P] stop ") | ftxui::bold | ftxui::color(theme_.error));
        right_items.push_back(text("[SPACE] talk ") | dim);
        right_items.push_back(text("[M] models ") | dim);
        right_items.push_back(text("[A] actions ") | dim);
        right_items.push_back(text("[B] bench ") | dim);
        right_items.push_back(text("[R] RAG ") | dim);
        right_items.push_back(text("[D] cleanup ") | dim);
        right_items.push_back(text("[Q] quit ") | dim);

        return hbox({
            text(" Voice AI + RAG  •  Powered by RunAnywhere ") | dim,
            filler(),
            hbox(right_items),
        });
    }

    void enter_cleanup_mode() {
        cleanup_entries_.clear();
        cleanup_cursor_ = 0;
        cleanup_message_.clear();
        std::string dir = args_.models_dir;

        auto llm_all = rcli::all_models();
        auto stt_all = rcli::all_stt_models();
        auto tts_all = rcli::all_tts_models();

        const auto* llm_active = rcli::resolve_active_model(dir, llm_all);
        const auto* stt_active = rcli::resolve_active_stt(dir, stt_all);
        const auto* tts_active = rcli::resolve_active_tts(dir, tts_all);

        for (auto& m : llm_all) {
            std::string p = dir + "/" + m.filename;
            if (access(p.c_str(), R_OK) == 0) {
                bool active = llm_active && llm_active->id == m.id;
                cleanup_entries_.push_back({m.name, p, m.id, "LLM", m.size_mb, active, false});
            }
        }
        for (auto& m : stt_all) {
            if (m.category == "streaming") continue;
            if (!rcli::is_stt_installed(dir, m)) continue;
            std::string p = dir + "/" + m.dir_name;
            bool active = stt_active && stt_active->id == m.id;
            cleanup_entries_.push_back({m.name, p, m.id, "STT", m.size_mb, active, true});
        }
        for (auto& v : tts_all) {
            if (!rcli::is_tts_installed(dir, v)) continue;
            std::string p = dir + "/" + v.dir_name;
            bool active = tts_active && tts_active->id == v.id;
            cleanup_entries_.push_back({v.name, p, v.id, "TTS", v.size_mb, active, true});
        }

        cleanup_mode_ = true;
    }

    void cleanup_delete_selected() {
        if (cleanup_cursor_ < 0 || cleanup_cursor_ >= (int)cleanup_entries_.size()) return;
        auto& e = cleanup_entries_[cleanup_cursor_];
        if (e.is_active) {
            cleanup_message_ = "Cannot delete active model. Switch first.";
            cleanup_msg_color_ = theme_.error;
            return;
        }
        std::string rm_cmd = e.is_dir
            ? "rm -rf '" + e.path + "'"
            : "rm -f '" + e.path + "'";
        system(rm_cmd.c_str());

        if (e.modality == "LLM") {
            std::string sel = rcli::read_selected_model_id();
            if (sel == e.id) rcli::clear_selected_model();
        } else if (e.modality == "STT") {
            std::string sel = rcli::read_selected_stt_id();
            if (sel == e.id) rcli::clear_selected_stt();
        } else if (e.modality == "TTS") {
            std::string sel = rcli::read_selected_tts_id();
            if (sel == e.id) rcli::clear_selected_tts();
        }

        cleanup_message_ = "Deleted: " + e.name;
        cleanup_msg_color_ = theme_.success;
        cleanup_entries_.erase(cleanup_entries_.begin() + cleanup_cursor_);
        if (cleanup_cursor_ >= (int)cleanup_entries_.size())
            cleanup_cursor_ = std::max(0, (int)cleanup_entries_.size() - 1);
    }

    Element build_cleanup_panel() {
        Elements lines;
        lines.push_back(text("  Model Cleanup") | ftxui::bold | ftxui::color(theme_.accent));
        lines.push_back(text("  Arrow keys to navigate, ENTER to delete, ESC/D to close") | dim);
        lines.push_back(text(""));

        if (cleanup_entries_.empty()) {
            lines.push_back(text("  No removable models found.") | dim);
        } else {
            for (int i = 0; i < (int)cleanup_entries_.size(); i++) {
                auto& e = cleanup_entries_[i];
                bool selected = (i == cleanup_cursor_);
                std::string prefix = selected ? " > " : "   ";
                std::string size_str = e.size_mb >= 1024
                    ? std::to_string(e.size_mb / 1024) + "." + std::to_string((e.size_mb % 1024) / 100) + " GB"
                    : std::to_string(e.size_mb) + " MB";
                std::string status = e.is_active ? " (active)" : "";
                std::string line = prefix + e.name + "  [" + e.modality + "]  " + size_str + status;

                auto elem = text(line);
                if (selected)
                    elem = elem | ftxui::bold | ftxui::color(theme_.text_selected);
                else if (e.is_active)
                    elem = elem | ftxui::color(theme_.success);
                else
                    elem = elem | dim;
                lines.push_back(elem);
            }
        }

        if (!cleanup_message_.empty()) {
            lines.push_back(text(""));
            lines.push_back(text("  " + cleanup_message_) | ftxui::bold | ftxui::color(cleanup_msg_color_));
        }

        return vbox(std::move(lines));
    }

    // ====================================================================
    // Panel helpers
    // ====================================================================

    void close_all_panels() {
        cleanup_mode_ = false;
        models_mode_ = false;
        actions_mode_ = false;
        bench_mode_ = false;
        rag_mode_ = false;
    }

    // ====================================================================
    // [M] Models panel — browse / switch / download
    // ====================================================================

    void enter_models_mode() {
        close_all_panels();
        models_entries_.clear();
        models_cursor_ = 0;
        models_message_.clear();
        std::string dir = args_.models_dir;

        auto llm_all = rcli::all_models();
        auto stt_all = rcli::all_stt_models();
        auto tts_all = rcli::all_tts_models();

        const auto* llm_active = rcli::resolve_active_model(dir, llm_all);
        const auto* stt_active = rcli::resolve_active_stt(dir, stt_all);
        const auto* tts_active = rcli::resolve_active_tts(dir, tts_all);

        { ModelEntry h; h.name = "LLM Models"; h.is_header = true; models_entries_.push_back(h); }
        for (auto& m : llm_all) {
            ModelEntry e;
            e.name = m.name; e.id = m.id; e.modality = "LLM"; e.size_mb = m.size_mb;
            e.installed = (access((dir + "/" + m.filename).c_str(), R_OK) == 0);
            e.is_active = (llm_active && llm_active->id == m.id);
            e.is_default = m.is_default; e.is_recommended = m.is_recommended;
            e.url = m.url; e.filename = m.filename; e.is_archive = false;
            models_entries_.push_back(e);
        }

        { ModelEntry h; h.name = "STT Models (Offline)"; h.is_header = true; models_entries_.push_back(h); }
        auto offline_stt = rcli::get_offline_stt_models(stt_all);
        for (auto* m : offline_stt) {
            ModelEntry e;
            e.name = m->name; e.id = m->id; e.modality = "STT"; e.size_mb = m->size_mb;
            e.installed = rcli::is_stt_installed(dir, *m);
            e.is_active = (stt_active && stt_active->id == m->id);
            e.is_default = m->is_default;
            e.url = m->download_url; e.filename = m->dir_name; e.is_archive = true;
            models_entries_.push_back(e);
        }

        { ModelEntry h; h.name = "TTS Voices"; h.is_header = true; models_entries_.push_back(h); }
        for (auto& v : tts_all) {
            ModelEntry e;
            e.name = v.name; e.id = v.id; e.modality = "TTS"; e.size_mb = v.size_mb;
            e.installed = rcli::is_tts_installed(dir, v);
            e.is_active = (tts_active && tts_active->id == v.id);
            e.is_default = v.is_default; e.is_recommended = v.is_recommended;
            e.url = v.download_url; e.filename = v.dir_name; e.is_archive = true;
            models_entries_.push_back(e);
        }

        for (int i = 0; i < (int)models_entries_.size(); i++) {
            if (!models_entries_[i].is_header) { models_cursor_ = i; break; }
        }
        models_mode_ = true;
    }

    void models_cursor_up() {
        int pos = models_cursor_ - 1;
        while (pos >= 0 && models_entries_[pos].is_header) pos--;
        if (pos >= 0) models_cursor_ = pos;
        models_message_.clear();
    }

    void models_cursor_down() {
        int n = (int)models_entries_.size();
        int pos = models_cursor_ + 1;
        while (pos < n && models_entries_[pos].is_header) pos++;
        if (pos < n) models_cursor_ = pos;
        models_message_.clear();
    }

    void models_select_or_download() {
        if (models_cursor_ < 0 || models_cursor_ >= (int)models_entries_.size()) return;
        auto& e = models_entries_[models_cursor_];
        if (e.is_header) return;

        if (e.installed) {
            if (e.modality == "LLM") rcli::write_selected_model_id(e.id);
            else if (e.modality == "STT") rcli::write_selected_stt_id(e.id);
            else if (e.modality == "TTS") rcli::write_selected_tts_id(e.id);
            for (auto& m : models_entries_) {
                if (m.modality == e.modality && !m.is_header) m.is_active = (m.id == e.id);
            }
            models_message_ = "Selected: " + e.name + ". Restart RCLI to apply.";
            models_msg_color_ = theme_.success;
        } else {
            if (e.url.empty()) {
                models_message_ = "No download URL for " + e.name + ". Use 'rcli setup' first.";
                models_msg_color_ = theme_.error;
                return;
            }
            models_message_ = "Downloading " + e.name + " (" + std::to_string(e.size_mb) + " MB)...";
            models_msg_color_ = theme_.warning;
            int idx = models_cursor_;
            std::string dir = args_.models_dir;
            std::string url = e.url, fname = e.filename, mod = e.modality;
            std::string id = e.id, nm = e.name;
            bool archive = e.is_archive;

            std::thread([this, idx, dir, url, fname, mod, id, nm, archive]() {
                int rc;
                if (archive) {
                    rc = system(("curl -sL '" + url + "' | tar xj -C '" + dir + "' 2>/dev/null").c_str());
                } else {
                    rc = system(("curl -sL -o '" + dir + "/" + fname + "' '" + url + "' 2>/dev/null").c_str());
                }
                if (rc == 0 && idx < (int)models_entries_.size()) {
                    models_entries_[idx].installed = true;
                    models_entries_[idx].is_active = true;
                    for (auto& m : models_entries_) {
                        if (m.modality == mod && !m.is_header && m.id != id) m.is_active = false;
                    }
                    if (mod == "LLM") rcli::write_selected_model_id(id);
                    else if (mod == "STT") rcli::write_selected_stt_id(id);
                    else if (mod == "TTS") rcli::write_selected_tts_id(id);
                    models_message_ = "Downloaded & selected: " + nm + ". Restart RCLI to apply.";
                    models_msg_color_ = theme_.success;
                } else {
                    models_message_ = "Download failed for " + nm + ". Check connection.";
                    models_msg_color_ = theme_.error;
                }
                screen_->Post(Event::Custom);
            }).detach();
        }
    }

    Element build_models_panel_interactive() {
        Elements lines;
        lines.push_back(text("  Models") | ftxui::bold | ftxui::color(theme_.accent));
        lines.push_back(text("  Up/Down navigate, ENTER select/download, ESC/M close") | dim);
        lines.push_back(text(""));

        for (int i = 0; i < (int)models_entries_.size(); i++) {
            auto& e = models_entries_[i];
            if (e.is_header) {
                lines.push_back(text("  --- " + e.name + " ---") |
                    ftxui::bold | ftxui::color(theme_.accent));
                continue;
            }
            bool selected = (i == models_cursor_);
            std::string prefix = selected ? " > " : "   ";
            std::string size_str = e.size_mb >= 1024
                ? std::to_string(e.size_mb / 1024) + "." +
                  std::to_string((e.size_mb % 1024) / 100) + " GB"
                : std::to_string(e.size_mb) + " MB";
            std::string status;
            if (e.is_active) status = " (active)";
            else if (e.installed) status = " (installed)";
            else status = " (not installed)";
            std::string star = e.is_recommended ? " *" : "";
            std::string line = prefix + e.name + star + "  " + size_str + status;

            auto elem = text(line);
            if (selected) elem = elem | ftxui::bold | ftxui::color(theme_.text_selected);
            else if (e.is_active) elem = elem | ftxui::color(theme_.success);
            else if (e.installed) elem = elem | dim;
            else elem = elem | ftxui::color(theme_.text_muted);
            lines.push_back(elem);
        }

        if (!models_message_.empty()) {
            lines.push_back(text(""));
            lines.push_back(text("  " + models_message_) |
                ftxui::bold | ftxui::color(models_msg_color_));
        }
        return vbox(std::move(lines));
    }

    // ====================================================================
    // [A] Actions panel — browse and run macOS actions
    // ====================================================================

    void enter_actions_mode() {
        close_all_panels();
        actions_entries_.clear();
        actions_cursor_ = 0;
        actions_message_.clear();

        auto defs = ::rcli_get_all_action_defs(engine_);
        std::sort(defs.begin(), defs.end(),
            [](const rcli::ActionDef& a, const rcli::ActionDef& b) {
                if (a.category != b.category) return a.category < b.category;
                return a.name < b.name;
            });

        std::string last_cat;
        for (auto& d : defs) {
            if (d.category != last_cat) {
                ActionEntry h;
                h.name = d.category.empty() ? "Other" : d.category;
                h.is_header = true;
                actions_entries_.push_back(h);
                last_cat = d.category;
            }
            ActionEntry e;
            e.name = d.name; e.description = d.description;
            e.category = d.category; e.params_json = d.parameters_json;
            actions_entries_.push_back(e);
        }

        for (int i = 0; i < (int)actions_entries_.size(); i++) {
            if (!actions_entries_[i].is_header) { actions_cursor_ = i; break; }
        }
        actions_mode_ = true;
    }

    void actions_cursor_up() {
        int pos = actions_cursor_ - 1;
        while (pos >= 0 && actions_entries_[pos].is_header) pos--;
        if (pos >= 0) actions_cursor_ = pos;
        actions_message_.clear();
    }

    void actions_cursor_down() {
        int n = (int)actions_entries_.size();
        int pos = actions_cursor_ + 1;
        while (pos < n && actions_entries_[pos].is_header) pos++;
        if (pos < n) actions_cursor_ = pos;
        actions_message_.clear();
    }

    void actions_execute_selected() {
        if (actions_cursor_ < 0 || actions_cursor_ >= (int)actions_entries_.size()) return;
        auto& e = actions_entries_[actions_cursor_];
        if (e.is_header) return;

        actions_message_ = "Running " + e.name + "...";
        actions_msg_color_ = theme_.warning;
        std::string action_name = e.name;
        std::thread([this, action_name]() {
            const char* result = rcli_action_execute(engine_, action_name.c_str(), "{}");
            if (result && result[0]) {
                std::string r = result;
                if (r.size() > 200) r = r.substr(0, 197) + "...";
                actions_message_ = action_name + ": " + r;
                actions_msg_color_ = theme_.success;
            } else {
                actions_message_ = action_name + ": (no output)";
                actions_msg_color_ = theme_.text_muted;
            }
            screen_->Post(Event::Custom);
        }).detach();
    }

    Element build_actions_panel_interactive() {
        int action_count = (int)std::count_if(
            actions_entries_.begin(), actions_entries_.end(),
            [](const ActionEntry& e) { return !e.is_header; });

        Elements lines;
        lines.push_back(text("  Actions (" + std::to_string(action_count) + ")") |
            ftxui::bold | ftxui::color(theme_.accent));
        lines.push_back(text("  Up/Down navigate, ENTER run, ESC/A close") | dim);
        lines.push_back(text(""));

        int total = (int)actions_entries_.size();
        int visible = 20;
        int start = std::max(0, actions_cursor_ - visible / 2);
        int end = std::min(total, start + visible);
        if (end - start < visible) start = std::max(0, end - visible);

        for (int i = start; i < end; i++) {
            auto& e = actions_entries_[i];
            if (e.is_header) {
                lines.push_back(text("  --- " + e.name + " ---") |
                    ftxui::bold | ftxui::color(theme_.info));
                continue;
            }
            bool sel = (i == actions_cursor_);
            std::string prefix = sel ? " > " : "   ";
            std::string line = prefix + e.name;
            if (!e.description.empty()) {
                std::string desc = e.description;
                if (desc.size() > 60) desc = desc.substr(0, 57) + "...";
                line += "  " + desc;
            }
            auto elem = text(line);
            if (sel) elem = elem | ftxui::bold | ftxui::color(theme_.text_selected);
            else elem = elem | dim;
            lines.push_back(elem);
        }

        if (!actions_message_.empty()) {
            lines.push_back(text(""));
            lines.push_back(text("  " + actions_message_) |
                ftxui::bold | ftxui::color(actions_msg_color_));
        }
        return vbox(std::move(lines));
    }

    // ====================================================================
    // [B] Bench panel — pick and run benchmarks
    // ====================================================================

    void enter_bench_mode() {
        close_all_panels();
        bench_entries_.clear();
        bench_cursor_ = 0;
        bench_output_.clear();
        bench_running_ = false;

        bench_entries_.push_back({"STT Benchmark", "stt"});
        bench_entries_.push_back({"LLM Benchmark", "llm"});
        bench_entries_.push_back({"TTS Benchmark", "tts"});
        bench_entries_.push_back({"E2E Pipeline Benchmark", "e2e"});
        if (rag_loaded_) bench_entries_.push_back({"RAG Benchmark", "rag"});
        bench_entries_.push_back({"All Benchmarks", "all"});
        bench_mode_ = true;
    }

    void bench_run_selected() {
        if (bench_cursor_ < 0 || bench_cursor_ >= (int)bench_entries_.size()) return;
        if (bench_running_) return;

        bench_running_ = true;
        std::string suite = bench_entries_[bench_cursor_].suite_key;
        bench_output_ = "Running " + bench_entries_[bench_cursor_].name + "...";

        std::thread([this, suite]() {
            int rc = rcli_run_full_benchmark(engine_, suite.c_str(), 3, nullptr);
            std::ostringstream out;
            if (rc == 0) {
                out << "Benchmark complete!\n";
                int tok = 0; double tps = 0, ttft = 0, total = 0;
                rcli_get_last_llm_perf(engine_, &tok, &tps, &ttft, &total);
                if (tok > 0) {
                    out << std::fixed;
                    out.precision(1);
                    out << "  LLM: " << tok << " tokens, "
                        << tps << " tok/s, TTFT " << ttft << "ms\n";
                }
                int samples = 0; double synth_ms = 0, rtf = 0;
                rcli_get_last_tts_perf(engine_, &samples, &synth_ms, &rtf);
                if (samples > 0) {
                    out.precision(1);
                    out << "  TTS: " << synth_ms << "ms, " << rtf << "x real-time\n";
                }
                double audio_ms = 0, trans_ms = 0;
                rcli_get_last_stt_perf(engine_, &audio_ms, &trans_ms);
                if (trans_ms > 0) {
                    out.precision(0);
                    out << "  STT: " << trans_ms << "ms transcription\n";
                }
                double ttfa_est = trans_ms + ttft + synth_ms;
                if (ttfa_est > 0) {
                    out.precision(0);
                    out << "  TTFA (est): " << ttfa_est << "ms"
                        << (ttfa_est < 500 ? " ok" : "") << "\n";
                }
            } else {
                out << "Benchmark failed (code " << rc << ")";
            }
            bench_output_ = out.str();
            bench_running_ = false;
            screen_->Post(Event::Custom);
        }).detach();
    }

    Element build_bench_panel() {
        Elements lines;
        lines.push_back(text("  Benchmarks") |
            ftxui::bold | ftxui::color(theme_.accent));
        lines.push_back(text("  Up/Down navigate, ENTER run, ESC/B close") | dim);
        lines.push_back(text(""));

        if (engine_) {
            const char* llm = rcli_get_llm_model(engine_);
            const char* stt = rcli_get_stt_model(engine_);
            const char* tts = rcli_get_tts_model(engine_);
            lines.push_back(hbox({
                text("  Active: ") | ftxui::bold,
                text(std::string(llm ? llm : "?") + " | " +
                     (stt ? stt : "?") + " | " + (tts ? tts : "?")),
            }) | dim);
            lines.push_back(text(""));
        }

        for (int i = 0; i < (int)bench_entries_.size(); i++) {
            bool sel = (i == bench_cursor_);
            std::string prefix = sel ? " > " : "   ";
            auto elem = text(prefix + bench_entries_[i].name);
            if (sel) elem = elem | ftxui::bold | ftxui::color(theme_.text_selected);
            else elem = elem | dim;
            lines.push_back(elem);
        }

        if (!bench_output_.empty()) {
            lines.push_back(text(""));
            std::istringstream iss(bench_output_);
            std::string line;
            while (std::getline(iss, line)) {
                auto c = bench_running_ ? theme_.warning : theme_.success;
                lines.push_back(text("  " + line) | ftxui::color(c));
            }
        }
        return vbox(std::move(lines));
    }

    // ====================================================================
    // [R] RAG panel — status, clear, ingest
    // ====================================================================

    void enter_rag_mode() {
        close_all_panels();
        rag_panel_cursor_ = 0;
        rag_panel_options_.clear();
        rag_input_active_ = false;
        rag_input_path_.clear();

        rag_panel_options_.push_back({"Ingest Documents (drag file or type path)", "ingest"});
        rag_panel_options_.push_back({"Clear RAG (unload from memory)", "clear"});
        rag_panel_options_.push_back({"Delete RAG + On-Disk Index", "delete"});

        if (rag_loaded_.load()) {
            rag_panel_message_ = "RAG is active — ask questions or ingest more docs.";
            rag_panel_msg_color_ = theme_.success;
        } else {
            rag_panel_message_.clear();
        }
        rag_mode_ = true;
    }

    void rag_execute_selected() {
        if (rag_panel_cursor_ < 0 ||
            rag_panel_cursor_ >= (int)rag_panel_options_.size()) return;
        auto& opt = rag_panel_options_[rag_panel_cursor_];

        if (opt.action == "ingest") {
            rag_input_active_ = true;
            rag_input_path_.clear();
            rag_panel_message_ = "Type/paste a path or drag a file from Finder, then press ENTER.";
            rag_panel_msg_color_ = theme_.warning;
        } else if (opt.action == "clear") {
            if (!rag_loaded_) {
                rag_panel_message_ = "Nothing to clear — RAG is not active.";
                rag_panel_msg_color_ = theme_.error;
                return;
            }
            rcli_rag_clear(engine_);
            rag_loaded_ = false;
            rag_panel_message_ = "RAG cleared. Back to normal LLM mode.";
            rag_panel_msg_color_ = theme_.success;
            add_system_message("RAG cleared. Back to normal LLM mode.");
        } else if (opt.action == "delete") {
            if (!rag_loaded_) {
                rag_panel_message_ = "Nothing to delete — RAG is not active.";
                rag_panel_msg_color_ = theme_.error;
                return;
            }
            rcli_rag_clear(engine_);
            rag_loaded_ = false;
            std::string idx_dir;
            if (const char* home = getenv("HOME"))
                idx_dir = std::string(home) + "/Library/RCLI/index";
            if (!idx_dir.empty()) system(("rm -rf '" + idx_dir + "'").c_str());
            rag_panel_message_ = "RAG cleared and index deleted from disk.";
            rag_panel_msg_color_ = theme_.success;
            add_system_message("RAG cleared and index deleted from disk.");
        }
    }

    void rag_start_ingest(const std::string& raw_path) {
        std::string path = raw_path;
        while (!path.empty() && (path.back() == ' ' || path.back() == '\n'))
            path.pop_back();
        while (!path.empty() && path.front() == ' ')
            path.erase(path.begin());
        if (path.empty()) {
            rag_panel_message_ = "No path provided.";
            rag_panel_msg_color_ = theme_.error;
            return;
        }

        struct stat st;
        if (stat(path.c_str(), &st) != 0) {
            rag_panel_message_ = "Path not found: " + path;
            rag_panel_msg_color_ = theme_.error;
            return;
        }

        rag_panel_message_ = "Indexing: " + path + " ...";
        rag_panel_msg_color_ = theme_.warning;
        rag_input_path_.clear();

        std::string path_copy = path;
        add_system_message("Indexing: " + path + " ...");
        std::thread([this, path_copy]() {
            int rc = rcli_rag_ingest(engine_, path_copy.c_str());
            if (rc == 0) {
                rag_loaded_ = true;
                rag_panel_message_ = "RAG active! Documents indexed — ask me anything.";
                rag_panel_msg_color_ = theme_.success;
                add_system_message("RAG active! Documents indexed — ask me anything about them.");
            } else {
                rag_panel_message_ = "Indexing failed for: " + path_copy;
                rag_panel_msg_color_ = theme_.error;
                add_system_message("RAG indexing failed for: " + path_copy);
            }
            screen_->Post(Event::Custom);
        }).detach();
    }

    Element build_rag_panel() {
        Elements lines;
        lines.push_back(text("  RAG") |
            ftxui::bold | ftxui::color(theme_.accent));
        lines.push_back(text("  Up/Down navigate, ENTER select, ESC/R close") | dim);
        lines.push_back(text(""));

        std::string status_str = rag_loaded_.load()
            ? "Active (documents indexed)" : "Not active";
        auto status_color = rag_loaded_.load()
            ? theme_.success : theme_.text_muted;
        lines.push_back(hbox({
            text("  Status: ") | ftxui::bold, text(status_str) | ftxui::color(status_color)
        }));
        lines.push_back(text(""));

        for (int i = 0; i < (int)rag_panel_options_.size(); i++) {
            bool sel = (i == rag_panel_cursor_) && !rag_input_active_;
            std::string prefix = sel ? " > " : "   ";
            auto& opt = rag_panel_options_[i];
            bool disabled = !rag_loaded_.load() &&
                (opt.action == "clear" || opt.action == "delete");
            auto elem = text(prefix + opt.name);
            if (disabled)
                elem = elem | dim | ftxui::color(theme_.text_muted);
            else if (sel)
                elem = elem | ftxui::bold | ftxui::color(theme_.text_selected);
            else
                elem = elem | dim;
            lines.push_back(elem);
        }

        if (rag_input_active_) {
            lines.push_back(text(""));
            lines.push_back(hbox({
                text("  Path: ") | ftxui::bold | ftxui::color(theme_.warning),
                text(rag_input_path_ + "_") | ftxui::bold,
            }));
            lines.push_back(
                text("  Drag file from Finder or type path, then ENTER. ESC to cancel.") | dim);
        }

        if (!rag_panel_message_.empty()) {
            lines.push_back(text(""));
            lines.push_back(text("  " + rag_panel_message_) |
                ftxui::bold | ftxui::color(rag_panel_msg_color_));
        }

        lines.push_back(text(""));
        lines.push_back(
            text("  Tip: You can also drag a file into the main chat to auto-index.") | dim);
        return vbox(std::move(lines));
    }

    // ====================================================================
    // process_input
    // ====================================================================

    void process_input(const std::string& input) {
        if (input.empty()) return;

        add_user_message(input);

        std::string cmd = input;
        if (!cmd.empty() && cmd[0] == '/') cmd = cmd.substr(1);

        if (cmd == "quit" || cmd == "q" || cmd == "exit") {
            screen_->Exit();
            return;
        }

        if (cmd == "help" || cmd == "h" || cmd == "?") {
            add_system_message("--- Commands ---");
            add_system_message("  help             Show this help");
            add_system_message("  models           Show active LLM / STT / TTS models");
            add_system_message("  actions          List all available macOS actions");
            add_system_message("  do <action>      Run an action directly (e.g. do open_app Safari)");
            add_system_message("--- RAG ---");
            add_system_message("  Drag a file or folder here to auto-index for RAG");
            add_system_message("  rag ingest <dir> Index documents from a folder");
            add_system_message("  rag query <text> Ask about your indexed documents");
            add_system_message("  rag status       Show RAG status");
            add_system_message("  rag clear        Unload RAG, return to normal LLM");
            add_system_message("  rag delete       Clear RAG + delete on-disk index");
            add_system_message("--- Shortcuts ---");
            add_system_message("  SPACE            Push-to-talk recording");
            add_system_message("  P / ESC          Stop all processing (LLM/TTS/STT)");
            add_system_message("  M                Models panel (browse/switch/download)");
            add_system_message("  A                Actions panel (browse/run)");
            add_system_message("  B                Benchmarks panel (run benchmarks)");
            add_system_message("  R                RAG panel (status/clear/ingest)");
            add_system_message("  D                Delete models (interactive cleanup)");
            add_system_message("  Q                Quit");
            return;
        }

        if (cmd == "models") {
            enter_models_mode();
            return;
        }

        if (cmd == "actions") {
            enter_actions_mode();
            return;
        }

        if (cmd == "bench" || cmd == "benchmark") {
            enter_bench_mode();
            return;
        }

        if (!engine_) {
            add_response("Engine not initialized.", "");
            return;
        }

        // RAG commands
        if (cmd == "rag status") {
            if (rag_loaded_) {
                add_system_message("RAG: active (index loaded). Ask questions and answers will be grounded in your docs.");
                add_system_message("Use 'rag clear' or press [R] to unload.");
            } else {
                add_system_message("RAG: not loaded. Drag a file/folder here or use 'rag ingest <dir>'.");
            }
            return;
        }

        if (cmd == "rag clear") {
            if (!rag_loaded_) {
                add_system_message("RAG is not active. Nothing to clear.");
                return;
            }
            rcli_rag_clear(engine_);
            rag_loaded_ = false;
            add_system_message("RAG cleared. Back to normal LLM mode.");
            return;
        }

        if (cmd == "rag delete") {
            rcli_rag_clear(engine_);
            rag_loaded_ = false;
            std::string idx_dir;
            if (const char* home = getenv("HOME"))
                idx_dir = std::string(home) + "/Library/RCLI/index";
            if (!idx_dir.empty()) {
                std::string rm_cmd = "rm -rf '" + idx_dir + "'";
                system(rm_cmd.c_str());
                add_system_message("RAG cleared and on-disk index deleted.");
            } else {
                add_system_message("RAG cleared (could not determine index path for deletion).");
            }
            return;
        }

        if (cmd.substr(0, 11) == "rag ingest " || cmd == "rag ingest") {
            std::string docs_dir;
            if (cmd.size() > 11) docs_dir = cmd.substr(11);
            while (!docs_dir.empty() && docs_dir.back() == ' ') docs_dir.pop_back();
            while (!docs_dir.empty() && docs_dir.front() == ' ') docs_dir.erase(docs_dir.begin());

            if (docs_dir.empty()) {
                add_system_message("Usage: rag ingest <directory>");
                add_system_message("Example: rag ingest ~/Documents/notes");
                return;
            }

            // Expand ~ to HOME
            if (!docs_dir.empty() && docs_dir[0] == '~') {
                if (const char* home = getenv("HOME"))
                    docs_dir = std::string(home) + docs_dir.substr(1);
            }

            add_system_message("Indexing documents from: " + docs_dir + " ...");
            std::string dir_copy = docs_dir;
            std::thread([this, dir_copy]() {
                int rc = rcli_rag_ingest(engine_, dir_copy.c_str());
                if (rc == 0) {
                    rag_loaded_ = true;
                    add_system_message("RAG active! Your documents are indexed. Ask me anything about them.");
                } else {
                    add_system_message("Indexing failed. Check the path and try again.");
                }
                screen_->Post(Event::Custom);
            }).detach();
            return;
        }

        if (cmd.substr(0, 10) == "rag query " && cmd.size() > 10) {
            std::string query = cmd.substr(10);
            if (!rag_loaded_) {
                add_system_message("RAG not loaded. Use 'rag ingest <dir>' first.");
                return;
            }

            voice_state_ = VoiceState::THINKING;
            std::string query_copy = query;
            std::thread([this, query_copy]() {
                auto t_start = std::chrono::steady_clock::now();
                const char* response = rcli_rag_query(engine_, query_copy.c_str());
                if (response && response[0]) {
                    std::string perf;
                    int tok = 0; double tps = 0, ttft = 0, total = 0;
                    rcli_get_last_llm_perf(engine_, &tok, &tps, &ttft, &total);
                    if (tok > 0) {
                        std::ostringstream os;
                        os << std::fixed;
                        os.precision(0);
                        os << "RAG+LLM: " << tok << " tok " << tps << " tok/s TTFT " << ttft << "ms";
                        perf = os.str();
                    }
                    add_response(response, perf);
                    if (!args_.no_speak) {
                        auto t_audio = std::chrono::steady_clock::now();
                        double ttfa = std::chrono::duration<double, std::milli>(t_audio - t_start).count();
                        last_ttfa_ms_.store(ttfa, std::memory_order_relaxed);
                        voice_state_ = VoiceState::SPEAKING;
                        screen_->Post(Event::Custom);
                        rcli_speak(engine_, response);
                        wait_for_speech();
                    }
                } else {
                    add_response("(no results found)", "");
                }
                voice_state_ = VoiceState::IDLE;
                screen_->Post(Event::Custom);
            }).detach();
            return;
        }

        // Auto-detect drag-dropped or typed file/folder path → auto-ingest for RAG
        // Use original `input` here, not `cmd` (which has leading / stripped for commands)
        {
            std::string resolved = input;
            // Strip quotes and trailing whitespace from drag-drop paste
            while (!resolved.empty() && (resolved.back() == ' ' || resolved.back() == '\'' || resolved.back() == '"'))
                resolved.pop_back();
            if (!resolved.empty() && (resolved.front() == '\'' || resolved.front() == '"'))
                resolved.erase(resolved.begin());
            // Expand ~ to HOME
            if (!resolved.empty() && resolved[0] == '~') {
                if (const char* home = getenv("HOME"))
                    resolved = std::string(home) + resolved.substr(1);
            }
            // Also handle backslash-escaped spaces from some terminals
            std::string unescaped;
            for (size_t i = 0; i < resolved.size(); i++) {
                if (resolved[i] == '\\' && i + 1 < resolved.size() && resolved[i + 1] == ' ') {
                    unescaped += ' ';
                    i++;
                } else {
                    unescaped += resolved[i];
                }
            }
            resolved = unescaped;

            struct stat path_st;
            if (!resolved.empty() && resolved[0] == '/' && stat(resolved.c_str(), &path_st) == 0) {
                add_system_message("Detected path: " + resolved);
                add_system_message("Indexing for RAG... this may take a moment.");
                std::string path_copy = resolved;
                std::thread([this, path_copy]() {
                    int rc = rcli_rag_ingest(engine_, path_copy.c_str());
                    if (rc == 0) {
                        rag_loaded_ = true;
                        add_system_message("RAG active! Your documents are indexed. Ask me anything about them.");
                    } else {
                        add_system_message("Indexing failed. Check the path and try again.");
                    }
                    screen_->Post(Event::Custom);
                }).detach();
                return;
            }
        }

        // Run LLM (or RAG+LLM) in background thread to keep UI responsive
        voice_state_ = VoiceState::THINKING;
        std::string input_copy = input;
        std::thread([this, input_copy]() {
            auto t_start = std::chrono::steady_clock::now();
            const char* response = rag_loaded_
                ? rcli_rag_query(engine_, input_copy.c_str())
                : rcli_process_command(engine_, input_copy.c_str());
            if (response && response[0]) {
                std::string perf;
                int tok = 0; double tps = 0, ttft = 0, total = 0;
                rcli_get_last_llm_perf(engine_, &tok, &tps, &ttft, &total);
                if (tok > 0) {
                    std::ostringstream os;
                    os << std::fixed;
                    os.precision(0);
                    os << (rag_loaded_ ? "RAG+LLM: " : "LLM: ")
                       << tok << " tok " << tps << " tok/s TTFT " << ttft << "ms";
                    perf = os.str();
                }

                add_response(response, perf);
                screen_->Post(Event::Custom);

                if (!args_.no_speak) {
                    auto t_audio = std::chrono::steady_clock::now();
                    double ttfa = std::chrono::duration<double, std::milli>(t_audio - t_start).count();
                    last_ttfa_ms_.store(ttfa, std::memory_order_relaxed);

                    voice_state_ = VoiceState::SPEAKING;
                    screen_->Post(Event::Custom);
                    rcli_speak(engine_, response);

                    int samples = 0; double synth_ms = 0, rtf = 0;
                    rcli_get_last_tts_perf(engine_, &samples, &synth_ms, &rtf);
                    if (samples > 0) {
                        std::ostringstream ts;
                        ts << std::fixed;
                        ts.precision(0);
                        ts << "TTFA: " << ttfa << "ms  TTS: ";
                        ts.precision(1);
                        ts << synth_ms << "ms " << rtf << "x RT";
                        add_system_message(ts.str());
                    }
                    wait_for_speech();
                }
            } else {
                add_response("(no response)", "");
            }

            voice_state_ = VoiceState::IDLE;
            screen_->Post(Event::Custom);
        }).detach();
    }

    void wait_for_speech() {
        while (rcli_is_speaking(engine_)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    void add_user_message(const std::string& text) {
        std::lock_guard<std::mutex> lock(chat_mu_);
        chat_history_.push_back({">", text, "", true});
        trim_history();
    }

    void add_response(const std::string& text, const std::string& perf) {
        std::lock_guard<std::mutex> lock(chat_mu_);
        chat_history_.push_back({"RA:", text, perf, false});
        trim_history();
    }

    void add_system_message(const std::string& text) {
        std::lock_guard<std::mutex> lock(chat_mu_);
        chat_history_.push_back({"*", text, "", false});
        trim_history();
    }

    void trim_history() {
        while (chat_history_.size() > 50) chat_history_.pop_front();
    }

    RCLIHandle engine_;
    Args args_;
    TuiTheme theme_;
    ChipInfo chip_;
    HardwareMonitor monitor_;
    ftxui::ScreenInteractive* screen_ = nullptr;

    std::atomic<VoiceState> voice_state_{VoiceState::IDLE};
    std::atomic<float> audio_level_{0.0f};
    std::atomic<float> peak_rms_{0.0f};
    std::atomic<int> anim_tick_{0};
    std::atomic<bool> rag_loaded_{false};
    std::atomic<double> last_ttfa_ms_{0.0};

    std::mutex chat_mu_;
    std::deque<ChatMessage> chat_history_;

    // Cleanup mode state
    struct CleanupEntry {
        std::string name;
        std::string path;
        std::string id;
        std::string modality;
        int size_mb;
        bool is_active;
        bool is_dir;
    };
    bool cleanup_mode_ = false;
    int cleanup_cursor_ = 0;
    std::vector<CleanupEntry> cleanup_entries_;
    std::string cleanup_message_;
    ftxui::Color cleanup_msg_color_ = theme_.warning;

    // Models panel state
    struct ModelEntry {
        std::string name, id, modality, url, filename;
        int size_mb = 0;
        bool installed = false, is_active = false, is_default = false;
        bool is_recommended = false, is_header = false, is_archive = false;
    };
    bool models_mode_ = false;
    int models_cursor_ = 0;
    std::vector<ModelEntry> models_entries_;
    std::string models_message_;
    ftxui::Color models_msg_color_ = theme_.warning;

    // Actions panel state
    struct ActionEntry {
        std::string name, description, category, params_json;
        bool is_header = false;
    };
    bool actions_mode_ = false;
    int actions_cursor_ = 0;
    std::vector<ActionEntry> actions_entries_;
    std::string actions_message_;
    ftxui::Color actions_msg_color_ = theme_.warning;

    // Bench panel state
    struct BenchEntry { std::string name, suite_key; };
    bool bench_mode_ = false;
    int bench_cursor_ = 0;
    std::vector<BenchEntry> bench_entries_;
    std::string bench_output_;
    bool bench_running_ = false;

    // RAG panel state
    struct RagOption { std::string name, action; };
    bool rag_mode_ = false;
    int rag_panel_cursor_ = 0;
    std::vector<RagOption> rag_panel_options_;
    std::string rag_panel_message_;
    ftxui::Color rag_panel_msg_color_ = theme_.warning;
    bool rag_input_active_ = false;
    std::string rag_input_path_;
};

} // namespace rcli_tui
