#pragma once
// =============================================================================
// RCLI TUI App — Full-screen interactive terminal UI (mactop-style)
// =============================================================================

#include "cli/tui_dashboard.h"
#include "cli/cli_common.h"
#include "cli/metalrt_beast_mode.h"  // BEAST MODE visualization
#include "api/rcli_api.h"
#include "models/model_registry.h"
#include "models/tts_model_registry.h"
#include "models/stt_model_registry.h"
#include "actions/action_registry.h"
#include "engines/metalrt_loader.h"
#include "core/log.h"
#include "core/personality.h"

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
    bool is_metalrt = false;
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

inline DogFrame get_dog_frame(VoiceState state, int tick, bool voice_mode = false) {
    auto cyan   = ftxui::Color::Cyan;
    auto yellow = ftxui::Color::Yellow;
    auto green  = ftxui::Color::Green;
    auto magenta = ftxui::Color::Magenta;
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
        screen_obj.TrackMouse(false);
        screen_ = &screen_obj;

        // Tool trace callback: fires on the rcli_process_command() background thread,
        // so we guard chat_history_ with chat_mu_ and use Post(Event::Custom) to
        // kick the FTXUI render loop. The "~" prefix is a sentinel used in
        // build_chat_panel() to apply distinct trace styling (cyan/dim).
        // Truncate data to 200 chars to keep the chat readable; raw JSON from
        // actions can be very long.
        rcli_set_tool_trace_callback(engine_,
            [](const char* event, const char* tool_name, const char* data,
               int success, void* ud) {
                auto* self = static_cast<TuiApp*>(ud);
                if (!self->tool_trace_enabled_.load(std::memory_order_relaxed)) return;

                std::string ev(event), name(tool_name), detail(data ? data : "");
                if (detail.size() > 200) detail = detail.substr(0, 197) + "...";

                std::string msg;
                if (ev == "detected") {
                    msg = "[TRACE] Tool call: " + name + "(" + detail + ")";
                } else if (ev == "result") {
                    msg = "[TRACE] " + name + " -> " +
                          std::string(success ? "OK" : "FAIL") + ": " + detail;
                }

                if (!msg.empty()) {
                    std::lock_guard<std::mutex> lock(self->chat_mu_);
                    self->chat_history_.push_back({"~", msg, "", false});
                    self->trim_history();
                    if (self->screen_)
                        self->screen_->Post(Event::Custom);
                }
            }, this);

        std::string input_text;
        auto input_box = Input(&input_text, "Type a command or question...");

        auto component = Container::Vertical({input_box});

        std::atomic<bool> should_quit{false};

        auto renderer = Renderer(component, [&]() {
            auto stats = monitor_.snapshot();
            return build_ui(stats, input_text);
        });

        auto handler = CatchEvent(renderer, [&](Event event) -> bool {
            // --- Bracketed paste / multi-char events → always let Input handle ---
            if (event.is_character() && event.input().size() > 1) return false;

            // --- Universal stop: ESC always interrupts processing ---
            if (event == Event::Escape &&
                (voice_state_ != VoiceState::IDLE || rcli_is_speaking(engine_))) {
                rcli_stop_processing(engine_);
                voice_state_ = VoiceState::IDLE;
                add_system_message("Stopped.");
                screen_->Post(Event::Custom);
                return true;
            }

            // --- Panel mode guards: each panel intercepts all input ---
            if (models_mode_) {
                if (event == Event::Escape) { models_mode_ = false; return true; }
                if (event == Event::ArrowUp) { models_cursor_up(); return true; }
                if (event == Event::ArrowDown) { models_cursor_down(); return true; }
                if (event == Event::Return) { models_select_or_download(); return true; }
                return true;
            }
            if (engine_mode_) {
                if (event == Event::Escape) { engine_mode_ = false; return true; }
                if (event == Event::ArrowUp) {
                    if (engine_cursor_ > 0) engine_cursor_--;
                    return true;
                }
                if (event == Event::ArrowDown) {
                    if (engine_cursor_ < (int)engine_entries_.size() - 1) engine_cursor_++;
                    return true;
                }
                if (event == Event::Return) { engine_select(); return true; }
                return true;
            }
            if (personality_mode_) {
                if (event == Event::Escape) { personality_mode_ = false; return true; }
                if (event == Event::ArrowUp) {
                    if (personality_cursor_ > 0) personality_cursor_--;
                    return true;
                }
                if (event == Event::ArrowDown) {
                    if (personality_cursor_ < (int)personality_entries_.size() - 1) personality_cursor_++;
                    return true;
                }
                if (event == Event::Return) { personality_select(); return true; }
                return true;
            }
            if (actions_mode_) {
                if (event == Event::Escape) { actions_mode_ = false; return true; }
                if (event == Event::ArrowUp) { actions_cursor_up(); return true; }
                if (event == Event::ArrowDown) { actions_cursor_down(); return true; }
                if (event == Event::Return) { actions_execute_selected(); return true; }
                if (event == Event::Character(' ')) { actions_toggle_selected(); return true; }
                return true;
            }
            if (bench_mode_) {
                if (event == Event::Escape) { bench_mode_ = false; return true; }
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
                if (event == Event::Escape) { rag_mode_ = false; return true; }
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
                if (event == Event::Escape) { cleanup_mode_ = false; return true; }
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

            // --- When user is typing (input not empty), let all chars go to Input ---
            if (!input_text.empty() && event.is_character()) return false;

            // --- Shortcut keys (no panel open, input empty) ---

            // ESC while idle + empty input → quit
            if (event == Event::Escape && input_text.empty() &&
                voice_state_ == VoiceState::IDLE) {
                rcli_stop_processing(engine_);
                should_quit = true;
                screen_->Exit();
                return true;
            }

            // SPACE with empty input → push-to-talk / barge-in
            if (event == Event::Character(' ') && input_text.empty()) {
                if (voice_state_ == VoiceState::IDLE) {
                    start_recording();
                } else if (voice_state_ == VoiceState::RECORDING) {
                    stop_recording();
                } else {
                    // Barge-in: stop whatever is happening (TTS/LLM/STT)
                    // and immediately start a new recording
                    rcli_stop_processing(engine_);
                    voice_state_ = VoiceState::IDLE;
                    screen_->Post(Event::Custom);
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    start_recording();
                }
                return true;
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

            // Single-letter shortcuts only when input is empty and idle
            if (input_text.empty() && voice_state_ == VoiceState::IDLE && event.is_character()) {
                auto c = event.character();
                if (c == "q" || c == "Q") {
                    rcli_stop_processing(engine_);
                    should_quit = true;
                    screen_->Exit();
                    return true;
                }
                if (c == "m" || c == "M") { enter_models_mode(); return true; }
                if (c == "a" || c == "A") { enter_actions_mode(); return true; }
                if (c == "b" || c == "B") { enter_bench_mode(); return true; }
                if (c == "r" || c == "R") { enter_rag_mode(); return true; }
                if (c == "d" || c == "D") { close_all_panels(); enter_cleanup_mode(); return true; }
                if (c == "e" || c == "E") { enter_engine_switcher(); return true; }
                if (c == "p" || c == "P") { enter_personality_mode(); return true; }
                // V key: voice mode removed — push-to-talk via SPACE is always active
                if (c == "t" || c == "T") {
                    tool_trace_enabled_ = !tool_trace_enabled_.load(std::memory_order_relaxed);
                    add_system_message(tool_trace_enabled_ ? "Tool call trace: ON" : "Tool call trace: OFF");
                    return true;
                }
                if (c == "v" || c == "V") {
                    verbose_metrics_ = !verbose_metrics_.load(std::memory_order_relaxed);
                    add_system_message(verbose_metrics_ ? "Verbose metrics: ON" : "Verbose metrics: OFF");
                    return true;
                }
                if (c == "x" || c == "X") {
                    rcli_clear_history(engine_);
                    ctx_prompt_tokens_.store(0, std::memory_order_relaxed);
                    last_ctx_pct_notified_ = 0;
                    {
                        std::lock_guard<std::mutex> lock(chat_mu_);
                        chat_history_.clear();
                    }
                    add_system_message("Conversation cleared. Context reset.");
                    screen_->Post(Event::Custom);
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

        rcli_set_tool_trace_callback(engine_, nullptr, nullptr);
    }

private:
    std::string format_llm_perf(bool is_rag = false) {
        if (!engine_) return "";
        int tok = 0; double tps = 0, ttft = 0, total = 0;
        rcli_get_last_llm_perf(engine_, &tok, &tps, &ttft, &total);
        if (tok <= 0) return "";

        double pfill_tps = 0, dec_tps = 0, pfill_ms = 0, dec_ms = 0;
        const char* eng_name = nullptr;
        rcli_get_last_llm_perf_extended(engine_,
            &pfill_tps, &dec_tps, &pfill_ms, &dec_ms, nullptr, &eng_name);

        // Update MetalRT metrics - single source of truth for GPU utilization
        if (eng_name && std::string(eng_name) == "MetalRT") {
            metalrt_active_.store(true);
            tokens_per_second_.store(static_cast<int>(tps));

            // Set streaming active when generating tokens
            if (tok > 0 && tps > 0) {
                streaming_active_.store(true);
            }

            // Single GPU utilization calculation
            // Smooth transitions with time-based decay
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - last_update_time_).count();

            float target_util = std::min(1.0f, static_cast<float>(tps) / 150.0f);  // Adjusted for more realistic scaling
            float current = gpu_utilization_.load();

            // Smooth transition
            if (elapsed > 100) {  // Update every 100ms
                float new_util = current + (target_util - current) * 0.3f;  // Smooth factor
                gpu_utilization_.store(new_util);
                last_update_time_ = now;
            }

            // Token stream update for 3D effect
            // (tokens are added elsewhere during streaming)
        } else {
            metalrt_active_.store(false);
            streaming_active_.store(false);

            // Gradual decay when not MetalRT
            float current = gpu_utilization_.load();
            if (current > 0.01f) {
                gpu_utilization_.store(current * 0.9f);  // Decay factor
            }
        }

        std::ostringstream os;
        os << std::fixed;
        os.precision(0);
        std::string label = is_rag ? "RAG+LLM" : (eng_name ? eng_name : "LLM");
        os << label << ": " << tok << " tok  "
           << tps << " tok/s  TTFT " << ttft << "ms";
        if (pfill_ms > 0)
            os << "  prefill " << pfill_ms << "ms";
        if (dec_ms > 0)
            os << "  decode " << dec_ms << "ms";
        return os.str();
    }

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
            fprintf(stderr, "[TRACE] [voice-thread] START\n");
            fprintf(stderr, "[TRACE] [voice-thread] calling stop_capture_and_transcribe ...\n");
            const char* transcript = rcli_stop_capture_and_transcribe(engine_);
            fprintf(stderr, "[TRACE] [voice-thread] STT done, transcript='%.40s'\n", transcript ? transcript : "NULL");

            if (!transcript || !transcript[0]) {
                voice_state_ = VoiceState::IDLE;
                add_system_message("(no speech detected - try speaking louder)");
                screen_->Post(Event::Custom);
                return;
            }

            std::string user_text = transcript;
            add_user_message(user_text);

            if (verbose_metrics_.load(std::memory_order_relaxed)) {
                double audio_ms = 0, trans_ms = 0;
                rcli_get_last_stt_perf(engine_, &audio_ms, &trans_ms);
                if (trans_ms > 0) {
                    std::ostringstream os;
                    os << std::fixed << std::setprecision(0) << "STT: " << trans_ms << "ms";
                    add_system_message(os.str());
                }
            }

            voice_state_ = VoiceState::THINKING;
            screen_->Post(Event::Custom);

            if (rag_loaded_) {
                // RAG path: retrieve + LLM, then stream TTS
                fprintf(stderr, "[TRACE] [voice-thread] calling rag_query ...\n");
                const char* response = rcli_rag_query(engine_, user_text.c_str());
                fprintf(stderr, "[TRACE] [voice-thread] rag_query returned\n");

                if (response && response[0]) {
                    std::string perf = format_llm_perf(true);
                    int pt = 0, cs = 0;
                    rcli_get_context_info(engine_, &pt, &cs);
                    if (pt > 0) ctx_prompt_tokens_.store(pt, std::memory_order_relaxed);
                    if (cs > 0) ctx_size_.store(cs, std::memory_order_relaxed);
                    add_response(response, perf);
                    check_context_full();
                    screen_->Post(Event::Custom);

                    if (!args_.no_speak) {
                        voice_state_ = VoiceState::SPEAKING;
                        screen_->Post(Event::Custom);
                        auto rag_tts_cb = [](const char* event, const char* data, void* ud) {
                            auto* app = static_cast<TuiApp*>(ud);
                            std::string ev(event);
                            if (ev == "first_audio") {
                                double ttfa = std::stod(data);
                                app->last_ttfa_ms_.store(ttfa, std::memory_order_relaxed);
                            } else if (ev == "complete") {
                                std::string json(data);
                                double total_tts = 0;
                                auto pos = json.find("\"total_tts_ms\":");
                                if (pos != std::string::npos) {
                                    total_tts = std::stod(json.substr(pos + 15));
                                }
                                if (app->verbose_metrics_.load(std::memory_order_relaxed)) {
                                    double ttfa = app->last_ttfa_ms_.load(std::memory_order_relaxed);
                                    if (total_tts > 0) {
                                        std::ostringstream ts;
                                        ts << std::fixed;
                                        ts.precision(0);
                                        ts << "TTFA: " << ttfa << "ms  TTS: ";
                                        ts.precision(1);
                                        ts << total_tts << "ms (streamed)";
                                        app->add_system_message(ts.str());
                                        app->screen_->Post(Event::Custom);
                                    }
                                }
                            }
                        };
                        rcli_speak_streaming(engine_, response, rag_tts_cb, this);
                    }
                } else {
                    add_response("(no response)", "");
                }
            } else if (args_.no_speak) {
                // No-speak mode: non-streaming
                fprintf(stderr, "[TRACE] [voice-thread] calling process_command (no-speak) ...\n");
                const char* response = rcli_process_command(engine_, user_text.c_str());
                if (response && response[0]) {
                    std::string perf = format_llm_perf(false);
                    int pt = 0, cs = 0;
                    rcli_get_context_info(engine_, &pt, &cs);
                    if (pt > 0) ctx_prompt_tokens_.store(pt, std::memory_order_relaxed);
                    if (cs > 0) ctx_size_.store(cs, std::memory_order_relaxed);
                    add_response(response, perf);
                    check_context_full();
                } else {
                    add_response("(no response)", "");
                }
                screen_->Post(Event::Custom);
            } else {
                // Streaming LLM → TTS pipeline
                fprintf(stderr, "[TRACE] [voice-thread] calling process_and_speak ...\n");
                auto event_cb = [](const char* event, const char* data, void* ud) {
                    auto* app = static_cast<TuiApp*>(ud);
                    std::string ev(event);
                    if (ev == "first_audio") {
                        double ttfa = std::stod(data);
                        app->last_ttfa_ms_.store(ttfa, std::memory_order_relaxed);
                        app->voice_state_ = VoiceState::SPEAKING;
                        app->screen_->Post(Event::Custom);
                    } else if (ev == "response") {
                        std::string perf = app->format_llm_perf(false);
                        int pt = 0, cs = 0;
                        rcli_get_context_info(app->engine_, &pt, &cs);
                        if (pt > 0) app->ctx_prompt_tokens_.store(pt, std::memory_order_relaxed);
                        if (cs > 0) app->ctx_size_.store(cs, std::memory_order_relaxed);
                        app->add_response(data, perf);
                        app->check_context_full();
                        app->screen_->Post(Event::Custom);
                    } else if (ev == "complete") {
                        // Parse TTS stats from JSON and display
                        std::string json(data);
                        // Simple parse of total_tts_ms
                        double total_tts = 0;
                        auto pos = json.find("\"total_tts_ms\":");
                        if (pos != std::string::npos) {
                            total_tts = std::stod(json.substr(pos + 15));
                        }
                        if (app->verbose_metrics_.load(std::memory_order_relaxed)) {
                            double ttfa = app->last_ttfa_ms_.load(std::memory_order_relaxed);
                            if (total_tts > 0) {
                                std::ostringstream ts;
                                ts << std::fixed;
                                ts.precision(0);
                                ts << "TTFA: " << ttfa << "ms  TTS: ";
                                ts.precision(1);
                                ts << total_tts << "ms (streamed)";
                                app->add_system_message(ts.str());
                                app->screen_->Post(Event::Custom);
                            }
                        }
                    }
                };

                const char* response = rcli_process_and_speak(
                    engine_, user_text.c_str(), event_cb, this);
                fprintf(stderr, "[TRACE] [voice-thread] process_and_speak returned\n");

                if (!response || !response[0]) {
                    add_response("(no response)", "");
                    screen_->Post(Event::Custom);
                }
            }

            fprintf(stderr, "[TRACE] [voice-thread] setting voice_state_ = IDLE\n");
            // Only transition to IDLE if still in a processing state — avoids
            // clobbering RECORDING when the user barged in with SPACE.
            VoiceState cur = voice_state_.load(std::memory_order_relaxed);
            if (cur != VoiceState::IDLE && cur != VoiceState::RECORDING)
                voice_state_.store(VoiceState::IDLE, std::memory_order_relaxed);
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
        else if (engine_mode_)
            layout.push_back(build_engine_panel() | flex);
        else if (personality_mode_)
            layout.push_back(build_personality_panel() | flex);
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
        auto frame = get_dog_frame(voice_state_, tick, voice_mode_active_);
        float level = audio_level_.load(std::memory_order_relaxed);
        float norm = std::min(1.0f, level * 5.0f);
        norm = std::sqrtf(norm);

        Elements rows;

        // Removed clutter - keep it clean

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
            text(" RCLI " + std::string(RA_VERSION) + " ") | ftxui::bold | ftxui::color(theme_.accent),
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
        std::string llm_perf;
        const char* perf_engine = nullptr;
        double prefill_tps = 0, decode_tps = 0;

        if (engine_) {
            const char* n = rcli_get_llm_model(engine_);
            if (n) llm_name = n;
            n = rcli_get_stt_model(engine_);
            if (n) stt_name = n;
            n = rcli_get_tts_model(engine_);
            if (n) tts_name = n;

            int tok = 0; double tps = 0, ttft = 0, total = 0;
            rcli_get_last_llm_perf(engine_, &tok, &tps, &ttft, &total);
            double prefill_ms = 0, decode_ms = 0;
            rcli_get_last_llm_perf_extended(engine_,
                &prefill_tps, &decode_tps, &prefill_ms, &decode_ms, nullptr, &perf_engine);
            if (tok > 0) {
                std::ostringstream os;
                os << std::fixed;
                os.precision(0);
                os << tok << " tok  " << tps << " tok/s  TTFT " << ttft << "ms";
                if (prefill_ms > 0)
                    os << "  prefill " << prefill_ms << "ms";
                if (decode_ms > 0)
                    os << "  decode " << decode_ms << "ms";
                double stt_audio = 0, stt_trans = 0;
                rcli_get_last_stt_perf(engine_, &stt_audio, &stt_trans);
                if (stt_trans > 0) {
                    os << "  STT " << stt_trans << "ms";
                }
                int tts_samples = 0; double tts_synth = 0, tts_rtf = 0;
                rcli_get_last_tts_perf(engine_, &tts_samples, &tts_synth, &tts_rtf);
                if (tts_samples > 0) {
                    os.precision(1);
                    os << "  TTS " << tts_synth << "ms " << tts_rtf << "x RT";
                }
                llm_perf = os.str();
            }

            // STT/TTS perf is now included in llm_perf above
        }

        auto rag_indicator = rag_loaded_.load()
            ? hbox({text("RAG: ") | ftxui::bold, text("active") | ftxui::bold | ftxui::color(theme_.success)})
            : hbox({text("RAG: ") | ftxui::bold, text("off") | dim});

        double ttfa = last_ttfa_ms_.load(std::memory_order_relaxed);

        // Engine indicator — use actual runtime engine, not config preference
        std::string engine_label_str = "llama.cpp";
        auto engine_color = theme_.text_normal;
        if (engine_) {
            const char* active = rcli_get_active_engine(engine_);
            if (active) engine_label_str = active;
        } else {
            std::string engine_pref = rcli::read_engine_preference();
            if (engine_pref == "metalrt")
                engine_label_str = "MetalRT";
            else if (engine_pref == "auto" || engine_pref.empty()) {
                bool mrt_avail = rastack::MetalRTLoader::instance().is_available();
                engine_label_str = mrt_avail ? "Auto (MetalRT)" : "Auto (llama.cpp)";
            }
        }
        bool is_metalrt_engine = engine_label_str.find("MetalRT") != std::string::npos;
        if (is_metalrt_engine)
            engine_color = theme_.info;

        Element engine_badge;
        if (is_metalrt_engine) {
            engine_badge = hbox({
                text("Engine: ") | ftxui::bold,
                text(engine_label_str) | ftxui::bold | ftxui::color(theme_.info),
            });
        } else {
            engine_badge = hbox({
                text("Engine: ") | ftxui::bold,
                text(engine_label_str) | ftxui::color(engine_color),
            });
        }

        // Whisper Medium/Small not yet fully optimized on Metal GPU
        bool stt_not_optimized = is_metalrt_engine &&
            (stt_name.find("Medium") != std::string::npos ||
             stt_name.find("Small") != std::string::npos);
        Element stt_label;
        if (stt_not_optimized) {
            stt_label = hbox({
                text("STT: ") | ftxui::bold,
                text(stt_name),
                text(" (GPU beta)") | dim | ftxui::color(theme_.warning),
            });
        } else {
            stt_label = hbox({text("STT: ") | ftxui::bold, text(stt_name)});
        }

        // Row 1: Model names + Engine + RAG
        auto row1 = hbox({
            hbox({text("LLM: ") | ftxui::bold, text(llm_name)}) | flex,
            text(" │ "),
            engine_badge,
            text(" │ "),
            stt_label | flex,
            text(" │ "),
            hbox({text("TTS: ") | ftxui::bold, text(tts_name)}) | flex,
            text(" │ "),
            rag_indicator,
        });

        // Row 2: Metrics — minimal by default, verbose with [V] toggle
        Elements metrics;
        bool verbose = verbose_metrics_.load(std::memory_order_relaxed);
        bool perf_is_metalrt = (perf_engine && std::string(perf_engine) == "MetalRT");

        if (is_metalrt_engine) {
            float gpu_util = gpu_utilization_.load();
            int tps = tokens_per_second_.load();
            bool is_active = streaming_active_.load() || metalrt_active_.load();

            std::string badge = metalrt_viz_.RenderBadge(gpu_util, tps, is_active, verbose);
            auto badge_color = is_active ? theme_.info : theme_.warning;
            metrics.push_back(text(badge) | ftxui::bold | ftxui::color(badge_color));
        }

        if (verbose) {
            if (ttfa > 0) {
                auto ttfa_color = (ttfa < 100) ? theme_.success
                                : (ttfa < 300) ? theme_.warning : theme_.error;
                std::ostringstream os;
                os << std::fixed << std::setprecision(0) << "  TTFA " << ttfa << "ms";
                metrics.push_back(text(os.str()) | ftxui::bold | ftxui::color(ttfa_color));
            }
            if (!llm_perf.empty()) {
                metrics.push_back(text("  " + llm_perf) | dim);
            }
        }

        if (metrics.empty()) {
            metrics.push_back(text("") | dim);
        }
        metrics.push_back(filler());

        // Context window usage indicator — always pull ctx_size live from the engine
        // so it reflects the currently loaded model (e.g. after a model switch) without
        // waiting for the next LLM call.
        int ctx_used = ctx_prompt_tokens_.load(std::memory_order_relaxed);
        int ctx_total = 0;
        if (engine_) {
            rcli_get_context_info(engine_, nullptr, &ctx_total);
            if (ctx_total > 0) ctx_size_.store(ctx_total, std::memory_order_relaxed);
        }
        if (ctx_total <= 0) ctx_total = ctx_size_.load(std::memory_order_relaxed);

        Element ctx_row;
        if (ctx_total > 0 && ctx_used > 0) {
            float ctx_frac = std::min(1.0f, (float)ctx_used / ctx_total);
            int ctx_pct = (int)(ctx_frac * 100);
            auto ctx_color = (ctx_pct >= 75) ? theme_.error
                           : (ctx_pct >= 50) ? theme_.warning
                           : theme_.success;
            std::string ctx_label = "Ctx " + std::to_string(ctx_pct) + "% "
                                  + "(" + std::to_string(ctx_used) + "/"
                                  + std::to_string(ctx_total) + " tok)";
            auto clear_hint = (ctx_pct >= 75)
                ? text(" FULL — press [X] to clear ") | ftxui::bold | ftxui::color(theme_.error)
                : text("[X] clear context") | dim;
            ctx_row = hbox({
                text(" ") | size(WIDTH, EQUAL, 9),
                gauge(ctx_frac) | flex | ftxui::color(ctx_color),
                text(" " + ctx_label + " ") | ftxui::bold | ftxui::color(ctx_color),
                filler(),
                clear_hint,
            });
        } else if (ctx_total > 0) {
            // Engine ready, no query yet — show window size but empty gauge
            std::string ctx_label = "Ctx 0% (0/" + std::to_string(ctx_total) + " tok)";
            ctx_row = hbox({
                text(" ") | size(WIDTH, EQUAL, 9),
                gauge(0.0f) | flex | ftxui::color(theme_.success),
                text(" " + ctx_label + " ") | dim,
                filler(),
                text("[X] clear context") | dim,
            });
        } else {
            ctx_row = hbox({
                text(" ") | size(WIDTH, EQUAL, 9),
                text("context: loading...") | dim,
                filler(),
                text("[X] clear context") | dim,
            });
        }

        return vbox({row1, hbox(std::move(metrics)), ctx_row});
    }

    Element build_chat_panel() {
        std::lock_guard<std::mutex> lock(chat_mu_);
        Elements lines;

        if (chat_history_.empty()) {
            lines.push_back(text("") | dim);
            lines.push_back(text(u8"██████╗  ██████╗██╗     ██╗") | ftxui::bold | ftxui::color(theme_.accent) | center);
            lines.push_back(text(u8"██╔══██╗██╔════╝██║     ██║") | ftxui::bold | ftxui::color(theme_.accent) | center);
            lines.push_back(text(u8"██████╔╝██║     ██║     ██║") | ftxui::bold | ftxui::color(theme_.accent) | center);
            lines.push_back(text(u8"██╔══██╗██║     ██║     ██║") | ftxui::bold | ftxui::color(theme_.accent) | center);
            lines.push_back(text(u8"██║  ██║╚██████╗███████╗██║") | ftxui::bold | ftxui::color(theme_.accent) | center);
            lines.push_back(text(u8"╚═╝  ╚═╝ ╚═════╝╚══════╝╚═╝") | ftxui::bold | ftxui::color(theme_.accent) | center);
            lines.push_back(text("On-device voice AI and RAG for macOS") | dim | center);
            lines.push_back(text("Powered by RunAnywhere") | dim | center);
            lines.push_back(text("") | dim);
            lines.push_back(text("  SPACE          Start/stop voice recording") | dim);
            lines.push_back(text("  Type + ENTER   Send a text command or question") | dim);
            lines.push_back(text("  help           Show all commands") | dim);
            lines.push_back(text("") | dim);
            lines.push_back(
                text("  RAG: Drag a file or folder here to index it, then ask questions about it.") | ftxui::bold);
            lines.push_back(text("") | dim);
            lines.push_back(text("  Try: \"open Safari\"   \"what's the weather?\"   \"set volume to 50\"") | dim);
        } else {
            for (size_t i = 0; i < chat_history_.size(); i++) {
                auto& msg = chat_history_[i];
                bool is_trace = (msg.prefix == "~");
                auto prefix_color = msg.is_user ? theme_.user_msg
                    : (is_trace ? theme_.info : theme_.accent);

                Element prefix_elem;
                if (!msg.is_user && !is_trace && msg.is_metalrt) {
                    prefix_elem = hbox({
                        text("  " + msg.prefix + " ") | ftxui::bold | ftxui::color(prefix_color),
                    });
                } else {
                    prefix_elem = text("  " + msg.prefix + " ") | ftxui::bold | ftxui::color(prefix_color);
                }

                auto body_elem = paragraph(msg.text);
                auto line = is_trace
                    ? hbox({prefix_elem, body_elem | flex | ftxui::color(theme_.info)}) | dim
                    : hbox({prefix_elem, body_elem | flex});
                if (i == chat_history_.size() - 1 && msg.perf.empty())
                    line = line | focus;
                lines.push_back(line);
                if (!msg.perf.empty() && verbose_metrics_.load(std::memory_order_relaxed)) {
                    Element perf_elem = text("    " + msg.perf) | dim;
                    if (i == chat_history_.size() - 1)
                        perf_elem = perf_elem | focus;
                    lines.push_back(perf_elem);
                }
            }
        }

        return vbox(std::move(lines)) | yframe | vscroll_indicator;
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
                text("[Up/Down] navigate  [Enter] delete  [ESC] close ") | dim,
            });
        }
        if (models_mode_) {
            return hbox({
                text(" Models ") | ftxui::bold | ftxui::color(theme_.accent),
                filler(),
                text("[Up/Down] navigate  [Enter] select/download  [ESC] close ") | dim,
            });
        }
        if (actions_mode_) {
            return hbox({
                text(" Actions ") | ftxui::bold | ftxui::color(theme_.accent),
                filler(),
                text("[Up/Down] navigate  [Enter] run  [ESC] close ") | dim,
            });
        }
        if (bench_mode_) {
            return hbox({
                text(" Benchmarks ") | ftxui::bold | ftxui::color(theme_.accent),
                filler(),
                text("[Up/Down] navigate  [Enter] run  [ESC] close ") | dim,
            });
        }
        if (rag_mode_) {
            return hbox({
                text(" RAG ") | ftxui::bold | ftxui::color(theme_.accent),
                filler(),
                text("[Up/Down] navigate  [Enter] select  [ESC] close ") | dim,
            });
        }

        bool trace_on = tool_trace_enabled_.load(std::memory_order_relaxed);
        bool verbose_on = verbose_metrics_.load(std::memory_order_relaxed);

        Elements left;
        left.push_back(text(" Voice AI + RAG") | ftxui::bold);
        left.push_back(text("  \xE2\x80\xA2  ") | dim);
        left.push_back(text("Powered by RunAnywhere") | dim);

        Elements right;
        // Build shortcuts with color highlights for active toggles
        right.push_back(text("[SPACE] talk  ") | dim);
        right.push_back(text("[M] models  ") | dim);
        right.push_back(text("[A] actions  ") | dim);
        right.push_back(text("[B] bench  ") | dim);
        right.push_back(text("[R] RAG  ") | dim);
        right.push_back(text("[D] cleanup  ") | dim);
        if (trace_on)
            right.push_back(text("[T] trace  ") | ftxui::color(theme_.info));
        else
            right.push_back(text("[T] trace  ") | dim);
        if (verbose_on)
            right.push_back(text("[V] metrics  ") | ftxui::color(theme_.info));
        else
            right.push_back(text("[V] metrics  ") | dim);
        right.push_back(text("[Q] quit") | dim);

        return hbox({
            hbox(std::move(left)),
            filler(),
            hbox(std::move(right)),
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
        lines.push_back(text("  Arrow keys to navigate, ENTER to delete, ESC to close") | dim);
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
                    elem = elem | ftxui::bold | ftxui::color(theme_.text_selected) | focus;
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

        return vbox(std::move(lines)) | yframe | vscroll_indicator;
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
        engine_mode_ = false;
        personality_mode_ = false;
    }

    // ====================================================================
    // [E] Engine panel — select llama.cpp / MetalRT
    // ====================================================================

    void enter_engine_switcher() {
        close_all_panels();
        engine_entries_.clear();
        engine_cursor_ = 0;
        engine_message_.clear();

        std::string current = rcli::read_engine_preference();
        if (current.empty()) current = "auto";

        bool metalrt_available = false;
        {
            std::string dylib_path = rastack::MetalRTLoader::engines_dir() + "/libmetalrt.dylib";
            struct stat st;
            metalrt_available = (stat(dylib_path.c_str(), &st) == 0);
        }

        engine_entries_.push_back({"llamacpp",
            "llama.cpp",
            "CPU inference \u00B7 GGUF models \u00B7 Universal compatibility",
            current == "llamacpp"});

        engine_entries_.push_back({"metalrt",
            "MetalRT",
            std::string("GPU-accelerated \u00B7 MLX 4-bit \u00B7 Apple Silicon optimized") +
                (metalrt_available ? "" : "  [not installed]"),
            current == "metalrt"});

        if (current == "auto") {
            for (auto& e : engine_entries_)
                if (e.id == "metalrt" && metalrt_available) e.is_active = true;
                else if (e.id == "llamacpp" && !metalrt_available) e.is_active = true;
        }

        engine_mode_ = true;
    }

    void engine_select() {
        if (engine_cursor_ < 0 || engine_cursor_ >= (int)engine_entries_.size()) return;
        auto& sel = engine_entries_[engine_cursor_];

        if (sel.is_active) {
            engine_message_ = sel.name + " is already active.";
            engine_msg_color_ = ftxui::Color::Yellow;
            return;
        }

        if (sel.id == "metalrt") {
            std::string dylib_path = rastack::MetalRTLoader::engines_dir() + "/libmetalrt.dylib";
            struct stat st;
            if (stat(dylib_path.c_str(), &st) != 0) {
                engine_message_ = "MetalRT dylib not installed. Run: rcli metalrt install";
                engine_msg_color_ = ftxui::Color::Red;
                return;
            }
        }

        rcli::write_engine_preference(sel.id);

        for (auto& e : engine_entries_) e.is_active = false;
        sel.is_active = true;

        if (sel.id == "metalrt") {
            engine_message_ = "MetalRT Engine selected. Restart to apply.";
            engine_msg_color_ = theme_.info;
        } else {
            engine_message_ = "Switched to llama.cpp. Restart to apply.";
            engine_msg_color_ = ftxui::Color::Green;
        }
    }

    // ====================================================================
    // Voice mode toggle
    // ====================================================================

    void toggle_voice_mode() {
        if (voice_mode_active_) {
            voice_mode_active_ = false;
            if (voice_mode_anim_thread_.joinable()) {
                voice_mode_anim_thread_.join();
            }
            voice_state_ = VoiceState::IDLE;
            add_system_message("Voice mode OFF. Push-to-talk still active (SPACE).");
            screen_->Post(Event::Custom);
            return;
        }

        // Voice mode ON: visual indicator + push-to-talk remains active via SPACE
        voice_mode_active_ = true;
        voice_state_ = VoiceState::IDLE;
        add_system_message("Voice mode ON. Press SPACE to talk. [V] to exit.");

        voice_mode_anim_thread_ = std::thread([this]() {
            while (voice_mode_active_) {
                anim_tick_.fetch_add(1, std::memory_order_relaxed);
                screen_->Post(Event::Custom);
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
        });

        screen_->Post(Event::Custom);
    }

    // Legacy toggle_voice_mode code kept for reference — wake-word version
    void toggle_voice_mode_wake_word_UNUSED() {
        if (voice_mode_active_) {
            rcli_stop_voice_mode(engine_);
            stop_voice_mode_cleanup();
            voice_state_ = VoiceState::IDLE;
            add_system_message("Voice mode OFF.");
            screen_->Post(Event::Custom);
            return;
        }

        rcli_set_transcript_callback(engine_, [](const char* text, int is_final, void* ud) {
            auto* app = static_cast<TuiApp*>(ud);
            if (is_final && text && text[0]) {
                app->add_user_message(text);
                app->voice_state_ = VoiceState::THINKING;
                app->screen_->Post(Event::Custom);
            }
        }, this);

        rcli_set_response_callback(engine_, [](const char* response, void* ud) {
            auto* app = static_cast<TuiApp*>(ud);
            if (response && response[0]) {
                app->add_response(response, "");
                app->screen_->Post(Event::Custom);
            }
        }, this);

        rcli_set_state_callback(engine_, [](int old_state, int new_state, void* ud) {
            auto* app = static_cast<TuiApp*>(ud);
            // Map pipeline states to TUI voice states
            // 0=IDLE, 1=LISTENING, 2=PROCESSING, 3=SPEAKING, 4=INTERRUPTED, 5=BARGE_IN, 6=VOICE_IDLE
            switch (new_state) {
                case 6: // VOICE_IDLE
                    app->voice_state_ = VoiceState::IDLE;
                    break;
                case 1: // LISTENING
                    app->voice_state_ = VoiceState::RECORDING;
                    break;
                case 2: // PROCESSING
                    app->voice_state_ = VoiceState::THINKING;
                    break;
                case 3: // SPEAKING
                    app->voice_state_ = VoiceState::SPEAKING;
                    break;
                case 5: // BARGE_IN
                    app->voice_state_ = VoiceState::RECORDING;
                    app->add_system_message("(interrupted)");
                    break;
            }
            app->screen_->Post(Event::Custom);
        }, this);

        int rc = rcli_start_voice_mode(engine_, "jarvis");
        if (rc == 0) {
            voice_mode_active_ = true;
            voice_state_ = VoiceState::IDLE; // will show voice mode animation
            add_system_message("Voice mode ON. Say \"Jarvis\" to activate. Barge-in enabled. [V] to stop.");

            // Start animation timer for voice mode pulsing
            voice_mode_anim_thread_ = std::thread([this]() {
                while (voice_mode_active_) {
                    anim_tick_.fetch_add(1, std::memory_order_relaxed);
                    screen_->Post(Event::Custom);
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                }
            });
        } else {
            // Clean up callbacks on failure
            rcli_set_transcript_callback(engine_, nullptr, nullptr);
            rcli_set_state_callback(engine_, nullptr, nullptr);
            add_system_message("Failed to start voice mode. Is another audio session running?");
        }
        screen_->Post(Event::Custom);
    }

    void stop_voice_mode_cleanup() {
        voice_mode_active_ = false;
        if (voice_mode_anim_thread_.joinable()) {
            voice_mode_anim_thread_.join();
        }
        // Unregister callbacks
        rcli_set_transcript_callback(engine_, nullptr, nullptr);
        rcli_set_state_callback(engine_, nullptr, nullptr);
        rcli_set_response_callback(engine_, nullptr, nullptr);
    }

    // ====================================================================
    // Personality panel
    // ====================================================================

    void enter_personality_mode() {
        close_all_panels();
        personality_entries_.clear();
        personality_cursor_ = 0;
        personality_message_.clear();

        std::string current = rcli::read_personality_preference();
        if (current.empty()) current = "default";

        for (auto& p : rastack::all_personalities()) {
            personality_entries_.push_back({
                p.key, p.name, p.tagline, (current == p.key)
            });
        }
        personality_mode_ = true;
    }

    void personality_select() {
        if (personality_cursor_ < 0 || personality_cursor_ >= (int)personality_entries_.size()) return;
        auto& sel = personality_entries_[personality_cursor_];

        if (sel.is_active) {
            personality_message_ = sel.name + " is already active.";
            personality_msg_color_ = ftxui::Color::Yellow;
            return;
        }

        rcli_set_personality(engine_, sel.key.c_str());

        for (auto& e : personality_entries_) e.is_active = false;
        sel.is_active = true;

        personality_message_ = "Personality set to " + sel.name + "!";
        personality_msg_color_ = ftxui::Color::Green;
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

        std::string engine_pref = rcli::read_engine_preference();
        bool is_metalrt = (engine_pref == "metalrt");
        std::string selected_model = rcli::read_selected_model_id();

        if (is_metalrt) {
            // ---- MetalRT engine: show MLX models only ----
            { ModelEntry h; h.name = "LLM Models (MetalRT \xC2\xB7 GPU)"; h.is_header = true; models_entries_.push_back(h); }
            for (auto& m : llm_all) {
                if (!m.metalrt_supported) continue;
                ModelEntry e;
                e.name = m.name; e.id = "mrt-" + m.metalrt_id;
                e.modality = "MRT-LLM"; e.size_mb = m.metalrt_size_mb;
                e.installed = rcli::is_metalrt_model_installed(m);
                e.is_active = (m.id == selected_model && e.installed);
                e.is_default = false; e.is_recommended = m.is_recommended;
                e.description = m.description + " [MLX " + m.metalrt_speed_est + "]";
                e.url = m.metalrt_url;
                e.filename = m.metalrt_dir_name;
                e.mrt_tokenizer_url = m.metalrt_tokenizer_url;
                e.mrt_family = m.metalrt_family;
                e.mrt_size = m.metalrt_size;
                e.mrt_name = m.metalrt_name;
                e.is_archive = false;
                models_entries_.push_back(e);
            }

            { ModelEntry h; h.name = "STT (MetalRT \xC2\xB7 GPU)"; h.is_header = true; models_entries_.push_back(h); }
            auto comp = rcli::metalrt_component_models();
            std::string stt_pref = rcli::read_selected_metalrt_stt_id();
            bool stt_active_found = false;
            for (auto& cm : comp) {
                if (cm.component != "stt") continue;
                ModelEntry e;
                e.name = cm.name; e.id = cm.id;
                e.modality = "MRT-STT"; e.size_mb = cm.size_mb;
                e.installed = rcli::is_metalrt_component_installed(cm);
                if (!stt_pref.empty()) {
                    e.is_active = (cm.id == stt_pref && e.installed);
                } else if (e.installed && !stt_active_found) {
                    e.is_active = true;
                    stt_active_found = true;
                } else {
                    e.is_active = false;
                }
                e.is_default = false; e.is_recommended = false;
                bool is_beta = (cm.name.find("Medium") != std::string::npos ||
                                cm.name.find("Small") != std::string::npos);
                e.description = cm.description;
                if (is_beta) e.description += " [GPU beta - not fully optimized yet]";
                e.url = cm.hf_repo;
                e.hf_subdir = cm.hf_subdir;
                e.filename = cm.dir_name;
                e.is_archive = false;
                models_entries_.push_back(e);
            }

            { ModelEntry h; h.name = "TTS (MetalRT \xC2\xB7 GPU)"; h.is_header = true; models_entries_.push_back(h); }
            bool tts_active_found = false;
            for (auto& cm : comp) {
                if (cm.component != "tts") continue;
                ModelEntry e;
                e.name = cm.name; e.id = cm.id;
                e.modality = "MRT-TTS"; e.size_mb = cm.size_mb;
                e.installed = rcli::is_metalrt_component_installed(cm);
                e.is_active = (e.installed && !tts_active_found);
                if (e.is_active) tts_active_found = true;
                e.is_default = false; e.is_recommended = false;
                e.description = cm.description;
                e.url = cm.hf_repo;
                e.hf_subdir = cm.hf_subdir;
                e.filename = cm.dir_name;
                e.is_archive = false;
                models_entries_.push_back(e);
            }
        } else {
            // ---- llama.cpp engine: show GGUF models only ----
            const auto* llm_active = rcli::resolve_active_model(dir, llm_all);
            const auto* stt_active = rcli::resolve_active_stt(dir, stt_all);
            const auto* tts_active = rcli::resolve_active_tts(dir, tts_all);

            { ModelEntry h; h.name = "LLM Models (llama.cpp \xC2\xB7 CPU)"; h.is_header = true; models_entries_.push_back(h); }
            for (auto& m : llm_all) {
                ModelEntry e;
                e.name = m.name; e.id = m.id; e.modality = "LLM"; e.size_mb = m.size_mb;
                e.installed = (access((dir + "/" + m.filename).c_str(), R_OK) == 0);
                e.is_active = (llm_active && llm_active->id == m.id);
                e.is_default = m.is_default; e.is_recommended = m.is_recommended;
                e.description = m.description;
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
                e.is_default = m->is_default; e.is_recommended = m->is_recommended;
                e.description = m->description;
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
                e.description = v.description;
                e.url = v.download_url; e.filename = v.dir_name; e.is_archive = true;
                e.archive_dir = v.archive_dir;
                models_entries_.push_back(e);
            }
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

        bool is_metalrt_entry = (e.modality.substr(0, 4) == "MRT-");

        if (e.installed) {
            if (is_metalrt_entry && e.modality == "MRT-LLM") {
                std::string base_id = e.id.substr(4); // strip "mrt-" prefix → "metalrt-qwen3-4b" etc
                auto models = rcli::all_models();
                for (auto& m : models) {
                    if (m.metalrt_supported && m.metalrt_id == base_id) {
                        rcli::write_selected_model_id(m.id);
                        for (auto& me : models_entries_) {
                            if (me.modality == "MRT-LLM" && !me.is_header) me.is_active = (me.id == e.id);
                        }
                        models_message_ = "Switched to " + e.name + ". Restart RCLI to apply.";
                        models_msg_color_ = theme_.success;
                        break;
                    }
                }
            } else if (is_metalrt_entry && e.modality == "MRT-STT") {
                rcli::write_selected_metalrt_stt_id(e.id);
                for (auto& m : models_entries_) {
                    if (m.modality == "MRT-STT" && !m.is_header) m.is_active = (m.id == e.id);
                }
                models_message_ = "Selected: " + e.name + ". Restart RCLI to apply.";
                models_msg_color_ = theme_.success;
            } else if (is_metalrt_entry) {
                models_message_ = e.name + " is active (auto-used with MetalRT engine).";
                models_msg_color_ = theme_.success;
            } else if (e.modality == "LLM") {
                models_message_ = "Switching to " + e.name + "...";
                models_msg_color_ = theme_.warning;
                std::string id = e.id, nm = e.name;
                std::thread([this, id, nm]() {
                    int rc = rcli_switch_llm(engine_, id.c_str());
                    if (rc == 0) {
                        for (auto& m : models_entries_) {
                            if (m.modality == "LLM" && !m.is_header) m.is_active = (m.id == id);
                        }
                        models_message_ = "Switched to " + nm;
                        models_msg_color_ = theme_.success;
                    } else {
                        models_message_ = "Failed to switch to " + nm;
                        models_msg_color_ = theme_.error;
                    }
                    screen_->Post(Event::Custom);
                }).detach();
            } else {
                if (e.modality == "STT") rcli::write_selected_stt_id(e.id);
                else if (e.modality == "TTS") rcli::write_selected_tts_id(e.id);
                for (auto& m : models_entries_) {
                    if (m.modality == e.modality && !m.is_header) m.is_active = (m.id == e.id);
                }
                models_message_ = "Selected: " + e.name + ". Restart RCLI to apply.";
                models_msg_color_ = theme_.success;
            }
        } else if (is_metalrt_entry) {
            // MetalRT model download (safetensors dir or HF repo)
            if (e.url.empty()) {
                models_message_ = "No download info for " + e.name;
                models_msg_color_ = theme_.error;
                return;
            }
            models_message_ = "Downloading " + e.name + " (" + std::to_string(e.size_mb) + " MB)...";
            models_msg_color_ = theme_.warning;
            int idx = models_cursor_;
            std::string nm = e.name, fname = e.filename, mod = e.modality;
            std::string url = e.url;
            std::string tok_url = e.mrt_tokenizer_url;
            std::string family = e.mrt_family, msize = e.mrt_size, mname = e.mrt_name;
            std::string mrt_base = rcli::metalrt_models_dir();

            std::thread([this, idx, nm, fname, mod, url, tok_url, family, msize, mname, mrt_base]() {
                std::string mrt_dir = mrt_base + "/" + fname;
                int rc;

                if (mod == "MRT-LLM") {
                    std::string cfg_url = url;
                    auto cfp = cfg_url.rfind("model.safetensors");
                    if (cfp != std::string::npos) cfg_url.replace(cfp, 17, "config.json");
                    std::string dl_cmd = "bash -c '"
                        "set -e; mkdir -p \"" + mrt_dir + "\"; "
                        "curl -fL -# -o \"" + mrt_dir + "/model.safetensors\" \"" + url + "\"; "
                        "curl -fL -# -o \"" + mrt_dir + "/tokenizer.json\" \"" + tok_url + "\"; "
                        "curl -fL -# -o \"" + mrt_dir + "/config.json\" \"" + cfg_url + "\"; "
                        "'";
                    rc = system(dl_cmd.c_str());
                } else {
                    std::string hf_base = "https://huggingface.co/" + url + "/resolve/main/";
                    std::string sub = models_entries_[idx].hf_subdir;
                    std::string sp = sub.empty() ? "" : sub + "/";
                    std::string dl_cmd;
                    if (mod == "MRT-TTS") {
                        dl_cmd = "bash -c '"
                            "set -e; mkdir -p \"" + mrt_dir + "/voices\"; "
                            "curl -fL -# -o \"" + mrt_dir + "/config.json\" \"" + hf_base + sp + "config.json\"; "
                            "curl -fL -# -o \"" + mrt_dir + "/kokoro-v1_0.safetensors\" \"" + hf_base + sp + "kokoro-v1_0.safetensors\"; "
                            "for v in af_heart af_alloy af_aoede af_bella af_jessica af_kore af_nicole af_nova af_river af_sarah af_sky "
                            "am_adam am_echo am_eric am_fenrir am_liam am_michael am_onyx am_puck am_santa "
                            "bf_alice bf_emma bf_isabella bf_lily bm_daniel bm_fable bm_george bm_lewis; do "
                            "curl -fL -s -o \"" + mrt_dir + "/voices/${v}.safetensors\" \"" + hf_base + sp + "voices/${v}.safetensors\"; "
                            "done; "
                            "'";
                    } else {
                        dl_cmd = "bash -c '"
                            "set -e; mkdir -p \"" + mrt_dir + "\"; "
                            "curl -fL -# -o \"" + mrt_dir + "/config.json\" \"" + hf_base + sp + "config.json\"; "
                            "curl -fL -# -o \"" + mrt_dir + "/model.safetensors\" \"" + hf_base + sp + "model.safetensors\"; "
                            "curl -fL -# -o \"" + mrt_dir + "/tokenizer.json\" \"" + hf_base + sp + "tokenizer.json\"; "
                            "'";
                    }
                    rc = system(dl_cmd.c_str());
                }

                if (rc == 0 && idx < (int)models_entries_.size()) {
                    models_entries_[idx].installed = true;
                    models_message_ = nm + " downloaded! Restart RCLI to use.";
                    models_msg_color_ = theme_.success;
                } else {
                    models_message_ = "Download failed for " + nm + ". Check connection.";
                    models_msg_color_ = theme_.error;
                }
                screen_->Post(Event::Custom);
            }).detach();
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

            std::string archive_dir_name = e.archive_dir;
            std::thread([this, idx, dir, url, fname, mod, id, nm, archive, archive_dir_name]() {
                int rc;
                if (archive) {
                    rc = system(("curl -sL '" + url + "' | tar xj -C '" + dir + "' 2>/dev/null").c_str());
                    if (rc == 0 && !archive_dir_name.empty() && archive_dir_name != fname) {
                        std::string src = dir + "/" + archive_dir_name;
                        std::string dst = dir + "/" + fname;
                        struct stat st;
                        if (stat(src.c_str(), &st) == 0 && stat(dst.c_str(), &st) != 0)
                            rename(src.c_str(), dst.c_str());
                    }
                } else {
                    rc = system(("curl -sL -o '" + dir + "/" + fname + "' '" + url + "' 2>/dev/null").c_str());
                }
                if (rc == 0 && idx < (int)models_entries_.size()) {
                    models_entries_[idx].installed = true;
                    models_entries_[idx].is_active = true;
                    for (auto& m : models_entries_) {
                        if (m.modality == mod && !m.is_header && m.id != id) m.is_active = false;
                    }
                    if (mod == "LLM") {
                        if (rcli_switch_llm(engine_, id.c_str()) == 0) {
                            models_message_ = "Downloaded & switched to " + nm;
                            models_msg_color_ = theme_.success;
                        } else {
                            rcli::write_selected_model_id(id);
                            models_message_ = "Downloaded " + nm + ". Restart RCLI to apply.";
                            models_msg_color_ = theme_.warning;
                        }
                    } else {
                        if (mod == "STT") rcli::write_selected_stt_id(id);
                        else if (mod == "TTS") rcli::write_selected_tts_id(id);
                        models_message_ = "Downloaded & selected: " + nm + ". Restart RCLI to apply.";
                        models_msg_color_ = theme_.success;
                    }
                } else {
                    models_message_ = "Download failed for " + nm + ". Check connection.";
                    models_msg_color_ = theme_.error;
                }
                screen_->Post(Event::Custom);
            }).detach();
        }
    }

    Element build_engine_panel() {
        Elements rows;
        rows.push_back(text("  \u2501\u2501\u2501 Engine Selection \u2501\u2501\u2501") | ftxui::bold | ftxui::color(theme_.accent));
        rows.push_back(text("  Choose inference backend. Restart RCLI after switching.") | dim);
        rows.push_back(text(""));

        for (int i = 0; i < (int)engine_entries_.size(); i++) {
            auto& e = engine_entries_[i];
            bool selected = (i == engine_cursor_);
            bool is_mrt = (e.id == "metalrt");

            std::string cursor = selected ? " \u25B6 " : "   ";
            std::string active_tag = e.is_active ? "  [active]" : "";

            Element name_el;
            if (is_mrt) {
                name_el = hbox({
                    text(cursor),
                    text(e.name) | ftxui::bold | ftxui::color(theme_.info),
                    text(active_tag) | ftxui::bold | ftxui::color(theme_.success),
                });
            } else {
                name_el = text(cursor + e.name + active_tag);
            }

            if (selected) {
                if (!is_mrt) name_el = name_el | ftxui::bold | ftxui::color(theme_.accent);
                name_el = name_el | inverted;
            } else if (e.is_active && !is_mrt) {
                name_el = name_el | ftxui::color(theme_.success);
            }

            auto desc_el = text("     " + e.description) | dim;

            rows.push_back(name_el);
            rows.push_back(desc_el);

            if (is_mrt) {
                rows.push_back(
                    text("     Fastest inference on Apple Silicon \u00B7 Sub-100ms TTFT")
                    | ftxui::bold | ftxui::color(theme_.success));
            } else {
                rows.push_back(
                    text("     Good for: Maximum model compatibility")
                    | dim);
            }

            if (i < (int)engine_entries_.size() - 1)
                rows.push_back(text(""));
        }

        rows.push_back(text(""));
        rows.push_back(text("  \u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500") | dim);
        rows.push_back(text("  [Enter] Select  [Esc] Back") | dim);

        if (!engine_message_.empty()) {
            rows.push_back(text(""));
            rows.push_back(text("  " + engine_message_) | ftxui::color(engine_msg_color_));
        }

        return vbox(std::move(rows));
    }

    Element build_personality_panel() {
        Elements rows;
        rows.push_back(text("  Personality") | ftxui::bold | ftxui::color(theme_.accent));
        rows.push_back(text("  Choose how RCLI talks to you. Takes effect immediately.") | dim);
        rows.push_back(separator());

        for (int i = 0; i < (int)personality_entries_.size(); i++) {
            auto& e = personality_entries_[i];
            bool selected = (i == personality_cursor_);

            std::string prefix = selected ? " > " : "   ";
            std::string active_tag = e.is_active ? "  [active]" : "";

            auto name_el = text(prefix + e.name + active_tag);
            if (selected) name_el = name_el | ftxui::bold | ftxui::color(theme_.accent);
            else if (e.is_active) name_el = name_el | ftxui::color(ftxui::Color::Green);

            auto desc_el = text("     " + e.tagline) | dim;

            rows.push_back(name_el);
            rows.push_back(desc_el);
            if (i < (int)personality_entries_.size() - 1)
                rows.push_back(text(""));
        }

        rows.push_back(separator());
        rows.push_back(text("  [Enter] Select  [Esc] Back") | dim);

        if (!personality_message_.empty()) {
            rows.push_back(text(""));
            rows.push_back(text("  " + personality_message_) | ftxui::color(personality_msg_color_));
        }

        return vbox(std::move(rows));
    }

    Element build_models_panel_interactive() {
        Elements header;
        header.push_back(text("  Models") | ftxui::bold | ftxui::color(theme_.accent));
        header.push_back(text("  Up/Down navigate, ENTER select/download, ESC close") | dim);
        header.push_back(text(""));

        Elements list_lines;
        for (int i = 0; i < (int)models_entries_.size(); i++) {
            auto& e = models_entries_[i];
            if (e.is_header) {
                list_lines.push_back(text("  --- " + e.name + " ---") |
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
            std::string tag = e.is_recommended ? " [recommended]" : "";
            bool is_stt_beta = (e.modality == "MRT-STT") &&
                (e.name.find("Medium") != std::string::npos ||
                 e.name.find("Small") != std::string::npos);
            std::string beta_tag = is_stt_beta ? " (GPU beta)" : "";
            std::string line = prefix + e.name + tag + beta_tag + "  " + size_str + status;

            Element elem;
            if (is_stt_beta && !selected) {
                elem = hbox({
                    text(prefix + e.name + tag) | (e.is_active ? ftxui::color(theme_.success) : (e.installed ? dim : ftxui::color(theme_.text_muted))),
                    text(" (GPU beta)") | ftxui::color(theme_.warning),
                    text("  " + size_str + status) | (e.is_active ? ftxui::color(theme_.success) : (e.installed ? dim : ftxui::color(theme_.text_muted))),
                });
            } else {
                elem = text(line);
                if (selected) elem = elem | ftxui::bold | ftxui::color(theme_.text_selected) | focus;
                else if (e.is_active) elem = elem | ftxui::color(theme_.success);
                else if (e.installed) elem = elem | dim;
                else elem = elem | ftxui::color(theme_.text_muted);
            }
            list_lines.push_back(elem);
        }

        Elements footer;
        if (models_cursor_ >= 0 && models_cursor_ < (int)models_entries_.size()) {
            auto& sel = models_entries_[models_cursor_];
            if (!sel.is_header && !sel.description.empty()) {
                footer.push_back(text("  " + sel.description) |
                    ftxui::color(theme_.info));
            }
        }
        if (!models_message_.empty()) {
            footer.push_back(text("  " + models_message_) |
                ftxui::bold | ftxui::color(models_msg_color_));
        }

        return vbox({
            vbox(std::move(header)),
            vbox(std::move(list_lines)) | yframe | vscroll_indicator | flex,
            separator() | dim,
            vbox(std::move(footer)),
        });
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
            [this](const rcli::ActionDef& a, const rcli::ActionDef& b) {
                if (a.category != b.category) return a.category < b.category;
                bool a_en = rcli_is_action_enabled(engine_, a.name.c_str()) != 0;
                bool b_en = rcli_is_action_enabled(engine_, b.name.c_str()) != 0;
                if (a_en != b_en) return a_en > b_en;
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
            e.example_voice = d.example_voice;
            e.enabled = rcli_is_action_enabled(engine_, d.name.c_str()) != 0;
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

    void actions_toggle_selected() {
        if (actions_cursor_ < 0 || actions_cursor_ >= (int)actions_entries_.size()) return;
        auto& e = actions_entries_[actions_cursor_];
        if (e.is_header) return;
        e.enabled = !e.enabled;
        rcli_set_action_enabled(engine_, e.name.c_str(), e.enabled ? 1 : 0);
        rcli_save_action_preferences(engine_);
        int enabled_count = 0;
        for (auto& entry : actions_entries_)
            if (!entry.is_header && entry.enabled) enabled_count++;
        if (enabled_count > 20) {
            actions_message_ = "Warning: " + std::to_string(enabled_count) +
                " actions enabled. More than 20 may reduce conversation quality.";
            actions_msg_color_ = theme_.warning;
        } else {
            actions_message_ = e.name + (e.enabled ? " enabled" : " disabled") +
                " for LLM (" + std::to_string(enabled_count) + " total)";
            actions_msg_color_ = e.enabled ? theme_.success : theme_.text_muted;
        }
    }

    Element build_actions_panel_interactive() {
        int action_count = 0, enabled_count = 0;
        for (auto& e : actions_entries_) {
            if (e.is_header) continue;
            action_count++;
            if (e.enabled) enabled_count++;
        }

        Elements header;
        header.push_back(text("  Actions (" + std::to_string(enabled_count) + "/" +
            std::to_string(action_count) + " enabled for LLM)") |
            ftxui::bold | ftxui::color(theme_.accent));
        header.push_back(text("  Up/Down navigate, SPACE toggle, ENTER run, ESC close") | dim);
        if (enabled_count > 20) {
            header.push_back(text("  Warning: >20 actions enabled may reduce quality") |
                ftxui::color(theme_.warning));
        }
        header.push_back(text(""));

        Elements list_lines;
        for (int i = 0; i < (int)actions_entries_.size(); i++) {
            auto& e = actions_entries_[i];
            if (e.is_header) {
                list_lines.push_back(text("  --- " + e.name + " ---") |
                    ftxui::bold | ftxui::color(theme_.info));
                continue;
            }
            bool sel = (i == actions_cursor_);
            std::string check = e.enabled ? "[x] " : "[ ] ";
            std::string prefix = sel ? " > " : "   ";
            std::string line = prefix + check + e.name;
            if (!e.description.empty()) {
                std::string desc = e.description;
                if (desc.size() > 50) desc = desc.substr(0, 47) + "...";
                line += "  " + desc;
            }
            auto elem = text(line);
            if (sel) elem = elem | ftxui::bold | ftxui::color(theme_.text_selected) | focus;
            else if (e.enabled) elem = elem | ftxui::color(theme_.success);
            else elem = elem | dim;
            list_lines.push_back(elem);
        }

        Elements footer;
        if (actions_cursor_ >= 0 && actions_cursor_ < (int)actions_entries_.size()) {
            auto& sel = actions_entries_[actions_cursor_];
            if (!sel.is_header && !sel.example_voice.empty()) {
                footer.push_back(text("  Try saying: \"" + sel.example_voice + "\"") |
                    ftxui::color(theme_.info));
            }
        }
        if (!actions_message_.empty()) {
            footer.push_back(text("  " + actions_message_) |
                ftxui::bold | ftxui::color(actions_msg_color_));
        }

        return vbox({
            vbox(std::move(header)),
            vbox(std::move(list_lines)) | yframe | vscroll_indicator | flex,
            separator() | dim,
            vbox(std::move(footer)),
        });
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

        bench_entries_.push_back({"Run Full Benchmark (STT + LLM + TTS + E2E)", "all"});
        bench_entries_.push_back({"LLM only", "llm"});
        bench_entries_.push_back({"STT only", "stt"});
        bench_entries_.push_back({"TTS only", "tts"});
        if (rag_loaded_) bench_entries_.push_back({"RAG only", "rag"});
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
            out << std::fixed;
            if (rc == 0) {
                bool show_llm = (suite == "llm" || suite == "all");
                bool show_stt = (suite == "stt" || suite == "all");
                bool show_tts = (suite == "tts" || suite == "all");
                bool show_e2e = (suite == "all");

                out << "Benchmark complete!\n";

                int tok = 0; double tps = 0, ttft = 0, total = 0;
                if (show_llm) {
                    rcli_get_last_llm_perf(engine_, &tok, &tps, &ttft, &total);
                    const char* bench_eng = nullptr;
                    rcli_get_last_llm_perf_extended(engine_, nullptr, nullptr, nullptr, nullptr, nullptr, &bench_eng);
                    std::string eng_label = bench_eng ? bench_eng : "LLM";
                    if (tok > 0) {
                        out.precision(1);
                        out << "  " << eng_label << ": " << tok << " tokens, "
                            << tps << " tok/s, TTFT " << ttft << "ms\n";
                    }
                }
                double audio_ms = 0, trans_ms = 0;
                if (show_stt) {
                    rcli_get_last_stt_perf(engine_, &audio_ms, &trans_ms);
                    if (trans_ms > 0) {
                        out.precision(0);
                        out << "  STT: " << trans_ms << "ms transcription\n";
                    }
                }
                int samples = 0; double synth_ms = 0, rtf = 0;
                if (show_tts) {
                    rcli_get_last_tts_perf(engine_, &samples, &synth_ms, &rtf);
                    if (samples > 0) {
                        out.precision(1);
                        out << "  TTS: " << synth_ms << "ms, " << rtf << "x real-time\n";
                    }
                }
                if (show_e2e && trans_ms > 0 && ttft > 0 && synth_ms > 0) {
                    double ttfa_est = trans_ms + ttft + synth_ms;
                    out.precision(0);
                    out << "  E2E (est): " << ttfa_est << "ms"
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
        lines.push_back(text("  Up/Down navigate, ENTER run, ESC close") | dim);
        lines.push_back(text(""));

        if (engine_) {
            const char* llm = rcli_get_llm_model(engine_);
            const char* stt = rcli_get_stt_model(engine_);
            const char* tts = rcli_get_tts_model(engine_);
            lines.push_back(text("  Active models (benchmarked):") | ftxui::bold);
            lines.push_back(hbox({
                text("    LLM: ") | ftxui::bold | ftxui::color(theme_.accent),
                text(llm ? llm : "none"),
            }));
            lines.push_back(hbox({
                text("    STT: ") | ftxui::bold | ftxui::color(theme_.accent),
                text(stt ? stt : "none"),
            }));
            lines.push_back(hbox({
                text("    TTS: ") | ftxui::bold | ftxui::color(theme_.accent),
                text(tts ? tts : "none"),
            }));
            lines.push_back(text(""));
        }

        for (int i = 0; i < (int)bench_entries_.size(); i++) {
            bool sel = (i == bench_cursor_);
            std::string prefix = sel ? " > " : "   ";
            auto elem = text(prefix + bench_entries_[i].name);
            if (sel) elem = elem | ftxui::bold | ftxui::color(theme_.text_selected) | focus;
            else elem = elem | dim;
            lines.push_back(elem);
        }

        if (!bench_output_.empty()) {
            lines.push_back(text(""));
            std::istringstream iss(bench_output_);
            std::string line;
            std::vector<std::string> out_lines;
            while (std::getline(iss, line)) out_lines.push_back(line);
            for (size_t i = 0; i < out_lines.size(); i++) {
                auto c = bench_running_ ? theme_.warning : theme_.success;
                auto elem = text("  " + out_lines[i]) | ftxui::color(c);
                if (i == out_lines.size() - 1) elem = elem | focus;
                lines.push_back(elem);
            }
        }
        return vbox(std::move(lines)) | yframe | vscroll_indicator;
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
        lines.push_back(text("  Up/Down navigate, ENTER select, ESC close") | dim);
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
                elem = elem | ftxui::bold | ftxui::color(theme_.text_selected) | focus;
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
        return vbox(std::move(lines)) | yframe | vscroll_indicator;
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
            add_system_message("  SPACE  Push-to-talk voice recording");
            add_system_message("  ESC    Stop processing (LLM / TTS / STT)");
            add_system_message("  X      Clear conversation + reset context");
            add_system_message("  Q      Quit");
            add_system_message("--- Panels ---");
            add_system_message("  E      Switch inference engine");
            add_system_message("  M      Models (browse / switch / download)");
            add_system_message("  A      Actions (browse / run macOS actions)");
            add_system_message("  B      Benchmarks");
            add_system_message("  P      Personality");
            add_system_message("  R      RAG panel");
            add_system_message("  D      Delete / cleanup models");
            add_system_message("--- Toggles ---");
            add_system_message("  T      Tool call trace (show tool calls & results)");
            add_system_message("  V      Verbose metrics (TTFA, STT, TTS, tok/s)");
            return;
        }

        if (cmd == "models") {
            enter_models_mode();
            return;
        }

        if (cmd == "personality") {
            enter_personality_mode();
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
                    std::string perf = format_llm_perf(true);
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
            fprintf(stderr, "[TRACE] [tui-thread] START input='%.40s'\n", input_copy.c_str());
            auto t_start = std::chrono::steady_clock::now();
            fprintf(stderr, "[TRACE] [tui-thread] calling process_command ...\n");
            const char* response = rag_loaded_
                ? rcli_rag_query(engine_, input_copy.c_str())
                : rcli_process_command(engine_, input_copy.c_str());
            fprintf(stderr, "[TRACE] [tui-thread] process_command returned, response=%s\n",
                     (response && response[0]) ? "non-empty" : "EMPTY");
            if (response && response[0]) {
                std::string perf = format_llm_perf(rag_loaded_.load());

                int pt = 0, cs = 0;
                rcli_get_context_info(engine_, &pt, &cs);
                if (pt > 0) ctx_prompt_tokens_.store(pt, std::memory_order_relaxed);
                if (cs > 0) ctx_size_.store(cs, std::memory_order_relaxed);

                add_response(response, perf);
                check_context_full();
                screen_->Post(Event::Custom);

                if (!args_.no_speak) {
                    auto t_audio = std::chrono::steady_clock::now();
                    double ttfa = std::chrono::duration<double, std::milli>(t_audio - t_start).count();
                    last_ttfa_ms_.store(ttfa, std::memory_order_relaxed);

                    voice_state_ = VoiceState::SPEAKING;
                    screen_->Post(Event::Custom);
                    fprintf(stderr, "[TRACE] [tui-thread] calling rcli_speak ...\n");
                    rcli_speak(engine_, response);
                    fprintf(stderr, "[TRACE] [tui-thread] rcli_speak returned\n");

                    if (verbose_metrics_.load(std::memory_order_relaxed)) {
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
                    }
                    fprintf(stderr, "[TRACE] [tui-thread] entering wait_for_speech ...\n");
                    wait_for_speech();
                    fprintf(stderr, "[TRACE] [tui-thread] wait_for_speech done\n");
                }
            } else {
                add_response("(no response)", "");
            }

            fprintf(stderr, "[TRACE] [tui-thread] setting voice_state_ = IDLE\n");
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
        bool mrt = perf.find("MetalRT") != std::string::npos;
        chat_history_.push_back({"RCLI:", text, perf, false, mrt});
        trim_history();
    }

    void add_system_message(const std::string& text) {
        std::lock_guard<std::mutex> lock(chat_mu_);
        chat_history_.push_back({"*", text, "", false});
        trim_history();
    }

    void check_context_full() {
        int used = ctx_prompt_tokens_.load(std::memory_order_relaxed);
        int total = ctx_size_.load(std::memory_order_relaxed);
        if (total <= 0 || used <= 0) return;
        float pct = (float)used / total;
        int pct_int = (int)(pct * 100);

        // Only notify at threshold crossings, never repeat the same threshold
        int prev = last_ctx_pct_notified_;
        if (pct_int >= 75 && prev < 75) {
            add_system_message(
                "Context at " + std::to_string(pct_int) +
                "%. Press [X] to clear or it will auto-compact soon.");
            if (screen_) screen_->Post(Event::Custom);
            last_ctx_pct_notified_ = 75;
        } else if (pct_int >= 50 && prev < 50) {
            add_system_message(
                "Context at " + std::to_string(pct_int) +
                "%. Older turns will be auto-summarized as needed.");
            if (screen_) screen_->Post(Event::Custom);
            last_ctx_pct_notified_ = 50;
        } else if (pct_int > prev) {
            last_ctx_pct_notified_ = pct_int;
        }
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
    // Checked with relaxed ordering inside the trace callback hot path — when
    // disabled, the callback returns immediately with no allocation or locking,
    // so leaving the callback always registered has effectively zero overhead.
    std::atomic<bool> tool_trace_enabled_{false};
    std::atomic<bool> verbose_metrics_{false};
    std::atomic<double> last_ttfa_ms_{0.0};
    std::atomic<int> ctx_prompt_tokens_{0};
    std::atomic<int> ctx_size_{0};
    int last_ctx_pct_notified_{0};

    // MetalRT dashboard visualization
    rcli::beast::MetalRTDashboard metalrt_viz_;

    std::atomic<bool> metalrt_active_{false};
    std::atomic<float> gpu_utilization_{0.0f};
    std::atomic<int> tokens_per_second_{0};
    std::atomic<bool> streaming_active_{false};
    std::chrono::steady_clock::time_point last_update_time_;

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
        std::string archive_dir, description;
        std::string mrt_tokenizer_url, mrt_family, mrt_size, mrt_name;
        std::string hf_subdir;
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
        std::string name, description, category, params_json, example_voice;
        bool is_header = false;
        bool enabled = true;
    };
    bool actions_mode_ = false;
    int actions_cursor_ = 0;
    std::vector<ActionEntry> actions_entries_;
    std::string actions_message_;
    ftxui::Color actions_msg_color_ = theme_.warning;

    // Engine panel state
    bool engine_mode_ = false;
    int engine_cursor_ = 0;
    struct EngineEntry { std::string id, name, description; bool is_active = false; };
    std::vector<EngineEntry> engine_entries_;
    std::string engine_message_;
    ftxui::Color engine_msg_color_;

    // Voice mode state
    bool voice_mode_active_ = false;
    std::thread voice_mode_anim_thread_;

    // Personality panel state
    bool personality_mode_ = false;
    int personality_cursor_ = 0;
    struct PersonalityEntry { std::string key, name, tagline; bool is_active = false; };
    std::vector<PersonalityEntry> personality_entries_;
    std::string personality_message_;
    ftxui::Color personality_msg_color_;

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
