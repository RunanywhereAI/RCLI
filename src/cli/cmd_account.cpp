#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#include <unistd.h>

#include "account/console.h"
#include "account/credentials.h"
#include "cli/commands.h"
#include "cli/output.h"

namespace rcli::cli {
namespace {

using out::Ink;

std::string Hostname() {
    char name[256] = {};
    if (gethostname(name, sizeof(name) - 1) == 0 && name[0] != '\0') {
        return name;
    }
    return "unknown";
}

/// Only ever handed a URL this process just received from the console, so there
/// is nothing user-supplied to quote around.
void OpenBrowser(const std::string& url) {
#if defined(__APPLE__)
    const std::string command = "open '" + url + "' >/dev/null 2>&1";
#elif defined(_WIN32)
    const std::string command = "start \"\" \"" + url + "\"";
#else
    const std::string command = "xdg-open '" + url + "' >/dev/null 2>&1";
#endif
    if (std::system(command.c_str()) != 0) {
        out::Status("could not open a browser for you");
    }
}

bool Ready() {
    if (Start()) {
        return true;
    }
    out::Error("the SDK would not start, so the console cannot be reached");
    return false;
}

int Login(bool open_browser) {
    if (!Ready()) {
        return 1;
    }
    account::Credentials credentials = account::Load();

    account::Authorization authorization;
    std::string failure;
    if (!account::BeginAuthorization(credentials.console_url, Hostname(), &authorization,
                                     &failure)) {
        out::Error(failure);
        return 1;
    }

    out::Status("approve this sign-in in your browser:");
    out::Line(authorization.verification_url);
    if (open_browser) {
        OpenBrowser(authorization.verification_url);
    }
    out::Status("waiting for approval");

    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(authorization.expires_in);
    account::Grant grant;
    while (std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::seconds(authorization.interval));
        switch (account::Poll(credentials.console_url, authorization, &grant, &failure)) {
            case account::PollResult::Pending:
                continue;
            case account::PollResult::Denied:
                out::Error("the request was denied in the browser");
                return 1;
            case account::PollResult::Expired:
                out::Error("the request expired before it was approved");
                return 1;
            case account::PollResult::Failed:
                out::Error(failure);
                return 1;
            case account::PollResult::Approved:
                credentials.email = grant.email;
                credentials.plan = grant.plan;
                credentials.access_token = grant.access_token;
                credentials.refresh_token = grant.refresh_token;
                if (!account::Save(credentials, &failure)) {
                    out::Error(failure);
                    return 1;
                }
                out::Status("signed in as " + grant.email + " on the " + grant.plan + " plan");
                out::Status("credentials in " + account::ProfileDirectory());
                return 0;
        }
    }
    out::Error("timed out waiting for approval");
    return 1;
}

int Logout() {
    const account::Credentials credentials = account::Load();
    if (!credentials.signed_in()) {
        out::Status("not signed in");
        return 0;
    }
    std::string failure;
    if (!account::Clear(&failure)) {
        out::Error(failure);
        return 1;
    }
    out::Status("signed out " + credentials.email);
    out::Status("revoke the token in the console to end it everywhere");
    return 0;
}

int WhoAmI() {
    if (!Ready()) {
        return 1;
    }
    account::Credentials credentials = account::Load();
    if (!credentials.signed_in()) {
        out::Error("not signed in — run `rcli login`");
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
            out::Error(failure.empty() ? "the session has expired — run `rcli login`" : failure);
            return 1;
        }
        credentials.access_token = grant.access_token;
        credentials.refresh_token = grant.refresh_token;
        credentials.email = grant.email;
        credentials.plan = grant.plan;
        account::Save(credentials, nullptr);
        if (!account::WhoAmI(credentials.console_url, credentials.access_token, &identity,
                             &failure)) {
            out::Error(failure);
            return 1;
        }
    }

    char line[220];
    std::snprintf(line, sizeof(line), "%-14s %s", "email", identity.email.c_str());
    out::Line(line);
    std::snprintf(line, sizeof(line), "%-14s %s", "plan", identity.plan.c_str());
    out::Line(line);
    std::snprintf(line, sizeof(line), "%-14s %ld of %ld", "tokens", identity.tokens_this_month,
                  identity.monthly_token_limit);
    out::Line(line);
    std::snprintf(line, sizeof(line), "%-14s %s", "console", credentials.console_url.c_str());
    out::Line(line);
    std::snprintf(line, sizeof(line), "%-14s %s", "profile", account::ProfileDirectory().c_str());
    out::Line(line);
    return 0;
}

}  // namespace

void RegisterAccount(CLI::App& app, Options& options) {
    auto no_browser = std::make_shared<bool>(false);
    auto* login = app.add_subcommand("login", "sign in through the RunAnywhere console");
    login->add_flag("--no-browser", *no_browser, "print the URL instead of opening it");
    login->callback([&options, no_browser] { options.status = Login(!*no_browser); });

    auto* logout = app.add_subcommand("logout", "forget the credentials on this machine");
    logout->callback([&options] { options.status = Logout(); });

    auto* whoami = app.add_subcommand("whoami", "who is signed in, and how much they have used");
    whoami->callback([&options] { options.status = WhoAmI(); });
}

}  // namespace rcli::cli
