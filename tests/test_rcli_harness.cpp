#include "test_common.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>

#include "account/console.h"
#include "account/credentials.h"
#include "harness/harness.h"

namespace {

namespace fs = std::filesystem;
using Json = nlohmann::json;

class Environment {
   public:
    Environment(const char* name, const char* value) : name_(name) {
        if (const char* previous = std::getenv(name)) {
            had_previous_ = true;
            previous_ = previous;
        }
        Set(value);
    }
    ~Environment() { Set(had_previous_ ? previous_.c_str() : nullptr); }

   private:
    void Set(const char* value) {
#if defined(_WIN32)
        _putenv_s(name_.c_str(), value != nullptr ? value : "");
#else
        value != nullptr ? setenv(name_.c_str(), value, 1) : unsetenv(name_.c_str());
#endif
    }
    std::string name_;
    std::string previous_;
    bool had_previous_ = false;
};

class TemporaryDirectory {
   public:
    TemporaryDirectory() {
        const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = fs::temp_directory_path() / ("rcli-harness-test-" + std::to_string(nonce));
        fs::create_directories(path_);
    }
    ~TemporaryDirectory() {
        std::error_code ignored;
        fs::remove_all(path_, ignored);
    }
    const fs::path& path() const { return path_; }

   private:
    fs::path path_;
};

long long Now() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// ---------------------------------------------------------------------------
// ModelIdIsSafe — finding 4: garbage / path-traversal / XML-structural
// strings must never reach a live editor session.
// ---------------------------------------------------------------------------

TestResult test_model_id_rejects_empty_and_control_characters() {
    TestResult result;
    result.test_name = "model_id_rejects_empty_and_control_characters";

    const std::string unsafe[] = {"", std::string("qwen\nrm -rf"), std::string("qwen\x7f", 5)};
    for (const std::string& id : unsafe) {
        if (rcli::harness::ModelIdIsSafe(id)) {
            result.details = "accepted an id containing a control character or empty id";
            return result;
        }
    }
    result.passed = true;
    return result;
}

TestResult test_model_id_rejects_xml_and_path_structural_characters() {
    TestResult result;
    result.test_name = "model_id_rejects_xml_and_path_structural_characters";

    // Each of these would corrupt or extend the raw string-concatenated XML
    // that ide::jetbrains_profile's ModelsXML writes into a live IDE's
    // settings, or claims a directory separator no real local/upstream id
    // ever contains.
    const std::string unsafe[] = {
        "qwen3\"/><option name=\"evil\" value=\"x",
        "qwen3</option><option name=\"x",
        "../../etc/passwd",
        "org/repo",
        "a\\b",
        "at&t-model",
        "it's-a-model",
    };
    for (const std::string& id : unsafe) {
        if (rcli::harness::ModelIdIsSafe(id)) {
            result.details = "accepted an XML/path-structural model id: " + id;
            return result;
        }
    }
    result.passed = true;
    return result;
}

TestResult test_model_id_accepts_ordinary_ids() {
    TestResult result;
    result.test_name = "model_id_accepts_ordinary_ids";

    const std::string safe[] = {"mlx-qwen3", "whisper-tiny", "qwen3.8-27b-1bit-npu", "smolvlm2"};
    for (const std::string& id : safe) {
        if (!rcli::harness::ModelIdIsSafe(id)) {
            result.details = "rejected an ordinary model id: " + id;
            return result;
        }
    }
    result.passed = true;
    return result;
}

// ---------------------------------------------------------------------------
// VerifyCloudSession — findings 1/2: Resolve() must confirm a session against
// the console, not just check that a token string is non-empty.
// ---------------------------------------------------------------------------

bool Seed(const fs::path& profile, const std::string& access_token,
         const std::string& refresh_token, long long expires_at, std::string* error) {
    Environment scoped_profile("RCLI_PROFILE_DIR", profile.string().c_str());
    rcli::account::Credentials credentials;
    credentials.console_url = "https://console.runanywhere.ai";
    credentials.email = "developer@example.test";
    credentials.access_token = access_token;
    credentials.refresh_token = refresh_token;
    credentials.expires_at = expires_at;
    return rcli::account::Save(credentials, error);
}

// A hand-written credentials.json with any non-empty access_token and no
// refresh_token — exactly what signed_in() alone accepted — must fail
// VerifyCloudSession instead of being treated as a real session.
TestResult test_verify_cloud_session_rejects_unverifiable_token() {
    TestResult result;
    result.test_name = "verify_cloud_session_rejects_unverifiable_token";
    TemporaryDirectory temporary;
    Environment profile("RCLI_PROFILE_DIR", temporary.path().string().c_str());
    std::string error;
    if (!Seed(temporary.path(), "hand-written-garbage-token", "", Now() + 3600, &error)) {
        result.details = error;
        return result;
    }

    bool contacted_console = false;
    rcli::account::ConsoleClient console([&](const rcli::account::HttpRequest& request,
                                             rcli::account::HttpResponse* response, std::string*) {
        contacted_console = true;
        response->status = 401;
        return true;
    });

    rcli::account::Credentials credentials;
    if (!rcli::account::Load(&credentials, &error)) {
        result.details = error;
        return result;
    }

    std::string email;
    std::string verify_error;
    const bool ok = rcli::harness::VerifyCloudSession(console, &credentials, &email, &verify_error);
    if (ok) {
        result.details = "a garbage token with no refresh token must not verify";
        return result;
    }
    if (!contacted_console) {
        result.details = "VerifyCloudSession must ask the console, not just check the token shape";
        return result;
    }
    if (verify_error.empty()) {
        result.details = "failure must explain why";
        return result;
    }
    result.passed = true;
    return result;
}

// An expired access token with a good refresh token is refreshed and then
// re-verified, exactly the `rcli usage` dance, and the refreshed session is
// what Resolve() goes on to use.
TestResult test_verify_cloud_session_refreshes_and_reverifies() {
    TestResult result;
    result.test_name = "verify_cloud_session_refreshes_and_reverifies";
    TemporaryDirectory temporary;
    Environment profile("RCLI_PROFILE_DIR", temporary.path().string().c_str());
    std::string error;
    if (!Seed(temporary.path(), "old-access-token", "refresh-token", Now() - 1, &error)) {
        result.details = error;
        return result;
    }

    bool refreshed = false;
    bool verified_with_new_token = false;
    rcli::account::ConsoleClient console([&](const rcli::account::HttpRequest& request,
                                             rcli::account::HttpResponse* response, std::string*) {
        if (request.url.ends_with("/auth/cli/refresh")) {
            refreshed = true;
            response->status = 200;
            response->body = Json{{"access_token", "new-access-token"},
                                  {"refresh_token", "new-refresh-token"},
                                  {"expires_in", 7200}}
                                 .dump();
            return true;
        }
        if (request.url.ends_with("/v1/me")) {
            verified_with_new_token = request.bearer_token == "new-access-token";
            response->status = 200;
            response->body = Json{{"email", "developer@example.test"}}.dump();
            return true;
        }
        return false;
    });

    rcli::account::Credentials credentials;
    if (!rcli::account::Load(&credentials, &error)) {
        result.details = error;
        return result;
    }

    std::string email;
    std::string verify_error;
    const bool ok = rcli::harness::VerifyCloudSession(console, &credentials, &email, &verify_error);
    if (!ok || !refreshed || !verified_with_new_token) {
        result.details = "expired token should be refreshed, then verified with the new token";
        result.actual = verify_error;
        return result;
    }
    if (email != "developer@example.test") {
        result.expected = "developer@example.test";
        result.actual = email;
        return result;
    }
    if (credentials.access_token != "new-access-token") {
        result.details = "credentials must carry the refreshed token back to the caller";
        return result;
    }
    result.passed = true;
    return result;
}

// A valid, unexpired token that the console still accepts verifies without
// ever calling refresh.
TestResult test_verify_cloud_session_accepts_real_session() {
    TestResult result;
    result.test_name = "verify_cloud_session_accepts_real_session";
    TemporaryDirectory temporary;
    Environment profile("RCLI_PROFILE_DIR", temporary.path().string().c_str());
    std::string error;
    if (!Seed(temporary.path(), "real-access-token", "refresh-token", Now() + 3600, &error)) {
        result.details = error;
        return result;
    }

    bool refresh_called = false;
    rcli::account::ConsoleClient console([&](const rcli::account::HttpRequest& request,
                                             rcli::account::HttpResponse* response, std::string*) {
        if (request.url.ends_with("/auth/cli/refresh")) {
            refresh_called = true;
            return false;
        }
        if (request.url.ends_with("/v1/me") && request.bearer_token == "real-access-token") {
            response->status = 200;
            response->body = Json{{"email", "developer@example.test"}}.dump();
            return true;
        }
        return false;
    });

    rcli::account::Credentials credentials;
    if (!rcli::account::Load(&credentials, &error)) {
        result.details = error;
        return result;
    }

    std::string email;
    std::string verify_error;
    const bool ok = rcli::harness::VerifyCloudSession(console, &credentials, &email, &verify_error);
    if (!ok || refresh_called) {
        result.details = "a valid session should verify without refreshing";
        result.actual = verify_error;
        return result;
    }
    result.passed = true;
    return result;
}

}  // namespace

int main(int argc, char** argv) {
    TestSuite suite("rcli_harness");
    suite.add("model_id_rejects_empty_and_control_characters",
              test_model_id_rejects_empty_and_control_characters);
    suite.add("model_id_rejects_xml_and_path_structural_characters",
              test_model_id_rejects_xml_and_path_structural_characters);
    suite.add("model_id_accepts_ordinary_ids", test_model_id_accepts_ordinary_ids);
    suite.add("verify_cloud_session_rejects_unverifiable_token",
              test_verify_cloud_session_rejects_unverifiable_token);
    suite.add("verify_cloud_session_refreshes_and_reverifies",
              test_verify_cloud_session_refreshes_and_reverifies);
    suite.add("verify_cloud_session_accepts_real_session",
              test_verify_cloud_session_accepts_real_session);
    return suite.run(argc, argv);
}
