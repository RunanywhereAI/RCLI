#ifndef RCLI_CHAT_TRANSCRIPT_H
#define RCLI_CHAT_TRANSCRIPT_H

#include <atomic>
#include <cstddef>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace rcli::chat {

enum class Line { Prompt, Answer, Thinking, Notice, Failure };

struct Entry {
    Line kind = Line::Notice;
    std::string text;
    /// Thinking only. Collapsed by default: the reasoning is worth keeping and
    /// rarely worth reading, so it folds to one line until asked for.
    bool expanded = false;
};

/// The conversation, shared with generation workers.
///
/// A reply can still be arriving when the screen goes away, so this is owned by
/// shared_ptr and workers check `open` before touching it. Every read and write
/// goes through the mutex, the render pass included.
class Transcript {
   public:
    std::mutex mutex;
    std::vector<Entry> entries;
    std::atomic<bool> open{true};

    /// Appends to the reply in progress, splitting <think>…</think> into its
    /// own entry. Reasoning arrives inline in the token stream, so the split
    /// has to happen here rather than after the fact.
    void AppendToken(std::string_view token);

    void Push(Line kind, std::string text);
    void Clear();
    std::size_t size();

   private:
    void PushLocked(Line kind, std::string text);

    bool in_think_ = false;
    /// A tag can arrive split across tokens, so a partial match is held back
    /// rather than printed and then regretted.
    std::string carry_;
};

}  // namespace rcli::chat

#endif  // RCLI_CHAT_TRANSCRIPT_H
