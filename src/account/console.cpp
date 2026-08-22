#include "account/console.h"

#include <cstring>
#include <string>
#include <vector>

#include "rac/infrastructure/http/rac_http_client.h"

namespace rcli::account {
namespace {

struct Reply {
    int status = 0;
    std::string body;
};

std::string Quote(const std::string& text) {
    std::string out = "\"";
    for (const char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            default: out += c;
        }
    }
    return out + "\"";
}

std::string Field(const std::string& document, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    std::size_t at = document.find(needle);
    if (at == std::string::npos) {
        return {};
    }
    at = document.find(':', at + needle.size());
    if (at == std::string::npos) {
        return {};
    }
    while (at < document.size() && (document[at] == ':' || document[at] == ' ')) {
        ++at;
    }
    if (at >= document.size()) {
        return {};
    }
    if (document[at] != '"') {
        std::string number;
        while (at < document.size() && (std::isdigit(document[at]) != 0 || document[at] == '-')) {
            number += document[at++];
        }
        return number;
    }
    std::string value;
    for (std::size_t i = at + 1; i < document.size(); ++i) {
        if (document[i] == '\\' && i + 1 < document.size()) {
            const char next = document[++i];
            value += next == 'n' ? '\n' : next;
            continue;
        }
        if (document[i] == '"') {
            break;
        }
        value += document[i];
    }
    return value;
}

bool Call(const std::string& url, const char* method, const std::string& body,
          const std::string& bearer, Reply* reply, std::string* error) {
    rac_http_client_t* client = nullptr;
    if (rac_http_client_create(&client) != RAC_SUCCESS || client == nullptr) {
        if (error != nullptr) {
            *error = "could not create an HTTP client";
        }
        return false;
    }

    std::vector<rac_http_header_kv_t> headers;
    headers.push_back({"Content-Type", "application/json"});
    const std::string authorization = "Bearer " + bearer;
    if (!bearer.empty()) {
        headers.push_back({"Authorization", authorization.c_str()});
    }

    rac_http_request_t request{};
    request.method = method;
    request.url = url.c_str();
    request.headers = headers.data();
    request.header_count = headers.size();
    request.body_bytes = reinterpret_cast<const uint8_t*>(body.data());
    request.body_len = body.size();
    request.timeout_ms = 30000;
    // A credential travels in these requests, so a 3xx must come back to us
    // rather than being replayed with its headers against another origin.
    request.follow_redirects = RAC_FALSE;

    rac_http_response_t response{};
    const rac_result_t rc = rac_http_request_send(client, &request, &response);
    if (rc != RAC_SUCCESS) {
        if (error != nullptr) {
            *error = "could not reach " + url;
        }
        rac_http_client_destroy(client);
        return false;
    }

    reply->status = response.status;
    if (response.body_bytes != nullptr && response.body_len > 0) {
        reply->body.assign(reinterpret_cast<const char*>(response.body_bytes), response.body_len);
    }
    rac_http_response_free(&response);
    rac_http_client_destroy(client);
    return true;
}

std::string Detail(const Reply& reply) {
    const std::string detail = Field(reply.body, "detail");
    return detail.empty() ? "the console returned " + std::to_string(reply.status) : detail;
}

}  // namespace

bool BeginAuthorization(const std::string& console_url, const std::string& hostname,
                        Authorization* authorization, std::string* error) {
    Reply reply;
    if (!Call(console_url + "/auth/cli/start", "POST", "{\"hostname\":" + Quote(hostname) + "}", {},
              &reply, error)) {
        return false;
    }
    if (reply.status != 200) {
        if (error != nullptr) {
            *error = Detail(reply);
        }
        return false;
    }
    authorization->request_code = Field(reply.body, "request_code");
    authorization->poll_secret = Field(reply.body, "poll_secret");
    authorization->verification_url = Field(reply.body, "verification_url");
    authorization->expires_in = std::stoi("0" + Field(reply.body, "expires_in"));
    const int interval = std::stoi("0" + Field(reply.body, "interval"));
    authorization->interval = interval > 0 ? interval : 2;
    if (authorization->request_code.empty() || authorization->verification_url.empty()) {
        if (error != nullptr) {
            *error = "the console did not open an authorization request";
        }
        return false;
    }
    return true;
}

namespace {

void ReadGrant(const std::string& body, Grant* grant) {
    grant->access_token = Field(body, "access_token");
    grant->refresh_token = Field(body, "refresh_token");
    grant->email = Field(body, "email");
    grant->plan = Field(body, "plan");
    grant->expires_in = std::stol("0" + Field(body, "expires_in"));
}

}  // namespace

PollResult Poll(const std::string& console_url, const Authorization& authorization, Grant* grant,
                std::string* error) {
    const std::string payload = "{\"request_code\":" + Quote(authorization.request_code) +
                                ",\"poll_secret\":" + Quote(authorization.poll_secret) + "}";
    Reply reply;
    if (!Call(console_url + "/auth/cli/poll", "POST", payload, {}, &reply, error)) {
        return PollResult::Failed;
    }
    if (reply.status != 200) {
        if (error != nullptr) {
            *error = Detail(reply);
        }
        return PollResult::Failed;
    }
    const std::string state = Field(reply.body, "status");
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
            *error = "the console reported an unknown state: " + state;
        }
        return PollResult::Failed;
    }
    ReadGrant(reply.body, grant);
    if (grant->access_token.empty()) {
        if (error != nullptr) {
            *error = "the console approved the request but issued no token";
        }
        return PollResult::Failed;
    }
    return PollResult::Approved;
}

bool Refresh(const std::string& console_url, const std::string& refresh_token, Grant* grant,
             std::string* error) {
    Reply reply;
    if (!Call(console_url + "/auth/cli/refresh", "POST",
              "{\"refresh_token\":" + Quote(refresh_token) + "}", {}, &reply, error)) {
        return false;
    }
    if (reply.status != 200) {
        if (error != nullptr) {
            *error = Detail(reply);
        }
        return false;
    }
    ReadGrant(reply.body, grant);
    return !grant->access_token.empty();
}

bool WhoAmI(const std::string& console_url, const std::string& token, Identity* identity,
            std::string* error) {
    Reply reply;
    if (!Call(console_url + "/v1/me", "GET", {}, token, &reply, error)) {
        return false;
    }
    if (reply.status != 200) {
        if (error != nullptr) {
            *error = Detail(reply);
        }
        return false;
    }
    identity->email = Field(reply.body, "email");
    identity->plan = Field(reply.body, "plan");
    identity->tokens_this_month = std::stol("0" + Field(reply.body, "tokens_this_month"));
    identity->monthly_token_limit = std::stol("0" + Field(reply.body, "monthly_token_limit"));
    return true;
}

}  // namespace rcli::account
