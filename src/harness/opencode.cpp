#include "harness/opencode.h"

#include "harness/harness.h"

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>

#include <sys/wait.h>
#endif

#include "account/credentials.h"
#include "io/output.h"

namespace rcli::harness {
namespace {

constexpr const char* kOpenCodeConfigVariable = "OPENCODE_CONFIG_CONTENT";



bool SetEnvironment(const char* name, const std::string& value) {
#if defined(_WIN32)
    return _putenv_s(name, value.c_str()) == 0;
#else
    return setenv(name, value.c_str(), 1) == 0;
#endif
}

bool UnsetEnvironment(const char* name) {
#if defined(_WIN32)
    return _putenv_s(name, "") == 0;
#else
    return unsetenv(name) == 0;
#endif
}

class ScopedOpenCodeConfig {
   public:
    ScopedOpenCodeConfig() {
        const char* previous = std::getenv(kOpenCodeConfigVariable);
        if (previous != nullptr) {
            had_previous_ = true;
            previous_ = previous;
        }
    }

    ScopedOpenCodeConfig(const ScopedOpenCodeConfig&) = delete;
    ScopedOpenCodeConfig& operator=(const ScopedOpenCodeConfig&) = delete;

    ~ScopedOpenCodeConfig() {
        if (!active_) {
            return;
        }
        if (had_previous_) {
            static_cast<void>(SetEnvironment(kOpenCodeConfigVariable, previous_));
        } else {
            static_cast<void>(UnsetEnvironment(kOpenCodeConfigVariable));
        }
    }

    bool Activate(const std::string& value) {
        active_ = SetEnvironment(kOpenCodeConfigVariable, value);
        return active_;
    }

   private:
    std::string previous_;
    bool had_previous_ = false;
    bool active_ = false;
};

/// Not an error line. Nothing went wrong — the tool simply is not here yet,
/// and the only useful thing to say is how to get it.
void MissingOpenCode() {
    out::status_line("opencode is not installed on this machine");
    out::status_line("install it with `npm i -g opencode-ai`, then run this again");
}

int Spawn(const std::string& executable, const std::vector<std::string>& arguments) {
    std::vector<std::string> owned;
    owned.reserve(arguments.size() + 1);
    owned.push_back(executable);
    owned.insert(owned.end(), arguments.begin(), arguments.end());

    std::vector<char*> argv;
    argv.reserve(owned.size() + 1);
    for (std::string& value : owned) {
        argv.push_back(value.data());
    }
    argv.push_back(nullptr);

#if defined(_WIN32)
    const intptr_t status = _spawnvp(_P_WAIT, executable.c_str(), argv.data());
    if (status < 0) {
        MissingOpenCode();
        return 127;
    }
    return static_cast<int>(status);
#else
    const pid_t child = fork();
    if (child < 0) {
        out::error_line("could not start OpenCode");
        return 1;
    }
    if (child == 0) {
        execvp(executable.c_str(), argv.data());
        _exit(127);
    }

    int status = 0;
    while (waitpid(child, &status, 0) < 0) {
        if (errno == EINTR) {
            continue;
        }
        out::error_line("lost track of OpenCode");
        return 1;
    }
    if (!WIFEXITED(status)) {
        return 1;
    }
    const int exit_code = WEXITSTATUS(status);
    if (exit_code == 127) {
        MissingOpenCode();
    }
    return exit_code;
#endif
}

}  // namespace

std::string BuildOpenCodeCloudConfig(const std::string& model, const std::string& base_url,
                                     const std::string& access_token) {
    using Json = nlohmann::json;
    const Json provider = {
        {"npm", "@ai-sdk/openai-compatible"},
        {"name", "RunAnywhere"},
        {"options", {{"baseURL", base_url}, {"apiKey", access_token}}},
        {"models", {{model, {{"name", model}}}}},
    };
    return Json{{"provider", {{"runanywhere", provider}}}, {"model", "runanywhere/" + model}}
        .dump();
}

int LaunchOpenCodeCloud(const std::string& model, const std::vector<std::string>& arguments,
                        const account::ConsoleClient& console, const SpawnFunction& spawn) {
    // The same two gates the local path gets. This function does not go through
    // harness::Resolve(), so before this it had its own weaker model check
    // (control characters only, so `/` and `<` sailed through) and trusted
    // `signed_in()` — a non-empty string — as proof of a session. A fabricated
    // token launched a real editor against the hosted endpoint.
    if (!ModelIdIsSafe(model)) {
        out::error_line("'" + model + "' is not a valid model id");
        return 2;
    }

    account::Credentials credentials;
    std::string error;
    if (!account::Load(&credentials, &error)) {
        out::error_line(error);
        return 1;
    }
    if (!credentials.signed_in()) {
        out::error_line("not signed in - run `rcli login`");
        return 1;
    }
    if (!VerifyCloudSession(console, &credentials, nullptr, &error)) {
        out::error_line("the signed-in cloud session did not check out: " + error);
        return 1;
    }

    const std::string base_url = credentials.console_url + "/v1";
    const std::string config = BuildOpenCodeCloudConfig(model, base_url, credentials.access_token);
    ScopedOpenCodeConfig environment;
    if (!environment.Activate(config)) {
        out::error_line("could not set the temporary OpenCode configuration");
        return 1;
    }

    out::status_line("launching OpenCode with the RunAnywhere cloud session");
    return spawn("opencode", arguments);
}

int LaunchOpenCodeCloud(const std::string& model, const std::vector<std::string>& arguments) {
    const account::ConsoleClient console;
    return LaunchOpenCodeCloud(model, arguments, console, Spawn);
}

}  // namespace rcli::harness
