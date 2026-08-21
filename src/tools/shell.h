#ifndef RCLI_TOOLS_SHELL_H
#define RCLI_TOOLS_SHELL_H

#include <string>

namespace rcli::tools {

struct Output {
    int status = -1;
    std::string text;
    bool timed_out = false;
};

/// Run a shell command and capture stdout and stderr together.
///
/// NOTHING here decides whether a command *should* run. That is the caller's
/// job, and in this app it means an explicit confirmation from the person at
/// the keyboard: a model that can propose commands must not also be the thing
/// that approves them. Keeping the decision out of this file makes it hard to
/// add an "auto-run" path later without noticing.
Output Run(const std::string& command, int timeout_seconds = 30);

}  // namespace rcli::tools

#endif  // RCLI_TOOLS_SHELL_H
