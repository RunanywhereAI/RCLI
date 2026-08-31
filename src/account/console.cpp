#include "account/console.h"

#include <algorithm>
#include <curl/curl.h>
#include <limits>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>

#include "account/credentials.h"

namespace rcli::account {
namespace {

using Json = nlohmann::json;
constexpr std::size_t kMaximumResponseBytes = 1024 * 1024;

struct ResponseBuffer {
    std::string* body = nullptr;
    bool too_large = false;
};

std::size_t CollectBody(char* bytes, std::size_t size, std::size_t count, void* userdata) {
    auto* buffer = static_cast<ResponseBuffer*>(userdata);
    if (buffer == nullptr || buffer->body == nullptr ||
        (count != 0 && size > std::numeric_limits<std::size_t>::max() / count)) {
        return 0;
    }
    const std::size_t length = size * count;
    if (buffer->body->size() > kMaximumResponseBytes ||
        length > kMaximumResponseBytes - buffer->body->size()) {
        buffer->too_large = true;
        return 0;
    }
    buffer->body->append(bytes, length);
    return length;
}

bool DefaultTransport(const HttpRequest& input, HttpResponse* output, std::string* error) {
    if (output == nullptr) {
        if (error != nullptr) {
            *error = "internal console transport error";
        }
        return false;
    }
    if (!BrowserUrlIsSafe(input.url)) {
        if (error != nullptr) {
            *error = "refusing an unsafe console request URL";
        }
        return false;
    }
    if (!input.bearer_token.empty() && !SessionTokenIsSafe(input.bearer_token)) {
        if (error != nullptr) {
            *error = "refusing an invalid console bearer token";
        }
        return false;
    }

    static std::once_flag curl_once;
    static CURLcode curl_init_result = CURLE_FAILED_INIT;
    std::call_once(curl_once, [] { curl_init_result = curl_global_init(CURL_GLOBAL_DEFAULT); });
    if (curl_init_result != CURLE_OK) {
        if (error != nullptr) {
            *error = "could not initialize the console HTTP client";
        }
        return false;
    }

    CURL* request = curl_easy_init();
    if (request == nullptr) {
        if (error != nullptr) {
            *error = "could not create the console HTTP client";
        }
        return false;
    }

    curl_slist* headers = nullptr;
    const auto add_header = [&headers](const std::string& value) {
        curl_slist* updated = curl_slist_append(headers, value.c_str());
        if (updated == nullptr) {
            return false;
        }
        headers = updated;
        return true;
    };
    bool configured =
        add_header("Accept: application/json") && add_header("Content-Type: application/json");
    const std::string authorization = "Authorization: Bearer " + input.bearer_token;
    if (configured && !input.bearer_token.empty()) {
        configured = add_header(authorization);
    }

    output->status = 0;
    output->body.clear();
    ResponseBuffer response{&output->body, false};
    configured =
        configured && curl_easy_setopt(request, CURLOPT_URL, input.url.c_str()) == CURLE_OK &&
        curl_easy_setopt(request, CURLOPT_CUSTOMREQUEST, input.method.c_str()) == CURLE_OK &&
        curl_easy_setopt(request, CURLOPT_HTTPHEADER, headers) == CURLE_OK &&
        curl_easy_setopt(request, CURLOPT_CONNECTTIMEOUT_MS, 10000L) == CURLE_OK &&
        curl_easy_setopt(request, CURLOPT_TIMEOUT_MS, 30000L) == CURLE_OK &&
        curl_easy_setopt(request, CURLOPT_NOSIGNAL, 1L) == CURLE_OK &&
        curl_easy_setopt(request, CURLOPT_NOPROXY, "localhost,127.0.0.1,::1") == CURLE_OK &&
        curl_easy_setopt(request, CURLOPT_FOLLOWLOCATION, 0L) == CURLE_OK &&
        curl_easy_setopt(request, CURLOPT_SSL_VERIFYPEER, 1L) == CURLE_OK &&
        curl_easy_setopt(request, CURLOPT_SSL_VERIFYHOST, 2L) == CURLE_OK &&
        curl_easy_setopt(request, CURLOPT_USERAGENT, "rcli-cloud-auth/1") == CURLE_OK &&
        curl_easy_setopt(request, CURLOPT_WRITEFUNCTION, CollectBody) == CURLE_OK &&
        curl_easy_setopt(request, CURLOPT_WRITEDATA, &response) == CURLE_OK;
    if (configured && !input.body.empty()) {
        configured =
            input.body.size() <= static_cast<std::size_t>(std::numeric_limits<long>::max()) &&
            curl_easy_setopt(request, CURLOPT_POSTFIELDS, input.body.data()) == CURLE_OK &&
            curl_easy_setopt(request, CURLOPT_POSTFIELDSIZE,
                             static_cast<long>(input.body.size())) == CURLE_OK;
    }

    const CURLcode result = configured ? curl_easy_perform(request) : CURLE_FAILED_INIT;
    long status = 0;
    const bool received = result == CURLE_OK &&
                          curl_easy_getinfo(request, CURLINFO_RESPONSE_CODE, &status) == CURLE_OK;
    curl_slist_free_all(headers);
    curl_easy_cleanup(request);
    if (!received || response.too_large || status < 100 || status > 599) {
        output->status = 0;
        output->body.clear();
        if (error != nullptr) {
            *error = response.too_large ? "console response exceeded the safety limit"
                                        : "could not reach the RunAnywhere console";
        }
        return false;
    }
    output->status = static_cast<int>(status);
    return true;
}

void HttpError(const char* operation, int status, std::string* error) {
    if (error != nullptr) {
        *error =
            std::string("console ") + operation + " failed with HTTP " + std::to_string(status);
    }
}

bool ParseObject(const HttpResponse& response, Json* object, std::string* error) {
    if (object == nullptr) {
        if (error != nullptr) {
            *error = "internal console response error";
        }
        return false;
    }
    try {
        Json parsed = Json::parse(response.body);
        if (!parsed.is_object()) {
            if (error != nullptr) {
                *error = "console returned a JSON value instead of an object";
            }
            return false;
        }
        *object = std::move(parsed);
        return true;
    } catch (const Json::exception&) {
        // Do not include the response body: an upstream error can echo a token.
        if (error != nullptr) {
            *error = "console returned malformed JSON";
        }
        return false;
    }
}

bool RequiredString(const Json& object, const char* key, std::string* value, std::string* error) {
    const auto found = object.find(key);
    if (found == object.end() || !found->is_string() ||
        found->get_ref<const std::string&>().empty()) {
        if (error != nullptr) {
            *error = std::string("console response is missing ") + key;
        }
        return false;
    }
    *value = found->get<std::string>();
    return true;
}

std::string OptionalString(const Json& object, const char* key) {
    const auto found = object.find(key);
    return found != object.end() && found->is_string() ? found->get<std::string>() : std::string();
}

long Number(const Json& object, const char* key, long fallback = 0) {
    const auto found = object.find(key);
    if (found == object.end() || !found->is_number_integer()) {
        return fallback;
    }
    try {
        return found->get<long>();
    } catch (const Json::exception&) {
        return fallback;
    }
}

bool Send(const Transport& transport, HttpRequest request, HttpResponse* response,
          std::string* error) {
    if (!transport(request, response, error)) {
        if (error != nullptr && error->empty()) {
            *error = "could not reach the RunAnywhere console";
        }
        return false;
    }
    return true;
}

bool DisplayTextIsSafe(const std::string& value, std::size_t maximum) {
    return !value.empty() && value.size() <= maximum &&
           std::all_of(value.begin(), value.end(),
                       [](unsigned char c) { return c >= 0x20 && c <= 0x7e; });
}

bool RequestCodeIsSafe(const std::string& value) {
    return value.size() >= 4 && value.size() <= 64 &&
           std::all_of(value.begin(), value.end(), [](unsigned char c) {
               return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                      c == '-';
           });
}

bool ReadGrant(const Json& object, Grant* grant, std::string* error) {
    grant->access_token = OptionalString(object, "access_token");
    grant->refresh_token = OptionalString(object, "refresh_token");
    grant->email = OptionalString(object, "email");
    grant->plan = OptionalString(object, "plan");
    grant->expires_in = std::max(0L, Number(object, "expires_in"));
    if ((!grant->access_token.empty() && !SessionTokenIsSafe(grant->access_token)) ||
        (!grant->refresh_token.empty() && !SessionTokenIsSafe(grant->refresh_token)) ||
        (!grant->email.empty() && !DisplayTextIsSafe(grant->email, 320)) ||
        (!grant->plan.empty() && !DisplayTextIsSafe(grant->plan, 80))) {
        if (error != nullptr) {
            *error = "console returned an invalid cloud session";
        }
        return false;
    }
    return true;
}

bool ConsoleOrigin(const std::string& input, std::string* origin, std::string* error) {
    return NormalizeConsoleUrl(input, origin, error);
}

}  // namespace

ConsoleClient::ConsoleClient(Transport transport)
    : transport_(transport ? std::move(transport) : Transport(DefaultTransport)) {}

bool ConsoleClient::BeginAuthorization(const std::string& console_url, const std::string& hostname,
                                       Authorization* authorization, std::string* error) const {
    if (authorization == nullptr) {
        if (error != nullptr) {
            *error = "internal authorization error";
        }
        return false;
    }
    std::string origin;
    if (!ConsoleOrigin(console_url, &origin, error)) {
        return false;
    }
    const Json payload = {{"hostname", hostname}, {"client", "rcli"}};
    HttpResponse response;
    if (!Send(transport_, {"POST", origin + "/auth/cli/start", payload.dump(), {}}, &response,
              error)) {
        return false;
    }
    if (response.status != 200) {
        HttpError("authorization", response.status, error);
        return false;
    }

    Json object;
    if (!ParseObject(response, &object, error) ||
        !RequiredString(object, "request_code", &authorization->request_code, error) ||
        !RequiredString(object, "poll_secret", &authorization->poll_secret, error) ||
        !RequiredString(object, "verification_url", &authorization->verification_url, error)) {
        return false;
    }
    if (!RequestCodeIsSafe(authorization->request_code) ||
        !SessionTokenIsSafe(authorization->poll_secret)) {
        if (error != nullptr) {
            *error = "console returned an invalid authorization request";
        }
        return false;
    }
    const long expires = Number(object, "expires_in", 600);
    const long interval = Number(object, "interval", 2);
    authorization->expires_in = static_cast<int>(std::clamp(expires, 30L, 1800L));
    authorization->interval = static_cast<int>(std::clamp(interval, 1L, 30L));
    return true;
}

PollResult ConsoleClient::Poll(const std::string& console_url, const Authorization& authorization,
                               Grant* grant, std::string* error) const {
    if (grant == nullptr) {
        if (error != nullptr) {
            *error = "internal authorization grant error";
        }
        return PollResult::Failed;
    }
    std::string origin;
    if (!ConsoleOrigin(console_url, &origin, error)) {
        return PollResult::Failed;
    }
    const Json payload = {{"request_code", authorization.request_code},
                          {"poll_secret", authorization.poll_secret}};
    HttpResponse response;
    if (!Send(transport_, {"POST", origin + "/auth/cli/poll", payload.dump(), {}}, &response,
              error)) {
        return PollResult::Failed;
    }
    if (response.status != 200) {
        HttpError("poll", response.status, error);
        return PollResult::Failed;
    }

    Json object;
    if (!ParseObject(response, &object, error)) {
        return PollResult::Failed;
    }
    std::string state;
    if (!RequiredString(object, "status", &state, error)) {
        return PollResult::Failed;
    }
    if (state == "pending") {
        return PollResult::Pending;
    }
    if (state == "denied") {
        return PollResult::Denied;
    }
    if (state == "expired") {
        return PollResult::Expired;
    }
    if (state != "approved") {
        if (error != nullptr) {
            *error = "console returned an unknown authorization state";
        }
        return PollResult::Failed;
    }

    if (!ReadGrant(object, grant, error)) {
        return PollResult::Failed;
    }
    if (grant->access_token.empty() || grant->refresh_token.empty()) {
        if (error != nullptr) {
            *error = "console approved the request without a complete session";
        }
        return PollResult::Failed;
    }
    return PollResult::Approved;
}

bool ConsoleClient::Refresh(const std::string& console_url, const std::string& refresh_token,
                            Grant* grant, std::string* error) const {
    if (grant == nullptr || !SessionTokenIsSafe(refresh_token)) {
        if (error != nullptr) {
            *error = "no refresh token is available";
        }
        return false;
    }
    std::string origin;
    if (!ConsoleOrigin(console_url, &origin, error)) {
        return false;
    }
    const Json payload = {{"refresh_token", refresh_token}};
    HttpResponse response;
    if (!Send(transport_, {"POST", origin + "/auth/cli/refresh", payload.dump(), {}}, &response,
              error)) {
        return false;
    }
    if (response.status != 200) {
        HttpError("refresh", response.status, error);
        return false;
    }
    Json object;
    if (!ParseObject(response, &object, error)) {
        return false;
    }
    if (!ReadGrant(object, grant, error)) {
        return false;
    }
    if (grant->access_token.empty()) {
        if (error != nullptr) {
            *error = "console refreshed the session without an access token";
        }
        return false;
    }
    return true;
}

IdentityResult ConsoleClient::WhoAmI(const std::string& console_url,
                                     const std::string& access_token, Identity* identity,
                                     std::string* error) const {
    if (identity == nullptr || !SessionTokenIsSafe(access_token)) {
        if (error != nullptr) {
            *error = "no access token is available";
        }
        return IdentityResult::Failed;
    }
    std::string origin;
    if (!ConsoleOrigin(console_url, &origin, error)) {
        return IdentityResult::Failed;
    }
    HttpResponse response;
    if (!Send(transport_, {"GET", origin + "/v1/me", {}, access_token}, &response, error)) {
        return IdentityResult::Failed;
    }
    if (response.status == 401) {
        if (error != nullptr) {
            *error = "console session expired";
        }
        return IdentityResult::Unauthorized;
    }
    if (response.status != 200) {
        HttpError("identity request", response.status, error);
        return IdentityResult::Failed;
    }

    Json object;
    if (!ParseObject(response, &object, error) ||
        !RequiredString(object, "email", &identity->email, error)) {
        return IdentityResult::Failed;
    }
    if (!DisplayTextIsSafe(identity->email, 320)) {
        if (error != nullptr) {
            *error = "console returned an invalid account identity";
        }
        return IdentityResult::Failed;
    }
    identity->plan = OptionalString(object, "plan");
    identity->tokens_this_month = Number(object, "tokens_this_month");
    identity->monthly_token_limit = Number(object, "monthly_token_limit");
    if ((!identity->plan.empty() && !DisplayTextIsSafe(identity->plan, 80)) ||
        identity->tokens_this_month < 0 || identity->monthly_token_limit < 0) {
        if (error != nullptr) {
            *error = "console returned invalid account usage";
        }
        return IdentityResult::Failed;
    }
    return IdentityResult::Ok;
}

bool ConsoleClient::Revoke(const std::string& console_url, const std::string& access_token,
                           const std::string& refresh_token, std::string* error) const {
    if (access_token.empty() && refresh_token.empty()) {
        return true;
    }
    if ((!access_token.empty() && !SessionTokenIsSafe(access_token)) ||
        (!refresh_token.empty() && !SessionTokenIsSafe(refresh_token))) {
        if (error != nullptr) {
            *error = "cloud session contains an invalid token encoding";
        }
        return false;
    }
    std::string origin;
    if (!ConsoleOrigin(console_url, &origin, error)) {
        return false;
    }
    const Json payload = {{"refresh_token", refresh_token}};
    HttpResponse response;
    if (!Send(transport_, {"POST", origin + "/auth/cli/revoke", payload.dump(), access_token},
              &response, error)) {
        return false;
    }
    if (response.status != 200 && response.status != 204) {
        HttpError("revoke", response.status, error);
        return false;
    }
    return true;
}

}  // namespace rcli::account

namespace rcli::account {

bool BeginAuthorization(const std::string& console_url, const std::string& hostname,
                        Authorization* authorization, std::string* error) {
    return ConsoleClient().BeginAuthorization(console_url, hostname, authorization, error);
}

PollResult Poll(const std::string& console_url, const Authorization& authorization, Grant* grant,
                std::string* error) {
    return ConsoleClient().Poll(console_url, authorization, grant, error);
}

bool Refresh(const std::string& console_url, const std::string& refresh_token, Grant* grant,
             std::string* error) {
    return ConsoleClient().Refresh(console_url, refresh_token, grant, error);
}

bool WhoAmI(const std::string& console_url, const std::string& token, Identity* identity,
            std::string* error) {
    return ConsoleClient().WhoAmI(console_url, token, identity, error) == IdentityResult::Ok;
}

}  // namespace rcli::account
