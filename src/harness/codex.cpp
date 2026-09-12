#include "harness/codex.h"

#include "harness/harness.h"

#include <cerrno>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>

#include <sys/wait.h>
#endif

#include "account/credentials.h"
#include "io/output.h"

namespace wally::harness {
namespace {

namespace fs = std::filesystem;

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

/// Restores one environment variable to whatever it was, or removes it if it
/// had no prior value. Copied rather than shared with opencode's because the
/// two harnesses set different variables and must not couple.
class ScopedEnv {
   public:
    ScopedEnv() = default;
    ScopedEnv(const ScopedEnv&) = delete;
    ScopedEnv& operator=(const ScopedEnv&) = delete;

    ~ScopedEnv() {
        if (name_ == nullptr) {
            return;
        }
        if (had_previous_) {
            static_cast<void>(SetEnvironment(name_, previous_));
        } else {
            static_cast<void>(UnsetEnvironment(name_));
        }
    }

    bool Set(const char* name, const std::string& value) {
        name_ = name;
        if (const char* previous = std::getenv(name)) {
            had_previous_ = true;
            previous_ = previous;
        }
        return SetEnvironment(name, value);
    }

   private:
    const char* name_ = nullptr;
    std::string previous_;
    bool had_previous_ = false;
};

/// A throwaway CODEX_HOME holding just config.toml, wiped on destruction. Codex
/// reads its provider from $CODEX_HOME/config.toml, so this is how a launch
/// points Codex at the RunAnywhere endpoint without touching the user's own
/// ~/.codex.
class ScopedCodexHome {
   public:
    ScopedCodexHome() = default;
    ScopedCodexHome(const ScopedCodexHome&) = delete;
    ScopedCodexHome& operator=(const ScopedCodexHome&) = delete;

    ~ScopedCodexHome() {
        if (dir_.empty()) {
            return;
        }
        std::error_code ec;
        fs::remove_all(dir_, ec);
    }

    /// Create the directory and write config.toml into it. Returns false and
    /// leaves nothing behind on any failure.
    bool Write(const std::string& config) {
        std::error_code ec;
        std::random_device rd;
        const fs::path base = fs::temp_directory_path(ec);
        if (ec) {
            return false;
        }
        const fs::path candidate =
            base / ("wally-codex-" + std::to_string(rd()) + std::to_string(rd()));
        if (!fs::create_directory(candidate, ec) || ec) {
            return false;
        }
        // Owner-only: CODEX_HOME can hold config.toml and, if Codex ever writes
        // one, an auth file, so it must not be world- or group-readable in the
        // shared temp directory. Best-effort; a filesystem without POSIX perms
        // (or Windows) simply keeps its own default ACLs.
        fs::permissions(candidate, fs::perms::owner_all, fs::perm_options::replace, ec);
        dir_ = candidate;
        std::ofstream out(dir_ / "config.toml", std::ios::binary | std::ios::trunc);
        out << config;
        out.flush();
        if (!out) {
            fs::remove_all(dir_, ec);
            dir_.clear();
            return false;
        }
        return true;
    }

    const fs::path& dir() const { return dir_; }

   private:
    fs::path dir_;
};

/// Not an error line. Codex simply is not here yet; the useful thing is how to
/// get it. Codex is distributed on npm as @openai/codex.
void MissingCodex() {
    out::status_line("codex is not installed on this machine");
    out::status_line("install it with `npm i -g @openai/codex`, then run this again");
}

#if defined(_WIN32)
// Quote one argument so the child re-parses it as a single token. The _spawn*
// family joins argv into a command line WITHOUT quoting, so an argument that
// contains a space would otherwise arrive split in two. Rules per the
// documented MSVCRT parser: double the run of backslashes that precedes a quote
// (or the closing quote), and backslash-escape embedded quotes. The POSIX path
// needs none of this -- execvp hands argv to the child verbatim.
std::string QuoteWindowsArg(const std::string& arg) {
    if (!arg.empty() && arg.find_first_of(" \t\n\v\"") == std::string::npos) {
        return arg;
    }
    std::string quoted = "\"";
    for (std::size_t i = 0;; ++i) {
        std::size_t backslashes = 0;
        while (i < arg.size() && arg[i] == '\\') {
            ++i;
            ++backslashes;
        }
        if (i == arg.size()) {
            quoted.append(backslashes * 2, '\\');
            break;
        }
        if (arg[i] == '"') {
            quoted.append(backslashes * 2 + 1, '\\');
            quoted.push_back('"');
        } else {
            quoted.append(backslashes, '\\');
            quoted.push_back(arg[i]);
        }
    }
    quoted.push_back('"');
    return quoted;
}
#endif

int Spawn(const std::string& executable, const std::vector<std::string>& arguments) {
#if defined(_WIN32)
    std::vector<std::string> owned;
    owned.reserve(arguments.size() + 1);
    owned.push_back(QuoteWindowsArg(executable));
    for (const std::string& argument : arguments) {
        owned.push_back(QuoteWindowsArg(argument));
    }
    std::vector<char*> argv;
    argv.reserve(owned.size() + 1);
    for (std::string& value : owned) {
        argv.push_back(value.data());
    }
    argv.push_back(nullptr);

    const intptr_t status = _spawnvp(_P_WAIT, executable.c_str(), argv.data());
    if (status < 0) {
        MissingCodex();
        return 127;
    }
    return static_cast<int>(status);
#else
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

    const pid_t child = fork();
    if (child < 0) {
        out::error_line("could not start Codex");
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
        out::error_line("lost track of Codex");
        return 1;
    }
    if (!WIFEXITED(status)) {
        return 1;
    }
    const int exit_code = WEXITSTATUS(status);
    if (exit_code == 127) {
        MissingCodex();
    }
    return exit_code;
#endif
}

}  // namespace

std::string BuildCodexConfig(const std::string& model, const std::string& base_url) {
    // TOML basic strings. `model` is ModelIdIsSafe-checked and `base_url` is the
    // console URL wally itself holds, so neither carries a quote or newline to
    // escape here.
    std::string config;
    config += "model = \"" + model + "\"\n";
    config += "model_provider = \"runanywhere\"\n\n";
    config += "[model_providers.runanywhere]\n";
    config += "name = \"RunAnywhere\"\n";
    config += "base_url = \"" + base_url + "\"\n";
    config += "env_key = \"" + std::string(kCodexKeyEnvVar) + "\"\n";
    config += "wire_api = \"responses\"\n";
    return config;
}

int LaunchCodexCloud(const std::string& model, const std::vector<std::string>& arguments,
                     const account::ConsoleClient& console, const SpawnFunction& spawn) {
    // The same gates the opencode cloud path gets: a real model id and a
    // verified session, never a fabricated token launching a real agent.
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
        out::error_line("not signed in - run `wally login`");
        return 1;
    }
    bool unverified = false;
    if (!VerifyCloudSession(console, &credentials, nullptr, &error, &unverified)) {
        if (!unverified) {
            out::error_line("cannot use the cloud session: " + error);
            return 1;
        }
        // The console could not be asked right now. That is not a disproof of
        // the session already on disk, and refusing here locks a signed-in
        // person out of their harness over a transient 429 (InferenceInfra#444).
        // Go in on the stored session; the harness's own calls surface the real
        // error if it is still there.
        out::status_line("could not confirm the cloud session (" + error +
                         ") - continuing on the stored session");
    }

    const std::string base_url = credentials.console_url + "/v1";
    ScopedCodexHome home;
    if (!home.Write(BuildCodexConfig(model, base_url))) {
        out::error_line("could not write the temporary Codex configuration");
        return 1;
    }

    ScopedEnv codex_home;
    ScopedEnv codex_key;
    if (!codex_home.Set("CODEX_HOME", home.dir().string()) ||
        !codex_key.Set(kCodexKeyEnvVar, credentials.access_token)) {
        out::error_line("could not set the temporary Codex environment");
        return 1;
    }

    out::status_line("launching Codex (Responses API) with the RunAnywhere cloud session");
    return spawn("codex", arguments);
}

int LaunchCodexCloud(const std::string& model, const std::vector<std::string>& arguments) {
    const account::ConsoleClient console;
    return LaunchCodexCloud(model, arguments, console, Spawn);
}

}  // namespace wally::harness
