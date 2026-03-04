#pragma once

#include <string>

namespace rcli {

// Execute AppleScript/JXA in a subprocess with timeout
struct ScriptResult {
    bool        success;
    std::string output;
    std::string error;
    int         exit_code;
};

// Execute an AppleScript string via osascript
ScriptResult run_applescript(const std::string& script, int timeout_ms = 10000);

// Execute a JXA (JavaScript for Automation) string via osascript -l JavaScript
ScriptResult run_jxa(const std::string& script, int timeout_ms = 10000);

// Execute a shell command and capture output
ScriptResult run_shell(const std::string& command, int timeout_ms = 10000);

} // namespace rcli
