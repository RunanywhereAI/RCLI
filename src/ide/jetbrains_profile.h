#ifndef RCLI_IDE_JETBRAINS_PROFILE_H
#define RCLI_IDE_JETBRAINS_PROFILE_H

#include <string>

/// AI Assistant's OpenAI-compatible provider, configured from outside the IDE.
///
/// A JetBrains IDE already ships an agent — AI Assistant, and Junie behind it —
/// so pointing one at a local model is a matter of telling that agent where the
/// model lives, not of nesting a second agent inside the editor. The provider
/// it exposes for this speaks plain OpenAI, which is the shape `rac_server`
/// already serves, so nothing has to be translated on the way.
///
/// Three things have to be written before the IDE starts, and it reads all of
/// them once at launch: a base URL in its own options tree, a key in the
/// platform credential store, and the provider selection that the Providers &
/// API keys page shows as a dropdown. The first two alone leave that dropdown
/// on "None" and the IDE reporting `byok=null`, which is what makes this look
/// like it worked when it has not.
///
/// The selection is stored as model ids rather than as a provider name: each is
/// `<providerId>/<model>`, and picking a provider is setting those three ids
/// and the enable flag beside them.
///
/// None of this needs a JetBrains AI subscription. BYOK is the supported path.
namespace rcli::ide {

/// The port the local server is asked for when serving a JetBrains IDE.
///
/// Fixed on purpose. The IDE reads its base URL once at startup, from a file
/// written before it launches, so a port that moved between runs would leave
/// that file naming something dead every time rcli exited first. Asking for the
/// same one keeps the configuration true, and a second rcli holding it only
/// costs this run a rewrite.
constexpr int kProviderPort = 11636;

/// A JetBrains IDE, named the way the reader types it.
struct Product {
    /// The subcommand: `clion`.
    const char* id;
    /// The application bundle to look for under /Applications.
    const char* bundle;
    /// The launcher inside the bundle, which doubles as the IDE's own CLI.
    const char* launcher;
    /// What its per-version configuration directory is called, before the
    /// version. JetBrains keeps one tree per release, so the newest wins.
    const char* config_prefix;
};

/// Installs AI Assistant if it is absent, points its provider at `base_url`,
/// and stores `api_key` where the IDE looks for it.
///
/// The install is the slow part and happens once; every later run finds the
/// plugin already there and only rewrites the URL. Returns false with `error`
/// set. Takes effect on the IDE's next launch, never on a running one.
bool ApplyProvider(const Product& product, const std::string& base_url,
                   const std::string& api_key, const std::string& model,
                   std::string* error);

/// Clears the base URL and drops the stored key, leaving the plugin installed.
///
/// The way out when a run left the IDE pointing at a port nothing is serving.
bool RestoreProvider(const Product& product, std::string* error);

/// The IDE's configuration directory, or empty when it has never been run.
std::string ConfigDirectory(const Product& product);

/// The application bundle's path, or empty when the IDE is not installed.
std::string BundlePath(const Product& product);

}  // namespace rcli::ide

#endif  // RCLI_IDE_JETBRAINS_PROFILE_H
