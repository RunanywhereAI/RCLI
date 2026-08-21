#include "repl/repl.h"

#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <utility>

#include <linenoise.h>

namespace rcli::repl {
namespace {

/// linenoise's completion callback is a bare C function pointer with no user
/// data, so the handler has to live here. One REPL at a time is the only shape
/// a terminal has anyway.
std::function<std::vector<std::string>(const std::string&)> g_complete;

void Complete(const char* buffer, linenoiseCompletions* completions) {
    if (!g_complete || buffer == nullptr) {
        return;
    }
    for (const std::string& option : g_complete(buffer)) {
        linenoiseAddCompletion(completions, option.c_str());
    }
}

}  // namespace

Line::Line(std::string history_path)
    : history_path_(std::move(history_path)), interactive_(isatty(STDIN_FILENO) != 0) {
    if (!interactive_) {
        return;
    }
    std::error_code ec;
    std::filesystem::create_directories(
        std::filesystem::path(history_path_).parent_path(), ec);
    linenoiseHistorySetMaxLen(512);
    linenoiseHistoryLoad(history_path_.c_str());
    linenoiseSetCompletionCallback(Complete);
    linenoiseSetMultiLine(1);
}

Line::~Line() {
    if (interactive_) {
        linenoiseHistorySave(history_path_.c_str());
        linenoiseSetCompletionCallback(nullptr);
        g_complete = nullptr;
    }
}

void Line::OnComplete(std::function<std::vector<std::string>(const std::string&)> handler) {
    g_complete = std::move(handler);
}

std::optional<std::string> Line::Read(const std::string& prompt) {
    if (!interactive_) {
        std::string line;
        if (!std::getline(std::cin, line)) {
            return std::nullopt;
        }
        return line;
    }
    char* raw = linenoise(prompt.c_str());
    if (raw == nullptr) {
        return std::nullopt;
    }
    std::string line(raw);
    if (!line.empty()) {
        linenoiseHistoryAdd(raw);
    }
    std::free(raw);
    return line;
}

}  // namespace rcli::repl
