#ifndef RCLI_HARNESS_HARNESS_H
#define RCLI_HARNESS_HARNESS_H

#include <string>
#include <vector>

/// Launching a coding tool against a model, whether that model runs here or
/// upstream.
///
/// The harness never learns which it got. It is handed one OpenAI-compatible
/// base URL and talks to that, exactly as it would to any provider. For a local
/// model the URL is a server this process starts and stops; for an upstream one
/// it is the provider's own. That is the same shape Ollama uses, and it is why
/// a harness needs no plugin to work with us.
namespace rcli::harness {

/// Runs `tool` against `model`, forwarding `args` to it, and returns the tool's
/// exit code. Blocks until the tool exits, then stops anything it started.
///
/// An empty `model` uses whatever the tool is already configured for, which
/// makes `rcli opencode` a plain passthrough.
int Launch(const std::string& tool, const std::string& model,
           const std::vector<std::string>& args);

}  // namespace rcli::harness

#endif  // RCLI_HARNESS_HARNESS_H
