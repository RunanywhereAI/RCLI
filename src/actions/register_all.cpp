#include "actions/macos_actions.h"
#include "actions/notes_actions.h"
#include "actions/app_control_actions.h"
#include "actions/window_actions.h"
#include "actions/files_actions.h"
#include "actions/clipboard_actions.h"
#include "actions/system_actions.h"
#include "actions/media_actions.h"
#include "actions/web_actions.h"
#include "actions/navigation_actions.h"
#include "actions/communication_actions.h"
#include "actions/browser_actions.h"

namespace rcli {

void register_macos_actions(ActionRegistry& registry) {
    register_notes_actions(registry);
    register_app_control_actions(registry);
    register_window_actions(registry);
    register_files_actions(registry);
    register_clipboard_actions(registry);
    register_system_actions(registry);
    register_media_actions(registry);
    register_web_actions(registry);
    register_navigation_actions(registry);
    register_communication_actions(registry);
    register_browser_actions(registry);
}

} // namespace rcli
