#ifndef WALLY_HARNESS_OPENCODE_H
#define WALLY_HARNESS_OPENCODE_H

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "account/console.h"

namespace wally::harness {

using SpawnFunction =
    std::function<int(const std::string& executable, const std::vector<std::string>& arguments)>;

/// OpenCode's complete, ephemeral provider configuration for a hosted model.
/// The model's real limits (context/output) and price are injected so OpenCode's
/// compaction and its usage display are correct; a 0 for any of them omits that
/// field. Exposed so the contract can be tested without launching a child.
std::string BuildOpenCodeCloudConfig(const std::string& model, const std::string& base_url,
                                     const std::string& access_token, std::int64_t context_window,
                                     std::int64_t max_output, std::int64_t input_per_mtok,
                                     std::int64_t output_per_mtok);

/// Launch OpenCode against the signed-in RunAnywhere cloud session.
///
/// Only OPENCODE_CONFIG_CONTENT is changed, only for the duration of the child.
/// No OpenCode or project configuration file is read or written. The default
/// overload starts `opencode` directly (never through a shell).
int LaunchOpenCodeCloud(const std::string& model, const std::vector<std::string>& arguments);

/// Test seam for the console refresh transport and child process.
int LaunchOpenCodeCloud(const std::string& model, const std::vector<std::string>& arguments,
                        const account::ConsoleClient& console, const SpawnFunction& spawn);

}  // namespace wally::harness

#endif  // WALLY_HARNESS_OPENCODE_H
