#include <ftxui/component/screen_interactive.hpp>

#include "screens/screens.h"
#include "theme/theme.h"
#include "ui/shell.h"

int main() {
    // Before the screen starts: the detector briefly takes stdin, which the
    // render loop owns from here on.
    rcli::theme::DetectMode();

    auto screen = ftxui::ScreenInteractive::Fullscreen();
    auto shell = rcli::ui::Shell(rcli::screens::All());
    screen.Loop(shell);
    return 0;
}
