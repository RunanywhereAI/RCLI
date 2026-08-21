#include "cli/output.h"

#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

namespace rcli::out {
namespace {

struct Rgb {
    int r;
    int g;
    int b;
};

/// AmbientColor.swift, dark mode. The CLI does not follow the terminal's
/// light/dark background the way the old full-screen UI did: it never paints a
/// background, so these are foregrounds over whatever the user already has.
constexpr Rgb kPalette[] = {
    {0xE6, 0xE6, 0xE6},  // Plain
    {0x9A, 0x9A, 0x9A},  // Dim
    {0x6B, 0x6B, 0x6B},  // Faint
    {0xE9, 0xA9, 0x4F},  // Accent — Recall Gold
    {0xF5, 0xC5, 0x7C},  // AccentBright
    {0x46, 0xD7, 0xC8},  // Live — Signal Teal
    {0x58, 0xC2, 0x89},  // Success
    {0x7F, 0xB3, 0xEC},  // Info
    {0xE0, 0xB0, 0x54},  // Warning
    {0xE2, 0x6D, 0x5A},  // Error
};

bool g_color = false;
bool g_truecolor = false;
FILE* g_notice = stderr;
// Whether the notice stream is a terminal. Sampled when the copy is made:
// asking later would ask about /dev/null, and every progress bar would decide
// it had nowhere to draw.
int g_notice_tty = -1;

bool EnvSaysNo() {
    // https://no-color.org — set at all, whatever the value.
    if (std::getenv("NO_COLOR") != nullptr) {
        return true;
    }
    const char* term = std::getenv("TERM");
    return term != nullptr && std::strcmp(term, "dumb") == 0;
}

}  // namespace

void KeepNotices() {
    const int copy = dup(STDERR_FILENO);
    if (copy < 0) {
        return;
    }
    FILE* stream = fdopen(copy, "w");
    if (stream != nullptr) {
        g_notice = stream;
        g_notice_tty = isatty(copy) != 0 ? 1 : 0;
    }
}

namespace {

bool NoticeIsTty() {
    if (g_notice_tty < 0) {
        g_notice_tty = isatty(STDERR_FILENO) != 0 ? 1 : 0;
    }
    return g_notice_tty == 1;
}

}  // namespace

void DetectColor() {
    g_color = isatty(STDOUT_FILENO) != 0 && !EnvSaysNo();
    const char* colorterm = std::getenv("COLORTERM");
    g_truecolor = colorterm != nullptr && (std::strstr(colorterm, "truecolor") != nullptr ||
                                           std::strstr(colorterm, "24bit") != nullptr);
}

void ForceColor(bool on) {
    g_color = on;
}

bool Colored() {
    return g_color;
}

std::string Paint(Ink ink, std::string_view text) {
    if (!g_color) {
        return std::string(text);
    }
    const Rgb& c = kPalette[static_cast<std::size_t>(ink)];
    char prefix[32];
    if (g_truecolor) {
        std::snprintf(prefix, sizeof(prefix), "\033[38;2;%d;%d;%dm", c.r, c.g, c.b);
    } else {
        // The 6x6x6 cube; close enough for a palette that only has to stay
        // distinguishable.
        const int index = 16 + 36 * (c.r * 5 / 255) + 6 * (c.g * 5 / 255) + (c.b * 5 / 255);
        std::snprintf(prefix, sizeof(prefix), "\033[38;5;%dm", index);
    }
    return std::string(prefix) + std::string(text) + "\033[0m";
}

std::string Bold(std::string_view text) {
    return g_color ? "\033[1m" + std::string(text) + "\033[0m" : std::string(text);
}

void Line(std::string_view text) {
    std::fwrite(text.data(), 1, text.size(), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

void Say(Ink ink, std::string_view text) {
    Line(Paint(ink, text));
}

void Status(std::string_view text) {
    const std::string painted = Paint(Ink::Faint, text);
    std::fwrite(painted.data(), 1, painted.size(), g_notice);
    std::fputc('\n', g_notice);
    std::fflush(g_notice);
}

void Error(std::string_view text) {
    const std::string painted = Paint(Ink::Error, text);
    std::fwrite(painted.data(), 1, painted.size(), g_notice);
    std::fputc('\n', g_notice);
    std::fflush(g_notice);
}

std::string HumanSize(std::int64_t bytes) {
    if (bytes <= 0) {
        return "0 B";
    }
    static const char* kUnits[] = {"B", "KB", "MB", "GB", "TB"};
    double value = static_cast<double>(bytes);
    std::size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < sizeof(kUnits) / sizeof(kUnits[0])) {
        value /= 1024.0;
        ++unit;
    }
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), value < 10.0 && unit > 0 ? "%.1f %s" : "%.0f %s", value,
                  kUnits[unit]);
    return buffer;
}

std::string HumanDuration(std::int64_t seconds) {
    char buffer[32];
    if (seconds < 60) {
        std::snprintf(buffer, sizeof(buffer), "%llds", static_cast<long long>(seconds));
    } else {
        std::snprintf(buffer, sizeof(buffer), "%lldm%02llds", static_cast<long long>(seconds / 60),
                      static_cast<long long>(seconds % 60));
    }
    return buffer;
}

Progress::Progress(std::string label)
    : label_(std::move(label)), live_(NoticeIsTty() && !EnvSaysNo()) {}

Progress::~Progress() {
    if (!finished_) {
        Finish("");
    }
}

void Progress::Draw(std::string_view body) {
    if (!live_) {
        return;
    }
    // \r and erase-to-end rather than a cleared line: a redraw that blanks
    // first flickers, and this is redrawn several times a second.
    std::fprintf(g_notice, "\r\033[K%s %.*s", Paint(Ink::Dim, label_).c_str(),
                 static_cast<int>(body.size()), body.data());
    std::fflush(g_notice);
}

void Progress::Update(float fraction, std::string_view detail) {
    constexpr int kCells = 24;
    const int filled = static_cast<int>(fraction * kCells);
    std::string bar;
    for (int i = 0; i < kCells; ++i) {
        bar += i < filled ? "█" : "░";
    }
    char percent[16];
    std::snprintf(percent, sizeof(percent), " %3d%% ", static_cast<int>(fraction * 100.0F));
    Draw(Paint(Ink::Accent, bar) + percent + std::string(detail));
}

void Progress::Tick(std::string_view detail) {
    static constexpr std::string_view kFrames = "|/-\\";
    const char frame = kFrames[static_cast<std::size_t>(frame_++) % kFrames.size()];
    Draw(Paint(Ink::Live, std::string(1, frame)) + " " + std::string(detail));
}

void Progress::Finish(std::string_view detail) {
    finished_ = true;
    if (!live_) {
        if (!detail.empty()) {
            Status(std::string(label_) + " " + std::string(detail));
        }
        return;
    }
    std::fprintf(g_notice, "\r\033[K");
    if (!detail.empty()) {
        std::fprintf(g_notice, "%s %s\n", Paint(Ink::Dim, label_).c_str(),
                     std::string(detail).c_str());
    }
    std::fflush(g_notice);
}

}  // namespace rcli::out
