#pragma once

#include <string>

namespace rastack {

// Strip think tags, tool_call tags, markdown symbols, and other non-speakable text
// so TTS only receives clean conversational text.
inline std::string sanitize_for_tts(const std::string& text) {
    std::string out = text;

    // 1. Strip <think>...</think> blocks (handles unclosed tags)
    while (true) {
        size_t ts = out.find("<think>");
        if (ts == std::string::npos) break;
        size_t te = out.find("</think>", ts);
        if (te != std::string::npos) {
            out.erase(ts, te - ts + 8); // 8 = len("</think>")
        } else {
            out.erase(ts); // unclosed — remove everything from <think> onward
            break;
        }
    }

    // 2. Strip <tool_call>...</tool_call> blocks
    while (true) {
        size_t ts = out.find("<tool_call>");
        if (ts == std::string::npos) break;
        size_t te = out.find("</tool_call>", ts);
        if (te != std::string::npos) {
            out.erase(ts, te - ts + 12); // 12 = len("</tool_call>")
        } else {
            out.erase(ts);
            break;
        }
    }

    // 3. Strip any remaining <...> tags (e.g. <|im_end|>, stray HTML)
    {
        std::string cleaned;
        cleaned.reserve(out.size());
        bool in_tag = false;
        for (size_t i = 0; i < out.size(); i++) {
            if (out[i] == '<') {
                in_tag = true;
            } else if (out[i] == '>' && in_tag) {
                in_tag = false;
            } else if (!in_tag) {
                cleaned += out[i];
            }
        }
        out = std::move(cleaned);
    }

    // 4. Strip markdown links [text](url) -> text
    {
        std::string cleaned;
        cleaned.reserve(out.size());
        for (size_t i = 0; i < out.size(); i++) {
            if (out[i] == '[') {
                size_t close = out.find(']', i + 1);
                if (close != std::string::npos && close + 1 < out.size() && out[close + 1] == '(') {
                    size_t pclose = out.find(')', close + 2);
                    if (pclose != std::string::npos) {
                        cleaned += out.substr(i + 1, close - i - 1);
                        i = pclose;
                        continue;
                    }
                }
            }
            cleaned += out[i];
        }
        out = std::move(cleaned);
    }

    // 5. Strip markdown symbols and non-speakable formatting
    {
        std::string cleaned;
        cleaned.reserve(out.size());
        bool at_line_start = true;
        for (size_t i = 0; i < out.size(); i++) {
            char c = out[i];
            if (c == '*' || c == '~' || c == '`') {
                continue;
            }
            if (c == '#' && at_line_start) {
                while (i < out.size() && out[i] == '#') i++;
                if (i < out.size() && out[i] == ' ') i++;
                i--;
                continue;
            }
            // Block quotes at line start
            if (c == '>' && at_line_start) {
                if (i + 1 < out.size() && out[i + 1] == ' ') i++;
                continue;
            }
            // Bullet points
            if (c == '-' && at_line_start && i + 1 < out.size() && out[i + 1] == ' ') {
                i++;
                continue;
            }
            // Horizontal rules (--- or ***)
            if (c == '-' && at_line_start && i + 2 < out.size() && out[i + 1] == '-' && out[i + 2] == '-') {
                while (i < out.size() && out[i] == '-') i++;
                i--;
                continue;
            }
            // Numbered list prefixes
            if (at_line_start && c >= '0' && c <= '9') {
                size_t j = i;
                while (j < out.size() && out[j] >= '0' && out[j] <= '9') j++;
                if (j < out.size() && out[j] == '.' && j + 1 < out.size() && out[j + 1] == ' ') {
                    i = j + 1;
                    continue;
                }
            }
            // Double slashes (// comments in LLM output)
            if (c == '/' && i + 1 < out.size() && out[i + 1] == '/') {
                i++;
                continue;
            }
            at_line_start = (c == '\n');
            cleaned += c;
        }
        out = std::move(cleaned);
    }

    // 6a. Normalize smart/curly quotes to ASCII (LLMs often output U+2019)
    {
        std::string cleaned;
        cleaned.reserve(out.size());
        for (size_t i = 0; i < out.size(); i++) {
            // U+2018 (') and U+2019 (') = \xe2\x80\x98 / \xe2\x80\x99
            if (i + 2 < out.size() &&
                (unsigned char)out[i] == 0xe2 && (unsigned char)out[i+1] == 0x80 &&
                ((unsigned char)out[i+2] == 0x98 || (unsigned char)out[i+2] == 0x99)) {
                cleaned += '\'';
                i += 2;
            }
            // U+201C (") and U+201D (") = \xe2\x80\x9c / \xe2\x80\x9d
            else if (i + 2 < out.size() &&
                     (unsigned char)out[i] == 0xe2 && (unsigned char)out[i+1] == 0x80 &&
                     ((unsigned char)out[i+2] == 0x9c || (unsigned char)out[i+2] == 0x9d)) {
                cleaned += '"';
                i += 2;
            }
            else {
                cleaned += out[i];
            }
        }
        out = std::move(cleaned);
    }

    // 6b. Expand contractions that Kokoro's G2P may spell out letter-by-letter
    {
        struct Contraction { const char* from; const char* to; };
        static const Contraction table[] = {
            {"you're", "you are"}, {"You're", "You are"},
            {"they're", "they are"}, {"They're", "They are"},
            {"we're", "we are"}, {"We're", "We are"},
            {"I'm", "I am"}, {"i'm", "I am"},
            {"he's", "he is"}, {"He's", "He is"},
            {"she's", "she is"}, {"She's", "She is"},
            {"it's", "it is"}, {"It's", "It is"},
            {"that's", "that is"}, {"That's", "That is"},
            {"what's", "what is"}, {"What's", "What is"},
            {"there's", "there is"}, {"There's", "There is"},
            {"here's", "here is"}, {"Here's", "Here is"},
            {"who's", "who is"}, {"Who's", "Who is"},
            {"don't", "do not"}, {"Don't", "Do not"},
            {"doesn't", "does not"}, {"Doesn't", "Does not"},
            {"didn't", "did not"}, {"Didn't", "Did not"},
            {"won't", "will not"}, {"Won't", "Will not"},
            {"wouldn't", "would not"}, {"Wouldn't", "Would not"},
            {"couldn't", "could not"}, {"Couldn't", "Could not"},
            {"shouldn't", "should not"}, {"Shouldn't", "Should not"},
            {"can't", "cannot"}, {"Can't", "Cannot"},
            {"isn't", "is not"}, {"Isn't", "Is not"},
            {"aren't", "are not"}, {"Aren't", "Are not"},
            {"wasn't", "was not"}, {"Wasn't", "Was not"},
            {"weren't", "were not"}, {"Weren't", "Were not"},
            {"haven't", "have not"}, {"Haven't", "Have not"},
            {"hasn't", "has not"}, {"Hasn't", "Has not"},
            {"hadn't", "had not"}, {"Hadn't", "Had not"},
            {"I'll", "I will"}, {"you'll", "you will"}, {"You'll", "You will"},
            {"he'll", "he will"}, {"she'll", "she will"},
            {"we'll", "we will"}, {"We'll", "We will"},
            {"they'll", "they will"}, {"They'll", "They will"},
            {"I've", "I have"}, {"you've", "you have"}, {"You've", "You have"},
            {"we've", "we have"}, {"We've", "We have"},
            {"they've", "they have"}, {"They've", "They have"},
            {"I'd", "I would"}, {"you'd", "you would"}, {"You'd", "You would"},
            {"he'd", "he would"}, {"she'd", "she would"},
            {"we'd", "we would"}, {"they'd", "they would"},
            {"let's", "let us"}, {"Let's", "Let us"},
        };
        for (auto& c : table) {
            std::string needle(c.from);
            std::string replacement(c.to);
            size_t pos = 0;
            while ((pos = out.find(needle, pos)) != std::string::npos) {
                // Only replace whole words: check boundaries
                bool left_ok = (pos == 0 || out[pos - 1] == ' ' || out[pos - 1] == '\n');
                size_t end = pos + needle.size();
                bool right_ok = (end >= out.size() || out[end] == ' ' || out[end] == ',' ||
                                 out[end] == '.' || out[end] == '!' || out[end] == '?' ||
                                 out[end] == '\n' || out[end] == ';' || out[end] == ':');
                if (left_ok && right_ok) {
                    out.replace(pos, needle.size(), replacement);
                    pos += replacement.size();
                } else {
                    pos += needle.size();
                }
            }
        }
    }

    // 7. Collapse multiple whitespace to single space, trim
    {
        std::string cleaned;
        cleaned.reserve(out.size());
        bool prev_space = true; // treat start as space to trim leading
        for (char c : out) {
            if (c == ' ' || c == '\t' || c == '\r') {
                if (!prev_space) {
                    cleaned += ' ';
                    prev_space = true;
                }
            } else if (c == '\n') {
                // Convert newlines to spaces
                if (!prev_space) {
                    cleaned += ' ';
                    prev_space = true;
                }
            } else {
                cleaned += c;
                prev_space = false;
            }
        }
        // Trim trailing space
        if (!cleaned.empty() && cleaned.back() == ' ') {
            cleaned.pop_back();
        }
        out = std::move(cleaned);
    }

    return out;
}

} // namespace rastack
