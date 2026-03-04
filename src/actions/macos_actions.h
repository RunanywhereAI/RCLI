#pragma once

#include "actions/action_registry.h"

namespace rcli {

// Register all built-in macOS actions into the registry.
// Delegates to per-domain register_*_actions() functions (see register_all.cpp).
void register_macos_actions(ActionRegistry& registry);

} // namespace rcli
