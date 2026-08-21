#ifndef RCLI_SETTINGS_SETTINGS_H
#define RCLI_SETTINGS_SETTINGS_H

#include <functional>
#include <string>
#include <vector>

namespace rcli::settings {

/// One setting, described well enough that the settings screen, the `/set`
/// command and its autocomplete can all be generated from it.
///
/// Declaring them once is the point: a new setting appears in the UI, in the
/// command and in the suggestions without touching any of the three.
struct Setting {
    std::string name;
    std::string summary;
    /// Allowed values. Empty means free text (a number, a path).
    std::vector<std::string> values;
    std::function<std::string()> get;
    /// Returns false when the value is not acceptable.
    std::function<bool(const std::string&)> set;
};

const std::vector<Setting>& All();
const Setting* Find(const std::string& name);

/// Values the SDK is told about at model-load time.
std::string Accelerator();
std::string Engine();
int ContextLength();

/// Values sent with each generation.
std::string Reasoning();
float Temperature();
int MaxTokens();

}  // namespace rcli::settings

#endif  // RCLI_SETTINGS_SETTINGS_H
