#include "actions/browser_actions.h"
#include "actions/action_helpers.h"
#include "actions/applescript_executor.h"

namespace rcli {

static ActionResult action_get_browser_url(const std::string& args_json) {
    (void)args_json;

    // Try Safari first
    auto r = run_applescript(
        "tell application \"Safari\"\n"
        "  if (count of windows) > 0 then\n"
        "    return URL of current tab of front window\n"
        "  end if\n"
        "end tell");
    if (r.success && !r.output.empty()) {
        std::string url = trim_output(r.output);
        return {true, "Current URL: " + url, "",
                "{\"action\": \"get_browser_url\", \"url\": \"" + escape_applescript(url) + "\", \"browser\": \"Safari\"}"};
    }

    // Try Chrome
    auto r2 = run_applescript(
        "tell application \"Google Chrome\"\n"
        "  if (count of windows) > 0 then\n"
        "    return URL of active tab of front window\n"
        "  end if\n"
        "end tell");
    if (r2.success && !r2.output.empty()) {
        std::string url = trim_output(r2.output);
        return {true, "Current URL: " + url, "",
                "{\"action\": \"get_browser_url\", \"url\": \"" + escape_applescript(url) + "\", \"browser\": \"Chrome\"}"};
    }

    // Try Arc
    auto r3 = run_applescript(
        "tell application \"Arc\"\n"
        "  if (count of windows) > 0 then\n"
        "    return URL of active tab of front window\n"
        "  end if\n"
        "end tell");
    if (r3.success && !r3.output.empty()) {
        std::string url = trim_output(r3.output);
        return {true, "Current URL: " + url, "",
                "{\"action\": \"get_browser_url\", \"url\": \"" + escape_applescript(url) + "\", \"browser\": \"Arc\"}"};
    }

    return {false, "", "No browser window found", "{\"error\": \"no browser\"}"};
}

static ActionResult action_get_browser_tabs(const std::string& args_json) {
    (void)args_json;

    // Try Safari
    auto r = run_applescript(
        "tell application \"Safari\"\n"
        "  if (count of windows) > 0 then\n"
        "    set output to \"\"\n"
        "    repeat with t in tabs of front window\n"
        "      set output to output & name of t & \" - \" & URL of t & return\n"
        "    end repeat\n"
        "    return output\n"
        "  end if\n"
        "end tell");
    if (r.success && !r.output.empty())
        return {true, r.output, "", "{\"action\": \"get_browser_tabs\", \"browser\": \"Safari\"}"};

    // Try Chrome
    auto r2 = run_applescript(
        "tell application \"Google Chrome\"\n"
        "  if (count of windows) > 0 then\n"
        "    set output to \"\"\n"
        "    repeat with t in tabs of front window\n"
        "      set output to output & title of t & \" - \" & URL of t & return\n"
        "    end repeat\n"
        "    return output\n"
        "  end if\n"
        "end tell");
    if (r2.success && !r2.output.empty())
        return {true, r2.output, "", "{\"action\": \"get_browser_tabs\", \"browser\": \"Chrome\"}"};

    return {false, "", "No browser window found", "{\"error\": \"no browser\"}"};
}

void register_browser_actions(ActionRegistry& registry) {
    registry.register_action(
        {"get_browser_url", "Get the URL of the active browser tab (Safari/Chrome/Arc)",
         "{}",
         true,
         "web",
         "What page am I looking at?",
         "rcli action get_browser_url '{}'"},
        action_get_browser_url);

    registry.register_action(
        {"get_browser_tabs", "List all open browser tabs in the front window",
         "{}",
         true,
         "web",
         "Show my open tabs",
         "rcli action get_browser_tabs '{}'"},
        action_get_browser_tabs);
}

} // namespace rcli
