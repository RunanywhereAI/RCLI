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

    // 6. Collapse multiple whitespace to single space, trim
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
