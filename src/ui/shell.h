#ifndef RCLI_UI_SHELL_H
#define RCLI_UI_SHELL_H

#include <ftxui/component/component_base.hpp>

#include <memory>
#include <vector>

#include "ui/screen.h"

namespace rcli::ui {

/// The application frame: tab strip on top, active screen's body in the
/// middle, bottom bar underneath. Owns the screens and the keys that move
/// between them.
///
/// Number keys select a screen by position, Tab and Shift+Tab step through
/// them, q or Esc exits. A screen sees an event only after the shell has
/// declined it.
ftxui::Component Shell(std::vector<std::unique_ptr<Screen>> screens);

}  // namespace rcli::ui

#endif  // RCLI_UI_SHELL_H
