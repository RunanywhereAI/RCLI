#pragma once

#include <string>
#include <functional>
#include <vector>

namespace rastack {

// Detects sentence boundaries in streaming LLM output.
// Smarter than simple punctuation:
//   - Minimum word count before emitting (avoids splitting on "Dr.", "U.S.")
//   - Secondary break points (colon, semicolon) for long sentences
//   - Prevents very short TTS calls that waste synthesis overhead
class SentenceDetector {
public:
    using SentenceCallback = std::function<void(const std::string& sentence)>;

    explicit SentenceDetector(SentenceCallback cb,
                              int min_words = 3,
                              int max_words_before_secondary_break = 25,
                              int word_flush_threshold = 0)
        : callback_(std::move(cb))
        , min_words_(min_words)
        , max_words_secondary_(max_words_before_secondary_break)
        , word_flush_threshold_(word_flush_threshold) {}

    // Feed a token/text fragment from LLM
    void feed(const std::string& text);

    // Flush any remaining buffered text (e.g., at end of generation)
    void flush();

    // Reset state
    void reset();

    // Get count of sentences detected
    int sentence_count() const { return sentence_count_; }

private:
    bool is_sentence_end(char c) const;
    bool is_secondary_break(char c) const;
    int  count_words(const std::string& s) const;

    SentenceCallback callback_;
    std::string      buffer_;
    int              sentence_count_ = 0;
    int              min_words_;
    int              max_words_secondary_;
    int              word_flush_threshold_;
};

} // namespace rastack
