#ifndef RCLI_ACCOUNT_CONSOLE_H
#define RCLI_ACCOUNT_CONSOLE_H

#include <cstdint>

#include <functional>
#include <string>
#include <vector>

namespace rcli::account {

struct HttpRequest {
    std::string method;
    std::string url;
    std::string body;
    std::string bearer_token;
};

struct HttpResponse {
    int status = 0;
    std::string body;
};

using Transport = std::function<bool(const HttpRequest&, HttpResponse*, std::string*)>;

struct Identity {
    std::string email;
    // Kept for the existing editor/proxy integrations from the parent PR.
    std::string plan;
    std::int64_t tokens_this_month = 0;
    std::int64_t monthly_token_limit = 0;
};

/// What the console meters. Money is integer micro-dollars everywhere: one
/// dollar is 1,000,000, and a single request routinely costs a few hundred.
struct UsageTotals {
    std::int64_t requests = 0;
    std::int64_t prompt_tokens = 0;
    std::int64_t completion_tokens = 0;
    std::int64_t cached_tokens = 0;
    std::int64_t cost_micros = 0;
};

struct UsageDay {
    std::string date;
    std::int64_t requests = 0;
    std::int64_t prompt_tokens = 0;
    std::int64_t completion_tokens = 0;
    std::int64_t cost_micros = 0;
};

struct UsageEvent {
    std::string request_id;
    std::string model;
    std::string harness;
    std::string started_at;
    std::string error_code;
    std::int64_t prompt_tokens = 0;
    std::int64_t completion_tokens = 0;
    std::int64_t cached_tokens = 0;
    std::int64_t cost_micros = 0;
    std::int64_t ttft_ms = 0;
    int status_code = 0;
};

struct UsageModel {
    std::string model;
    std::int64_t requests = 0;
    std::int64_t prompt_tokens = 0;
    std::int64_t completion_tokens = 0;
    std::int64_t cached_tokens = 0;
    std::int64_t cost_micros = 0;
};

struct Credits {
    std::int64_t balance_micros = 0;
    std::int64_t granted_micros = 0;
    std::int64_t spent_micros = 0;
};

/// Spend over a window ending now, totalled by the console.
///
/// Not derivable here. `timeline` is grouped by calendar date, so its finest
/// grain is a day, and summing the recent-request page instead would describe
/// the last N requests while claiming to describe the window — the two part
/// company the moment anyone is busy.
struct UsageWindow {
    /// "1h" or "24h" as the console labels it.
    std::string window;
    /// The span. Carried so nothing here has to parse `window`.
    std::int64_t seconds = 0;
    UsageTotals totals;
};

struct Usage {
    Credits credit;
    UsageTotals totals;
    /// Empty against a console that predates windowed totals, which is every
    /// deployed one until `/v1/cli/usage` ships. Callers render what is missing
    /// as missing rather than substituting a wider window's numbers.
    std::vector<UsageWindow> windows;
    std::vector<UsageDay> timeline;
    std::vector<UsageModel> models;
    std::vector<UsageEvent> events;
};

struct UsageQuery {
    int days = 30;
    std::string model;
    int limit = 20;
};

struct Authorization {
    std::string request_code;
    std::string poll_secret;
    std::string verification_url;
    int expires_in = 0;
    int interval = 2;
};

struct Grant {
    std::string access_token;
    std::string refresh_token;
    std::string email;
    std::string plan;
    long expires_in = 0;
};

enum class PollResult { Pending, Approved, Denied, Expired, Failed };
enum class IdentityResult { Ok, Unauthorized, Failed };

/// Console client independent of SDK/bootstrap state.
///
/// The default transport uses WinHTTP on Windows and libcurl elsewhere. Tests
/// inject a hermetic transport so auth contracts never need an account or network.
class ConsoleClient {
   public:
    explicit ConsoleClient(Transport transport = {});

    bool BeginAuthorization(const std::string& console_url, const std::string& hostname,
                            Authorization* authorization, std::string* error) const;
    PollResult Poll(const std::string& console_url, const Authorization& authorization,
                    Grant* grant, std::string* error) const;
    bool Refresh(const std::string& console_url, const std::string& refresh_token, Grant* grant,
                 std::string* error) const;
    IdentityResult WhoAmI(const std::string& console_url, const std::string& access_token,
                          Identity* identity, std::string* error) const;
    bool Revoke(const std::string& console_url, const std::string& access_token,
                const std::string& refresh_token, std::string* error) const;
    IdentityResult FetchUsage(const std::string& console_url, const std::string& access_token,
                              const UsageQuery& query, Usage* usage, std::string* error) const;

   private:
    Transport transport_;
};

// Compatibility facade for the editor/harness code introduced by PR #34.
// New code should prefer ConsoleClient so transports can be injected in tests.
bool BeginAuthorization(const std::string& console_url, const std::string& hostname,
                        Authorization* authorization, std::string* error);
PollResult Poll(const std::string& console_url, const Authorization& authorization, Grant* grant,
                std::string* error);
bool Refresh(const std::string& console_url, const std::string& refresh_token, Grant* grant,
             std::string* error);
bool WhoAmI(const std::string& console_url, const std::string& token, Identity* identity,
            std::string* error);

}  // namespace rcli::account

#endif  // RCLI_ACCOUNT_CONSOLE_H
