#ifndef RCLI_THEME_THEME_H
#define RCLI_THEME_THEME_H

#include <ftxui/screen/color.hpp>

namespace rcli::theme {

/// Ambient's design tokens, named by intent rather than by colour.
///
/// Feature code asks for `error` or `live`, never for a hex value, so a palette
/// swap is one edit here instead of a search across the app. The values track
/// AmbientColor.swift; that file is the source of truth and any change there
/// should land here too.
///
/// Terminals have no alpha, so Ambient's translucent tokens are pre-composited
/// against their own background: `separator` is its white-at-10% (dark) or
/// black-at-10% (light) flattened onto `background`.
struct Palette {
    ftxui::Color background;
    ftxui::Color surface;
    ftxui::Color raised;
    ftxui::Color inset;

    ftxui::Color text;
    ftxui::Color textDim;
    ftxui::Color textFaint;
    ftxui::Color separator;

    /// Recall Gold, Ambient's identity.
    ftxui::Color accent;
    ftxui::Color accentBright;
    ftxui::Color accentDeep;
    /// Label drawn on top of a filled accent surface.
    ftxui::Color onAccent;

    /// Signal Teal. Active listening only — never decoration.
    ftxui::Color live;

    ftxui::Color success;
    ftxui::Color info;
    ftxui::Color warning;
    /// Muted terracotta. Failures only; a recording state is never red.
    ftxui::Color error;
};

enum class Mode { Dark, Light };

/// Dark is the default identity, matching Ambient, and is what an unrecognised
/// terminal gets.
void SetMode(Mode mode);
Mode CurrentMode();

/// Reads COLORFGBG when the terminal sets it, which is the only widely
/// supported hint at background luminance. Absent or unparseable leaves the
/// mode alone rather than guessing.
void DetectMode();

const Palette& Current();

}  // namespace rcli::theme

#endif  // RCLI_THEME_THEME_H
