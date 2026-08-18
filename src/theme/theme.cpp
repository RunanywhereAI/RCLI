#include "theme/theme.h"

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
    const char* colorfgbg = std::getenv("COLORFGBG");
    if (colorfgbg == nullptr) {
        return;
    }
    bool is_light = false;
    if (ParseBackgroundIsLight(colorfgbg, &is_light)) {
        g_mode = is_light ? Mode::Light : Mode::Dark;
    }
}

const Palette& Current() {
    return g_mode == Mode::Light ? kLight : kDark;
}

}  // namespace rcli::theme
