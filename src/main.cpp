#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include <string>

#include "theme/theme.h"

namespace {

using namespace ftxui;

Element Row(const std::string& label, const std::string& value, Color value_color) {
    const auto& t = rcli::theme::Current();
    return hbox({
        text(label) | color(t.textDim),
        filler(),
        text(value) | color(value_color),
    });
}

Element Header() {
    const auto& t = rcli::theme::Current();
    return hbox({
        text("rcli") | bold | color(t.accent),
        text(" " RCLI_VERSION) | color(t.textFaint),
        filler(),
        text("idle") | color(t.live),
    });
}

Element Splash() {
    const auto& t = rcli::theme::Current();
    return vbox({
               Header(),
               separator() | color(t.separator),
               Row("engine", "ready", t.success),
               Row("models", "0 local", t.info),
               Row("microphone", "not granted", t.error),
               text(""),
               text("press q to quit") | color(t.textFaint),
           }) |
           border | color(t.separator) | bgcolor(t.background);
}

}  // namespace

int main() {
    rcli::theme::DetectMode();

    auto screen = ftxui::ScreenInteractive::TerminalOutput();
    auto view = ftxui::Renderer([] { return Splash(); });

    auto app = ftxui::CatchEvent(view, [&](const ftxui::Event& event) {
        if (event == ftxui::Event::Character('q') || event == ftxui::Event::Escape) {
            screen.Exit();
            return true;
        }
        return false;
    });

    screen.Loop(app);
    return 0;
}
