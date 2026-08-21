#include "chat/transcript.h"

#include <algorithm>
#include <utility>

namespace rcli::chat {
void Transcript::PushLocked(Line kind, std::string text) {
    entries.push_back({kind, std::move(text), false});
}

void Transcript::Push(Line kind, std::string text) {
    std::lock_guard<std::mutex> lock(mutex);
    PushLocked(kind, std::move(text));
}

void Transcript::AppendPiece(bool thinking, std::string_view text) {
    if (text.empty()) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex);
    const Line kind = thinking ? Line::Thinking : Line::Answer;
    if (entries.empty() || entries.back().kind != kind) {
        PushLocked(kind, std::string(text));
        return;
    }
    entries.back().text += text;
}

void Transcript::Clear() {
    std::lock_guard<std::mutex> lock(mutex);
    entries.clear();
}

std::size_t Transcript::size() {
    std::lock_guard<std::mutex> lock(mutex);
    return entries.size();
}

}  // namespace rcli::chat
