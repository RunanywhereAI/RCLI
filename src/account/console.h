#ifndef RCLI_ACCOUNT_CONSOLE_H
#define RCLI_ACCOUNT_CONSOLE_H

#include <string>

namespace rcli::account {

struct Identity {
    std::string email;
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

/// Opens an authorization request the browser will approve.
///
/// No credential is exchanged here or anywhere in the CLI. The request code
/// identifies the attempt publicly; the poll secret proves that the process
/// collecting the grant is the one that started it.
bool BeginAuthorization(const std::string& console_url, const std::string& hostname,
                        Authorization* authorization, std::string* error);

enum class PollResult { Pending, Approved, Denied, Expired, Failed };

PollResult Poll(const std::string& console_url, const Authorization& authorization, Grant* grant,
                std::string* error);

bool Refresh(const std::string& console_url, const std::string& refresh_token, Grant* grant,
             std::string* error);

bool WhoAmI(const std::string& console_url, const std::string& token, Identity* identity,
            std::string* error);

}  // namespace rcli::account

#endif  // RCLI_ACCOUNT_CONSOLE_H
