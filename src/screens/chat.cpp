#include "screens/chat.h"

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "catalog/catalog.h"
#include "chat/complete.h"
#include "chat/transcript.h"
#include "sdk/install.h"
#include "sdk/llm.h"
#include "settings/settings.h"
#include "theme/theme.h"
#include "tools/shell.h"

// Chat reads as a terminal session — a prompt, what you typed, what came back.
// The other screens are navigable UI; this one is a conversation.
namespace rcli::screens {
namespace {

using namespace ftxui;
using chat::Line;

class Chat final : public ui::Screen {
   public:
    Chat() {
        InputOption option;
        option.content = &draft_;
        option.placeholder = "send a message (/? for help)";
        option.multiline = false;
        option.on_change = [this] { suggestions_ = chat::Suggest(draft_); highlight_ = 0; };
        option.on_enter = [this] { Submit(); };
        option.transform = [](InputState state) {
            const auto& t = theme::Current();
            return hbox({
                text(">>> ") | color(t.accent),
                std::move(state.element) | color(t.text) | flex,
            });
        };
        input_ = Input(option);

        // Thinking blocks are the only focusable things in the transcript, so
        // Up and Down step between them and Enter folds one open.
        thinking_ = Container::Vertical({});
        // Input FIRST in the focus order even though it renders last. A
        // Container::Vertical hands focus to its first child, and while there
        // are no thought blocks that child is an empty container which accepts
        // nothing — so every keystroke vanished. Visual order comes from
        // Render(), not from this list.
        auto layout = Container::Vertical({input_, thinking_});

        body_ = Renderer(layout, [this] { return Render(); });
    }

    ~Chat() override { log_->open.store(false); }

    Component Body() override { return body_; }
    std::string_view Title() const override { return "chat"; }
    bool CapturesTyping() const override { return true; }

    Element Hints() const override {
        const auto& t = theme::Current();
        return hbox({
            text(llm_.loaded() ? llm_.model_id() : "no model") |
                color(llm_.loaded() ? t.accent : t.textFaint),
            text("   "),
            text(llm_.busy() ? "generating" : "ready") | color(llm_.busy() ? t.live : t.textDim),
            text("   "),
            text("tab") | color(t.accent),
            text(" complete") | color(t.textDim),
        });
    }

    bool OnEvent(const Event& event) override {
        if (!suggestions_.empty()) {
            if (event == Event::ArrowDown) {
                highlight_ = (highlight_ + 1) % static_cast<int>(suggestions_.size());
                return true;
            }
            if (event == Event::ArrowUp) {
                highlight_ = (highlight_ + static_cast<int>(suggestions_.size()) - 1) %
                             static_cast<int>(suggestions_.size());
                return true;
            }
            if (event == Event::Tab) {
                Accept();
                return true;
            }
            if (event == Event::Escape) {
                suggestions_.clear();
                return true;
            }
        }
        return false;
    }

   private:
    /// Tab takes the highlighted suggestion. With several still matching and no
    /// selection moved yet, it fills in the shared prefix instead — the shell
    /// behaviour people already expect.
    void Accept() {
        const std::string shared = chat::CommonPrefix(suggestions_);
        const chat::Completion& choice =
            suggestions_[static_cast<std::size_t>(highlight_)];
        if (highlight_ == 0 && suggestions_.size() > 1 && shared.size() > LastWord().size()) {
            draft_ = choice.replacement.substr(0, choice.replacement.find(shared) + shared.size());
        } else {
            draft_ = choice.replacement;
        }
        suggestions_ = chat::Suggest(draft_);
        highlight_ = 0;
    }

    std::string LastWord() const {
        const std::size_t space = draft_.find_last_of(' ');
        return space == std::string::npos ? draft_ : draft_.substr(space + 1);
    }

    Element DrawEntry(const chat::Entry& entry) const {
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
            case Line::Thinking:
                break;
        }
        return text(entry.text);
    }

    /// Collapsed thinking is one dim line with a word count, because the length
    /// is the only part of a reasoning block worth seeing at a glance.
    static Element DrawThinking(const chat::Entry& entry, bool focused) {
        const auto& t = theme::Current();
        std::size_t words = 0;
        bool inside = false;
        for (const char c : entry.text) {
            const bool space = c == ' ' || c == '\n' || c == '\t';
            if (!space && !inside) {
                ++words;
            }
            inside = !space;
        }
        const Color line = focused ? t.accent : t.textFaint;
        Element header = hbox({
            text(entry.expanded ? "▾ " : "▸ ") | color(line),
            text("thought") | color(line),
            text("  " + std::to_string(words) + " words") | color(t.textFaint),
        });
        if (!entry.expanded) {
            return header;
        }
        return vbox({header, paragraph(entry.text) | color(t.textFaint) | dim});
    }

    /// The focusable set is rebuilt only when the number of entries changes.
    /// Rebuilding per token would drop focus on every character.
    void SyncThinking() {
        std::vector<std::size_t> indices;
        {
            std::lock_guard<std::mutex> lock(log_->mutex);
            for (std::size_t i = 0; i < log_->entries.size(); ++i) {
                if (log_->entries[i].kind == Line::Thinking) {
                    indices.push_back(i);
                }
            }
        }
        if (indices == thinking_indices_) {
            return;
        }
        thinking_indices_ = indices;
        thinking_->DetachAllChildren();
        auto log = log_;
        for (const std::size_t index : indices) {
            ButtonOption option;
            option.label = "thought";
            option.on_click = [log, index] {
                std::lock_guard<std::mutex> lock(log->mutex);
                if (index < log->entries.size()) {
                    log->entries[index].expanded = !log->entries[index].expanded;
                }
            };
            option.transform = [log, index](const EntryState& state) {
                std::lock_guard<std::mutex> lock(log->mutex);
                if (index >= log->entries.size()) {
                    return text("");
                }
                return DrawThinking(log->entries[index], state.focused);
            };
            thinking_->Add(Button(option));
        }
    }

    Element Render() {
        const auto& t = theme::Current();
        SyncThinking();

        // Snapshot under the lock, then draw without it. A thinking block is
        // drawn by its own component whose transform locks the same mutex, so
        // rendering inside the critical section deadlocked the UI thread the
        // moment a reply contained reasoning.
        std::vector<chat::Entry> snapshot;
        {
            std::lock_guard<std::mutex> lock(log_->mutex);
            snapshot = log_->entries;
        }

        Elements lines;
        std::size_t thinking_seen = 0;
        for (const chat::Entry& entry : snapshot) {
            if (entry.kind == Line::Thinking) {
                if (thinking_seen < thinking_->ChildCount()) {
                    lines.push_back(thinking_->ChildAt(thinking_seen)->Render());
                }
                ++thinking_seen;
                continue;
            }
            lines.push_back(DrawEntry(entry));
        }
        if (lines.empty()) {
            lines.push_back(text("No model loaded. /load to start, /? for help.") |
                            color(t.textFaint));
        }

        Elements rows{vbox(std::move(lines)) | vscroll_indicator | yframe | flex};
        if (!suggestions_.empty()) {
            rows.push_back(SuggestionStrip());
        }
        rows.push_back(input_->Render());
        return vbox(std::move(rows));
    }

    Element SuggestionStrip() const {
        const auto& t = theme::Current();
        Elements shown;
        const std::size_t limit = std::min<std::size_t>(suggestions_.size(), 6);
        for (std::size_t i = 0; i < limit; ++i) {
            const bool picked = static_cast<int>(i) == highlight_;
            shown.push_back(hbox({
                text(picked ? "▌ " : "  ") | color(t.accent),
                text(suggestions_[i].label) | color(picked ? t.text : t.textDim),
                text("  " + suggestions_[i].detail) | color(t.textFaint),
            }));
        }
        if (suggestions_.size() > limit) {
            shown.push_back(text("  +" + std::to_string(suggestions_.size() - limit) + " more") |
                            color(t.textFaint));
        }
        return vbox(std::move(shown)) | bgcolor(t.inset);
    }

    void Note(std::string text) { log_->Push(Line::Notice, std::move(text)); }
    void Fail(std::string text) { log_->Push(Line::Failure, std::move(text)); }

    void Submit() {
        std::string line = draft_;
        draft_.clear();
        suggestions_.clear();
        if (line.empty()) {
            return;
        }
        if (ResolvePending(line)) {
            return;
        }
        if (line.front() == '/') {
            Command(line);
            return;
        }
        if (!llm_.loaded()) {
            Fail("no model loaded — /load <id>");
            return;
        }
        if (llm_.busy()) {
            Fail("still generating");
            return;
        }

        log_->Push(Line::Prompt, line);
        auto* screen = ScreenInteractive::Active();
        auto log = log_;
        llm_.Generate(
            std::move(line),
            [log, screen](std::string token) {
                if (!log->open.load()) {
                    return;
                }
                log->AppendToken(token);
                if (screen != nullptr) {
                    screen->PostEvent(Event::Custom);
                }
            },
            [log, screen](std::string failure) {
                if (!log->open.load()) {
                    return;
                }
                if (!failure.empty()) {
                    log->Push(Line::Failure, std::move(failure));
                }
                if (screen != nullptr) {
                    screen->PostEvent(Event::Custom);
                }
            });
    }

    void Propose(const std::string& command) {
        if (command.empty()) {
            Fail("usage: /run <command>");
            return;
        }
        pending_ = command;
        Note("about to run:  " + command);
        Note("press y to run it, anything else cancels");
    }

    bool ResolvePending(const std::string& line) {
        if (pending_.empty()) {
            return false;
        }
        const std::string command = pending_;
        pending_.clear();
        if (line != "y" && line != "yes") {
            Note("cancelled");
            return true;
        }
        log_->Push(Line::Prompt, command);
        const tools::Output out = tools::Run(command);
        if (!out.text.empty()) {
            log_->Push(Line::Answer, out.text);
        }
        log_->Push(out.status == 0 ? Line::Notice : Line::Failure,
                   "exit " + std::to_string(out.status));
        return true;
    }

    void Command(const std::string& line) {
        const std::size_t space = line.find(' ');
        const std::string name = line.substr(0, space);
        const std::string rest = space == std::string::npos ? "" : line.substr(space + 1);

        if (name == "/?" || name == "/help") {
            Note("/load <id>      load a model already on this machine");
            Note("/pull <id>      download one from the catalog");
            Note("/models         what is on this machine");
            Note("/set <k> <v>    change a setting; tab completes");
            Note("/show           current settings");
            Note("/image <path>   ask about an image");
            Note("/doc <path>     add a document to the conversation");
            Note("/run <cmd>      run a shell command, after you confirm");
            Note("/think          expand or collapse all reasoning");
            Note("/clear          clear the conversation");
            Note("/bye            quit");
            return;
        }
        if (name == "/models") {
            const std::vector<sdk::LocalModel> models = sdk::LocalModels();
            if (models.empty()) {
                Note("nothing downloaded — /pull <id>, or the models screen");
                return;
            }
            for (const sdk::LocalModel& model : models) {
                Note("  " + model.id + "  (" + model.framework + ")");
            }
            return;
        }
        if (name == "/show") {
            for (const settings::Setting& setting : settings::All()) {
                Note("  " + setting.name + "  " + setting.get());
            }
            return;
        }
        if (name == "/set") {
            Set(rest);
            return;
        }
        if (name == "/load") {
            Load(rest);
            return;
        }
        if (name == "/pull") {
            Pull(rest);
            return;
        }
        if (name == "/think") {
            std::lock_guard<std::mutex> lock(log_->mutex);
            bool any_collapsed = false;
            for (const chat::Entry& entry : log_->entries) {
                any_collapsed |= entry.kind == Line::Thinking && !entry.expanded;
            }
            for (chat::Entry& entry : log_->entries) {
                if (entry.kind == Line::Thinking) {
                    entry.expanded = any_collapsed;
                }
            }
            return;
        }
        if (name == "/run" || name == "/sh") {
            Propose(rest);
            return;
        }
        if (name == "/tools") {
            Note("run   execute a shell command, after you confirm it");
            return;
        }
        if (name == "/image" || name == "/doc") {
            Fail(name + " is not wired yet — it needs a vision model and the VLM path");
            return;
        }
        if (name == "/clear") {
            log_->Clear();
            thinking_indices_.clear();
            thinking_->DetachAllChildren();
            return;
        }
        if (name == "/bye" || name == "/exit") {
            if (auto* screen = ScreenInteractive::Active()) {
                screen->Exit();
            }
            return;
        }
        Fail("unknown command " + name + " — /? for help");
    }

    void Set(const std::string& rest) {
        const std::size_t space = rest.find(' ');
        if (space == std::string::npos) {
            Fail("usage: /set <setting> <value>");
            return;
        }
        const std::string name = rest.substr(0, space);
        const std::string value = rest.substr(space + 1);
        const settings::Setting* setting = settings::Find(name);
        if (setting == nullptr) {
            Fail("no setting called " + name);
            return;
        }
        if (!setting->set(value)) {
            Fail(value + " is not a valid " + name);
            return;
        }
        Note(name + " = " + setting->get());
    }

    void Load(const std::string& id) {
        if (id.empty()) {
            Fail("usage: /load <id>");
            return;
        }
        for (const sdk::LocalModel& model : sdk::LocalModels()) {
            if (model.id == id) {
                Note("loading " + model.id + "...");
                std::string error;
                if (llm_.Load(model, &error)) {
                    Note("ready");
                } else {
                    Fail(error);
                }
                return;
            }
        }
        Fail("no local model called " + id + " — /pull " + id + " to download it");
    }

    void Pull(const std::string& id) {
        if (id.empty()) {
            Fail("usage: /pull <id>");
            return;
        }
        for (const catalog::Model& model : catalog::All()) {
            if (model.id != id) {
                continue;
            }
            if (!catalog::Installable(model)) {
                Fail(id + " is a multi-file model; not supported yet");
                return;
            }
            Note("downloading " + std::string(model.id) + " (" +
                 catalog::HumanSize(model.bytes) + ")...");
            std::string error;
            if (sdk::Install(model, nullptr, &error)) {
                Note("downloaded — /load " + std::string(model.id));
            } else {
                Fail(error);
            }
            return;
        }
        Fail("no catalog entry called " + id);
    }

    std::string draft_;
    std::string pending_;
    std::vector<chat::Completion> suggestions_;
    int highlight_ = 0;
    std::shared_ptr<chat::Transcript> log_ = std::make_shared<chat::Transcript>();
    std::vector<std::size_t> thinking_indices_;
    sdk::Llm llm_;
    Component input_;
    Component thinking_;
    Component body_;
};

}  // namespace

std::unique_ptr<ui::Screen> MakeChat() {
    return std::make_unique<Chat>();
}

}  // namespace rcli::screens
