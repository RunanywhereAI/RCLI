#include "test_common.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <unistd.h>

#include <sys/stat.h>
#endif

#include "account/console.h"
#include "account/credentials.h"

namespace {

namespace fs = std::filesystem;
using Json = nlohmann::json;

class EnvVar {
   public:
    EnvVar(const char* name, const char* value) : name_(name) {
        if (const char* previous = std::getenv(name)) {
            had_previous_ = true;
            previous_ = previous;
        }
#if defined(_WIN32)
        _putenv_s(name, value != nullptr ? value : "");
#else
        value != nullptr ? setenv(name, value, 1) : unsetenv(name);
#endif
    }

    ~EnvVar() {
#if defined(_WIN32)
        _putenv_s(name_.c_str(), had_previous_ ? previous_.c_str() : "");
#else
        had_previous_ ? setenv(name_.c_str(), previous_.c_str(), 1) : unsetenv(name_.c_str());
#endif
    }

   private:
    std::string name_;
    std::string previous_;
    bool had_previous_ = false;
};

class TempDirectory {
   public:
    TempDirectory() {
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = fs::temp_directory_path() / ("rcli-account-test-" + std::to_string(nonce));
        fs::create_directories(path_);
    }
    ~TempDirectory() {
        std::error_code ignored;
        fs::remove_all(path_, ignored);
    }
    const fs::path& path() const { return path_; }

   private:
    fs::path path_;
};

TestResult test_console_url_validation() {
    TestResult result;
    result.test_name = "console_url_validation";

    struct Accepted {
        const char* input;
        const char* normalized;
    };
    const Accepted accepted[] = {
        {"https://console.runanywhere.ai", "https://console.runanywhere.ai"},
        {"HTTPS://CONSOLE.RUNANYWHERE.AI/", "https://console.runanywhere.ai"},
        {"http://localhost:8080", "http://localhost:8080"},
        {"http://127.0.0.1:8002", "http://127.0.0.1:8002"},
        {"http://[::1]:9000", "http://[::1]:9000"},
    };
    for (const Accepted& test : accepted) {
        std::string normalized;
        std::string error;
        if (!rcli::account::NormalizeConsoleUrl(test.input, &normalized, &error) ||
            normalized != test.normalized) {
            result.details = std::string("rejected safe origin: ") + test.input + " " + error;
            result.expected = test.normalized;
            result.actual = normalized;
            return result;
        }
    }

    const char* rejected[] = {
        "http://console.runanywhere.ai",
        "http://localhost.evil.example",
        "http://127.0.0.1.evil.example",
        "http://[::1].evil.example",
        "http://localhost@evil.example",
        "https://user:password@console.runanywhere.ai",
        "https://console.runanywhere.ai/path",
        "https://console.runanywhere.ai?query=1",
        "https://console.runanywhere.ai:",
        "http://[::1]:",
        "https://",
        "file:///tmp/credentials",
    };
    for (const char* input : rejected) {
        std::string normalized;
        std::string error;
        if (rcli::account::NormalizeConsoleUrl(input, &normalized, &error)) {
            result.details = std::string("accepted unsafe origin: ") + input;
            return result;
        }
    }

    if (!rcli::account::BrowserUrlIsSafe("https://console.runanywhere.ai/device?code=ABCD-EFGH") ||
        !rcli::account::BrowserUrlIsSafe("http://localhost:8080/device?code=ABCD") ||
        rcli::account::BrowserUrlIsSafe("http://localhost.evil.example/device") ||
        !rcli::account::BrowserUrlMatchesConsole(
            "https://console.runanywhere.ai/device?code=ABCD-EFGH",
            "https://console.runanywhere.ai") ||
        rcli::account::BrowserUrlMatchesConsole("https://auth.attacker.example/device",
                                                "https://console.runanywhere.ai")) {
        result.details = "browser URL policy does not match the origin policy";
        return result;
    }
    result.passed = true;
    return result;
}

TestResult test_credential_roundtrip_and_permissions() {
    TestResult result;
    result.test_name = "credential_roundtrip_and_permissions";
    TempDirectory temporary;
    EnvVar profile("RCLI_PROFILE_DIR", temporary.path().string().c_str());
    EnvVar console("RCLI_CONSOLE_URL", nullptr);

    rcli::account::Credentials expected;
    expected.console_url = "https://CONSOLE.RUNANYWHERE.AI/";
    expected.email = "dev+\"json\"@example.test";
    expected.access_token = "access-secret-that-must-not-be-logged";
    expected.refresh_token = "refresh-secret-that-must-not-be-logged";
    expected.expires_at = 123456789;

    std::string error;
    if (!rcli::account::Save(expected, &error)) {
        result.details = error;
        return result;
    }

#if !defined(_WIN32)
    struct stat directory{};
    struct stat file{};
    if (::stat(temporary.path().c_str(), &directory) != 0 ||
        ::stat(rcli::account::CredentialsPath().c_str(), &file) != 0 ||
        (directory.st_mode & 0777) != 0700 || (file.st_mode & 0777) != 0600) {
        result.details = "credentials must be stored in mode 0700/0600";
        return result;
    }
#endif

    rcli::account::Credentials actual;
    if (!rcli::account::Load(&actual, &error)) {
        result.details = error;
        return result;
    }
    if (actual.console_url != "https://console.runanywhere.ai" || actual.email != expected.email ||
        actual.access_token != expected.access_token ||
        actual.refresh_token != expected.refresh_token ||
        actual.expires_at != expected.expires_at) {
        result.details = "credential JSON did not round-trip exactly";
        return result;
    }
    if (!rcli::account::Clear(&error) || fs::exists(rcli::account::CredentialsPath())) {
        result.details = error.empty() ? "credential file still exists after clear" : error;
        return result;
    }
    result.passed = true;
    return result;
}

TestResult test_console_client_contract() {
    TestResult result;
    result.test_name = "console_client_contract";
    std::vector<rcli::account::HttpRequest> requests;
    int polls = 0;
    rcli::account::Transport transport = [&](const rcli::account::HttpRequest& request,
                                             rcli::account::HttpResponse* response, std::string*) {
        requests.push_back(request);
        if (request.url.ends_with("/auth/cli/start")) {
            response->status = 200;
            response->body =
                Json{{"request_code", "ABCD-EFGH"},
                     {"poll_secret", "poll-secret"},
                     {"verification_url", "https://console.runanywhere.ai/device?code=ABCD-EFGH"},
                     {"expires_in", 300},
                     {"interval", 1}}
                    .dump();
        } else if (request.url.ends_with("/auth/cli/poll")) {
            response->status = 200;
            response->body = polls++ == 0 ? Json{{"status", "pending"}}.dump()
                                          : Json{{"status", "approved"},
                                                 {"access_token", "access-one"},
                                                 {"refresh_token", "refresh-one"},
                                                 {"email", "dev@example.test"},
                                                 {"expires_in", 3600}}
                                                .dump();
        } else if (request.url.ends_with("/auth/cli/refresh")) {
            response->status = 200;
            response->body = Json{{"access_token", "access-two"},
                                  {"refresh_token", "refresh-two"},
                                  {"expires_in", 3600}}
                                 .dump();
        } else if (request.url.ends_with("/v1/me")) {
            response->status = 200;
            response->body = Json{{"email", "dev@example.test"}}.dump();
        } else if (request.url.ends_with("/auth/cli/revoke")) {
            response->status = 204;
        } else {
            return false;
        }
        return true;
    };

    rcli::account::ConsoleClient client(transport);
    std::string error;
    rcli::account::Authorization authorization;
    if (!client.BeginAuthorization("https://console.runanywhere.ai", "test-host", &authorization,
                                   &error) ||
        authorization.request_code != "ABCD-EFGH" || authorization.interval != 1) {
        result.details = error.empty() ? "authorization response mismatch" : error;
        return result;
    }
    rcli::account::Grant grant;
    if (client.Poll("https://console.runanywhere.ai", authorization, &grant, &error) !=
            rcli::account::PollResult::Pending ||
        client.Poll("https://console.runanywhere.ai", authorization, &grant, &error) !=
            rcli::account::PollResult::Approved ||
        grant.access_token != "access-one" || grant.refresh_token != "refresh-one") {
        result.details = error.empty() ? "poll contract mismatch" : error;
        return result;
    }
    rcli::account::Identity identity;
    if (client.WhoAmI("https://console.runanywhere.ai", grant.access_token, &identity, &error) !=
        rcli::account::IdentityResult::Ok) {
        result.details = error.empty() ? "identity contract mismatch" : error;
        return result;
    }
    rcli::account::Grant refreshed;
    if (!client.Refresh("https://console.runanywhere.ai", grant.refresh_token, &refreshed,
                        &error) ||
        refreshed.access_token != "access-two" || refreshed.refresh_token != "refresh-two" ||
        !client.Revoke("https://console.runanywhere.ai", refreshed.access_token,
                       refreshed.refresh_token, &error)) {
        result.details = error.empty() ? "refresh/revoke contract mismatch" : error;
        return result;
    }

    if (requests.size() != 6 || !requests[0].bearer_token.empty() ||
        !requests[1].bearer_token.empty() || !requests[2].bearer_token.empty() ||
        requests[3].bearer_token != "access-one" || !requests[4].bearer_token.empty() ||
        requests[5].bearer_token != "access-two") {
        result.details = "bearer tokens were attached to the wrong endpoint";
        return result;
    }
    const Json start = Json::parse(requests[0].body);
    const Json poll = Json::parse(requests[1].body);
    const Json refresh = Json::parse(requests[4].body);
    const Json revoke = Json::parse(requests[5].body);
    if (start.value("hostname", "") != "test-host" ||
        poll.value("poll_secret", "") != "poll-secret" ||
        refresh.value("refresh_token", "") != "refresh-one" ||
        revoke.value("refresh_token", "") != "refresh-two") {
        result.details = "request JSON contract mismatch";
        return result;
    }
    result.passed = true;
    return result;
}

TestResult test_console_errors_do_not_echo_secrets() {
    TestResult result;
    result.test_name = "console_errors_do_not_echo_secrets";
    const std::string secret = "access-secret-from-server";
    rcli::account::ConsoleClient client([&](const rcli::account::HttpRequest&,
                                            rcli::account::HttpResponse* response, std::string*) {
        response->status = 500;
        response->body = Json{{"detail", secret}}.dump();
        return true;
    });
    rcli::account::Authorization authorization;
    std::string error;
    if (client.BeginAuthorization("https://console.runanywhere.ai", "host", &authorization,
                                  &error) ||
        error.find(secret) != std::string::npos || error.find("HTTP 500") == std::string::npos) {
        result.details = "HTTP error exposed the response body or lost its status";
        return result;
    }

    rcli::account::ConsoleClient malformed([&](const rcli::account::HttpRequest&,
                                               rcli::account::HttpResponse* response,
                                               std::string*) {
        response->status = 200;
        response->body = "{\"access_token\":\"" + secret;
        return true;
    });
    error.clear();
    if (malformed.BeginAuthorization("https://console.runanywhere.ai", "host", &authorization,
                                     &error) ||
        error.find(secret) != std::string::npos || error != "console returned malformed JSON") {
        result.details = "JSON error exposed the response body";
        return result;
    }
    result.passed = true;
    return result;
}

TestResult test_console_rejects_header_injection() {
    TestResult result;
    result.test_name = "console_rejects_header_injection";
    rcli::account::ConsoleClient client([](const rcli::account::HttpRequest& request,
                                           rcli::account::HttpResponse* response, std::string*) {
        response->status = 200;
        if (request.url.ends_with("/auth/cli/poll")) {
            response->body = Json{{"status", "approved"},
                                  {"access_token", "safe\r\nX-Injected: yes"},
                                  {"refresh_token", "refresh-token"}}
                                 .dump();
        } else if (request.url.ends_with("/auth/cli/start")) {
            response->body = Json{{"request_code", "ABCD\nEFGH"},
                                  {"poll_secret", "poll-secret"},
                                  {"verification_url", "https://console.runanywhere.ai/device"}}
                                 .dump();
        }
        return true;
    });

    std::string error;
    rcli::account::Authorization authorization;
    if (client.BeginAuthorization("https://console.runanywhere.ai", "host", &authorization,
                                  &error) ||
        error != "console returned an invalid authorization request") {
        result.details = "terminal control characters were accepted in an authorization code";
        return result;
    }

    authorization.request_code = "ABCD-EFGH";
    authorization.poll_secret = "poll-secret";
    rcli::account::Grant grant;
    error.clear();
    if (client.Poll("https://console.runanywhere.ai", authorization, &grant, &error) !=
            rcli::account::PollResult::Failed ||
        error != "console returned an invalid cloud session") {
        result.details = "HTTP header control characters were accepted in an access token";
        return result;
    }
    result.passed = true;
    return result;
}

}  // namespace

int main(int argc, char** argv) {
    TestSuite suite("rcli_account");
    suite.add("console_url_validation", test_console_url_validation);
    suite.add("credential_roundtrip_and_permissions", test_credential_roundtrip_and_permissions);
    suite.add("console_client_contract", test_console_client_contract);
    suite.add("console_errors_do_not_echo_secrets", test_console_errors_do_not_echo_secrets);
    suite.add("console_rejects_header_injection", test_console_rejects_header_injection);
    return suite.run(argc, argv);
}
