#ifndef RCLI_REPL_REPL_H
#define RCLI_REPL_REPL_H

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace rcli::repl {

/// The interactive prompt: line editing, history across runs, and Tab
/// completion supplied by the caller.
///
/// linenoise rather than readline, matching the SDK's rcli — readline is GPL
/// and this ships under a permissive licence. When stdin is not a terminal it
/// falls back to plain getline, so `echo /help | rcli run` works and the test
/// harnesses do not need a pty.
class Line {
   public:
    /// `history_path` is created if its directory exists; an unwritable path is
    /// not an error, it just means no history between runs.
    explicit Line(std::string history_path);
    ~Line();

    /// Offers completions for the word being typed. Return the full replacement
    /// lines, not just the tail.
    void OnComplete(std::function<std::vector<std::string>(const std::string&)> handler);

    /// Nothing when the user asked to end the session (EOF or interrupt).
    std::optional<std::string> Read(const std::string& prompt);

   private:
    std::string history_path_;
    bool interactive_;
};

}  // namespace rcli::repl

#endif  // RCLI_REPL_REPL_H
