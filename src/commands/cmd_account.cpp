#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#if defined(_WIN32)
// gethostname is Winsock on Windows, not unistd, and Winsock needs starting
// before it answers. src/harness does the same for its port probe.
#include <process.h>
#include <winsock2.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "account/console.h"
#include "account/credentials.h"
#include "bootstrap.h"
#include "commands/commands.h"
#include "io/output.h"

namespace rcli::commands {
namespace {

/// CLI11 callbacks return void, so a non-zero status leaves as the runtime
/// error the app turns back into an exit code.
void fail(int status) {
    if (status != 0) {
        throw CLI::RuntimeError(status);
    }
}

std::string Hostname() {
#if defined(_WIN32)
    WSADATA data;
    static const bool ready = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    if (!ready) {
        return "unknown";
    }
#endif
    char name[256] = {};
    if (gethostname(name, sizeof(name) - 1) == 0 && name[0] != '\0') {
        return name;
    }
    return "unknown";
}

/// The URL arrives in a console response and the console itself is whatever
/// RCLI_CONSOLE_URL names, so it is untrusted input. It never reaches a shell:
/// the opener is executed with an argument vector, and only after the scheme
/// has been checked.
bool OpensSafely(const std::string& url) {
    return url.rfind("https://", 0) == 0 || url.rfind("http://", 0) == 0;
}

void OpenBrowser(const std::string& url) {
    if (!OpensSafely(url)) {
        out::status_line("refusing to open a URL that is not http or https");
        return;
    }
#if defined(_WIN32)
    const intptr_t rc = _spawnlp(_P_NOWAIT, "rundll32", "rundll32", "url.dll,FileProtocolHandler",
                                 url.c_str(), nullptr);
    if (rc < 0) {
        out::status_line("could not open a browser for you");
    }
#else
#if defined(__APPLE__)
    const char* opener = "open";
#else
    const char* opener = "xdg-open";
#endif
    const pid_t child = fork();
    if (child < 0) {
        out::status_line("could not open a browser for you");
        return;
    }
    if (child == 0) {
        const int devnull = ::open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        std::string target = url;
        char* argv[] = {const_cast<char*>(opener), target.data(), nullptr};
        execvp(opener, argv);
        _exit(127);
    }
    int status = 0;
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
    }
#endif
}

/// Brings the SDK up so the console can be reached.
///
/// The CLI used to carry its own lazy Start(); the kit consumer brings the SDK
/// up through bootstrap, which also resolves the control-plane connection, so
/// the options have to reach here rather than being read from a global.
bool Ready(const GlobalOptions& options) {
    Bootstrapped env;
    if (bootstrap(options, &env) == RAC_SUCCESS) {
        return true;
    }
    out::error_line("the SDK would not start, so the console cannot be reached");
    return false;
}

int Login(const GlobalOptions& options, bool open_browser) {
    if (!Ready(options)) {
        return 1;
    }
    account::Credentials credentials = account::Load();
    if (!account::ConsoleUrlIsSafe(credentials.console_url)) {
        out::error_line("refusing to send credentials to " + credentials.console_url);
        out::status_line("the console must be https, or http on loopback");
        return 1;
    }

    account::Authorization authorization;
    std::string failure;
    if (!account::BeginAuthorization(credentials.console_url, Hostname(), &authorization,
                                     &failure)) {
        out::error_line(failure);
        return 1;
    }

    out::status_line("approve this sign-in in your browser:");
    out::result_line(authorization.verification_url);
    if (open_browser) {
        OpenBrowser(authorization.verification_url);
    }
    out::status_line("waiting for approval");

    // A console that omits expires_in would otherwise put the deadline in the
    // past, so the loop never runs and the user is told it timed out before a
    // single poll was sent.
    const int window = authorization.expires_in > 0 ? authorization.expires_in : 600;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(window);
    account::Grant grant;
    while (std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::seconds(authorization.interval));
        switch (account::Poll(credentials.console_url, authorization, &grant, &failure)) {
            case account::PollResult::Pending:
                continue;
            case account::PollResult::Denied:
                out::error_line("the request was denied in the browser");
                return 1;
            case account::PollResult::Expired:
                out::error_line("the request expired before it was approved");
                return 1;
            case account::PollResult::Failed:
                out::error_line(failure);
                return 1;
            case account::PollResult::Approved:
                credentials.email = grant.email;
                credentials.plan = grant.plan;
                credentials.access_token = grant.access_token;
                credentials.refresh_token = grant.refresh_token;
                if (!account::Save(credentials, &failure)) {
                    out::error_line(failure);
                    return 1;
                }
                out::status_line("signed in as " + grant.email + " on the " + grant.plan + " plan");
                out::status_line("credentials in " + account::ProfileDirectory());
                return 0;
        }
    }
    out::error_line("timed out waiting for approval");
    return 1;
}

int Logout(const GlobalOptions& options) {
    const account::Credentials credentials = account::Load();
    if (!credentials.signed_in()) {
        out::status_line("not signed in");
        return 0;
    }
    std::string failure;
    if (!account::Clear(&failure)) {
        out::error_line(failure);
        return 1;
    }
    out::status_line("signed out " + credentials.email);
    out::status_line("revoke the token in the console to end it everywhere");
    return 0;
}

int WhoAmI(const GlobalOptions& options) {
    if (!Ready(options)) {
        return 1;
    }
    account::Credentials credentials = account::Load();
    if (!credentials.signed_in()) {
        out::error_line("not signed in — run `rcli login`");
        return 1;
    }

    account::Identity identity;
    std::string failure;
    if (!account::WhoAmI(credentials.console_url, credentials.access_token, &identity, &failure)) {
        // An access token lives eight hours, so a working install meets this
        // routinely. Trading the refresh token for a new one is the normal path,
        // not an error worth showing.
        account::Grant grant;
        if (credentials.refresh_token.empty() ||
            !account::Refresh(credentials.console_url, credentials.refresh_token, &grant,
                              &failure)) {
            out::error_line(failure.empty() ? "the session has expired — run `rcli login`" : failure);
            return 1;
        }
        credentials.access_token = grant.access_token;
        credentials.refresh_token = grant.refresh_token;
        credentials.email = grant.email;
        credentials.plan = grant.plan;
        // The console has already rotated the refresh token, so losing this
        // write costs the session. Say so rather than failing silently on the
        // next command.
        if (!account::Save(credentials, &failure)) {
            out::status_line("could not store the refreshed session: " + failure);
        }
        if (!account::WhoAmI(credentials.console_url, credentials.access_token, &identity,
                             &failure)) {
            out::error_line(failure);
            return 1;
        }
    }

    char line[220];
    std::snprintf(line, sizeof(line), "%-14s %s", "email", identity.email.c_str());
    out::result_line(line);
    std::snprintf(line, sizeof(line), "%-14s %s", "plan", identity.plan.c_str());
    out::result_line(line);
    std::snprintf(line, sizeof(line), "%-14s %ld of %ld", "tokens", identity.tokens_this_month,
                  identity.monthly_token_limit);
    out::result_line(line);
    std::snprintf(line, sizeof(line), "%-14s %s", "console", credentials.console_url.c_str());
    out::result_line(line);
    std::snprintf(line, sizeof(line), "%-14s %s", "profile", account::ProfileDirectory().c_str());
    out::result_line(line);
    return 0;
}

}  // namespace

void register_account(CLI::App& app, GlobalOptions& options) {
    auto no_browser = std::make_shared<bool>(false);
    auto* login = app.add_subcommand("login", "sign in through the RunAnywhere console");
    login->add_flag("--no-browser", *no_browser, "print the URL instead of opening it");
    login->callback([&options, no_browser] { fail(Login(options, !*no_browser)); });

    auto* logout = app.add_subcommand("logout", "forget the credentials on this machine");
    logout->callback([&options] { fail(Logout(options)); });

    auto* whoami = app.add_subcommand("whoami", "who is signed in, and how much they have used");
    whoami->callback([&options] { fail(WhoAmI(options)); });
}

}  // namespace rcli::commands
