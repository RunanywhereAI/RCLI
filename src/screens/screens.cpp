#include "screens/screens.h"

#include <ftxui/dom/elements.hpp>

#include <string>
#include <string_view>

#include "theme/theme.h"

// Four placeholder screens. Each is small enough to live here; the first one to
// grow real state moves to its own file behind the same interface.
namespace rcli::screens {
namespace {

using namespace ftxui;

/// An empty screen says what it is for and what will fill it, rather than
/// pretending to be busy. Nothing here is wired to the SDK yet and the copy
/// should not imply otherwise.
Element Placeholder(std::string_view headline, std::string_view detail) {
    const auto& t = theme::Current();
    return vbox({
               filler(),
               hbox({
                   filler(),
                   vbox({
                       text(std::string(headline)) | color(t.text) | center,
                       text(std::string(detail)) | color(t.textFaint) | center,
                   }),
                   filler(),
               }),
               filler(),
           }) |
           flex;
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
    std::string_view Title() const override { return "chat"; }
    Element Body() override {
        return Placeholder("No conversation yet", "a model has to be loaded first");
    }
    Element Hints() const override {
        return hbox({Hint("enter", "send"), Hint("ctrl-l", "clear")});
    }
};

class Models final : public ui::Screen {
   public:
    std::string_view Title() const override { return "models"; }
    Element Body() override {
        return Placeholder("No models installed", "the catalog is not connected yet");
    }
    Element Hints() const override {
        return hbox({Hint("/", "search"), Hint("enter", "install"), Hint("d", "remove")});
    }
};

class Bench final : public ui::Screen {
   public:
    std::string_view Title() const override { return "bench"; }
    Element Body() override {
        return Placeholder("No benchmark runs", "run one to compare models on this machine");
    }
    Element Hints() const override { return hbox({Hint("r", "run"), Hint("x", "export")}); }
};

class Settings final : public ui::Screen {
   public:
    std::string_view Title() const override { return "settings"; }
    Element Body() override {
        const auto& t = theme::Current();
        const bool dark = theme::CurrentMode() == theme::Mode::Dark;
        return vbox({
                   text("appearance") | color(t.textFaint),
                   hbox({
                       text("theme") | color(t.textDim),
                       filler(),
                       text(dark ? "dark" : "light") | color(t.accent),
                   }),
               }) |
               flex;
    }
    Element Hints() const override { return Hint("t", "toggle theme"); }
    bool OnEvent(const Event& event) override {
        if (event == Event::Character('t')) {
            theme::SetMode(theme::CurrentMode() == theme::Mode::Dark ? theme::Mode::Light
                                                                     : theme::Mode::Dark);
            return true;
        }
        return false;
    }
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
