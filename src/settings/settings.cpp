#include "settings/settings.h"

#include <algorithm>
#include <cstdlib>

#include "theme/theme.h"

namespace rcli::settings {
namespace {

std::string g_accelerator = "auto";
std::string g_engine = "auto";
int g_context_length = 0;

bool OneOf(const std::vector<std::string>& allowed, const std::string& value) {
    return std::find(allowed.begin(), allowed.end(), value) != allowed.end();
}

const std::vector<std::string> kAccelerators{"auto", "cpu", "gpu", "npu"};
const std::vector<std::string> kEngines{"auto", "llamacpp", "mlx", "neurt", "onnx", "sherpa"};
const std::vector<std::string> kThemes{"dark", "light"};

std::vector<Setting> Build() {
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
            .set =
                [](const std::string& value) {
                    char* end = nullptr;
                    const long parsed = std::strtol(value.c_str(), &end, 10);
                    if (end == value.c_str() || *end != '\0' || parsed < 0) {
                        return false;
                    }
                    g_context_length = static_cast<int>(parsed);
                    return true;
                },
        },
        Setting{
            .name = "theme",
            .summary = "dark or light; detected from the terminal at startup",
            .values = kThemes,
            .get = [] { return theme::CurrentMode() == theme::Mode::Dark ? "dark" : "light"; },
            .set =
                [](const std::string& value) {
                    if (!OneOf(kThemes, value)) {
                        return false;
                    }
                    theme::SetMode(value == "dark" ? theme::Mode::Dark : theme::Mode::Light);
                    return true;
                },
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

}  // namespace rcli::settings
