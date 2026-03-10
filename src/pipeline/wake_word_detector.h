#pragma once

#include <string>
#include <vector>
#include <algorithm>
#include <cctype>

namespace rastack {

class WakeWordDetector {
public:
    void set_phrases(const std::vector<std::string>& phrases) {
        phrases_.clear();
        for (auto& p : phrases) {
            std::string lower;
            lower.reserve(p.size());
            for (char c : p) lower += std::tolower(static_cast<unsigned char>(c));
            phrases_.push_back(lower);
        }
    }

    void set_phrase(const std::string& phrase) {
        set_phrases({phrase});
    }

    // Check if transcript contains a wake phrase. Returns true if found.
    bool check(const std::string& transcript) const {
        if (phrases_.empty()) return false;
        std::string lower;
        lower.reserve(transcript.size());
        for (char c : transcript) lower += std::tolower(static_cast<unsigned char>(c));
        for (auto& phrase : phrases_) {
            if (lower.find(phrase) != std::string::npos) return true;
        }
        return false;
    }

    // Strip the wake phrase from transcript, returning the remaining text.
    std::string strip_wake_word(const std::string& transcript) const {
        std::string lower;
        lower.reserve(transcript.size());
        for (char c : transcript) lower += std::tolower(static_cast<unsigned char>(c));
        for (auto& phrase : phrases_) {
            auto pos = lower.find(phrase);
            if (pos != std::string::npos) {
                std::string result = transcript.substr(pos + phrase.size());
                // Trim leading whitespace/punctuation
                auto start = result.find_first_not_of(" \t,.");
                if (start == std::string::npos) return "";
                return result.substr(start);
            }
        }
        return transcript;
    }

private:
    std::vector<std::string> phrases_;
};

} // namespace rastack
