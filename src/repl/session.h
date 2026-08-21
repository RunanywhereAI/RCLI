#ifndef RCLI_REPL_SESSION_H
#define RCLI_REPL_SESSION_H

#include <memory>
#include <string>
#include <vector>

#include "audio/audio.h"
#include "sdk/imagine.h"
#include "sdk/llm.h"
#include "sdk/speech.h"

namespace rcli::repl {

/// The interactive conversation: everything that happens between `>>>` and the
/// next prompt.
///
/// It is a class rather than a loop because the same slash commands have to be
/// reachable from a one-shot invocation (`rcli run model "prompt"`) and from
/// the prompt, and because a model, a voice and an image pipeline all stay
/// loaded across turns.
class Session {
   public:
    Session();
    ~Session();

    /// Loads a model by catalog id, alias, or local directory name. Downloads
    /// it first if it is not here, reporting progress.
    bool Load(const std::string& id);
    bool loaded() const { return llm_.loaded(); }
    const std::string& model() const { return llm_.model_id(); }

    /// One line from the user: a slash command, or a message to the model.
    /// False means the session should end.
    bool Submit(const std::string& line);

    /// Completions for the word being typed, for the prompt's Tab key.
    std::vector<std::string> Complete(const std::string& line) const;

    std::string Prompt() const;

   private:
    bool Command(const std::string& line);
    void Ask(const std::string& prompt);
    void Help() const;
    void ShowSettings() const;
    void Set(const std::string& rest);
    void Models() const;
    void Pull(const std::string& id);
    void Remove(const std::string& id);
    void Attach(const std::string& path);
    void Document(const std::string& path);
    void Draw(const std::string& prompt);
    void Speak(const std::string& text);
    void Listen();
    void Shell(const std::string& command);

    sdk::Llm llm_;
    sdk::Speech speech_;
    sdk::Imagine imagine_;
    audio::Recorder recorder_;
    audio::Player player_;

    std::vector<sdk::Turn> history_;
    std::string last_answer_;
    std::string pending_image_;
    bool show_reasoning_ = true;
};

}  // namespace rcli::repl

#endif  // RCLI_REPL_SESSION_H
