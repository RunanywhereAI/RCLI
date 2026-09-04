/**
 * @file engine_options.h
 * @brief Shared parsing for wally engine/framework hints.
 */

#ifndef WALLY_COMMANDS_ENGINE_OPTIONS_H
#define WALLY_COMMANDS_ENGINE_OPTIONS_H

#include <string>

#include "model_types.pb.h"
#include "catalog/model_ref.h"

namespace wally::commands {

struct EngineHintResolution {
    runanywhere::v1::InferenceFramework framework =
        runanywhere::v1::INFERENCE_FRAMEWORK_UNSPECIFIED;
    model_ref::ResolveOptions resolve_options;
};

bool parse_engine_hint(const std::string& engine,
                       runanywhere::v1::InferenceFramework* out_framework,
                       std::string* error);

bool resolve_engine_hint(const std::string& engine, EngineHintResolution* out_resolution,
                         std::string* error);

}  // namespace wally::commands

#endif  // WALLY_COMMANDS_ENGINE_OPTIONS_H
