#include "screens/screens.h"

#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>

#include <string>
#include <string_view>

#include "sdk/session.h"
#include "theme/theme.h"
#include "screens/chat.h"
#include "screens/models.h"
#include "ui/list.h"

// Four screens. Each is small enough to live here; the first one to grow real
// state moves to its own file behind the same interface.
namespace rcli::screens {
namespace {

using namespace ftxui;

/// An empty screen says what it is for and what is missing, rather than
/// pretending to be busy. Nothing here is wired to the SDK yet and the copy
/// should not imply otherwise.
Component Placeholder(std::string headline, std::string detail) {
    return Renderer([headline, detail] {
        const auto& t = theme::Current();
        return vbox({
                   filler(),
                   hbox({filler(),
                         vbox({
                             text(headline) | color(t.text) | center,
                             text(detail) | color(t.textFaint) | center,
                         }),
                         filler()}),
                   filler(),
               }) |
               flex;
    });
}

Element Hint(std::string_view key, std::string_view label) {
    const auto& t = theme::Current();
    return hbox({
        text(std::string(key)) | color(t.accent),
        text(" "),
        text(std::string(label)) | color(t.textDim),
        text("   "),
    });
}

class Bench final : public ui::Screen {
   public:
    Component Body() override { return body_; }
    std::string_view Title() const override { return "bench"; }
    Element Hints() const override { return hbox({Hint("r", "run"), Hint("x", "export")}); }

   private:
    Component body_ = Placeholder("No benchmark runs", "run one to compare models here");
};

class Settings final : public ui::Screen {
   public:
    Component Body() override { return body_; }
    std::string_view Title() const override { return "settings"; }
    Element Hints() const override {
        return hbox({Hint("up/down", "move"), Hint("enter", "change")});
    }

   private:
    static void ToggleTheme() {
        theme::SetMode(theme::CurrentMode() == theme::Mode::Dark ? theme::Mode::Light
                                                                 : theme::Mode::Dark);
    }

    // One row for now because there is exactly one setting that does anything.
    // More arrive with the features that need them.
    static std::string BackendSummary() {
        const auto& session = sdk::Session::Instance();
        if (!session.started()) {
            return std::string(session.error().empty() ? "not started" : session.error());
        }
        return std::to_string(session.backends().size()) + " registered";
    }

    Component body_ = ui::List({
        ui::Row{
            .label = "Engines",
            .value = BackendSummary,
            .activate = nullptr,
        },
        ui::Row{
            .label = "Storage",
            .value = [] { return std::string(sdk::Session::Instance().home()); },
            .activate = nullptr,
        },
        ui::Row{
            .label = "Theme",
            .value = [] { return theme::CurrentMode() == theme::Mode::Dark ? "dark" : "light"; },
            .activate = ToggleTheme,
        },
    });
};

}  // namespace

std::vector<std::unique_ptr<ui::Screen>> All() {
    std::vector<std::unique_ptr<ui::Screen>> screens;
    screens.push_back(MakeChat());
    screens.push_back(MakeModels());
    screens.push_back(std::make_unique<Bench>());
    screens.push_back(std::make_unique<Settings>());
    return screens;
}

}  // namespace rcli::screens
