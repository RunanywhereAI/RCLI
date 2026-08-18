#include "ui/shell.h"

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/terminal.hpp>

#include <string>
#include <utility>

#include "theme/theme.h"

namespace rcli::ui {
namespace {

using namespace ftxui;

/// One tab. Numbered so the key that selects it is visible on the tab itself.
Element TabLabel(const EntryState& entry) {
    const auto& t = theme::Current();
    auto row = hbox({
        text(std::to_string(entry.index + 1)) | color(entry.active ? t.onAccent : t.textFaint),
        text(" "),
        text(entry.label) | color(entry.active ? t.onAccent : t.textDim),
    });
    if (entry.active) {
        return row | bgcolor(t.accent) | bold;
    }
    // Hovering is worth showing now that the strip is clickable: without it a
    // mouse user has no way to tell the tabs are targets before clicking one.
    return entry.focused ? row | bgcolor(t.raised) : row;
}

Element Chrome(const Screen& screen) {
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
        std::vector<std::string> titles;
        int active = 0;
    };
    auto state = std::make_shared<State>();
    state->screens = std::move(screens);
    for (const auto& screen : state->screens) {
        state->titles.emplace_back(screen->Title());
    }

    // A Menu rather than hand-drawn text: it already handles clicks, hover and
    // arrow keys, and reports the selection through `active`. Writing our own
    // hit-testing against x-ranges would reimplement all of it, worse.
    MenuOption tabs_option = MenuOption::Horizontal();
    tabs_option.entries = &state->titles;
    tabs_option.selected = &state->active;
    tabs_option.entries_option.transform = TabLabel;
    tabs_option.elements_infix = [] { return text("  "); };
    auto tabs = Menu(tabs_option);

    // The bodies are the focusable tree, not the tab strip: Up and Down must
    // always act inside the current screen. Container::Tab keeps every screen's
    // selection alive while showing only the active one, so switching away and
    // back returns to the row you were on.
    Components bodies;
    bodies.reserve(state->screens.size());
    for (const auto& screen : state->screens) {
        bodies.push_back(screen->Body());
    }
    auto pages = Container::Tab(std::move(bodies), &state->active);

    auto renderer = Renderer(pages, [state, tabs, pages] {
        const auto& t = theme::Current();
        const Screen& current = *state->screens[static_cast<std::size_t>(state->active)];
        return vbox({
                   hbox({
                       text("rcli") | bold | color(t.accent),
                       text(" " RCLI_VERSION "  ") | color(t.textFaint),
                       tabs->Render(),
                       filler(),
                       text("tab") | color(t.textFaint),
                       text(" switch  ") | color(t.textDim),
                       text("q") | color(t.textFaint),
                       text(" quit") | color(t.textDim),
                   }) | bgcolor(t.surface),
                   separator() | color(t.separator),
                   pages->Render() | flex,
                   separator() | color(t.separator),
                   Chrome(current) | bgcolor(t.surface),
               }) |
               bgcolor(t.background) | flex;
    });

    return CatchEvent(renderer, [state, tabs, pages](const Event& event) {
        const int count = static_cast<int>(state->screens.size());

        // Clicks land on the tab strip; let it decide before anything else so a
        // press on a tab is never read as a key by the active screen.
        if (event.is_mouse() && tabs->OnEvent(event)) {
            return true;
        }

        const Screen& current = *state->screens[static_cast<std::size_t>(state->active)];
        if (!current.CapturesTyping()) {
            for (int i = 0; i < count && i < 9; ++i) {
                if (event == Event::Character(static_cast<char>('1' + i))) {
                    state->active = i;
                    return true;
                }
            }
            if (event == Event::ArrowRight) {
                state->active = (state->active + 1) % count;
                return true;
            }
            if (event == Event::ArrowLeft) {
                state->active = (state->active + count - 1) % count;
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

        // The active body gets the event before the global quit, so a search
        // field receives a plain q instead of the app exiting under it. The
        // screen's own hook comes next, for keys no widget claimed.
        if (pages->OnEvent(event)) {
            return true;
        }
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
