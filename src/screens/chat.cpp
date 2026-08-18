#include "screens/chat.h"

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/dom/elements.hpp>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "theme/theme.h"

namespace rcli::screens {
namespace {

using namespace ftxui;

struct Message {
    bool from_user;
    std::string text;
};

class Chat final : public ui::Screen {
   public:
    Chat() {
        ButtonOption option;
        option.label = "New chat";
        option.on_click = [this] { StartNewChat(); };
        option.transform = [](const EntryState& state) {
            const auto& t = theme::Current();
            const Color line = state.focused ? t.accent : t.separator;
            return vbox({
                       hbox({filler(),
                             text("+ New chat") | color(state.focused ? t.accent : t.textDim),
                             filler()}),
                   }) |
                   borderStyled(DASHED, line);
        };
        new_chat_ = Button(option);

        // The button is the first row of the same list the messages live in, so
        // Up from the first message lands on it rather than on nothing.
        rows_ = Container::Vertical({new_chat_});
        body_ = Renderer(rows_, [this] {
            return vbox({rows_->Render(), Transcript()}) | vscroll_indicator | yframe | flex;
        });
    }

    Component Body() override { return body_; }
    std::string_view Title() const override { return "chat"; }

    Element Hints() const override {
        const auto& t = theme::Current();
        return hbox({
            text(std::to_string(messages_.size())) | color(t.accent),
            text(messages_.size() == 1 ? " message   " : " messages   ") | color(t.textDim),
            text("enter") | color(t.accent),
            text(" new chat") | color(t.textDim),
        });
    }

   private:
    void StartNewChat() { messages_.clear(); }

    Element Bubble(const Message& message) const {
        const auto& t = theme::Current();
        const std::string who = message.from_user ? "you" : "model";
        return vbox({
                   text(who) | color(message.from_user ? t.accent : t.live),
                   paragraph(message.text) | color(t.text),
                   text(""),
               }) |
               flex;
    }

    Element Transcript() const {
        const auto& t = theme::Current();
        if (messages_.empty()) {
            // No model is loaded and nothing can be sent yet, so the empty state
            // says that instead of showing a prompt that would do nothing.
            return vbox({
                text(""),
                hbox({filler(), text("No messages yet") | color(t.textFaint), filler()}),
                hbox({filler(), text("a model has to be loaded first") | color(t.textFaint),
                      filler()}),
            });
        }
        Elements bubbles;
        bubbles.push_back(text(""));
        for (const Message& message : messages_) {
            bubbles.push_back(Bubble(message));
        }
        return vbox(std::move(bubbles));
    }

    std::vector<Message> messages_;
    Component new_chat_;
    Component rows_;
    Component body_;
};

}  // namespace

std::unique_ptr<ui::Screen> MakeChat() {
    return std::make_unique<Chat>();
}

}  // namespace rcli::screens
