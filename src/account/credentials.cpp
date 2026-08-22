#include "account/credentials.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#if !defined(_WIN32)
#include <sys/stat.h>
#endif

namespace rcli::account {
namespace {

constexpr const char* kFileName = "credentials.json";

std::string Env(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr ? std::string(value) : std::string();
}

std::string HomeDirectory() {
#if defined(_WIN32)
    const std::string profile = Env("USERPROFILE");
    if (!profile.empty()) {
        return profile;
    }
    return Env("HOMEDRIVE") + Env("HOMEPATH");
#else
    return Env("HOME");
#endif
}

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
    at = document.find('"', at);
    if (at == std::string::npos) {
        return {};
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

}  // namespace

std::string DefaultConsoleUrl() {
    const std::string configured = Env("RCLI_CONSOLE_URL");
    return configured.empty() ? "http://localhost:8080" : configured;
}

std::string ProfileDirectory() {
    const std::string override_dir = Env("RCLI_PROFILE_DIR");
    if (!override_dir.empty()) {
        return override_dir;
    }
    const std::string home = HomeDirectory();
    if (home.empty()) {
        return ".rcli";
    }
    return home + "/.config/rcli";
}

Credentials Load() {
    Credentials credentials;
    credentials.console_url = DefaultConsoleUrl();

    std::ifstream file(ProfileDirectory() + "/" + kFileName);
    if (!file) {
        return credentials;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    const std::string document = buffer.str();

    const std::string stored_url = Field(document, "console_url");
    if (!stored_url.empty() && Env("RCLI_CONSOLE_URL").empty()) {
        credentials.console_url = stored_url;
    }
    credentials.email = Field(document, "email");
    credentials.plan = Field(document, "plan");
    credentials.access_token = Field(document, "access_token");
    credentials.refresh_token = Field(document, "refresh_token");
    return credentials;
}

bool Save(const Credentials& credentials, std::string* error) {
    const std::string directory = ProfileDirectory();
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec) {
        if (error != nullptr) {
            *error = "could not create " + directory + ": " + ec.message();
        }
        return false;
    }

    const std::string path = directory + "/" + kFileName;
    std::ofstream file(path, std::ios::trunc);
    if (!file) {
        if (error != nullptr) {
            *error = "could not write " + path;
        }
        return false;
    }
    file << "{\n"
         << "  \"console_url\": " << Quote(credentials.console_url) << ",\n"
         << "  \"email\": " << Quote(credentials.email) << ",\n"
         << "  \"plan\": " << Quote(credentials.plan) << ",\n"
         << "  \"access_token\": " << Quote(credentials.access_token) << ",\n"
         << "  \"refresh_token\": " << Quote(credentials.refresh_token) << "\n"
         << "}\n";
    file.close();

#if !defined(_WIN32)
    // The file holds a bearer token, so it is readable only by its owner. On
    // Windows the profile directory under the user account carries that.
    ::chmod(path.c_str(), S_IRUSR | S_IWUSR);
#endif
    return true;
}

bool Clear(std::string* error) {
    std::error_code ec;
    std::filesystem::remove(ProfileDirectory() + "/" + kFileName, ec);
    if (ec) {
        if (error != nullptr) {
            *error = ec.message();
        }
        return false;
    }
    return true;
}

}  // namespace rcli::account
