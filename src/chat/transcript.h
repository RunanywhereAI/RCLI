#ifndef RCLI_CHAT_TRANSCRIPT_H
#define RCLI_CHAT_TRANSCRIPT_H

#include <atomic>
#include <cstddef>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace rcli::chat {

/// Image carries a file path, not text: the transcript stays a list of strings
/// and the screen decides how to show a picture in a terminal.
enum class Line { Prompt, Answer, Thinking, Notice, Failure, Image };

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

    /// Appends to the reply in progress. The SDK reports reasoning as its own
    /// event kind, so no tag parsing is needed: the caller says which it is.
    void AppendPiece(bool thinking, std::string_view text);

    void Push(Line kind, std::string text);
    void Clear();
    std::size_t size();

   private:
    void PushLocked(Line kind, std::string text);
};

}  // namespace rcli::chat

#endif  // RCLI_CHAT_TRANSCRIPT_H
