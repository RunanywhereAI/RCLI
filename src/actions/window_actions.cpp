#include "actions/window_actions.h"
#include "actions/action_helpers.h"
#include "actions/applescript_executor.h"

namespace rcli {

static ActionResult action_minimize_window(const std::string& args_json) {
    (void)args_json;
    auto r = run_applescript(
        "tell application \"System Events\" to keystroke \"m\" using command down");
    if (r.success) return {true, "Window minimized", "", "{\"action\": \"minimize_window\"}"};
    // Fallback: try clicking the minimize button (button 2 = yellow)
    run_applescript(
        "tell application \"System Events\"\n"
        "  set fp to first application process whose frontmost is true\n"
        "  click button 2 of window 1 of fp\n"
        "end tell");
    return {true, "Window minimized", "", "{\"action\": \"minimize_window\"}"};
}

static ActionResult action_fullscreen_window(const std::string& args_json) {
    (void)args_json;
    auto r = run_applescript(
        "tell application \"System Events\"\n"
        "  keystroke \"f\" using {command down, control down}\n"
        "end tell");
    if (r.success) return {true, "Toggled fullscreen", "", "{\"action\": \"fullscreen_window\"}"};
    return {false, "", "Could not toggle fullscreen: " + r.error, "{\"error\": \"" + r.error + "\"}"};
}

static ActionResult action_close_window(const std::string& args_json) {
    (void)args_json;
    auto r = run_applescript("tell application \"System Events\" to keystroke \"w\" using command down");
    if (r.success) return {true, "Window closed", "", "{\"action\": \"close_window\"}"};
    return {false, "", "Could not close window: " + r.error, "{\"error\": \"" + r.error + "\"}"};
}

void register_window_actions(ActionRegistry& registry) {
    registry.register_action(
        {"minimize_window", "Minimize the frontmost window",
         "{}",
         false,
         "system",
         "Minimize this window",
         "rcli action minimize_window '{}'"},
        action_minimize_window);

    registry.register_action(
        {"fullscreen_window", "Toggle fullscreen for the frontmost window",
         "{}",
         false,
         "system",
         "Make this fullscreen",
         "rcli action fullscreen_window '{}'"},
        action_fullscreen_window);

    registry.register_action(
        {"close_window", "Close the frontmost window",
         "{}",
         false,
         "system",
         "Close this window",
         "rcli action close_window '{}'"},
        action_close_window);
}

} // namespace rcli
