#include <ftxui/component/screen_interactive.hpp>

#include <cstdio>
#include <cstdlib>

#include "screens/screens.h"
#include "sdk/session.h"
#include "theme/theme.h"
#include "ui/shell.h"

int main() {
    // Before the screen starts: the detector briefly takes stdin, which the
    // render loop owns from here on.
    rcli::theme::DetectMode();

    // Before the screen: bringing the SDK up writes to stderr on failure paths
    // inside libcurl and the engines, which would land on top of the UI.
    rcli::sdk::Session::Instance().Start();

    // The engines log to stderr at INFO regardless of the SDK log level —
    // llama.cpp writes its own, and commons logs generation lifecycle — and
    // stderr is the same terminal the UI is drawing on. Send it somewhere else
    // for the duration of the screen. RCLI_LOG names a file when you want it.
    const char* log_path = std::getenv("RCLI_LOG");
    std::freopen(log_path != nullptr ? log_path : "/dev/null", "a", stderr);

    auto screen = ftxui::ScreenInteractive::Fullscreen();
    auto shell = rcli::ui::Shell(rcli::screens::All());
    screen.Loop(shell);
    return 0;
}
