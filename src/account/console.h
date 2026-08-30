#ifndef RCLI_ACCOUNT_CONSOLE_H
#define RCLI_ACCOUNT_CONSOLE_H

#include <functional>
#include <string>

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
    long tokens_this_month = 0;
    long monthly_token_limit = 0;
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
/// The default transport uses libcurl directly. Tests inject a hermetic
/// transport so auth contracts never need a real account or network.
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
