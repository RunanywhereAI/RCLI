#ifndef RCLI_UI_SCREEN_H
#define RCLI_UI_SCREEN_H

#include <ftxui/component/component_base.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>

#include <string_view>

namespace rcli::ui {

/// One destination in the app: chat, models, benchmarks, settings.
///
/// A screen owns the two regions that change with it — the body and the hints
/// in the bottom bar — and nothing else. The shell draws the frame, the tab
/// strip and the global keys, so a new screen is one subclass and one entry in
/// the screen list, with no chrome to reimplement.
class Screen {
   public:
    virtual ~Screen() = default;

    /// Shown in the tab strip. Its position in the list is its number key.
    virtual std::string_view Title() const = 0;

    /// Built once and kept, because it holds the screen's focus and selection.
    /// A screen with nothing to navigate returns a Renderer over static
    /// elements; Up and Down then do nothing, which is the honest result.
    virtual ftxui::Component Body() = 0;

    /// Left side of the bottom bar: what this screen can do right now. The
    /// shell owns the right side, which stays the same everywhere.
    virtual ftxui::Element Hints() const = 0;

    /// True when the screen has a text field that should receive ordinary
    /// characters. The shell then stops claiming digits and Left/Right for tab
    /// switching, which would otherwise make a search box impossible to type
    /// "2" into. Tab and Shift+Tab keep working everywhere.
    virtual bool CapturesTyping() const { return false; }

    /// Return true to consume the event. Global keys are handled by the shell
    /// first, so a screen never has to care about tab switching or quitting.
    virtual bool OnEvent(const ftxui::Event& event) {
        (void)event;
        return false;
    }
};

}  // namespace rcli::ui

#endif  // RCLI_UI_SCREEN_H
