#ifndef RCLI_SDK_INSTALL_H
#define RCLI_SDK_INSTALL_H

#include <functional>
#include <string>

#include "catalog/catalog.h"

namespace rcli::sdk {

struct Progress {
    std::string stage;
    int percent = 0;
};

/// Register a catalog entry with the SDK registry so its id resolves.
///
/// Registration is cheap and idempotent, and it is what turns "an id we printed
/// on screen" into something the lifecycle can fetch.
bool Register(const catalog::Model& model, std::string* error);

/// Download the model if it is not already on disk.
///
/// This is one call into the SDK, not a plan/start/poll loop of our own:
/// `rac_model_lifecycle_load_proto` with validate_availability drives the
/// download orchestrator, resolves artifacts and loads the engine, and it is
/// the same path every other RunAnywhere app takes.
///
/// Blocking, so callers run it off the render thread. `on_progress` is invoked
/// from the download's own thread.
bool Install(const catalog::Model& model, std::function<void(Progress)> on_progress,
             std::string* error);

}  // namespace rcli::sdk

#endif  // RCLI_SDK_INSTALL_H
