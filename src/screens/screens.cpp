#include "screens/screens.h"

#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>

#include <string>
#include <string_view>

#include "theme/theme.h"
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

class Chat final : public ui::Screen {
   public:
    Component Body() override { return body_; }
    std::string_view Title() const override { return "chat"; }
    Element Hints() const override {
        return hbox({Hint("enter", "send"), Hint("ctrl-l", "clear")});
    }

   private:
    Component body_ = Placeholder("No conversation yet", "a model has to be loaded first");
};

class Models final : public ui::Screen {
   public:
    Component Body() override { return body_; }
    std::string_view Title() const override { return "models"; }
    Element Hints() const override {
        return hbox({Hint("/", "search"), Hint("enter", "install"), Hint("d", "remove")});
    }

   private:
    Component body_ = Placeholder("No models installed", "the catalog is not connected yet");
};

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
    Component body_ = ui::List({
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
    screens.push_back(std::make_unique<Chat>());
    screens.push_back(std::make_unique<Models>());
    screens.push_back(std::make_unique<Bench>());
    screens.push_back(std::make_unique<Settings>());
    return screens;
}

}  // namespace rcli::screens
