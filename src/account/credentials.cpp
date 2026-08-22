#include "account/credentials.h"

#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#if !defined(_WIN32)
#include <fcntl.h>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>
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
    const std::string home = Env("HOME");
    if (!home.empty()) {
        return home;
    }
    // Falling back to a relative path would drop a token file wherever the
    // command happened to run, which is often a source tree.
    const passwd* entry = getpwuid(getuid());
    return entry != nullptr && entry->pw_dir != nullptr ? entry->pw_dir : std::string();
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

namespace {

/// Bearer and refresh tokens travel to this origin, so plain HTTP is only
/// tolerable when it cannot leave the machine.
bool LoopbackOrigin(const std::string& url) {
    return url.rfind("http://localhost", 0) == 0 || url.rfind("http://127.0.0.1", 0) == 0 ||
           url.rfind("http://[::1]", 0) == 0;
}

}  // namespace

bool ConsoleUrlIsSafe(const std::string& url) {
    return url.rfind("https://", 0) == 0 || LoopbackOrigin(url);
}

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
        return {};
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
    if (directory.empty()) {
        if (error != nullptr) {
            *error = "no home directory to store credentials in";
        }
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec) {
        if (error != nullptr) {
            *error = "could not create " + directory + ": " + ec.message();
        }
        return false;
    }

    const std::string path = directory + "/" + kFileName;
    std::ostringstream document;
    document << "{\n"
             << "  \"console_url\": " << Quote(credentials.console_url) << ",\n"
             << "  \"email\": " << Quote(credentials.email) << ",\n"
             << "  \"plan\": " << Quote(credentials.plan) << ",\n"
             << "  \"access_token\": " << Quote(credentials.access_token) << ",\n"
             << "  \"refresh_token\": " << Quote(credentials.refresh_token) << "\n"
             << "}\n";
    const std::string body = document.str();

#if defined(_WIN32)
    std::ofstream file(path, std::ios::trunc | std::ios::binary);
    if (!file || !(file << body)) {
        if (error != nullptr) {
            *error = "could not write " + path;
        }
        return false;
    }
    return true;
#else
    // Opened 0600 rather than written and then chmod'ed: the default umask
    // makes the file world-readable, and the tokens are on disk for the whole
    // window between the two calls.
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    if (fd < 0) {
        if (error != nullptr) {
            *error = "could not write " + path;
        }
        return false;
    }
    // An existing file keeps its old mode through O_CREAT, so tighten it too.
    ::fchmod(fd, S_IRUSR | S_IWUSR);
    std::size_t written = 0;
    while (written < body.size()) {
        const ssize_t chunk = ::write(fd, body.data() + written, body.size() - written);
        if (chunk <= 0) {
            if (errno == EINTR) {
                continue;
            }
            ::close(fd);
            if (error != nullptr) {
                *error = "could not write " + path;
            }
            return false;
        }
        written += static_cast<std::size_t>(chunk);
    }
    if (::close(fd) != 0) {
        if (error != nullptr) {
            *error = "could not finish writing " + path;
        }
        return false;
    }
    return true;
#endif
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
