#include <chrono>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "anthropic/messages.h"
#include "commands/commands.h"
#include "io/output.h"
#include "desktop/claude_profile.h"
#include "harness/harness.h"
#include "ide/jetbrains_profile.h"
#include "ide/openai_proxy.h"

namespace wally::commands {
namespace {

/// CLI11 callbacks return void, so a non-zero status leaves as the runtime
/// error the app turns back into an exit code.
void fail(int status) {
    if (status != 0) {
        throw CLI::RuntimeError(status);
    }
}


/// One editor or agent wally can point at a model.
///
/// The list is the whole integration surface: a new target is a row here plus
/// whatever `apply` has to set. Everything before that — resolving the model,
/// serving it, translating the wire format — is shared, which is the point.
/// How a tool is told where the model lives.
enum class Wiring {
    /// Variables in the launched process. Works for anything that reads
    /// ANTHROPIC_BASE_URL itself, or spawns something that does.
    Environment,
    /// Claude Desktop's third-party gateway profile, because it ignores the
    /// environment for authentication and says so.
    ClaudeProfile,
    /// AI Assistant's OpenAI-compatible provider, for a JetBrains IDE. The one
    /// wiring that needs no translator: the endpoint is already that shape.
    JetBrainsProvider,
};

struct Editor {
    /// What the reader types after `wally`.
    const char* id;
    /// An executable on PATH, or empty when this is a desktop app.
    const char* command;
    /// A macOS application bundle, or empty when `command` is on PATH.
    const char* bundle;
    const char* summary;
    Wiring wiring;
    /// Set only for Wiring::JetBrainsProvider.
    const ide::Product* jetbrains;
};

constexpr ide::Product kCLion{"clion", "CLion.app", "clion", "CLion"};
constexpr ide::Product kRustRover{"rustrover", "RustRover.app", "rustrover", "RustRover"};

/// Only tools that speak the Anthropic Messages API belong here. Anything
/// OpenAI-shaped needs no translator and goes through `wally opencode`.
///
/// Claude Desktop earns its place because it forwards a fixed set of variables
/// to the Claude Code it runs inside itself, and ANTHROPIC_BASE_URL is one of
/// them. That is the same trick as `wally claude-code`, one process further out.
constexpr Editor kEditors[] = {
    {"claude-code", "claude", "", "open Claude Code against a model", Wiring::Environment,
     nullptr},
    {"claude-desktop", "", "Claude.app", "open Claude Desktop against a model",
     Wiring::ClaudeProfile, nullptr},
    // A JetBrains IDE gets its own agent pointed at the model rather than a
    // second one nested inside it. Wiring the bundled Claude Agent through the
    // environment was the first attempt and bought nothing: the IDE already
    // ships AI Assistant and Junie, and the nested agent asks for its own
    // credential regardless.
    {"clion", "", "CLion.app", "open CLion against a model", Wiring::JetBrainsProvider,
     &kCLion},
    {"rustrover", "", "RustRover.app", "open RustRover against a model",
     Wiring::JetBrainsProvider, &kRustRover},
};

/// Where `editor`'s application bundle is, or empty when it is not installed.
std::string BundlePath(const Editor& editor) {
    if (editor.bundle[0] == '\0') {
        return {};
    }
    const char* home = std::getenv("HOME");
    std::vector<std::string> roots{"/Applications/"};
    if (home != nullptr) {
        roots.push_back(std::string(home) + "/Applications/");
    }
    for (const std::string& root : roots) {
        const std::string path = root + editor.bundle;
        std::ifstream probe(path + "/Contents/Info.plist");
        if (probe.good()) {
            return path;
        }
    }
    return {};
}

/// `open -n -W -a <bundle>`: a new instance, waited on until it quits.
///
/// Waiting is the point: the translator has to outlive the app exactly, and no
/// longer. `-n` is what makes the wait mean that. open(1) without it "waits
/// until the applications it opens **or that were already open** have exited",
/// so a copy the reader already had running would both miss the wiring and hold
/// the translator open behind it.
std::vector<std::string> OpenArgs(const std::string& bundle, const anthropic::Shim& shim,
                                  const std::vector<std::string>& passthrough) {
    std::vector<std::string> args{"-n", "-W"};
    if (shim.running) {
        // `open --env` is what carries them across; launchd would otherwise
        // start the app with the reader's login environment instead of ours.
        args.push_back("--env");
        args.push_back("ANTHROPIC_BASE_URL=" + shim.base_url);
        args.push_back("--env");
        args.push_back("ANTHROPIC_AUTH_TOKEN=" + shim.auth_token);
        args.push_back("--env");
        args.push_back("ANTHROPIC_API_KEY=" + shim.auth_token);
    }
    args.push_back("-a");
    args.push_back(bundle);
    if (!passthrough.empty()) {
        args.push_back("--args");
        args.insert(args.end(), passthrough.begin(), passthrough.end());
    }
    return args;
}

/// Whether a process named `name` is currently running. Matched on the
/// process name rather than the command line: `pgrep -f` would also match the
/// shell running the search, and report the IDE as alive forever.
bool ProcessRunning(const std::string& name) {
    const std::string probe = "pgrep -x " + name + " >/dev/null 2>&1";
    return harness::Launch("/bin/sh", {}, {"-c", probe}) == 0;
}

/// Blocks until `name` is running or gone, whichever `running` asks for.
/// `timeout` in seconds, or 0 to wait indefinitely.
void AwaitProcess(const std::string& name, bool running, int timeout) {
    for (int waited = 0; timeout == 0 || waited < timeout; waited += 2) {
        if (ProcessRunning(name) == running) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
}

/// Sets `name` for the child, remembering what was there so it can be undone.
class ScopedEnv {
  public:
    ScopedEnv(std::string name, const std::string& value) : name_(std::move(name)) {
        const char* previous = std::getenv(name_.c_str());
        had_previous_ = previous != nullptr;
        if (had_previous_) {
            previous_ = previous;
        }
        Set(value);
    }

    ~ScopedEnv() {
        if (had_previous_) {
            Set(previous_);
        } else {
#if defined(_WIN32)
            _putenv_s(name_.c_str(), "");
#else
            unsetenv(name_.c_str());
#endif
        }
    }

    ScopedEnv(const ScopedEnv&) = delete;
    ScopedEnv& operator=(const ScopedEnv&) = delete;

  private:
    void Set(const std::string& value) {
#if defined(_WIN32)
        _putenv_s(name_.c_str(), value.c_str());
#else
        setenv(name_.c_str(), value.c_str(), 1);
#endif
    }

    std::string name_;
    std::string previous_;
    bool had_previous_ = false;
};

/// Starts the translator and holds it open, printing what to point at it.
///
/// Worth having beyond debugging: it is how anything that speaks the Anthropic
/// API but is not on the list above gets wired up, without wally needing to know
/// that tool exists.
int Serve(const std::string& model, bool verbose) {
    harness::Endpoint endpoint;
    if (!harness::Resolve(model, &endpoint)) {
        return 1;
    }
    anthropic::Shim shim;
    if (!anthropic::Start(endpoint, model, &shim, verbose)) {
        harness::Release(endpoint);
        return 1;
    }
    out::result_line("ANTHROPIC_BASE_URL=" + shim.base_url);
    out::result_line("ANTHROPIC_AUTH_TOKEN=" + shim.auth_token);
    out::status_line("serving " + model + "; press Ctrl-C to stop");
    // No signal handling: Ctrl-C ends the process, and the OS reclaims the port
    // and the model. Anything subtler would be pretending this outlives it.
    for (;;) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

/// Puts the app back on Anthropic without starting anything.
///
/// The way out when a run was interrupted before it could undo itself, and the
/// same verb Ollama offers for the same reason.
int Restore(const Editor& editor) {
    std::string failure;
    if (editor.wiring == Wiring::JetBrainsProvider) {
        if (!ide::RestoreProvider(*editor.jetbrains, &failure)) {
            out::error_line(failure);
            return 1;
        }
        out::status_line(std::string(editor.id) + " no longer points at a local model");
        return 0;
    }
    if (!desktop::RestoreGateway(&failure)) {
        out::error_line(failure);
        return 1;
    }
    out::status_line(std::string(editor.id) + " is back on Anthropic; restart it to pick that up");
    return 0;
}

int Run(const Editor& editor, const std::string& model,
        const std::vector<std::string>& args, bool verbose) {
    const bool is_bundle = editor.bundle[0] != '\0';
    std::string bundle;
    if (is_bundle) {
#if defined(__APPLE__)
        bundle = BundlePath(editor);
        if (bundle.empty()) {
            out::error_line(std::string(editor.bundle) + " is not installed");
            return 1;
        }
#else
        out::error_line(std::string(editor.id) + " is a macOS application");
        return 1;
#endif
    }

    if (model.empty()) {
        // No model named means no wiring to do, so the tool runs exactly as the
        // reader has it configured. Same contract as `wally opencode`.
        return is_bundle ? harness::Launch("open", {}, OpenArgs(bundle, {}, args))
                         : harness::Launch(editor.command, {}, args);
    }

    harness::Endpoint endpoint;
    if (!harness::Resolve(model, &endpoint,
                          editor.wiring == Wiring::JetBrainsProvider ? ide::kProviderPort : 0)) {
        return 1;
    }

    if (editor.wiring == Wiring::JetBrainsProvider) {
        // No translator on this path. AI Assistant's provider speaks OpenAI,
        // which is what `Resolve` already handed us, so the IDE talks to the
        // model directly and nothing sits in between to get the wire format
        // wrong.
        // An upstream model arrives with a credential, and the IDE has no way
        // to take one from us. Keep it here and hand the IDE a loopback address
        // that needs none, which is the arrangement a local model already uses.
        ide::Proxy proxy;
        std::string reachable = endpoint.base_url;
        if (!endpoint.api_key.empty()) {
            if (!ide::StartProxy(endpoint, model, ide::kProviderPort, &proxy, verbose)) {
                harness::Release(endpoint);
                return 1;
            }
            reachable = proxy.base_url;
        }

        std::string failure;
        if (!ide::ApplyProvider(*editor.jetbrains, reachable, std::string(), model, &failure)) {
            out::error_line(failure);
            ide::StopProxy(&proxy);
            harness::Release(endpoint);
            return 1;
        }
        out::status_line(std::string(editor.id) + " will talk to " + model + " through " + reachable);
        // A second instance, never the running one: whatever the reader has open
        // in it — unsaved buffers, a debug session, terminal state — is not ours
        // to close because they named a model.
        //
        // Not `-W` here, unlike the Anthropic paths. The IDE reads the provider
        // settings at startup, so a second instance can still be handed off to
        // the copy already running by the IntelliJ platform's own single-instance
        // logic; `-W` would then return at once and pull the endpoint out from
        // under a live IDE. Waiting on the launcher process covers both.
        std::vector<std::string> open_args{"-n", "-a", bundle};
        if (!args.empty()) {
            open_args.push_back("--args");
            open_args.insert(open_args.end(), args.begin(), args.end());
        }
        const int status = harness::Launch("open", {}, open_args);
        if (status == 0) {
            AwaitProcess(editor.jetbrains->launcher, true, 60);
            out::status_line("serving " + model + " until " + std::string(editor.id) + " quits");
            AwaitProcess(editor.jetbrains->launcher, false, 0);
        }
        // The settings stay written on the way out. Nothing in them moves
        // between runs, so the configuration the reader sat through once is
        // never asked for again.
        ide::StopProxy(&proxy);
        harness::Release(endpoint);
        return status;
    }

    // Claude Desktop only lists gateway models it can map to an Anthropic
    // family, so the gateway answers under one of those ids while serving the
    // model the reader asked for. Only the desktop app needs this; the CLI
    // takes the real id happily.
    const std::string advertised =
        editor.wiring == Wiring::ClaudeProfile ? std::string("claude-sonnet-4-5") : model;

    anthropic::Shim shim;
    if (!anthropic::Start(endpoint, model, &shim, verbose, advertised)) {
        harness::Release(endpoint);
        return 1;
    }
    out::status_line(std::string(editor.id) + " will talk to " + model + " through " +
                shim.base_url);
    if (advertised != model) {
        out::status_line("advertised to the app as " + advertised + "; the picker shows " + model);
    }

    int status = 0;
    if (editor.wiring == Wiring::ClaudeProfile) {
        // The profile, not the environment. Written before the app starts and
        // taken back when it exits, so a crash here is the one case that leaves
        // it applied — which is what `--restore` is for.
        std::string failure;
        if (!desktop::ApplyGateway(shim.base_url, shim.auth_token, advertised, model,
                                   "RunAnywhere · " + model, &failure)) {
            out::error_line(failure);
            anthropic::Stop(&shim);
            harness::Release(endpoint);
            return 1;
        }
        // A new instance reads the gateway profile at startup. The one already
        // running keeps the profile it started with, and keeps whatever the
        // reader has open in it, which is the trade we want.
        status = harness::Launch("open", {}, OpenArgs(bundle, shim, args));
        if (!desktop::RestoreGateway(&failure)) {
            out::error_line(failure);
        }
    } else if (is_bundle) {
        // An app that reads the variables itself, or spawns something that
        // does. A process only ever gets the environment it was started with,
        // so the wiring reaches a new instance and not the running one — which
        // is the whole reason `OpenArgs` passes `-n`.
        status = harness::Launch("open", {}, OpenArgs(bundle, shim, args));
    } else {
        // Scoped so the reader's own environment is back before we report
        // anything, and before a later call in the same process reads it.
        const ScopedEnv base("ANTHROPIC_BASE_URL", shim.base_url);
        const ScopedEnv token("ANTHROPIC_AUTH_TOKEN", shim.auth_token);
        // An API key set in the environment outranks the token above and would
        // send the session to Anthropic instead of to us.
        const ScopedEnv key("ANTHROPIC_API_KEY", shim.auth_token);
        status = harness::Launch(editor.command, {}, args);
    }

    anthropic::Stop(&shim);
    harness::Release(endpoint);
    return status;
}

}  // namespace

void register_editors(CLI::App& app, GlobalOptions& options) {
    for (const Editor& editor : kEditors) {
        auto model = std::make_shared<std::string>();
        auto restore = std::make_shared<bool>(false);
        auto rest = std::make_shared<std::vector<std::string>>();
        auto serve = std::make_shared<bool>(false);
        auto* command = app.add_subcommand(editor.id, editor.summary);
        command->add_option("-m,--model", *model,
                            "a model on this machine, or one served upstream");
        command->add_flag("--serve", *serve,
                          "hold the endpoint open and print it, instead of launching");
        if (editor.wiring == Wiring::ClaudeProfile ||
            editor.wiring == Wiring::JetBrainsProvider) {
            command->add_flag("--restore", *restore,
                              "undo what we configured and launch nothing");
        }
        command->add_option("args", *rest, "passed through")->allow_extra_args();
        command->prefix_command();
        command->callback([&options, &editor, model, rest, serve, restore] {
            if (*restore) {
                fail(Restore(editor));
                return;
            }
            fail(*serve ? Serve(*model, options.verbose)
                        : Run(editor, *model, *rest, options.verbose));
        });
    }
}

}  // namespace wally::commands
