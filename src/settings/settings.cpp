#include "settings/settings.h"

#include <algorithm>
#include <cstdlib>

#include "sdk/session.h"

namespace rcli::settings {
namespace {

std::string g_accelerator = "auto";
std::string g_engine = "auto";
int g_context_length = 0;
std::string g_reasoning = "auto";
float g_temperature = 0.7F;
int g_max_tokens = 512;

bool OneOf(const std::vector<std::string>& allowed, const std::string& value) {
    return std::find(allowed.begin(), allowed.end(), value) != allowed.end();
}

const std::vector<std::string> kAccelerators{"auto", "cpu", "gpu", "npu"};
/// Only engines this build actually registered. Offering `mlx` on a build
/// without MLX would be a choice that silently does nothing.
std::vector<std::string> Engines() {
    std::vector<std::string> names{"auto"};
    for (const sdk::BackendInfo& backend : sdk::Session::Instance().backends()) {
        names.push_back(backend.name);
    }
    return names;
}
const std::vector<std::string> kReasoning{"auto", "on", "off"};

/// A free-text number setting. Rejecting out-of-range here is what keeps a
/// value that the engine would silently clamp from being reported as accepted.
template <typename T>
std::function<bool(const std::string&)> Number(T* target, double low, double high) {
    return [target, low, high](const std::string& value) {
        char* end = nullptr;
        const double parsed = std::strtod(value.c_str(), &end);
        if (end == value.c_str() || *end != '\0' || parsed < low || parsed > high) {
            return false;
        }
        *target = static_cast<T>(parsed);
        return true;
    };
}

std::vector<Setting> Build() {
    static const std::vector<std::string> kEngines = Engines();
    return {
        Setting{
            .name = "accelerator",
            .summary = "where a model runs; advisory, an engine may ignore it",
            .values = kAccelerators,
            .get = [] { return g_accelerator; },
            .set =
                [](const std::string& value) {
                    if (!OneOf(kAccelerators, value)) {
                        return false;
                    }
                    g_accelerator = value;
                    return true;
                },
        },
        Setting{
            .name = "engine",
            .summary = "pin an engine instead of letting priority decide",
            .values = kEngines,
            .get = [] { return g_engine; },
            .set =
                [](const std::string& value) {
                    if (!OneOf(kEngines, value)) {
                        return false;
                    }
                    g_engine = value;
                    return true;
                },
        },
        Setting{
            .name = "context-length",
            .summary = "context window at load time; 0 leaves it to the engine",
            .values = {},
            .get = [] { return std::to_string(g_context_length); },
            .set = Number(&g_context_length, 0, 1048576),
        },
        Setting{
            .name = "reasoning",
            .summary = "think before answering; auto follows the model",
            .values = kReasoning,
            .get = [] { return g_reasoning; },
            .set =
                [](const std::string& value) {
                    if (!OneOf(kReasoning, value)) {
                        return false;
                    }
                    g_reasoning = value;
                    return true;
                },
        },
        Setting{
            .name = "temperature",
            .summary = "0 is greedy, 2 is the most random the sampler allows",
            .values = {},
            .get =
                [] {
                    std::string text = std::to_string(g_temperature);
                    text.erase(text.find_last_not_of('0') + 1);
                    return text.back() == '.' ? text + "0" : text;
                },
            .set = Number(&g_temperature, 0.0, 2.0),
        },
        Setting{
            .name = "max-tokens",
            .summary = "longest answer the model may produce",
            .values = {},
            .get = [] { return std::to_string(g_max_tokens); },
            .set = Number(&g_max_tokens, 1, 131072),
        },
    };
}

}  // namespace

const std::vector<Setting>& All() {
    static const std::vector<Setting> settings = Build();
    return settings;
}

const Setting* Find(const std::string& name) {
    for (const Setting& setting : All()) {
        if (setting.name == name) {
            return &setting;
        }
    }
    return nullptr;
}

std::string Accelerator() { return g_accelerator; }
std::string Engine() { return g_engine; }
int ContextLength() { return g_context_length; }
std::string Reasoning() { return g_reasoning; }
float Temperature() { return g_temperature; }
int MaxTokens() { return g_max_tokens; }

}  // namespace rcli::settings
