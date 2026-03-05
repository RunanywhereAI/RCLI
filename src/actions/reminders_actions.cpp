#include "actions/reminders_actions.h"
#include "actions/action_helpers.h"
#include "actions/applescript_executor.h"

namespace rcli {

static ActionResult action_create_reminder(const std::string& args_json) {
    std::string title = json_get_string(args_json, "title");
    std::string due   = json_get_string(args_json, "due");
    std::string list  = json_get_string(args_json, "list");

    if (title.empty())
        return {false, "", "Title required", "{\"error\": \"missing title\"}"};

    std::string script =
        "tell application \"Reminders\"\n"
        "  tell list \"" + escape_applescript(list.empty() ? "Reminders" : list) + "\"\n"
        "    set r to make new reminder with properties {name:\"" + escape_applescript(title) + "\"";

    if (!due.empty())
        script += ", due date:date \"" + escape_applescript(due) + "\"";

    script += "}\n  end tell\nend tell";

    auto r = run_applescript(script, 20000);
    if (r.success)
        return {true, "Reminder created: " + title, "",
                "{\"action\": \"create_reminder\", \"title\": \"" + title + "\", \"status\": \"created\"}"};
    return {false, "", r.error, "{\"error\": \"" + r.error + "\"}"};
}

void register_reminders_actions(ActionRegistry& registry) {
    registry.register_action(
        {"create_reminder", "Create a reminder in Apple Reminders",
         "{\"title\": \"reminder text\"}",
         true,
         "productivity",
         "Remind me to buy groceries",
         "rcli action create_reminder '{\"title\": \"Buy groceries\"}'"},
        action_create_reminder);
}

} // namespace rcli
