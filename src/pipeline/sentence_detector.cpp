#include "pipeline/sentence_detector.h"
#include <algorithm>

namespace rastack {

void SentenceDetector::feed(const std::string& text) {
    buffer_ += text;

    // Scan for sentence boundaries
    size_t start = 0;
    for (size_t i = 0; i < buffer_.size(); i++) {
        bool is_primary = is_sentence_end(buffer_[i]);
        bool is_secondary = is_secondary_break(buffer_[i]);

        if (is_primary || is_secondary) {
            // Check if next char validates the boundary
            bool valid_end = (i + 1 >= buffer_.size()) ||
                             (buffer_[i + 1] == ' ') ||
                             (buffer_[i + 1] == '\n') ||
                             (buffer_[i + 1] == '"') ||
                             (buffer_[i + 1] == '\'');

            if (!valid_end) continue;

            std::string candidate = buffer_.substr(start, i - start + 1);
            // Trim leading whitespace
            size_t first = candidate.find_first_not_of(" \n\r\t");
            if (first != std::string::npos) {
                candidate = candidate.substr(first);
            }

            int words = count_words(candidate);

            // Primary breaks (. ! ? \n): require minimum word count
            // to avoid splitting on abbreviations like "Dr." or "U.S."
            // Use a lower threshold for the first sentence to minimize TTFA.
            int effective_min = (sentence_count_ == 0) ? first_min_words_ : min_words_;
            if (is_primary && words >= effective_min) {
                if (!candidate.empty() && callback_) {
                    callback_(candidate);
                    sentence_count_++;
                }
                start = i + 1;
            }
            // Secondary breaks (; :): only trigger for long sentences
            // Prevents speaker from waiting too long for a pause point
            else if (is_secondary && words >= max_words_secondary_) {
                if (!candidate.empty() && callback_) {
                    callback_(candidate);
                    sentence_count_++;
                }
                start = i + 1;
            }
        }
    }

    // Keep unprocessed remainder in buffer
    if (start > 0) {
        buffer_ = buffer_.substr(start);
    }

    // Word-level flush: if no sentence boundary found but enough words accumulated,
    // emit at the last word boundary for earlier TTS start.
    // For the first sentence, use a lower threshold to minimize TTFA.
    int effective_word_flush = word_flush_threshold_;
    if (sentence_count_ == 0 && first_min_words_ < min_words_)
        effective_word_flush = std::max(effective_word_flush, 5);
    if (effective_word_flush > 0 && !buffer_.empty()) {
        std::string trimmed = buffer_;
        size_t first = trimmed.find_first_not_of(" \n\r\t");
        if (first != std::string::npos) {
            trimmed = trimmed.substr(first);
        }
        if (count_words(trimmed) >= effective_word_flush) {
            // Find last space to emit a clean word boundary
            size_t last_space = buffer_.rfind(' ');
            if (last_space != std::string::npos && last_space > 0) {
                std::string to_emit = buffer_.substr(0, last_space);
                size_t f = to_emit.find_first_not_of(" \n\r\t");
                if (f != std::string::npos) {
                    to_emit = to_emit.substr(f);
                }
                if (!to_emit.empty() && callback_) {
                    callback_(to_emit);
                    sentence_count_++;
                }
                buffer_ = buffer_.substr(last_space + 1);
            }
        }
    }
}

void SentenceDetector::flush() {
    // Emit whatever is left as a final sentence
    std::string trimmed = buffer_;
    size_t first = trimmed.find_first_not_of(" \n\r\t");
    if (first != std::string::npos) {
        trimmed = trimmed.substr(first);
    }
    size_t last = trimmed.find_last_not_of(" \n\r\t");
    if (last != std::string::npos) {
        trimmed = trimmed.substr(0, last + 1);
    }

    if (!trimmed.empty() && callback_) {
        callback_(trimmed);
        sentence_count_++;
    }
    buffer_.clear();
}

void SentenceDetector::reset() {
    buffer_.clear();
    sentence_count_ = 0;
}

bool SentenceDetector::is_sentence_end(char c) const {
    return c == '.' || c == '!' || c == '?' || c == '\n';
}

bool SentenceDetector::is_secondary_break(char c) const {
    return c == ';' || c == ':';
}

int SentenceDetector::count_words(const std::string& s) const {
    int count = 0;
    bool in_word = false;
    for (char c : s) {
        if (c == ' ' || c == '\t' || c == '\n') {
            in_word = false;
        } else if (!in_word) {
            in_word = true;
            count++;
        }
    }
    return count;
}

} // namespace rastack
