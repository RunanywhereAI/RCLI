#pragma once

#include <string>
#include <cstring>
#include <cctype>
#include <unordered_set>

namespace rastack {

// Lightweight text query cleanup for better retrieval quality.
// Removes filler words common in typed text, normalizes case, deduplicates stuttering.
// Uses stack buffers to avoid heap allocations for typical queries (<512 chars).

inline std::string preprocess_query(const std::string& text) {
    static const std::unordered_set<std::string> fillers = {
        "basically", "actually", "literally",
        "you know", "i mean", "sort of", "kind of",
        "well", "so", "right", "okay", "ok"
    };

    if (text.empty()) return text;

    // Stack buffer for lowercase + working copy
    char buf[512];
    size_t len = text.size();
    if (len >= sizeof(buf)) len = sizeof(buf) - 1;

    // Lowercase into buf
    for (size_t i = 0; i < len; i++) {
        buf[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(text[i])));
    }
    buf[len] = '\0';

    // Extract words into stack-allocated word table
    // Max ~64 words for a typical query
    struct WordRef { const char* start; size_t len; };
    WordRef words[64];
    int n_words = 0;

    const char* p = buf;
    while (*p && n_words < 64) {
        // Skip whitespace
        while (*p && std::isspace(static_cast<unsigned char>(*p))) p++;
        if (!*p) break;

        const char* word_start = p;
        while (*p && !std::isspace(static_cast<unsigned char>(*p))) p++;
        size_t wlen = p - word_start;

        // Strip punctuation from edges
        while (wlen > 0 && std::ispunct(static_cast<unsigned char>(*word_start))) {
            word_start++; wlen--;
        }
        while (wlen > 0 && std::ispunct(static_cast<unsigned char>(word_start[wlen - 1]))) {
            wlen--;
        }

        if (wlen == 0) continue;

        // Check single-word filler (need null-terminated for unordered_set lookup)
        std::string w(word_start, wlen);
        if (fillers.count(w)) continue;

        // Remove stuttering
        if (n_words > 0 && words[n_words - 1].len == wlen &&
            std::memcmp(words[n_words - 1].start, word_start, wlen) == 0) continue;

        words[n_words++] = {word_start, wlen};
    }

    // Remove two-word fillers ("you know", "i mean", etc.)
    WordRef cleaned[64];
    int n_cleaned = 0;
    for (int i = 0; i < n_words; i++) {
        if (i + 1 < n_words) {
            // Build bigram
            std::string bigram(words[i].start, words[i].len);
            bigram += ' ';
            bigram.append(words[i + 1].start, words[i + 1].len);
            if (fillers.count(bigram)) {
                i++; // skip both
                continue;
            }
        }
        cleaned[n_cleaned++] = words[i];
    }

    if (n_cleaned == 0) return text;

    // Reconstruct result
    std::string result;
    result.reserve(len);
    for (int i = 0; i < n_cleaned; i++) {
        if (i > 0) result += ' ';
        result.append(cleaned[i].start, cleaned[i].len);
    }
    return result;
}

} // namespace rastack
