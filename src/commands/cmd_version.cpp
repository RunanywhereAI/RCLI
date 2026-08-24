/**
 * @file cmd_version.cpp
 * @brief `rcli version` — CLI + commons versions. No bootstrap needed.
 */

#include "commands/commands.h"

#include <cstring>
#include <string>

#include "rac/core/rac_core.h"
#include "runanywhere/proto/schema_lock.h"

#include "io/output.h"

#ifndef RCLI_VERSION
#define RCLI_VERSION "0.0.0-dev"
#endif

#ifndef RCLI_PINNED_SDK_VERSION
#define RCLI_PINNED_SDK_VERSION ""
#endif

namespace rcli::commands {

void register_version(CLI::App& app, GlobalOptions& options) {
    CLI::App* cmd = app.add_subcommand("version", "Show rcli and commons versions");
    cmd->callback([&options]() {
        const rac_version_t commons = rac_get_version();
        const std::string commons_version =
            commons.string ? commons.string : "unknown";
        const bool pin_ok = RCLI_PINNED_SDK_VERSION[0] == '\0' ||
                            (commons.string &&
                             std::strcmp(commons.string, RCLI_PINNED_SDK_VERSION) == 0);
        if (options.json) {
            out::JsonWriter json;
            json.begin_object()
                .field("rcli", RCLI_VERSION)
                .field("commons", commons_version)
                .field("pinned_sdk", RCLI_PINNED_SDK_VERSION)
                .field("idl_version", RUNANYWHERE_IDL_VERSION)
                .field("idl_schema_sha256", RUNANYWHERE_IDL_SCHEMA_SHA256)
                .field("idl_protoc", RUNANYWHERE_IDL_PROTOC_VERSION)
                .field("pin_ok", pin_ok)
                .end_object();
            out::result_line(json.str());
        } else {
            out::result_line(std::string("rcli ") + RCLI_VERSION + " (commons " +
                             commons_version + ")");
            out::result_line(std::string("idl ") + RUNANYWHERE_IDL_VERSION +
                             " sha256 " + RUNANYWHERE_IDL_SCHEMA_SHA256);
        }
        if (!pin_ok) {
            out::error_line(std::string("commons ") + commons_version +
                            " does not match pin " + RCLI_PINNED_SDK_VERSION);
            throw CLI::RuntimeError(1);
        }
    });
}

}  // namespace rcli::commands
