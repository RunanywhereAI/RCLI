#include "screens/chat.h"

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include <memory>
#include <atomic>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "sdk/llm.h"
#include "theme/theme.h"

// Chat is deliberately NOT a panelled TUI. It reads as a terminal session —
// a prompt, what you typed, what came back — because that is what people
// already know how to use. The other screens are navigable UI; this one is a
// conversation.
namespace rcli::screens {
namespace {

using namespace ftxui;

enum class Line { Prompt, Answer, Notice, Failure };

struct Entry {
    Line kind;
    std::string text;
};

/// Everything a generation callback touches, owned by shared_ptr so a reply
/// still arriving after the screen is gone writes to live memory instead of a
/// destroyed object. `open` tells the worker nobody is listening any more.
struct Conversation {
    std::mutex mutex;
    std::vector<Entry> log;
    std::atomic<bool> open{true};
};

class Chat final : public ui::Screen {
   public:
    Chat() {
        InputOption option;
        option.content = &draft_;
        option.placeholder = "send a message (/? for help)";
        option.multiline = false;
        option.on_enter = [this] { Submit(); };
        option.transform = [](InputState state) {
            const auto& t = theme::Current();
            return hbox({
                text(">>> ") | color(t.accent),
                std::move(state.element) | color(t.text) | flex,
            });
        };
        input_ = Input(option);

        body_ = Renderer(input_, [this] {
            const auto& t = theme::Current();
            Elements lines;
            {
                std::lock_guard<std::mutex> lock(chat_->mutex);
                for (const Entry& entry : chat_->log) {
                    lines.push_back(Draw(entry));
                }
            }
            if (lines.empty()) {
                lines.push_back(text("No model loaded. /load <id> to start, /? for help.") |
                                color(t.textFaint));
            }
            return vbox({
                vbox(std::move(lines)) | vscroll_indicator | yframe | flex,
                input_->Render(),
            });
        });
    }

    ~Chat() override { chat_->open.store(false); }

    Component Body() override { return body_; }
    std::string_view Title() const override { return "chat"; }
    bool CapturesTyping() const override { return true; }

    Element Hints() const override {
        const auto& t = theme::Current();
        const std::string model = llm_.loaded() ? llm_.model_id() : "no model";
        return hbox({
            text(model) | color(llm_.loaded() ? t.accent : t.textFaint),
            text("   "),
            text(llm_.busy() ? "generating" : "ready") | color(llm_.busy() ? t.live : t.textDim),
        });
    }

   private:
    Element Draw(const Entry& entry) const {
        const auto& t = theme::Current();
        switch (entry.kind) {
            case Line::Prompt:
                return hbox({text(">>> ") | color(t.accent), text(entry.text) | color(t.text)});
            case Line::Answer:
                return paragraph(entry.text) | color(t.text);
            case Line::Notice:
                return text(entry.text) | color(t.textFaint);
            case Line::Failure:
                return text(entry.text) | color(t.error);
        }
        return text(entry.text);
    }

    void Append(Line kind, std::string text) {
        std::lock_guard<std::mutex> lock(chat_->mutex);
        chat_->log.push_back({kind, std::move(text)});
    }

    void Notice(std::string text) { Append(Line::Notice, std::move(text)); }

    void Submit() {
        std::string line = draft_;
        draft_.clear();
        if (line.empty()) {
            return;
        }
        if (line.front() == '/') {
            Command(line);
            return;
        }
        if (!llm_.loaded()) {
            Append(Line::Failure, "no model loaded — /load <id>");
            return;
        }
        if (llm_.busy()) {
            Append(Line::Failure, "still generating — ctrl-c to stop");
            return;
        }

        Append(Line::Prompt, line);
        Append(Line::Answer, "");
        // Tokens arrive on the generation thread. Appending under the lock and
        // asking the screen to repaint is the whole handoff; touching the
        // element tree from that thread would race the renderer.
        auto* screen = ScreenInteractive::Active();
        auto chat = chat_;
        llm_.Generate(
            std::move(line),
            [chat, screen](std::string token) {
                if (!chat->open.load()) {
                    return;
                }
                {
                    std::lock_guard<std::mutex> lock(chat->mutex);
                    if (chat->log.empty()) {
                        return;  // cleared mid-reply
                    }
                    chat->log.back().text += token;
                }
                if (screen != nullptr) {
                    screen->PostEvent(Event::Custom);
                }
            },
            [chat, screen](std::string failure) {
                if (!chat->open.load()) {
                    return;
                }
                if (!failure.empty()) {
                    std::lock_guard<std::mutex> lock(chat->mutex);
                    chat->log.push_back({Line::Failure, std::move(failure)});
                }
                if (screen != nullptr) {
                    screen->PostEvent(Event::Custom);
                }
            });
    }

    void Command(const std::string& line) {
        const std::size_t space = line.find(' ');
        const std::string name = line.substr(0, space);
        const std::string argument = space == std::string::npos ? "" : line.substr(space + 1);

        if (name == "/?" || name == "/help") {
            Notice("/load <id>   load a model that is already downloaded");
            Notice("/models      list what is on this machine");
            Notice("/clear       clear this conversation");
            Notice("/bye         quit");
            return;
        }
        if (name == "/models") {
            const std::vector<sdk::LocalModel> models = sdk::LocalModels();
            if (models.empty()) {
                Notice("nothing downloaded yet — the models screen lists the catalog");
                return;
            }
            for (const sdk::LocalModel& model : models) {
                Notice("  " + model.id + "  (" + model.framework + ")");
            }
            return;
        }
        if (name == "/load") {
            Load(argument);
            return;
        }
        if (name == "/clear") {
            std::lock_guard<std::mutex> lock(chat_->mutex);
            chat_->log.clear();
            return;
        }
        if (name == "/bye" || name == "/exit") {
            if (auto* screen = ScreenInteractive::Active()) {
                screen->Exit();
            }
            return;
        }
        Append(Line::Failure, "unknown command " + name + " — /? for help");
    }

    void Load(const std::string& id) {
        if (id.empty()) {
            Append(Line::Failure, "usage: /load <id>");
            return;
        }
        for (const sdk::LocalModel& model : sdk::LocalModels()) {
            if (model.id == id) {
                Notice("loading " + model.id + "...");
                std::string error;
                if (llm_.Load(model, &error)) {
                    Notice("ready");
                } else {
                    Append(Line::Failure, error);
                }
                return;
            }
        }
        Append(Line::Failure, "no local model called " + id + " — /models to see what is here");
    }

    std::string draft_;
    std::shared_ptr<Conversation> chat_ = std::make_shared<Conversation>();
    sdk::Llm llm_;
    Component input_;
    Component body_;
};

}  // namespace

std::unique_ptr<ui::Screen> MakeChat() {
    return std::make_unique<Chat>();
}

}  // namespace rcli::screens
