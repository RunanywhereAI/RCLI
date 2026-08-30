#ifndef RCLI_HARNESS_OPENCODE_H
#define RCLI_HARNESS_OPENCODE_H

#include <functional>
#include <string>
#include <vector>

#include "account/console.h"

namespace rcli::harness {

using SpawnFunction =
    std::function<int(const std::string& executable, const std::vector<std::string>& arguments)>;

/// OpenCode's complete, ephemeral provider configuration for a hosted model.
/// Exposed so the contract can be tested without launching a child process.
std::string BuildOpenCodeCloudConfig(const std::string& model, const std::string& base_url,
                                     const std::string& access_token);

/// Launch OpenCode against the signed-in RunAnywhere cloud session.
///
/// Only OPENCODE_CONFIG_CONTENT is changed, only for the duration of the child.
/// No OpenCode or project configuration file is read or written. The default
/// overload starts `opencode` directly (never through a shell).
int LaunchOpenCodeCloud(const std::string& model, const std::vector<std::string>& arguments);

/// Test seam for the console refresh transport and child process.
int LaunchOpenCodeCloud(const std::string& model, const std::vector<std::string>& arguments,
                        const account::ConsoleClient& console, const SpawnFunction& spawn);

}  // namespace rcli::harness

#endif  // RCLI_HARNESS_OPENCODE_H
