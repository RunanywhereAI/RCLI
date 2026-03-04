#include "actions/app_control_actions.h"
#include "actions/action_helpers.h"
#include "actions/applescript_executor.h"

namespace rcli {

static ActionResult action_open_app(const std::string& args_json) {
    std::string app = json_get_string(args_json, "app");
    if (app.empty()) return {false, "", "App name required", "{\"error\": \"missing app\"}"};
    auto r = run_shell("open -a '" + escape_applescript(app) + "'");
    if (r.success)
        return {true, "Opened " + app, "", "{\"action\": \"open_app\", \"app\": \"" + app + "\", \"status\": \"opened\"}"};
    return {false, "", r.error, "{\"error\": \"" + r.error + "\"}"};
}

static ActionResult action_open_url(const std::string& args_json) {
    std::string url = json_get_string(args_json, "url");
    if (url.empty()) return {false, "", "URL required", "{\"error\": \"missing url\"}"};
    if (url.find("://") == std::string::npos && url.find('.') != std::string::npos)
        url = "https://" + url;
    auto r = run_shell("open '" + escape_applescript(url) + "'");
    if (r.success)
        return {true, "Opened " + url, "", "{\"action\": \"open_url\", \"url\": \"" + url + "\", \"status\": \"opened\"}"};
    return {false, "", r.error, "{\"error\": \"" + r.error + "\"}"};
}

static ActionResult action_list_apps(const std::string& args_json) {
    (void)args_json;
    auto r = run_applescript(
        "tell application \"System Events\"\n"
        "  set appNames to name of every application process whose visible is true\n"
        "  set AppleScript's text item delimiters to \", \"\n"
        "  set output to appNames as text\n"
        "  return output\n"
        "end tell");
    if (r.success) return {true, trim_output(r.output), "", "{\"action\": \"list_apps\"}"};
    return {false, "", r.error, "{\"error\": \"" + r.error + "\"}"};
}

static ActionResult action_quit_app(const std::string& args_json) {
    std::string app = json_get_string(args_json, "app");
    if (app.empty()) return {false, "", "App name required", "{\"error\": \"missing app\"}"};
    auto r = run_applescript("tell application \"" + escape_applescript(app) + "\" to quit");
    if (r.success) return {true, "Quit " + app, "", "{\"action\": \"quit_app\", \"app\": \"" + app + "\"}"};
    return {false, "", r.error, "{\"error\": \"" + r.error + "\"}"};
}

static ActionResult action_switch_app(const std::string& args_json) {
    std::string app = json_get_string(args_json, "app");
    if (app.empty()) return {false, "", "App name required", "{\"error\": \"missing app\"}"};
    auto r = run_applescript("tell application \"" + escape_applescript(app) + "\" to activate");
    if (r.success) return {true, "Switched to " + app, "", "{\"action\": \"switch_app\", \"app\": \"" + app + "\"}"};
    return {false, "", r.error, "{\"error\": \"" + r.error + "\"}"};
}

static ActionResult action_get_frontmost_app(const std::string& args_json) {
    (void)args_json;
    auto r = run_applescript(
        "tell application \"System Events\" to get name of first application process whose frontmost is true");
    if (r.success) {
        std::string name = trim_output(r.output);
        return {true, "Frontmost app: " + name, "", "{\"action\": \"get_frontmost_app\", \"app\": \"" + name + "\"}"};
    }
    return {false, "", r.error, "{\"error\": \"" + r.error + "\"}"};
}

void register_app_control_actions(ActionRegistry& registry) {
    registry.register_action(
        {"open_app", "Launch a macOS application",
         "{\"app\": \"application name\"}",
         {"open app", "launch app", "start app", "open", "launch", "start", "run app"},
         "system",
         "Open Safari",
         "rcli action open_app '{\"app\": \"Safari\"}'"},
        action_open_app);

    registry.register_action(
        {"open_url", "Open a URL in the default browser",
         "{\"url\": \"https://...\"}",
         {"open url", "go to", "browse", "visit", "navigate to"},
         "system",
         "Go to github.com",
         "rcli action open_url '{\"url\": \"https://github.com\"}'"},
        action_open_url);

    registry.register_action(
        {"list_apps", "List currently running applications",
         "{}",
         {"running apps", "what apps", "active apps", "what's open", "show apps", "list apps",
          "which apps", "apps are running", "what's running", "what is running"},
         "system",
         "What apps are running?",
         "rcli action list_apps '{}'"},
        action_list_apps);

    registry.register_action(
        {"quit_app", "Quit a macOS application",
         "{\"app\": \"application name\"}",
         {"quit", "close app", "kill app", "exit app", "force quit", "quit app", "close"},
         "system",
         "Quit Safari",
         "rcli action quit_app '{\"app\": \"Safari\"}'"},
        action_quit_app);

    registry.register_action(
        {"switch_app", "Switch to (activate) a macOS application",
         "{\"app\": \"application name\"}",
         {"switch to", "go to app", "bring up", "activate", "focus on"},
         "system",
         "Switch to Slack",
         "rcli action switch_app '{\"app\": \"Slack\"}'"},
        action_switch_app);

    registry.register_action(
        {"get_frontmost_app", "Get the name of the currently active application",
         "{}",
         {"what app", "which app", "current app", "frontmost app", "active app", "what am i using",
          "what application", "using right now"},
         "system",
         "What app am I using?",
         "rcli action get_frontmost_app '{}'"},
        action_get_frontmost_app);
}

} // namespace rcli
