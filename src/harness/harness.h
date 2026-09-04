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

/// Where a model can be reached over HTTP, and whether we are serving it.
struct Endpoint {
    /// An OpenAI-compatible root, ending in `/v1`.
    std::string base_url;
    /// Empty for a local server, which ignores what is in the header.
    std::string api_key;
    /// True when `Resolve` started a server that `Release` has to stop.
    bool serving = false;
};

/// Points `endpoint` at `model`, starting a local server when the model is on
/// this machine and using the signed-in console when it is not.
///
/// `preferred_port` asks the local server for one particular port, and is
/// ignored when something else already holds it or the model is upstream.
/// An integration that writes the port into a file the tool reads at startup
/// wants this: the same port every run is what keeps that file true.
///
/// Returns false having already explained why not: an unknown model, a
/// framework the local server cannot load, or an upstream model with nobody
/// signed in. Every integration needs this same answer, so it is separate from
/// launching anything.
bool Resolve(const std::string& model, Endpoint* endpoint, int preferred_port = 0);

/// Stops whatever `Resolve` started. Safe on an endpoint it did not serve.
void Release(const Endpoint& endpoint);

/// Runs `tool` against `model`, forwarding `args` to it, and returns the tool's
/// exit code. Blocks until the tool exits, then stops anything it started.
///
/// An empty `model` uses whatever the tool is already configured for, which
/// makes `rcli opencode` a plain passthrough.
int Launch(const std::string& tool, const std::string& model,
           const std::vector<std::string>& args);

}  // namespace rcli::harness

#endif  // RCLI_HARNESS_HARNESS_H
