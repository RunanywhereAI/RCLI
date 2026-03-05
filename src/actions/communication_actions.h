#pragma once
#include "actions/action_registry.h"
#include <string>

namespace rcli {
void register_communication_actions(ActionRegistry& registry);

// Shared: resolve a contact name to phone/email via Contacts.app
std::string resolve_contact(const std::string& input);
} // namespace rcli
