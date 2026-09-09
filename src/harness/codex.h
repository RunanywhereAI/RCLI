#ifndef WALLY_HARNESS_CODEX_H
#define WALLY_HARNESS_CODEX_H

#include <string>
#include <vector>

#include "account/console.h"
#include "harness/opencode.h"  // SpawnFunction

namespace wally::harness {

/// The environment variable Codex reads the key from. Named in config.toml's
/// `env_key`, exported for the child only, so the credential is never written
/// to the config file or passed on argv.
inline constexpr const char* kCodexKeyEnvVar = "WALLY_CODEX_API_KEY";

/// Codex's `config.toml` for one hosted model. The key is deliberately absent —
/// the provider block names `kCodexKeyEnvVar` and Codex reads it at runtime.
/// Codex speaks the Responses API (`wire_api = "responses"`). Exposed so the
/// contract can be tested without launching a child or touching the filesystem.
std::string BuildCodexConfig(const std::string& model, const std::string& base_url);

/// Launch Codex against the signed-in RunAnywhere cloud session.
///
/// Codex reads its provider from `$CODEX_HOME/config.toml`, so this writes a
/// throwaway CODEX_HOME for the child and removes it afterwards, and passes the
/// key through `kCodexKeyEnvVar` rather than into the file. The user's real
/// ~/.codex is never read or written.
int LaunchCodexCloud(const std::string& model, const std::vector<std::string>& arguments);

/// Test seam for the console refresh transport and child process.
int LaunchCodexCloud(const std::string& model, const std::vector<std::string>& arguments,
                     const account::ConsoleClient& console, const SpawnFunction& spawn);

}  // namespace wally::harness

#endif  // WALLY_HARNESS_CODEX_H
