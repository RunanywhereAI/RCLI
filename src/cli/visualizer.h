#pragma once

#include <cstdio>
#include <cmath>
#include <string>
#include <algorithm>
#include <unistd.h>
#include <fcntl.h>

namespace visualizer {

// Output stream for visualizer drawing — defaults to stderr but can be
// replaced with /dev/tty so that pipeline logs redirected to /dev/null
// don't corrupt the visual output.
static FILE* vis_out = stderr;

static inline void set_output(FILE* f) { vis_out = f; }

static inline FILE* open_tty() {
    FILE* f = fopen("/dev/tty", "w");
    return f ? f : stderr;
}

// Redirect stderr to /dev/null, return saved fd (to restore later).
static inline int suppress_stderr() {
    int saved = dup(STDERR_FILENO);
    int devnull = open("/dev/null", O_WRONLY);
    if (devnull >= 0) { dup2(devnull, STDERR_FILENO); close(devnull); }
    return saved;
}

// Restore stderr from saved fd.
static inline void restore_stderr(int saved_fd) {
    if (saved_fd >= 0) { dup2(saved_fd, STDERR_FILENO); close(saved_fd); }
}

enum class DogState { IDLE, LISTENING, SPEAKING, THINKING };

// ANSI helpers
static constexpr const char* ESC_HIDE_CURSOR  = "\033[?25l";
static constexpr const char* ESC_SHOW_CURSOR  = "\033[?25h";
static constexpr const char* ESC_CLEAR_LINE   = "\033[2K";
static constexpr const char* ESC_RESET        = "\033[0m";
static constexpr const char* ESC_BOLD         = "\033[1m";
static constexpr const char* ESC_DIM          = "\033[2m";
static constexpr const char* ESC_ORANGE       = "\033[38;5;208m";
static constexpr const char* ESC_CYAN         = "\033[36m";
static constexpr const char* ESC_YELLOW       = "\033[33m";
static constexpr const char* ESC_GREEN        = "\033[32m";
static constexpr const char* ESC_MAGENTA      = "\033[35m";

static constexpr int VIS_HEIGHT = 8;

static inline void move_up(int n) { fprintf(vis_out, "\033[%dA", n); }
static inline void move_to_col(int c) { fprintf(vis_out, "\033[%dG", c); }

struct Frame {
    const char* line1;
    const char* line2;
    const char* line3;
    const char* line4;
    const char* label;
    const char* color;
};

static const Frame FRAMES_IDLE[] = {
    { "  /^ ^\\      \xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7",
      " ( o.o )  < Hey! Press SPACE to talk",
      "  > ^ <      \xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7",
      "   |_|   ",
      "\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80 RCLI \xe2\x94\x80\xe2\x94\x80\xe2\x94\x80",
      ESC_DIM },
    { "  /^ ^\\      \xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7",
      " ( o.o )  < Hey! Press SPACE to talk",
      "  > ^ <      \xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7\xc2\xb7",
      "  _|_|_  ",
      "\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80 RCLI \xe2\x94\x80\xe2\x94\x80\xe2\x94\x80",
      ESC_DIM },
};

static const Frame FRAMES_LISTENING[] = {
    { "  /^ ^\\   ",
      " ( o.o ) ))",
      "  > ^ <   ",
      "   |_|    ",
      "Listening... speak now! Press ENTER when done.",
      ESC_CYAN },
    { "  /^ ^\\    ",
      " ( O.O ) )))",
      "  > ^ <    ",
      "   |_|     ",
      "Listening... speak now! Press ENTER when done.",
      ESC_CYAN },
    { "  /^ ^\\  ",
      " ( o.o ) )",
      "  > ^ <  ",
      "   |_|   ",
      "Listening... speak now! Press ENTER when done.",
      ESC_CYAN },
};

static const Frame FRAMES_SPEAKING[] = {
    { "  /^ ^\\  ",
      " ( >o< ) ",
      "  > ^ <  ",
      "   |_|   ",
      "Speaking...",
      ESC_GREEN },
    { "  /^ ^\\  ",
      " ( >O< ) ",
      "  > ^ <  ",
      "  _|_|_  ",
      "Speaking...",
      ESC_GREEN },
};

static const Frame FRAMES_THINKING[] = {
    { "  /^ ^\\  ",
      " ( -.- ) ",
      "  > ^ <  ",
      "   |_|   ",
      "Thinking...",
      ESC_YELLOW },
    { "  /^ ^\\  ",
      " ( -.o ) ",
      "  > ^ <  ",
      "   |_|   ",
      "Thinking...",
      ESC_YELLOW },
};

static inline const Frame& get_frame(DogState state, int tick) {
    switch (state) {
        case DogState::IDLE:
            return FRAMES_IDLE[tick % 2];
        case DogState::LISTENING:
            return FRAMES_LISTENING[tick % 3];
        case DogState::SPEAKING:
            return FRAMES_SPEAKING[tick % 2];
        case DogState::THINKING:
            return FRAMES_THINKING[tick % 2];
    }
    return FRAMES_IDLE[0];
}

// Build a waveform bar string from audio_level (0.0 – 1.0 scale).
// Uses Unicode block characters for a smooth meter look.
static inline std::string waveform_bar(float audio_level, int width = 24) {
    // Clamp and apply a mild log curve for perceptual scaling
    float level = std::min(1.0f, std::max(0.0f, audio_level));
    level = std::sqrtf(level); // perceptual scaling
    int filled = static_cast<int>(level * width);

    static const char* blocks[] = { " ", "\xe2\x96\x8f", "\xe2\x96\x8e", "\xe2\x96\x8d",
                                    "\xe2\x96\x8c", "\xe2\x96\x8b", "\xe2\x96\x8a",
                                    "\xe2\x96\x89", "\xe2\x96\x88" };
    std::string bar;
    for (int i = 0; i < width; ++i) {
        if (i < filled)
            bar += blocks[8]; // full block
        else if (i == filled) {
            float frac = (level * width) - filled;
            int idx = static_cast<int>(frac * 8);
            bar += blocks[std::min(8, std::max(0, idx))];
        } else {
            bar += "\xc2\xb7"; // middle dot
        }
    }
    return bar;
}

// Draw the full visualizer (dog + waveform + label). Call at ~10Hz.
// This draws VIS_HEIGHT lines. On subsequent calls, it first moves up
// to overwrite the previous frame.
static inline void draw(DogState state, float audio_level, int tick, bool first_draw = false) {
    if (!first_draw) {
        move_up(VIS_HEIGHT);
    }

    const Frame& f = get_frame(state, tick);
    const char* clr = f.color;

    // Normalize audio_level: raw RMS from mic is usually 0.0-0.3 range
    float norm = std::min(1.0f, audio_level * 5.0f);

    fprintf(vis_out, "%s%s\r", ESC_CLEAR_LINE, "");
    fprintf(vis_out, "  %s%s%s%s\n", ESC_BOLD, clr, f.line1, ESC_RESET);
    fprintf(vis_out, "%s", ESC_CLEAR_LINE);
    fprintf(vis_out, "  %s%s%s%s\n", ESC_BOLD, clr, f.line2, ESC_RESET);
    fprintf(vis_out, "%s", ESC_CLEAR_LINE);
    fprintf(vis_out, "  %s%s%s%s\n", ESC_BOLD, clr, f.line3, ESC_RESET);
    fprintf(vis_out, "%s", ESC_CLEAR_LINE);
    fprintf(vis_out, "  %s%s%s%s\n", ESC_BOLD, clr, f.line4, ESC_RESET);
    fprintf(vis_out, "%s", ESC_CLEAR_LINE);

    if (state == DogState::LISTENING || state == DogState::SPEAKING) {
        std::string bar = waveform_bar(norm);
        fprintf(vis_out, "  %s[%s]%s\n", clr, bar.c_str(), ESC_RESET);
    } else {
        fprintf(vis_out, "\n");
    }

    fprintf(vis_out, "%s", ESC_CLEAR_LINE);
    fprintf(vis_out, "  %s%s%s%s\n", ESC_DIM, clr, f.label, ESC_RESET);

    fprintf(vis_out, "%s", ESC_CLEAR_LINE);
    if (state == DogState::IDLE) {
        fprintf(vis_out, "\n");
    } else {
        fprintf(vis_out, "  %s%s\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80 RCLI \xe2\x94\x80\xe2\x94\x80\xe2\x94\x80%s\n", ESC_BOLD, ESC_ORANGE, ESC_RESET);
    }

    fprintf(vis_out, "%s", ESC_CLEAR_LINE);
    fprintf(vis_out, "\n");
    fflush(vis_out);
}

static inline void begin() {
    fprintf(vis_out, "%s", ESC_HIDE_CURSOR);
    fflush(vis_out);
}

static inline void end() {
    fprintf(vis_out, "%s", ESC_SHOW_CURSOR);
    fflush(vis_out);
}

// Clear the visualizer area and restore cursor
static inline void clear() {
    move_up(VIS_HEIGHT);
    for (int i = 0; i < VIS_HEIGHT; ++i)
        fprintf(vis_out, "%s\n", ESC_CLEAR_LINE);
    move_up(VIS_HEIGHT);
    end();
}

} // namespace visualizer
