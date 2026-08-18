#include "theme/theme.h"

#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

#include <cmath>
#include <cstdlib>
#include <string>
#include <string_view>

namespace rcli::theme {
namespace {

using ftxui::Color;

// Not constexpr: ftxui::Color is not a literal type.
const Palette kDark{
    .background = Color::RGB(0x0B, 0x0E, 0x16),
    .surface = Color::RGB(0x14, 0x19, 0x26),
    .raised = Color::RGB(0x1C, 0x22, 0x33),
    .inset = Color::RGB(0x0F, 0x13, 0x1E),

    .text = Color::RGB(0xF0, 0xF2, 0xF7),
    .textDim = Color::RGB(0xA6, 0xAD, 0xBD),
    .textFaint = Color::RGB(0x6C, 0x74, 0x84),
    .separator = Color::RGB(0x23, 0x26, 0x2D),

    .accent = Color::RGB(0xE9, 0xA9, 0x4F),
    .accentBright = Color::RGB(0xFF, 0xCE, 0x8A),
    .accentDeep = Color::RGB(0xB5, 0x76, 0x2A),
    .onAccent = Color::RGB(0x1B, 0x1F, 0x29),

    .live = Color::RGB(0x46, 0xD7, 0xC8),

    .success = Color::RGB(0x58, 0xC2, 0x89),
    .info = Color::RGB(0x7F, 0xB3, 0xEC),
    .warning = Color::RGB(0xE0, 0xB5, 0x4F),
    .error = Color::RGB(0xE2, 0x6D, 0x5A),
};

// Not constexpr: ftxui::Color is not a literal type.
const Palette kLight{
    .background = Color::RGB(0xF6, 0xF5, 0xF1),
    .surface = Color::RGB(0xFF, 0xFF, 0xFF),
    .raised = Color::RGB(0xF2, 0xF0, 0xE9),
    .inset = Color::RGB(0xED, 0xEB, 0xE4),

    .text = Color::RGB(0x1B, 0x1F, 0x29),
    .textDim = Color::RGB(0x55, 0x5C, 0x6B),
    .textFaint = Color::RGB(0x87, 0x8E, 0x9C),
    .separator = Color::RGB(0xDD, 0xDC, 0xD9),

    // Light-mode accent, live and semantics are darkened in Ambient so body
    // text clears WCAG AA on warm paper; the fixed tokens stay shared.
    .accent = Color::RGB(0x96, 0x62, 0x1A),
    .accentBright = Color::RGB(0xFF, 0xCE, 0x8A),
    .accentDeep = Color::RGB(0xB5, 0x76, 0x2A),
    .onAccent = Color::RGB(0x1B, 0x1F, 0x29),

    .live = Color::RGB(0x0B, 0x71, 0x66),

    .success = Color::RGB(0x2E, 0x8F, 0x5B),
    .info = Color::RGB(0x2D, 0x6B, 0xAA),
    .warning = Color::RGB(0x8A, 0x6A, 0x10),
    .error = Color::RGB(0xC2, 0x4E, 0x3A),
};

Mode g_mode = Mode::Dark;

/// One sRGB channel, linearised for the WCAG relative-luminance formula.
double Linearise(double channel) {
    return channel <= 0.04045 ? channel / 12.92 : std::pow((channel + 0.055) / 1.055, 2.4);
}

/// Parses one `RRRR` group of an `rgb:` reply into 0..1. Terminals answer with
/// anything from one to four hex digits per channel, so the width scales the
/// divisor rather than assuming 16-bit.
bool ParseChannel(std::string_view digits, double* out) {
    if (digits.empty() || digits.size() > 4) {
        return false;
    }
    unsigned value = 0;
    for (const char c : digits) {
        const int nibble = c >= '0' && c <= '9'   ? c - '0'
                           : c >= 'a' && c <= 'f' ? c - 'a' + 10
                           : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                                  : -1;
        if (nibble < 0) {
            return false;
        }
        value = value * 16 + static_cast<unsigned>(nibble);
    }
    *out = static_cast<double>(value) / ((1u << (4 * digits.size())) - 1);
    return true;
}

/// Reply shape: `ESC ] 11 ; rgb:RRRR/GGGG/BBBB` closed by ST or BEL.
bool ParseBackgroundLuminance(std::string_view reply, double* out_luminance) {
    const std::size_t rgb = reply.find("rgb:");
    if (rgb == std::string_view::npos) {
        return false;
    }
    std::string_view rest = reply.substr(rgb + 4);
    double channels[3] = {};
    for (double& channel : channels) {
        const std::size_t end = rest.find_first_not_of("0123456789abcdefABCDEF");
        if (!ParseChannel(rest.substr(0, end), &channel)) {
            return false;
        }
        rest = end == std::string_view::npos ? std::string_view{} : rest.substr(end + 1);
    }
    *out_luminance = 0.2126 * Linearise(channels[0]) + 0.7152 * Linearise(channels[1]) +
                     0.0722 * Linearise(channels[2]);
    return true;
}

/// Ask the terminal what its background is and wait briefly for the answer.
///
/// Terminals that do not implement OSC 11 ignore it silently and we time out,
/// so this costs an unsupported terminal one short pause at startup and never
/// prints stray bytes. stdin has to be raw for the reply not to be swallowed by
/// line discipline, and the original attributes are always restored.
bool QueryBackgroundLuminance(double* out_luminance) {
    if (isatty(STDIN_FILENO) == 0 || isatty(STDOUT_FILENO) == 0) {
        return false;
    }
    termios original{};
    if (tcgetattr(STDIN_FILENO, &original) != 0) {
        return false;
    }
    termios raw = original;
    raw.c_lflag &= ~static_cast<tcflag_t>(ICANON | ECHO);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) {
        return false;
    }

    static constexpr std::string_view kQuery = "\033]11;?\033\\";
    std::string reply;
    if (write(STDOUT_FILENO, kQuery.data(), kQuery.size()) ==
        static_cast<ssize_t>(kQuery.size())) {
        // 150 ms total, in slices, so a terminal that answers quickly is not
        // made to wait for the whole budget.
        for (int elapsed_ms = 0; elapsed_ms < 150 && reply.find('\\') == std::string::npos &&
                                 reply.find('\a') == std::string::npos;
             elapsed_ms += 15) {
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(STDIN_FILENO, &fds);
            timeval slice{.tv_sec = 0, .tv_usec = 15 * 1000};
            if (select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &slice) <= 0) {
                continue;
            }
            char chunk[64];
            const ssize_t n = read(STDIN_FILENO, chunk, sizeof(chunk));
            if (n <= 0) {
                break;
            }
            reply.append(chunk, static_cast<std::size_t>(n));
        }
    }

    tcsetattr(STDIN_FILENO, TCSANOW, &original);
    return ParseBackgroundLuminance(reply, out_luminance);
}

/// COLORFGBG is "<fg>;<bg>" or "<fg>;<extra>;<bg>". The background index is the
/// last field: 0-6 and 8 are the dark half of the ANSI palette, 7 and 9-15 the
/// light half.
bool ParseBackgroundIsLight(std::string_view value, bool* out_is_light) {
    const std::size_t last = value.find_last_of(';');
    if (last == std::string_view::npos || last + 1 >= value.size()) {
        return false;
    }
    int index = 0;
    for (const char c : value.substr(last + 1)) {
        if (c < '0' || c > '9') {
            return false;
        }
        index = index * 10 + (c - '0');
    }
    if (index > 15) {
        return false;
    }
    *out_is_light = index == 7 || index >= 9;
    return true;
}

}  // namespace

void SetMode(Mode mode) {
    g_mode = mode;
}

Mode CurrentMode() {
    return g_mode;
}

void DetectMode() {
    if (const char* override_mode = std::getenv("RCLI_THEME")) {
        const std::string_view value(override_mode);
        if (value == "dark") {
            g_mode = Mode::Dark;
            return;
        }
        if (value == "light") {
            g_mode = Mode::Light;
            return;
        }
    }

    // Halfway up the luminance range. A background lighter than mid-grey wants
    // the paper palette; anything darker keeps Ambient's default identity.
    double luminance = 0.0;
    if (QueryBackgroundLuminance(&luminance)) {
        g_mode = luminance > 0.5 ? Mode::Light : Mode::Dark;
        return;
    }

    if (const char* colorfgbg = std::getenv("COLORFGBG")) {
        bool is_light = false;
        if (ParseBackgroundIsLight(colorfgbg, &is_light)) {
            g_mode = is_light ? Mode::Light : Mode::Dark;
        }
    }
}

const Palette& Current() {
    return g_mode == Mode::Light ? kLight : kDark;
}

}  // namespace rcli::theme
