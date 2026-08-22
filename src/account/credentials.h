#ifndef RCLI_ACCOUNT_CREDENTIALS_H
#define RCLI_ACCOUNT_CREDENTIALS_H

#include <string>

namespace rcli::account {

struct Credentials {
    std::string console_url;
    std::string email;
    std::string plan;
    std::string access_token;
    std::string refresh_token;

    bool signed_in() const { return !access_token.empty(); }
};

/// Where this profile keeps its credentials.
///
/// RCLI_PROFILE_DIR overrides it outright, which is what lets several signed-in
/// users share one machine without seeing each other's tokens.
std::string ProfileDirectory();

Credentials Load();

bool Save(const Credentials& credentials, std::string* error);

bool Clear(std::string* error);

std::string DefaultConsoleUrl();

}  // namespace rcli::account

#endif  // RCLI_ACCOUNT_CREDENTIALS_H
