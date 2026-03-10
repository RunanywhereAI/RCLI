#include "actions/messages_actions.h"
#include "actions/action_helpers.h"
#include "actions/applescript_executor.h"
#include "actions/communication_actions.h"

namespace rcli {

static ActionResult action_send_message(const std::string& args_json) {
    std::string to   = json_get_string(args_json, "to");
    std::string text = json_get_string(args_json, "text");

    if (to.empty() || text.empty())
        return {false, "", "Recipient and text required", "{\"error\": \"missing to or text\"}"};

    std::string resolved = resolve_contact(to);

    auto check = run_shell("which imsg 2>/dev/null");
    if (check.success) {
        auto r = run_shell("imsg send --to '" + escape_shell(resolved) +
                           "' --text '" + escape_shell(text) + "'");
        if (r.success)
            return {true, "Message sent to " + to, "",
                    "{\"action\": \"send_message\", \"to\": \"" + to +
                    "\", \"resolved\": \"" + resolved + "\", \"status\": \"sent\"}"};
    }

    // Modern macOS (Ventura+): use the buddy-based approach which is more reliable
    std::string script =
        "tell application \"Messages\"\n"
        "  set targetBuddy to a reference to buddy \"" + escape_applescript(resolved) + "\" of service 1\n"
        "  send \"" + escape_applescript(text) + "\" to targetBuddy\n"
        "end tell";

    auto r = run_applescript(script, 15000);
    if (r.success)
        return {true, "Message sent to " + to, "",
                "{\"action\": \"send_message\", \"to\": \"" + escape_applescript(to) +
                "\", \"resolved\": \"" + escape_applescript(resolved) + "\", \"status\": \"sent\"}"};

    // Fallback: older account/participant approach
    std::string fallback_script =
        "tell application \"Messages\"\n"
        "  set targetService to 1st account whose service type = iMessage\n"
        "  set targetBuddy to participant \"" + escape_applescript(resolved) + "\" of targetService\n"
        "  send \"" + escape_applescript(text) + "\" to targetBuddy\n"
        "end tell";

    auto r2 = run_applescript(fallback_script, 15000);
    if (r2.success)
        return {true, "Message sent to " + to, "",
                "{\"action\": \"send_message\", \"to\": \"" + escape_applescript(to) +
                "\", \"resolved\": \"" + escape_applescript(resolved) + "\", \"status\": \"sent\"}"};
    return {false, "", "Failed to send message. Messages may require manual permission. Error: " + r2.error,
            "{\"error\": \"" + escape_applescript(r2.error) + "\"}"};
}

void register_messages_actions(ActionRegistry& registry) {
    registry.register_action(
        {"send_message", "Send an iMessage or SMS via Messages.app",
         "{\"to\": \"contact name, phone number, or email\", \"text\": \"message content\"}",
         true,
         "communication",
         "Send a message to John saying I'll be 10 minutes late",
         "rcli action send_message '{\"to\": \"+1234567890\", \"text\": \"Running late!\"}'"},
        action_send_message);

}

} // namespace rcli
