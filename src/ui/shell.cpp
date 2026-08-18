#include "ui/shell.h"

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/terminal.hpp>

#include <string>
#include <utility>

#include "theme/theme.h"

namespace rcli::ui {
namespace {

using namespace ftxui;

Element Tab(std::string_view title, int index, bool active) {
    const auto& t = theme::Current();
    const std::string number = std::to_string(index + 1);
    if (active) {
        return hbox({
                   text(number) | color(t.onAccent),
                   text(" "),
                   text(std::string(title)) | color(t.onAccent),
               }) |
               bgcolor(t.accent) | bold;
    }
    return hbox({
        text(number) | color(t.textFaint),
        text(" "),
        text(std::string(title)) | color(t.textDim),
    });
}

Element TabStrip(const std::vector<std::unique_ptr<Screen>>& screens, int active) {
    const auto& t = theme::Current();
    Elements tabs;
    tabs.push_back(text("rcli") | bold | color(t.accent));
    tabs.push_back(text(" " RCLI_VERSION "  ") | color(t.textFaint));
    for (std::size_t i = 0; i < screens.size(); ++i) {
        if (i > 0) {
            tabs.push_back(text("  "));
        }
        tabs.push_back(Tab(screens[i]->Title(), static_cast<int>(i), static_cast<int>(i) == active));
    }
    tabs.push_back(filler());
    tabs.push_back(text("tab") | color(t.textFaint));
    tabs.push_back(text(" switch  ") | color(t.textDim));
    tabs.push_back(text("q") | color(t.textFaint));
    tabs.push_back(text(" quit") | color(t.textDim));
    return hbox(std::move(tabs));
}

Element BottomBar(const Screen& screen) {
    const auto& t = theme::Current();
    const auto size = Terminal::Size();
    return hbox({
        screen.Hints(),
        filler(),
        text(std::to_string(size.dimx) + "x" + std::to_string(size.dimy)) | color(t.textFaint),
    });
}

}  // namespace

Component Shell(std::vector<std::unique_ptr<Screen>> screens) {
    struct State {
        std::vector<std::unique_ptr<Screen>> screens;
        int active = 0;
    };
    auto state = std::make_shared<State>();
    state->screens = std::move(screens);

    auto renderer = Renderer([state] {
        const auto& t = theme::Current();
        Screen& current = *state->screens[static_cast<std::size_t>(state->active)];
        return vbox({
                   TabStrip(state->screens, state->active) | bgcolor(t.surface),
                   separator() | color(t.separator),
                   current.Body() | flex,
                   separator() | color(t.separator),
                   BottomBar(current) | bgcolor(t.surface),
               }) |
               bgcolor(t.background) | flex;
    });

    return CatchEvent(renderer, [state](const Event& event) {
        const int count = static_cast<int>(state->screens.size());
        for (int i = 0; i < count && i < 9; ++i) {
            if (event == Event::Character(static_cast<char>('1' + i))) {
                state->active = i;
                return true;
            }
        }
        if (event == Event::Tab) {
            state->active = (state->active + 1) % count;
            return true;
        }
        if (event == Event::TabReverse) {
            state->active = (state->active + count - 1) % count;
            return true;
        }
        // The screen gets a look before the global quit, so a text field can
        // still receive a plain q.
        if (state->screens[static_cast<std::size_t>(state->active)]->OnEvent(event)) {
            return true;
        }
        if (event == Event::Character('q') || event == Event::Escape) {
            ScreenInteractive::Active()->Exit();
            return true;
        }
        return false;
    });
}

}  // namespace rcli::ui
