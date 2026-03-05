#pragma once

#include <string>
#include <cctype>
#include <cstdio>

namespace rcli {

inline std::string json_get_string(const std::string& json, const std::string& key) {
    auto pos = json.find("\"" + key + "\"");
    if (pos == std::string::npos) return "";
    auto colon = json.find(':', pos);
    if (colon == std::string::npos) return "";
    auto start = json.find_first_not_of(" \t\n\r", colon + 1);
    if (start == std::string::npos) return "";
    if (json[start] == '"') {
        // Walk the string handling \" escapes
        std::string result;
        for (size_t i = start + 1; i < json.size(); i++) {
            if (json[i] == '\\' && i + 1 < json.size()) {
                char next = json[i + 1];
                if (next == '"')       { result += '"';  i++; }
                else if (next == '\\') { result += '\\'; i++; }
                else if (next == 'n')  { result += '\n'; i++; }
                else if (next == 't')  { result += '\t'; i++; }
                else                   { result += next;  i++; }
            } else if (json[i] == '"') {
                break;
            } else {
                result += json[i];
            }
        }
        return result;
    }
    auto end = json.find_first_of(",} \t\n\r", start);
    if (end == std::string::npos) end = json.size();
    std::string val = json.substr(start, end - start);
    return val;
}

inline int json_get_int(const std::string& json, const std::string& key, int default_val = 0) {
    std::string s = json_get_string(json, key);
    if (s.empty()) return default_val;
    try { return std::stoi(s); } catch (...) { return default_val; }
}

inline bool json_get_bool(const std::string& json, const std::string& key, bool default_val = false) {
    std::string s = json_get_string(json, key);
    if (s.empty()) {
        auto pos = json.find("\"" + key + "\"");
        if (pos == std::string::npos) return default_val;
        auto colon = json.find(':', pos);
        if (colon == std::string::npos) return default_val;
        auto start = json.find_first_not_of(" \t\n\r", colon + 1);
        if (start == std::string::npos) return default_val;
        if (json.compare(start, 4, "true") == 0) return true;
        if (json.compare(start, 5, "false") == 0) return false;
        return default_val;
    }
    return s == "true" || s == "1" || s == "yes";
}

inline std::string escape_applescript(const std::string& s) {
    std::string result;
    for (char c : s) {
        if (c == '"') result += "\\\"";
        else if (c == '\\') result += "\\\\";
        else result += c;
    }
    return result;
}

// Escape for use inside single-quoted shell strings: ' → '\''
inline std::string escape_shell(const std::string& s) {
    std::string result;
    for (char c : s) {
        if (c == '\'') result += "'\\''";
        else result += c;
    }
    return result;
}

inline std::string url_encode(const std::string& s, bool plus_for_space = false) {
    std::string result;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            result += static_cast<char>(c);
        } else if (c == ' ') {
            result += plus_for_space ? "+" : "%20";
        } else {
            char hex[4];
            std::snprintf(hex, sizeof(hex), "%%%02X", c);
            result += hex;
        }
    }
    return result;
}

inline bool is_dangerous_command(const std::string& cmd) {
    static const char* blocklist[] = {
        "rm -rf /", "rm -rf ~", "rm -rf /*", "sudo rm",
        "mkfs", "dd if=", ":(){ :|:", "chmod -R 777 /",
        "sudo su", "sudo bash", "sudo sh",
        "> /dev/sda", "shutdown", "reboot", "halt",
    };
    std::string lower = cmd;
    for (auto& c : lower) c = std::tolower(static_cast<unsigned char>(c));
    for (auto& blocked : blocklist) {
        if (lower.find(blocked) != std::string::npos) return true;
    }
    return false;
}

inline std::string trim_output(const std::string& s) {
    std::string result = s;
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
        result.pop_back();
    return result;
}

} // namespace rcli
