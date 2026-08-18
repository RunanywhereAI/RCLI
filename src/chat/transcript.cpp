#include "chat/transcript.h"

#include <algorithm>
#include <utility>

namespace rcli::chat {
namespace {

constexpr std::string_view kOpen = "<think>";
constexpr std::string_view kClose = "</think>";

/// Longest suffix of `text` that could still become the start of `tag`.
std::size_t PartialTail(std::string_view text, std::string_view tag) {
    const std::size_t most = std::min(text.size(), tag.size() - 1);
    for (std::size_t n = most; n > 0; --n) {
        if (text.substr(text.size() - n) == tag.substr(0, n)) {
            return n;
        }
    }
    return 0;
}

}  // namespace

void Transcript::PushLocked(Line kind, std::string text) {
    entries.push_back({kind, std::move(text), false});
}

void Transcript::Push(Line kind, std::string text) {
    std::lock_guard<std::mutex> lock(mutex);
    PushLocked(kind, std::move(text));
}

void Transcript::AppendToken(std::string_view token) {
    std::lock_guard<std::mutex> lock(mutex);
    carry_ += token;

    while (!carry_.empty()) {
        const std::string_view tag = in_think_ ? kClose : kOpen;
        const std::size_t at = carry_.find(tag);
        if (at == std::string::npos) {
            // Keep back anything that might be the first half of a tag.
            const std::size_t hold = PartialTail(carry_, tag);
            const std::string ready = carry_.substr(0, carry_.size() - hold);
            carry_.erase(0, carry_.size() - hold);
            if (ready.empty()) {
                return;
            }
            const Line kind = in_think_ ? Line::Thinking : Line::Answer;
            if (entries.empty() || entries.back().kind != kind) {
                PushLocked(kind, ready);
            } else {
                entries.back().text += ready;
            }
            return;
        }

        const std::string before = carry_.substr(0, at);
        if (!before.empty()) {
            const Line kind = in_think_ ? Line::Thinking : Line::Answer;
            if (entries.empty() || entries.back().kind != kind) {
                PushLocked(kind, before);
            } else {
                entries.back().text += before;
            }
        }
        carry_.erase(0, at + tag.size());
        in_think_ = !in_think_;
        // Open the next block eagerly so an empty one still shows the model
        // spent time thinking rather than vanishing.
        PushLocked(in_think_ ? Line::Thinking : Line::Answer, "");
    }
}

void Transcript::Clear() {
    std::lock_guard<std::mutex> lock(mutex);
    entries.clear();
    carry_.clear();
    in_think_ = false;
}

std::size_t Transcript::size() {
    std::lock_guard<std::mutex> lock(mutex);
    return entries.size();
}

}  // namespace rcli::chat
