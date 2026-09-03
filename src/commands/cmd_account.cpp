#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>

#if defined(_WIN32)
#include <process.h>
#include <winsock2.h>
#else
#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <sys/wait.h>
#endif

#include "account/console.h"
#include "account/credentials.h"
#include "commands/commands.h"
#include "io/output.h"

namespace rcli::commands {
namespace {

void fail(int status) {
    if (status != 0) {
        throw CLI::RuntimeError(status);
    }
}

/// Send the approval link to a console of your choosing.
///
/// The control plane builds the approval URL from its own configured console
/// origin, so a locally served console is otherwise unreachable: sign-in keeps
/// opening the deployed one no matter where the CLI is pointed. The origin-match
/// check above still runs against what the server sent; this replaces the origin
/// only afterwards, only from the environment, and only with an origin that
/// passes the same rules — the path and request code stay exactly as sent.
/// The console origin the operator declared, normalised, or empty if none.
std::string ConsoleWebOrigin() {
    const char* configured = std::getenv("RCLI_CONSOLE_WEB_URL");
    std::string origin;
    if (configured == nullptr || *configured == '\0' ||
        !account::NormalizeConsoleUrl(configured, &origin, nullptr)) {
        return {};
    }
    return origin;
}

std::string RebaseApprovalUrl(const std::string& url) {
    const std::string origin = ConsoleWebOrigin();
    if (origin.empty()) {
        return url;
    }
    const std::size_t scheme = url.find("://");
    if (scheme == std::string::npos) {
        return url;
    }
    const std::size_t path = url.find('/', scheme + 3);
    return path == std::string::npos ? origin : origin + url.substr(path);
}

long long EpochSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
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
    return gethostname(name, sizeof(name) - 1) == 0 && name[0] != '\0' ? name : "unknown";
}

void OpenBrowser(const std::string& url) {
#if defined(_WIN32)
    const intptr_t rc = _spawnlp(_P_NOWAIT, "rundll32", "rundll32", "url.dll,FileProtocolHandler",
                                 url.c_str(), nullptr);
    if (rc < 0) {
        out::status_line("could not open a browser; use the URL printed above");
    }
#else
#if defined(__APPLE__)
    const char* opener = "open";
#else
    const char* opener = "xdg-open";
#endif
    const pid_t child = fork();
    if (child < 0) {
        out::status_line("could not open a browser; use the URL printed above");
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
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
#endif
}

bool LoadCredentials(account::Credentials* credentials) {
    std::string failure;
    if (!account::Load(credentials, &failure)) {
        out::error_line(failure);
        return false;
    }
    return true;
}

void ApplyGrant(const account::Grant& grant, account::Credentials* credentials) {
    credentials->access_token = grant.access_token;
    if (!grant.refresh_token.empty()) {
        credentials->refresh_token = grant.refresh_token;
    }
    if (!grant.email.empty()) {
        credentials->email = grant.email;
    }
    credentials->expires_at = EpochSeconds() + (grant.expires_in > 0 ? grant.expires_in : 3600);
}

bool RefreshSession(const account::ConsoleClient& client, account::Credentials* credentials,
                    std::string* error) {
    if (credentials->refresh_token.empty()) {
        if (error != nullptr) {
            *error = "the cloud session cannot be refreshed; run `rcli login`";
        }
        return false;
    }
    account::Grant grant;
    if (!client.Refresh(credentials->console_url, credentials->refresh_token, &grant, error)) {
        return false;
    }
    ApplyGrant(grant, credentials);
    return account::Save(*credentials, error);
}

int Login(const std::string& requested_console, bool open_browser) {
    std::string console_url;
    std::string failure;
    const std::string configured =
        requested_console.empty() ? account::DefaultConsoleUrl() : requested_console;
    if (!account::NormalizeConsoleUrl(configured, &console_url, &failure)) {
        out::error_line(failure);
        return 1;
    }

    account::ConsoleClient client;
    account::Authorization authorization;
    if (!client.BeginAuthorization(console_url, Hostname(), &authorization, &failure)) {
        out::error_line(failure);
        return 1;
    }
    // Rebase first, then check what we will actually open.
    //
    // "Must match the API's own origin" was the wrong test: the control plane
    // runs on Cloud Run and the console it hands you runs on Railway, so the two
    // are never equal and the check refused every real sign-in. The origin we
    // trust is the console origin the operator declared; with none declared we
    // fall back to the old behaviour and demand the API's own.
    const std::string trusted = ConsoleWebOrigin();
    const std::string approval_url = RebaseApprovalUrl(authorization.verification_url);
    if (!account::BrowserUrlMatchesConsole(approval_url,
                                           trusted.empty() ? console_url : trusted)) {
        out::error_line("console returned an approval URL outside its origin");
        return 1;
    }

    out::status_line("approve this sign-in in your browser");
    out::result_line("code  " + authorization.request_code);
    out::result_line("url   " + approval_url);
    if (open_browser) {
        OpenBrowser(approval_url);
    }
    out::status_line("waiting for approval");

    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(authorization.expires_in);
    account::Grant grant;
    while (std::chrono::steady_clock::now() < deadline) {
        switch (client.Poll(console_url, authorization, &grant, &failure)) {
            case account::PollResult::Pending:
                std::this_thread::sleep_for(std::chrono::seconds(authorization.interval));
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
            case account::PollResult::Approved: {
                account::Credentials credentials;
                credentials.console_url = console_url;
                ApplyGrant(grant, &credentials);
                if (!account::Save(credentials, &failure)) {
                    out::error_line(failure);
                    return 1;
                }
                const std::string identity =
                    credentials.email.empty() ? "your account" : credentials.email;
                out::status_line("signed in as " + identity);
                out::status_line("cloud session stored in " + account::ProfileDirectory());
                return 0;
            }
        }
    }
    out::error_line("timed out waiting for approval");
    return 1;
}

int Logout() {
    account::Credentials credentials;
    if (!LoadCredentials(&credentials)) {
        return 1;
    }
    if (!credentials.signed_in() && credentials.refresh_token.empty()) {
        out::status_line("not signed in");
        return 0;
    }

    account::ConsoleClient client;
    std::string revoke_failure;
    const bool revoked = client.Revoke(credentials.console_url, credentials.access_token,
                                       credentials.refresh_token, &revoke_failure);
    std::string clear_failure;
    if (!account::Clear(&clear_failure)) {
        out::error_line(clear_failure);
        return 1;
    }
    out::status_line("signed out on this machine");
    if (!revoked) {
        out::error_line(revoke_failure + "; the local session was removed");
        return 1;
    }
    out::status_line("cloud session revoked");
    return 0;
}

int WhoAmI() {
    account::Credentials credentials;
    if (!LoadCredentials(&credentials)) {
        return 1;
    }
    if (!credentials.signed_in()) {
        out::error_line("not signed in — run `rcli login`");
        return 1;
    }

    account::ConsoleClient client;
    std::string failure;
    if (credentials.access_token_expired(EpochSeconds()) &&
        !RefreshSession(client, &credentials, &failure)) {
        out::error_line(failure);
        return 1;
    }

    account::Identity identity;
    account::IdentityResult result =
        client.WhoAmI(credentials.console_url, credentials.access_token, &identity, &failure);
    if (result == account::IdentityResult::Unauthorized) {
        if (!RefreshSession(client, &credentials, &failure)) {
            out::error_line(failure);
            return 1;
        }
        result =
            client.WhoAmI(credentials.console_url, credentials.access_token, &identity, &failure);
    }
    if (result != account::IdentityResult::Ok) {
        out::error_line(failure);
        return 1;
    }

    char line[220];
    std::snprintf(line, sizeof(line), "%-14s %s", "email", identity.email.c_str());
    out::result_line(line);
    std::snprintf(line, sizeof(line), "%-14s %s", "session", "active");
    out::result_line(line);
    std::snprintf(line, sizeof(line), "%-14s %s", "console", credentials.console_url.c_str());
    out::result_line(line);
    return 0;
}

}  // namespace

void register_account(CLI::App& app, GlobalOptions& options) {
    static_cast<void>(options);
    auto no_browser = std::make_shared<bool>(false);
    auto console_url = std::make_shared<std::string>();
    auto* login = app.add_subcommand("login", "sign in through the RunAnywhere console");
    login->add_flag("--no-browser", *no_browser, "print the URL instead of opening it");
    login
        ->add_option("--console-url", *console_url,
                     "console origin (default: https://console.runanywhere.ai)")
        ->envname("RCLI_CONSOLE_URL");
    login->callback([no_browser, console_url] { fail(Login(*console_url, !*no_browser)); });

    auto* logout = app.add_subcommand("logout", "revoke and remove the cloud session");
    logout->callback([] { fail(Logout()); });

    auto* whoami = app.add_subcommand("whoami", "show the signed-in cloud account");
    whoami->callback([] { fail(WhoAmI()); });
}

}  // namespace rcli::commands
