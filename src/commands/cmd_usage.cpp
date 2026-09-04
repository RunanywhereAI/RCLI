#include <chrono>
#include <cstdio>
#include <string>

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

long long EpochSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

/// Money is integer micro-dollars: one dollar is 1,000,000.
std::string Money(long micros) {
    const int places = micros != 0 && micros < 1'000'000 ? 4 : 2;
    char text[48];
    std::snprintf(text, sizeof(text), "$%.*f", places, static_cast<double>(micros) / 1'000'000.0);
    return text;
}

std::string Grouped(long value) {
    std::string digits = std::to_string(value < 0 ? -value : value);
    for (std::size_t at = digits.size(); at > 3;) {
        at -= 3;
        digits.insert(at, ",");
    }
    return value < 0 ? "-" + digits : digits;
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
    credentials->access_token = grant.access_token;
    if (!grant.refresh_token.empty()) {
        credentials->refresh_token = grant.refresh_token;
    }
    credentials->expires_at = EpochSeconds() + (grant.expires_in > 0 ? grant.expires_in : 3600);
    return account::Save(*credentials, error);
}

/// `GET /v1/cli/usage` takes `days` as an integer of at least 1, and its
/// `timeline` groups by calendar date, so one day is the shortest window the
/// console can total. `usage.Filter` on the console side already carries
/// `since`/`until` and would answer an hour; the route is what does not offer
/// it yet.
///
/// The recent-request page is not a substitute. Rolling it up here would
/// describe the last N requests while claiming to describe an hour, and the two
/// part company exactly when somebody is busy enough to be looking.
constexpr bool kConsoleAnswersHours = false;

void PrintJson(const account::Usage& usage) {
    out::JsonWriter json;
    json.begin_object();
    json.field("balance_micros", static_cast<int64_t>(usage.credit.balance_micros));
    json.field("granted_micros", static_cast<int64_t>(usage.credit.granted_micros));
    json.field("spent_micros", static_cast<int64_t>(usage.credit.spent_micros));

    json.begin_array("windows");
    json.begin_array_object();
    json.field("window", "1h");
    json.field("available", kConsoleAnswersHours);
    json.end_object();
    json.begin_array_object();
    json.field("window", "24h");
    json.field("available", true);
    json.field("input_tokens", static_cast<int64_t>(usage.totals.prompt_tokens));
    json.field("output_tokens", static_cast<int64_t>(usage.totals.completion_tokens));
    json.field("cached_tokens", static_cast<int64_t>(usage.totals.cached_tokens));
    json.field("cost_micros", static_cast<int64_t>(usage.totals.cost_micros));
    json.end_object();
    json.end_array();

    json.end_object();
    out::result_line(json.str());
}

void PrintReport(const account::Usage& usage) {
    char line[200];
    std::snprintf(line, sizeof(line), "credit     %s left of %s granted",
                  Money(usage.credit.balance_micros).c_str(),
                  Money(usage.credit.granted_micros).c_str());
    out::result_line(line);
    out::result_line("");

    std::snprintf(line, sizeof(line), "%-10s %11s %11s %11s %11s", "window", "input", "output",
                  "cache", "spend");
    out::result_line(line);

    // An hour is not a number this console can produce, so the row says so
    // rather than quietly showing a day's figures under an hour's heading.
    std::snprintf(line, sizeof(line), "%-10s %11s %11s %11s %11s", "past 1h", "-", "-", "-", "-");
    out::result_line(line);

    const account::UsageTotals& totals = usage.totals;
    std::snprintf(line, sizeof(line), "%-10s %11s %11s %11s %11s", "past 24h",
                  Grouped(totals.prompt_tokens).c_str(), Grouped(totals.completion_tokens).c_str(),
                  Grouped(totals.cached_tokens).c_str(), Money(totals.cost_micros).c_str());
    out::result_line(line);

    if (!kConsoleAnswersHours) {
        out::result_line("");
        out::status_line("the console totals whole days only; the 1h row needs a sub-day window "
                         "on /v1/cli/usage");
    }
}

int Usage(bool as_json) {
    account::Credentials credentials;
    std::string failure;
    if (!account::Load(&credentials, &failure)) {
        out::error_line(failure);
        return 1;
    }
    if (!credentials.signed_in()) {
        out::error_line("not signed in — run `rcli login`");
        return 1;
    }

    account::ConsoleClient client;
    if (credentials.access_token_expired(EpochSeconds()) &&
        !RefreshSession(client, &credentials, &failure)) {
        out::error_line(failure);
        return 1;
    }

    // One day back, and the shortest recent-request page the route accepts:
    // nothing here renders those, and `by_model`/`totals` group in SQL, so the
    // page size cannot move the numbers above.
    account::UsageQuery query;
    query.days = 1;
    query.limit = 1;

    account::Usage usage;
    account::IdentityResult result = client.FetchUsage(credentials.console_url,
                                                       credentials.access_token, query, &usage,
                                                       &failure);
    if (result == account::IdentityResult::Unauthorized) {
        std::string refresh_failure;
        if (!RefreshSession(client, &credentials, &refresh_failure)) {
            // Not "expired". The console answers unknown, revoked, expired and
            // malformed with the same 401 on purpose, so which of the four this
            // was is not something we know — and sending someone to re-login
            // over a revoked key wastes the trip.
            out::error_line("the console rejected this session (" + refresh_failure +
                            "); run `rcli login`");
            return 1;
        }
        usage = account::Usage{};
        result = client.FetchUsage(credentials.console_url, credentials.access_token, query, &usage,
                                   &failure);
    }
    if (result != account::IdentityResult::Ok) {
        out::error_line(failure);
        return 1;
    }

    if (as_json) {
        PrintJson(usage);
    } else {
        PrintReport(usage);
    }
    return 0;
}

}  // namespace

void register_usage(CLI::App& app, GlobalOptions& options) {
    static_cast<void>(options);
    auto as_json = std::make_shared<bool>(false);

    auto* usage = app.add_subcommand("usage", "credit left, and what the last day cost");
    usage->add_flag("--json", *as_json, "machine-readable output");
    usage->callback([as_json] { fail(Usage(*as_json)); });
}

}  // namespace rcli::commands
