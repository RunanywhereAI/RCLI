#ifndef RCLI_ACCOUNT_CREDENTIALS_H
#define RCLI_ACCOUNT_CREDENTIALS_H

#include <string>

namespace rcli::account {

struct Credentials {
    std::string console_url;
    std::string email;
    std::string access_token;
    std::string refresh_token;
    long long expires_at = 0;

    bool signed_in() const { return !access_token.empty(); }
    bool access_token_expired(long long now, long long skew_seconds = 60) const {
        return expires_at > 0 && expires_at <= now + skew_seconds;
    }
};

/// Production console used when neither a login flag nor RCLI_CONSOLE_URL is set.
std::string DefaultConsoleUrl();

/// Validate and canonicalize a console origin. HTTPS is required except for an
/// exact loopback host. Origins may not contain credentials, paths or queries.
bool NormalizeConsoleUrl(const std::string& input, std::string* normalized, std::string* error);

/// Browser URLs may include a path/query but obey the same HTTPS/loopback rule.
bool BrowserUrlIsSafe(const std::string& url);

/// Approval links must stay on the configured console origin so a compromised
/// response cannot silently open an unrelated sign-in page.
bool BrowserUrlMatchesConsole(const std::string& url, const std::string& console_url);

/// Bearer/refresh tokens are constrained to RFC 6750 b64token characters so
/// values loaded from disk or returned by a server cannot inject HTTP headers.
bool SessionTokenIsSafe(const std::string& token);

std::string ProfileDirectory();
std::string CredentialsPath();

/// Missing credentials are not an error; `out` receives the production default.
bool Load(Credentials* out, std::string* error);
// Compatibility overload for the editor/harness code introduced by PR #34.
// Errors are surfaced by the command-level pointer overload; callers using
// this legacy form receive an empty session on read failure.
Credentials Load();
bool Save(const Credentials& credentials, std::string* error);
bool Clear(std::string* error);

}  // namespace rcli::account

#endif  // RCLI_ACCOUNT_CREDENTIALS_H
