#include "ide/jetbrains_profile.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "io/output.h"
#include "harness/harness.h"

#include <nlohmann/json.hpp>

#if defined(__APPLE__)
#include <Security/Security.h>
#endif

namespace wally::ide {
namespace {

namespace fs = std::filesystem;

/// AI Assistant's marketplace id. The IDE's own `installPlugins` resolves it.
constexpr const char* kPluginID = "com.intellij.ml.llm";
/// What the plugin unpacks to inside the configuration tree.
constexpr const char* kPluginDirectory = "ml-llm";
/// The settings file behind `@State(name = "OpenAILikeLlmProviderSettings")`.
constexpr const char* kSettingsFile = "llm.provider.openai.like.xml";
constexpr const char* kComponent = "OpenAILikeLlmProviderSettings";
/// The provider selection, behind `@State(name = "LlmCustomModelsSettings")`.
constexpr const char* kModelsFile = "llm.custom.models.xml";
constexpr const char* kModelsComponent = "LlmCustomModelsSettings";
/// The set of providers the IDE will talk to at all.
constexpr const char* kProvidersFile = "llm.third.party.ai.providers.xml";
constexpr const char* kProvidersComponent = "LLMThirdPartyAIProvidersSettings";
/// `enableProvider` refuses to add anything until this has been accepted, so
/// the set above is ignored without it. Third-party providers are a beta
/// feature and this is the acknowledgement the IDE would otherwise ask for.
constexpr const char* kAcknowledgementKey =
    "llm.third.party.ai.services.acknowledgement.accepted";
/// Application properties, kept as a JSON blob inside a CDATA section.
constexpr const char* kPropertiesFile = "other.xml";
/// `OPEN_AI_API_PROVIDER_ID`, which is also the credential's key.
constexpr const char* kProviderID = "OpenAIAPI";
/// The subsystem the platform prefixes credentials with.
constexpr const char* kSubsystem = "AI Assistant";
/// cpp-httplib serves HTTP/1.1 only, and the client's default is negotiated
/// upward. Left unset, the first request fails before the model is ever asked.
constexpr const char* kHttpVersion = "HTTP_1_1";

std::string Home() {
    const char* home = std::getenv("HOME");
    return home != nullptr ? std::string(home) : std::string();
}

/// The credential store's service name: subsystem and key joined by an em dash,
/// which is the separator the platform writes and therefore the one it reads.
std::string ServiceName() {
    return std::string("IntelliJ Platform ") + kSubsystem + " \xE2\x80\x94 " + kProviderID;
}

bool WriteFile(const fs::path& path, const std::string& contents, std::string* error) {
    std::error_code code;
    fs::create_directories(path.parent_path(), code);
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        *error = "cannot write " + path.string();
        return false;
    }
    out << contents;
    if (!out) {
        *error = "cannot write " + path.string();
        return false;
    }
    return true;
}

/// The settings tree, with only the two options the state class actually
/// persists. Anything else here is dropped on the IDE's next write anyway.
std::string SettingsXML(const std::string& base_url) {
    return std::string("<application>\n  <component name=\"") + kComponent + "\">\n" +
           "    <option name=\"baseUrl\" value=\"" + base_url + "\" />\n" +
           "    <option name=\"httpClientVersion\" value=\"" + kHttpVersion + "\" />\n" +
           "  </component>\n</application>\n";
}

/// Which model each of the IDE's three roles should use.
///
/// One model answers all three because that is what wally is serving. The id is
/// `<providerId>/<model>`, the separator being `ThirdPartyLLMProfileId.DELIM`.
std::string ModelsXML(const std::string& model) {
    const std::string id = std::string(kProviderID) + "/" + model;
    return std::string("<application>\n  <component name=\"") + kModelsComponent + "\">\n" +
           "    <option name=\"isEnabled\" value=\"true\" />\n" +
           "    <option name=\"defaultModelId\" value=\"" + id + "\" />\n" +
           "    <option name=\"fastModelId\" value=\"" + id + "\" />\n" +
           "    <option name=\"editorModelId\" value=\"" + id + "\" />\n" +
           "  </component>\n</application>\n";
}

/// The one provider the IDE is allowed to talk to.
///
/// The members are nested directly, with no element naming the collection.
/// A `<set>` wrapper — the shape most IntelliJ collections serialize to — is
/// silently dropped on load, which reads exactly like the file being ignored.
std::string ProvidersXML() {
    return std::string("<application>\n  <component name=\"") + kProvidersComponent + "\">\n" +
           "    <option name=\"enabledThirdPartyAIProviders\">\n" +
           "      <option value=\"" + kProviderID + "\" />\n" +
           "    </option>\n  </component>\n</application>\n";
}

std::string ReadFile(const fs::path& path) {
    std::ifstream in(path);
    return in ? std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>())
              : std::string();
}

/// Accepts the third-party acknowledgement in the IDE's application properties.
///
/// The properties are a JSON object inside a CDATA section inside the XML, so
/// the blob is parsed rather than pattern-matched — every other value in there
/// belongs to the reader and has to survive untouched.
bool AcceptAcknowledgement(const fs::path& path, std::string* error) {
    std::string document = ReadFile(path);
    constexpr const char* kOpen = "<component name=\"PropertyService\"><![CDATA[";
    constexpr const char* kClose = "]]></component>";
    const size_t open = document.find(kOpen);
    if (open == std::string::npos) {
        // No properties yet, which a never-launched IDE has not written. The
        // component is ours to create, beside whatever else is in the file.
        nlohmann::json properties;
        properties["keyToString"][kAcknowledgementKey] = "true";
        const std::string component = std::string("  ") + kOpen + properties.dump(2) + kClose + "\n";
        const size_t end = document.find("</application>");
        if (document.empty() || end == std::string::npos) {
            return WriteFile(path, "<application>\n" + component + "</application>\n", error);
        }
        document.insert(end, component);
        return WriteFile(path, document, error);
    }

    const size_t start = open + std::string(kOpen).size();
    const size_t close = document.find(kClose, start);
    if (close == std::string::npos) {
        *error = "cannot read the properties in " + path.string();
        return false;
    }
    nlohmann::json properties = nlohmann::json::parse(document.substr(start, close - start),
                                                     nullptr, false);
    if (properties.is_discarded()) {
        *error = "cannot read the properties in " + path.string();
        return false;
    }
    properties["keyToString"][kAcknowledgementKey] = "true";
    document.replace(start, close - start, properties.dump(2));
    return WriteFile(path, document, error);
}

#if defined(__APPLE__)
CFStringRef CopyString(const std::string& value) {
    return CFStringCreateWithBytes(nullptr, reinterpret_cast<const UInt8*>(value.data()),
                                   static_cast<CFIndex>(value.size()), kCFStringEncodingUTF8,
                                   false);
}

/// The query identifying our credential, without the secret in it.
CFMutableDictionaryRef CopyQuery() {
    CFMutableDictionaryRef query = CFDictionaryCreateMutable(
        nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(query, kSecClass, kSecClassGenericPassword);
    CFStringRef service = CopyString(ServiceName());
    CFStringRef account = CopyString(kProviderID);
    CFDictionarySetValue(query, kSecAttrService, service);
    CFDictionarySetValue(query, kSecAttrAccount, account);
    CFRelease(service);
    CFRelease(account);
    return query;
}

/// An access list naming the two programs allowed to read the item without
/// asking: this one, which writes it, and the IDE, which reads it.
///
/// Without this the item belongs to wally alone, and the IDE's first read pops a
/// keychain dialog — the one manual step this command exists to remove. The
/// legacy access APIs are what create such a list, and they are also what the
/// platform's own `MacOSKeychainStorage` uses, so the item ends up the shape
/// the IDE already expects.
CFTypeRef CopyAccess(const std::string& reader) {
    SecTrustedApplicationRef self = nullptr;
    SecTrustedApplicationRef other = nullptr;
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    if (SecTrustedApplicationCreateFromPath(nullptr, &self) != errSecSuccess ||
        SecTrustedApplicationCreateFromPath(reader.c_str(), &other) != errSecSuccess) {
        if (self != nullptr) {
            CFRelease(self);
        }
        if (other != nullptr) {
            CFRelease(other);
        }
        return nullptr;
    }
    const void* trusted[] = {self, other};
    CFArrayRef list = CFArrayCreate(nullptr, trusted, 2, &kCFTypeArrayCallBacks);
    CFStringRef label = CopyString(ServiceName());
    SecAccessRef access = nullptr;
    const OSStatus status = SecAccessCreate(label, list, &access);
#pragma clang diagnostic pop
    CFRelease(label);
    CFRelease(list);
    CFRelease(self);
    CFRelease(other);
    return status == errSecSuccess ? access : nullptr;
}

/// Stores `secret`, replacing whatever was there, readable by `reader` without
/// a prompt.
///
/// Written through the Security framework rather than the `security` tool
/// because that one takes the secret as an argument, where every other process
/// on the machine can read it out of the process list.
bool StoreSecret(const std::string& secret, const std::string& reader, std::string* error) {
    CFMutableDictionaryRef query = CopyQuery();
    SecItemDelete(query);
    CFDataRef data = CFDataCreate(nullptr, reinterpret_cast<const UInt8*>(secret.data()),
                                  static_cast<CFIndex>(secret.size()));
    CFDictionarySetValue(query, kSecValueData, data);
    CFTypeRef access = CopyAccess(reader);
    if (access != nullptr) {
        CFDictionarySetValue(query, kSecAttrAccess, access);
    }
    const OSStatus status = SecItemAdd(query, nullptr);
    if (access != nullptr) {
        CFRelease(access);
    }
    CFRelease(data);
    CFRelease(query);
    if (status != errSecSuccess) {
        *error = "cannot store the key in the keychain (OSStatus " + std::to_string(status) + ")";
        return false;
    }
    return true;
}

void DropSecret() {
    CFMutableDictionaryRef query = CopyQuery();
    SecItemDelete(query);
    CFRelease(query);
}
#else
bool StoreSecret(const std::string&, const std::string&, std::string* error) {
    *error = "the JetBrains credential store is only wired up on macOS";
    return false;
}

void DropSecret() {}
#endif

/// Installs AI Assistant through the IDE's own command line.
///
/// The IDE is the only thing that knows which build of the plugin matches it,
/// so asking it beats resolving a download ourselves. It also creates the
/// configuration directory on the way, which a never-launched IDE has not.
bool InstallPlugin(const Product& product, const std::string& bundle, std::string* error) {
    const std::string launcher = bundle + "/Contents/MacOS/" + product.launcher;
    out::status_line("installing JetBrains AI Assistant; this happens once and takes a minute");
    if (harness::Launch(launcher, {}, {"installPlugins", kPluginID}) != 0) {
        *error = "could not install AI Assistant into " + std::string(product.id);
        return false;
    }
    return true;
}

bool PluginInstalled(const std::string& config) {
    std::error_code code;
    return !config.empty() && fs::exists(fs::path(config) / "plugins" / kPluginDirectory, code);
}

}  // namespace

std::string BundlePath(const Product& product) {
    const std::string home = Home();
    std::vector<std::string> roots{"/Applications/"};
    if (!home.empty()) {
        roots.push_back(home + "/Applications/");
    }
    std::error_code code;
    for (const std::string& root : roots) {
        const std::string path = root + product.bundle;
        if (fs::exists(fs::path(path) / "Contents" / "Info.plist", code)) {
            return path;
        }
    }
    return {};
}

std::string ConfigDirectory(const Product& product) {
    const std::string home = Home();
    if (home.empty()) {
        return {};
    }
    const fs::path root = fs::path(home) / "Library" / "Application Support" / "JetBrains";
    std::error_code code;
    // One tree per release, so the newest name wins. Sorting the names works
    // because JetBrains pads the version the same way in every one of them.
    std::string newest;
    for (const fs::directory_entry& entry : fs::directory_iterator(root, code)) {
        const std::string name = entry.path().filename().string();
        if (name.rfind(product.config_prefix, 0) == 0 && name > newest) {
            newest = name;
        }
    }
    return newest.empty() ? std::string() : (root / newest).string();
}

bool ApplyProvider(const Product& product, const std::string& base_url,
                   const std::string& api_key, const std::string& model,
                   std::string* error) {
    const std::string bundle = BundlePath(product);
    if (bundle.empty()) {
        *error = std::string(product.bundle) + " is not installed";
        return false;
    }

    std::string config = ConfigDirectory(product);
    if (!PluginInstalled(config)) {
        if (!InstallPlugin(product, bundle, error)) {
            return false;
        }
        // The install is what creates the tree on an IDE nobody has launched.
        config = ConfigDirectory(product);
        if (!PluginInstalled(config)) {
            *error = "AI Assistant did not appear in " +
                     (config.empty() ? std::string("the configuration directory") : config);
            return false;
        }
    }

    const fs::path options = fs::path(config) / "options";
    // The model ids are deliberately not written. The IDE picks them up from
    // the provider once it can reach it, and deletes any file we leave behind.
    (void)model;
    if (!WriteFile(options / kSettingsFile, SettingsXML(base_url), error) ||
        !WriteFile(options / kProvidersFile, ProvidersXML(), error) ||
        !AcceptAcknowledgement(options / kPropertiesFile, error)) {
        return false;
    }
    // A local server needs no key, and the IDE is content without one — it
    // reports the provider configured with an empty key and talks to it anyway.
    // So nothing is put in the reader's keychain unless there is a real secret
    // to put there, which is the upstream case.
    if (api_key.empty()) {
        DropSecret();
        return true;
    }
    return StoreSecret(api_key, bundle + "/Contents/MacOS/" + product.launcher, error);
}

bool RestoreProvider(const Product& product, std::string* error) {
    const std::string config = ConfigDirectory(product);
    if (config.empty()) {
        *error = std::string(product.id) + " has no configuration directory to clear";
        return false;
    }
    DropSecret();
    std::error_code code;
    fs::remove(fs::path(config) / "options" / kSettingsFile, code);
    fs::remove(fs::path(config) / "options" / kModelsFile, code);
    fs::remove(fs::path(config) / "options" / kProvidersFile, code);
    return true;
}

}  // namespace wally::ide
