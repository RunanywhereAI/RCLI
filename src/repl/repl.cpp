#include "repl/repl.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <utility>

#if defined(_WIN32)
#include <io.h>
#define RCLI_ISATTY _isatty
#define RCLI_STDIN_FILENO _fileno(stdin)
#else
#include <unistd.h>
#define RCLI_ISATTY isatty
#define RCLI_STDIN_FILENO STDIN_FILENO
#endif

// The vendored linenoise is POSIX only: it drives the terminal through termios,
// which Windows has no equivalent of. Windows consoles do their own line
// editing, so the fallback below keeps the command surface identical and gives
// up only completion and in-session recall.
#if !defined(RCLI_NO_LINENOISE)
#include <linenoise.h>
#endif

namespace rcli::repl {
namespace {

#if !defined(RCLI_NO_LINENOISE)
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
#endif

}  // namespace

Line::Line(std::string history_path)
    : history_path_(std::move(history_path)), interactive_(RCLI_ISATTY(RCLI_STDIN_FILENO) != 0) {
    if (!interactive_) {
        return;
    }
    std::error_code ec;
    std::filesystem::create_directories(
        std::filesystem::path(history_path_).parent_path(), ec);
#if !defined(RCLI_NO_LINENOISE)
    linenoiseHistorySetMaxLen(512);
    linenoiseHistoryLoad(history_path_.c_str());
    linenoiseSetCompletionCallback(Complete);
    linenoiseSetMultiLine(1);
#endif
}

Line::~Line() {
#if !defined(RCLI_NO_LINENOISE)
    if (interactive_) {
        linenoiseHistorySave(history_path_.c_str());
        linenoiseSetCompletionCallback(nullptr);
        g_complete = nullptr;
    }
#endif
}

void Line::OnComplete(std::function<std::vector<std::string>(const std::string&)> handler) {
#if defined(RCLI_NO_LINENOISE)
    (void)handler;
#else
    g_complete = std::move(handler);
#endif
}

std::optional<std::string> Line::Read(const std::string& prompt) {
    if (!interactive_) {
        std::string line;
        if (!std::getline(std::cin, line)) {
            return std::nullopt;
        }
        return line;
    }
#if defined(RCLI_NO_LINENOISE)
    // The prompt is commentary, so it goes to stderr with everything else that
    // is not a result; redirecting stdout still captures answers alone.
    std::cerr << prompt << std::flush;
    std::string line;
    if (!std::getline(std::cin, line)) {
        return std::nullopt;
    }
    if (!line.empty() && !history_path_.empty()) {
        std::ofstream history(history_path_, std::ios::app);
        history << line << '\n';
    }
    return line;
#else
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
#endif
}

}  // namespace rcli::repl
