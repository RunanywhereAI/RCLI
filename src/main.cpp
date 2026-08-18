#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include <string>

namespace {

using namespace ftxui;

Element StatusRow(const std::string& label, const std::string& value, Color value_color) {
    return hbox({
        text(label) | dim,
        filler(),
        text(value) | color(value_color),
    });
}

Element Splash() {
    return vbox({
               hbox({
                   text("rcli") | bold,
                   text(" " RCLI_VERSION) | dim,
               }),
               separatorLight(),
               StatusRow("engine", "not wired", Color::GrayDark),
               StatusRow("models", "none", Color::GrayDark),
               text(""),
               text("press q to quit") | dim,
           }) |
           border | size(WIDTH, LESS_THAN, 60);
}

}  // namespace

int main() {
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
