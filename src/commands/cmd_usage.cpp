#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

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

/// Money is integer micro-dollars: one dollar is 1,000,000. A single request
/// costs a few hundred of them, so a total and a unit price cannot share a
/// precision — printing a per-request cost to two places renders it free.
std::string Dollars(long micros, int places) {
    char text[48];
    std::snprintf(text, sizeof(text), "$%.*f", places, static_cast<double>(micros) / 1'000'000.0);
    return text;
}

/// Under a dollar, four places. Two would round a column of per-request costs
/// to $0.00 and a day's spend to $0.02 side by side, which reads as noise.
std::string Money(long micros) {
    return Dollars(micros, micros != 0 && micros < 1'000'000 ? 4 : 2);
}

std::string Grouped(long value) {
    std::string digits = std::to_string(value < 0 ? -value : value);
    for (std::size_t at = digits.size(); at > 3;) {
        at -= 3;
        digits.insert(at, ",");
    }
    return value < 0 ? "-" + digits : digits;
}

/// "2026-09-02T18:40:10.123Z" -> "09-02 18:40". Whatever the console sends that
/// is not that shape is printed as-is rather than guessed at.
std::string ShortTime(const std::string& iso) {
    if (iso.size() < 16 || iso[10] != 'T') {
        return iso;
    }
    return iso.substr(5, 5) + " " + iso.substr(11, 5);
}

std::string Bar(long value, long peak, int width) {
    if (peak <= 0) {
        return std::string();
    }
    const int filled = static_cast<int>((static_cast<double>(value) / peak) * width + 0.5);
    std::string bar;
    for (int i = 0; i < width; ++i) {
        bar += i < filled ? "#" : ".";
    }
    return bar;
}

bool LoadCredentials(account::Credentials* credentials) {
    std::string failure;
    if (!account::Load(credentials, &failure)) {
        out::error_line(failure);
        return false;
    }
    return true;
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

void PrintJson(const account::Usage& usage) {
    // Flat by necessity: the writer has no nested-object-under-a-key form, and
    // adding one for this is not worth a JSON dependency.
    out::JsonWriter json;
    json.begin_object();
    json.field("balance_micros", static_cast<int64_t>(usage.credit.balance_micros));
    json.field("granted_micros", static_cast<int64_t>(usage.credit.granted_micros));
    json.field("spent_micros", static_cast<int64_t>(usage.credit.spent_micros));
    json.field("requests", static_cast<int64_t>(usage.totals.requests));
    json.field("prompt_tokens", static_cast<int64_t>(usage.totals.prompt_tokens));
    json.field("completion_tokens", static_cast<int64_t>(usage.totals.completion_tokens));
    json.field("cached_tokens", static_cast<int64_t>(usage.totals.cached_tokens));
    json.field("cost_micros", static_cast<int64_t>(usage.totals.cost_micros));

    json.begin_array("timeline");
    for (const account::UsageDay& day : usage.timeline) {
        json.begin_array_object();
        json.field("date", day.date);
        json.field("requests", static_cast<int64_t>(day.requests));
        json.field("prompt_tokens", static_cast<int64_t>(day.prompt_tokens));
        json.field("completion_tokens", static_cast<int64_t>(day.completion_tokens));
        json.field("cost_micros", static_cast<int64_t>(day.cost_micros));
        json.end_object();
    }
    json.end_array();

    json.begin_array("models");
    for (const account::UsageModel& row : usage.models) {
        json.begin_array_object();
        json.field("model", row.model);
        json.field("requests", static_cast<int64_t>(row.requests));
        json.field("prompt_tokens", static_cast<int64_t>(row.prompt_tokens));
        json.field("completion_tokens", static_cast<int64_t>(row.completion_tokens));
        json.field("cached_tokens", static_cast<int64_t>(row.cached_tokens));
        json.field("cost_micros", static_cast<int64_t>(row.cost_micros));
        json.end_object();
    }
    json.end_array();

    json.begin_array("events");
    for (const account::UsageEvent& event : usage.events) {
        json.begin_array_object();
        json.field("request_id", event.request_id);
        json.field("model", event.model);
        json.field("harness", event.harness);
        json.field("started_at", event.started_at);
        json.field("prompt_tokens", static_cast<int64_t>(event.prompt_tokens));
        json.field("completion_tokens", static_cast<int64_t>(event.completion_tokens));
        json.field("cached_tokens", static_cast<int64_t>(event.cached_tokens));
        json.field("cost_micros", static_cast<int64_t>(event.cost_micros));
        json.field("ttft_ms", static_cast<int64_t>(event.ttft_ms));
        json.field("status_code", static_cast<int64_t>(event.status_code));
        json.field("error_code", event.error_code);
        json.end_object();
    }
    json.end_array();
    json.end_object();
    out::result_line(json.str());
}

void PrintReport(const account::Usage& usage, int days, const std::string& model_filter) {
    const account::UsageTotals& totals = usage.totals;

    char line[240];
    std::snprintf(line, sizeof(line), "%-10s %s left of %s granted", "credit",
                  Money(usage.credit.balance_micros).c_str(),
                  Money(usage.credit.granted_micros).c_str());
    out::result_line(line);
    out::result_line("");

    const std::string window =
        "last " + std::to_string(days) + (days == 1 ? " day" : " days") +
        (model_filter.empty() ? std::string() : " · " + model_filter);
    out::result_line(window);
    std::snprintf(line, sizeof(line), "%-10s %s", "requests", Grouped(totals.requests).c_str());
    out::result_line(line);
    std::snprintf(line, sizeof(line), "%-10s %s in · %s out · %s cached", "tokens",
                  Grouped(totals.prompt_tokens).c_str(), Grouped(totals.completion_tokens).c_str(),
                  Grouped(totals.cached_tokens).c_str());
    out::result_line(line);
    if (totals.requests > 0) {
        std::snprintf(line, sizeof(line), "%-10s %s (%s per request)", "spend",
                      Money(totals.cost_micros).c_str(),
                      Money(totals.cost_micros / totals.requests).c_str());
    } else {
        std::snprintf(line, sizeof(line), "%-10s %s", "spend", Money(totals.cost_micros).c_str());
    }
    out::result_line(line);

    if (totals.requests == 0) {
        out::result_line("");
        out::result_line("nothing served in this window");
        return;
    }

    if (!usage.timeline.empty()) {
        long peak = 0;
        for (const account::UsageDay& day : usage.timeline) {
            peak = std::max(peak, day.requests);
        }
        out::result_line("");
        out::result_line("by day");
        for (const account::UsageDay& day : usage.timeline) {
            std::snprintf(line, sizeof(line), "  %-10s %-20s %6s req  %10s", day.date.c_str(),
                          Bar(day.requests, peak, 20).c_str(), Grouped(day.requests).c_str(),
                          Money(day.cost_micros).c_str());
            out::result_line(line);
        }
    }

    if (!usage.models.empty()) {
        out::result_line("");
        out::result_line("by model");
        for (const account::UsageModel& row : usage.models) {
            std::snprintf(line, sizeof(line), "  %-28s %5s req  %9s in  %9s out  %10s",
                          row.model.c_str(), Grouped(row.requests).c_str(),
                          Grouped(row.prompt_tokens).c_str(),
                          Grouped(row.completion_tokens).c_str(), Money(row.cost_micros).c_str());
            out::result_line(line);
        }
    }

    if (!usage.events.empty()) {
        out::result_line("");
        std::vector<std::vector<std::string>> rows;
        for (const account::UsageEvent& event : usage.events) {
            const std::string state = event.status_code == 200
                                          ? std::string("ok")
                                          : (event.error_code.empty()
                                                 ? std::to_string(event.status_code)
                                                 : event.error_code);
            rows.push_back({ShortTime(event.started_at), event.model,
                            Grouped(event.prompt_tokens), Grouped(event.completion_tokens),
                            event.ttft_ms > 0 ? std::to_string(event.ttft_ms) + "ms" : "-",
                            Money(event.cost_micros), state});
        }
        out::table({"time", "model", "in", "out", "ttft", "cost", "status"}, rows);
    }
}

int Usage(int days, const std::string& model, int limit, bool as_json) {
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

    account::UsageQuery query;
    query.days = days;
    query.model = model;
    query.limit = limit;

    account::Usage usage;
    account::IdentityResult result =
        client.FetchUsage(credentials.console_url, credentials.access_token, query, &usage,
                          &failure);
    if (result == account::IdentityResult::Unauthorized) {
        // failure currently holds the 401 detail ("console session expired");
        // a failed refresh here would otherwise overwrite it with the
        // refresh call's own error and the reader would never learn the
        // usage request was unauthorized in the first place.
        std::string refresh_failure;
        if (!RefreshSession(client, &credentials, &refresh_failure)) {
            out::error_line("not signed in — the cloud session expired and could not be "
                            "refreshed (" +
                            refresh_failure + "); run `rcli login`");
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
        PrintReport(usage, days, model);
    }
    return 0;
}

}  // namespace

void register_usage(CLI::App& app, GlobalOptions& options) {
    static_cast<void>(options);
    auto days = std::make_shared<int>(30);
    auto limit = std::make_shared<int>(20);
    auto model = std::make_shared<std::string>();
    auto as_json = std::make_shared<bool>(false);

    auto* usage = app.add_subcommand("usage", "show cloud spend, tokens and recent requests");
    usage->add_option("--days", *days, "window in days (default: 30)")->check(CLI::Range(1, 365));
    usage->add_option("--model", *model, "only this model");
    usage->add_option("--limit", *limit, "recent requests to list (default: 20)")
        ->check(CLI::Range(1, 200));
    usage->add_flag("--json", *as_json, "machine-readable output");
    usage->callback(
        [days, model, limit, as_json] { fail(Usage(*days, *model, *limit, *as_json)); });
}

}  // namespace rcli::commands
