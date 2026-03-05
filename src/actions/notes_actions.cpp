#include "actions/notes_actions.h"
#include "actions/action_helpers.h"
#include "actions/applescript_executor.h"

namespace rcli {

static ActionResult action_create_note(const std::string& args_json) {
    std::string title = json_get_string(args_json, "title");
    std::string body  = json_get_string(args_json, "body");
    std::string folder = json_get_string(args_json, "folder");

    if (title.empty() && body.empty())
        return {false, "", "Title or body required", "{\"error\": \"missing content\"}"};

    auto check = run_shell("which memo 2>/dev/null");
    if (check.success) {
        std::string cmd = "printf '%s' '" + escape_shell(body.empty() ? title : body)
                         + "' | memo notes -a '" + escape_shell(title) + "'";
        auto r = run_shell(cmd);
        if (r.success)
            return {true, "Created note: " + title, "",
                    "{\"action\": \"create_note\", \"title\": \"" + escape_applescript(title) + "\", \"status\": \"created\"}"};
    }

    std::string script =
        "tell application \"Notes\"\n"
        "  tell folder \"" + escape_applescript(folder.empty() ? "Notes" : folder) + "\"\n"
        "    make new note with properties {name:\"" + escape_applescript(title) +
        "\", body:\"" + escape_applescript(body.empty() ? title : body) + "\"}\n"
        "  end tell\n"
        "end tell";

    auto r = run_applescript(script);
    if (r.success)
        return {true, "Created note: " + title, "",
                "{\"action\": \"create_note\", \"title\": \"" + title + "\", \"status\": \"created\"}"};
    return {false, "", "Failed to create note: " + r.error, "{\"error\": \"" + r.error + "\"}"};
}

void register_notes_actions(ActionRegistry& registry) {
    registry.register_action(
        {"create_note", "Create a new note in Apple Notes",
         "{\"title\": \"note title\"}",
         true,
         "productivity",
         "Create a note called Meeting Notes",
         "rcli action create_note '{\"title\": \"Meeting Notes\"}'"},
        action_create_note);
}

} // namespace rcli
