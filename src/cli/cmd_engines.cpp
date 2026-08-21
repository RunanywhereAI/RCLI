#include <cstdio>
#include <memory>
#include <string>

#include "cli/commands.h"
#include "cli/output.h"
#include "sdk/session.h"
#include "settings/settings.h"

namespace rcli::cli {
namespace {

int Engines() {
    if (!Start()) {
        return 1;
    }
    const auto& session = sdk::Session::Instance();
    for (const sdk::BackendInfo& backend : session.backends()) {
        std::string serves;
        for (const std::string& primitive : backend.primitives) {
            serves += serves.empty() ? primitive : ", " + primitive;
        }
        char line[220];
        std::snprintf(line, sizeof(line), "%-12s %4d  %s", backend.name.c_str(), backend.priority,
                      serves.c_str());
        out::Line(line);
    }
    // An engine that is compiled in but did not come up is worth naming: a gap
    // the reader has to explain to themselves is worse than a stated reason.
    for (const std::string& reason : session.skipped()) {
        out::Status(reason);
    }
    return 0;
}

int Config(const std::string& name, const std::string& value) {
    // The engine setting's allowed values are the engines that actually
    // registered, so the session has to be up before the registry is read.
    if (!Start()) {
        return 1;
    }
    if (name.empty()) {
        for (const settings::Setting& setting : settings::All()) {
            char line[200];
            std::string allowed;
            for (const std::string& option : setting.values) {
                allowed += allowed.empty() ? option : "|" + option;
            }
            std::snprintf(line, sizeof(line), "%-16s %-12s %s", setting.name.c_str(),
                          setting.get().c_str(),
                          allowed.empty() ? setting.summary.c_str() : allowed.c_str());
            out::Line(line);
        }
        return 0;
    }
    const settings::Setting* setting = settings::Find(name);
    if (setting == nullptr) {
        out::Error("no setting called " + name);
        return 1;
    }
    if (value.empty()) {
        out::Line(setting->get());
        return 0;
    }
    // Settings live for the process. Persisting them would need a config file,
    // and inventing one that `rcli run` silently obeys is a bigger decision
    // than this command should make on its own.
    if (!setting->set(value)) {
        out::Error(value + " is not a valid " + name);
        return 1;
    }
    out::Line(name + " = " + setting->get());
    return 0;
}

int Where() {
    if (!Start()) {
        return 1;
    }
    out::Line(std::string(sdk::Session::Instance().home()));
    return 0;
}

}  // namespace

void RegisterEngines(CLI::App& app, Options& options) {
    auto* engines = app.add_subcommand("engines", "which backends came up, and what they serve");
    engines->callback([&options] { options.status = Engines(); });

    auto name = std::make_shared<std::string>();
    auto value = std::make_shared<std::string>();
    auto* config = app.add_subcommand("config", "read or change a setting for this run");
    config->add_option("setting", *name, "omit to list them all");
    config->add_option("value", *value, "omit to read the current one");
    config->callback([&options, name, value] { options.status = Config(*name, *value); });

    auto* where = app.add_subcommand("where", "where models and images are kept");
    where->callback([&options] { options.status = Where(); });
}

}  // namespace rcli::cli
