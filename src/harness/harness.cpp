#include "harness/harness.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#include <winsock2.h>
// Winsock spells these differently: a socket is an unsigned SOCKET rather than
// a file descriptor, and getsockname takes an int length rather than socklen_t.
using rcli_socklen_t = int;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
using rcli_socklen_t = socklen_t;
#endif

#include "rac/server/rac_server.h"

#include "account/credentials.h"
#include "cli/commands.h"
#include "cli/output.h"
#include "sdk/llm.h"
#include "sdk/session.h"
#include "settings/settings.h"

namespace rcli::harness {
namespace {

using out::Ink;

/// A port nothing is listening on, found by letting the OS pick one and giving
/// it straight back. There is a race between closing and the server binding,
/// but the alternative is a fixed port that collides with a second rcli.
///
/// `preferred` asks for one particular port and settles for any free one when
/// it is taken. An integration that writes the port into a config file wants
/// that: the file keeps working between runs instead of naming a dead port.
int FreePort(int preferred) {
#if defined(_WIN32)
    // Winsock has to be initialised before any socket call, and the server that
    // would otherwise do it has not started yet. The count is per-process and
    // refcounted, so starting it here and leaving it up is harmless.
    static const bool ready = [] {
        WSADATA data;
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    if (!ready) {
        return 0;
    }
    const SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    // INVALID_SOCKET, not a negative number: SOCKET is unsigned on Windows, so
    // the usual `< 0` check silently passes for a failed call.
    if (sock == INVALID_SOCKET) {
        return 0;
    }
#else
    const int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return 0;
    }
#endif
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(static_cast<uint16_t>(preferred));
    int port = 0;
    if (bind(sock, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0) {
        rcli_socklen_t length = static_cast<rcli_socklen_t>(sizeof(address));
        if (getsockname(sock, reinterpret_cast<sockaddr*>(&address), &length) == 0) {
            port = ntohs(address.sin_port);
        }
    }
#if defined(_WIN32)
    closesocket(sock);
#else
    close(sock);
#endif
    if (port == 0 && preferred != 0) {
        return FreePort(0);
    }
    return port;
}

/// JSON string escaping, for the handful of characters that can appear in a
/// model id, a path or a key. Not a general encoder: it exists so a Windows
/// path with backslashes does not silently produce invalid config.
std::string Quote(const std::string& text) {
    std::string out = "\"";
    for (const char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c;
        }
    }
    return out + "\"";
}

/// The provider block opencode reads out of OPENCODE_CONFIG_CONTENT.
///
/// Inline rather than a file on purpose: writing to the user's project or to
/// ~/.config/opencode would outlive the session and change how opencode behaves
/// when they run it themselves.
std::string OpencodeConfig(const std::string& model, const std::string& base_url,
                           const std::string& api_key) {
    // A key is always present because opencode's OpenAI client sends an
    // Authorization header regardless; a local server ignores what is in it.
    const std::string key = api_key.empty() ? std::string("local") : api_key;
    return std::string("{\"provider\":{\"runanywhere\":{") +
           "\"npm\":\"@ai-sdk/openai-compatible\"," + "\"name\":\"RunAnywhere\"," +
           "\"options\":{\"baseURL\":" + Quote(base_url) + ",\"apiKey\":" + Quote(key) + "}," +
           "\"models\":{" + Quote(model) + ":{\"name\":" + Quote(model) + "}}}}," +
           "\"model\":" + Quote("runanywhere/" + model) + "}";
}

constexpr const char* kConfigVariable = "OPENCODE_CONFIG_CONTENT";

void SetConfigVariable(const std::string& value) {
#if defined(_WIN32)
    _putenv_s(kConfigVariable, value.c_str());
#else
    setenv(kConfigVariable, value.c_str(), 1);
#endif
}

void UnsetConfigVariable() {
#if defined(_WIN32)
    _putenv_s(kConfigVariable, "");
#else
    unsetenv(kConfigVariable);
#endif
}

int Spawn(const std::string& tool, const std::vector<std::string>& args) {
    std::vector<std::string> owned;
    owned.push_back(tool);
    owned.insert(owned.end(), args.begin(), args.end());
    std::vector<char*> argv;
    argv.reserve(owned.size() + 1);
    for (std::string& piece : owned) {
        argv.push_back(piece.data());
    }
    argv.push_back(nullptr);

#if defined(_WIN32)
    const intptr_t rc = _spawnvp(_P_WAIT, tool.c_str(), argv.data());
    if (rc < 0) {
        out::Error(tool + " is not on PATH");
        return 127;
    }
    return static_cast<int>(rc);
#else
    // fork rather than exec: the local server lives in this process, and
    // replacing the image would take it down with us before the tool ran.
    const pid_t child = fork();
    if (child < 0) {
        out::Error("could not start " + tool);
        return 1;
    }
    if (child == 0) {
        execvp(tool.c_str(), argv.data());
        // Only reached when exec failed. 127 is what a shell reports for a
        // command it cannot find, and the parent cannot tell why otherwise.
        _exit(127);
    }
    int status = 0;
    // status is undefined after a failed waitpid, so a bare 0 there would read
    // as a clean exit while the child is still running.
    while (waitpid(child, &status, 0) < 0) {
        if (errno == EINTR) {
            continue;
        }
        out::Error("lost track of " + tool);
        return 1;
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return 1;
#endif
}

}  // namespace

bool Resolve(const std::string& model, Endpoint* endpoint, int preferred_port) {
    if (endpoint == nullptr || model.empty()) {
        return false;
    }
    if (!rcli::cli::Start()) {
        return false;
    }

    std::string base_url;
    std::string api_key;
    bool serving = false;

    const sdk::LocalModel* local = nullptr;
    const std::vector<sdk::LocalModel> installed = sdk::LocalModels();
    for (const sdk::LocalModel& candidate : installed) {
        if (candidate.id == model && candidate.complete) {
            local = &candidate;
            break;
        }
    }

    if (local != nullptr) {
        // The server creates its handle with rac_llm_create(path), which routes
        // on the path alone rather than asking the registry what framework the
        // model belongs to. An MLX directory does not look like anything it
        // recognises, so it lands on llama.cpp and fails to load. Saying so
        // beats starting a server that answers every request with an error.
        if (local->framework != "LlamaCpp") {
            out::Error(model + " runs on " + local->framework +
                       ", and the local server can only serve LlamaCpp models today");
            out::Status("use a GGUF model here, or point at an upstream one");
            return false;
        }
        const int port = FreePort(preferred_port);
        if (port == 0) {
            out::Error("could not find a free port for the local server");
            return false;
        }
        rac_server_config_t config = RAC_SERVER_CONFIG_DEFAULT;
        config.host = "127.0.0.1";
        config.port = static_cast<uint16_t>(port);
        const std::string path = local->path.empty() ? local->dir : local->path;
        config.model_path = path.c_str();
        config.model_id = model.c_str();
        config.context_size = settings::ContextLength() > 0 ? settings::ContextLength() : 8192;
        out::Status("serving " + model + " on 127.0.0.1:" + std::to_string(port));
        if (rac_server_start(&config) != RAC_SUCCESS) {
            out::Error("the local server would not start for " + model);
            return false;
        }
        serving = true;
        base_url = "http://127.0.0.1:" + std::to_string(port) + "/v1";
    } else {
        const account::Credentials credentials = account::Load();
        if (!credentials.signed_in()) {
            out::Error(model + " is not on this machine, and you are not signed in");
            out::Status("run `rcli login`, or `rcli pull " + model + "` to run it here");
            return false;
        }
        base_url = credentials.console_url + "/v1";
        api_key = credentials.access_token;
        out::Status("using " + model + " as " + credentials.email);
    }

    endpoint->base_url = base_url;
    endpoint->api_key = api_key;
    endpoint->serving = serving;
    return true;
}

void Release(const Endpoint& endpoint) {
    if (endpoint.serving) {
        rac_server_stop();
    }
}

int Launch(const std::string& tool, const std::string& model,
           const std::vector<std::string>& args) {
    if (model.empty()) {
        // Nothing to wire, so do not pretend to: run the tool as the user has
        // it configured.
        return Spawn(tool, args);
    }

    Endpoint endpoint;
    if (!Resolve(model, &endpoint)) {
        return 1;
    }

    const std::string config = OpencodeConfig(model, endpoint.base_url, endpoint.api_key);
    const char* previous = std::getenv(kConfigVariable);
    const std::string restored = previous != nullptr ? previous : std::string();
    const bool had_previous = previous != nullptr;
    SetConfigVariable(config);

    const int status = Spawn(tool, args);

    // Launch runs more than once in a process during tests, and a stale value
    // here would override the tool's own configuration on a later call that
    // named no model.
    if (had_previous) {
        SetConfigVariable(restored);
    } else {
        UnsetConfigVariable();
    }
    Release(endpoint);
    return status;
}

}  // namespace rcli::harness
