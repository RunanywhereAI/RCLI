#include "actions/messages_actions.h"
#include "actions/action_helpers.h"
#include "actions/applescript_executor.h"

namespace rcli {

static std::string resolve_contact_for_message(const std::string& input) {
    if (input.find('@') != std::string::npos) return input;
    bool has_digit = false;
    for (char c : input)
        if (std::isdigit(static_cast<unsigned char>(c))) has_digit = true;
    if (has_digit) return input;

    std::string script =
        "tell application \"Contacts\"\n"
        "  set matchList to (every person whose name contains \"" + escape_applescript(input) + "\")\n"
        "  if (count of matchList) > 0 then\n"
        "    set p to item 1 of matchList\n"
        "    if (count of phones of p) > 0 then\n"
        "      return value of phone 1 of p\n"
        "    else if (count of emails of p) > 0 then\n"
        "      return value of email 1 of p\n"
        "    else\n"
        "      return name of p\n"
        "    end if\n"
        "  end if\n"
        "  return \"\"\n"
        "end tell";
    auto r = run_applescript(script, 5000);
    if (r.success && !r.output.empty()) {
        std::string resolved = trim_output(r.output);
        if (!resolved.empty()) return resolved;
    }
    return input;
}

static ActionResult action_send_message(const std::string& args_json) {
    std::string to   = json_get_string(args_json, "to");
    std::string text = json_get_string(args_json, "text");

    if (to.empty() || text.empty())
        return {false, "", "Recipient and text required", "{\"error\": \"missing to or text\"}"};

    std::string resolved = resolve_contact_for_message(to);

    auto check = run_shell("which imsg 2>/dev/null");
    if (check.success) {
        auto r = run_shell("imsg send --to '" + escape_applescript(resolved) +
                           "' --text '" + escape_applescript(text) + "'");
        if (r.success)
            return {true, "Message sent to " + to, "",
                    "{\"action\": \"send_message\", \"to\": \"" + to +
                    "\", \"resolved\": \"" + resolved + "\", \"status\": \"sent\"}"};
    }

    std::string script =
        "tell application \"Messages\"\n"
        "  set targetService to 1st account whose service type = iMessage\n"
        "  set targetBuddy to participant \"" + escape_applescript(resolved) + "\" of targetService\n"
        "  send \"" + escape_applescript(text) + "\" to targetBuddy\n"
        "end tell";

    auto r = run_applescript(script);
    if (r.success)
        return {true, "Message sent to " + to, "",
                "{\"action\": \"send_message\", \"to\": \"" + to +
                "\", \"resolved\": \"" + resolved + "\", \"status\": \"sent\"}"};
    return {false, "", r.error, "{\"error\": \"" + r.error + "\"}"};
}

void register_messages_actions(ActionRegistry& registry) {
    registry.register_action(
        {"send_message", "Send an iMessage or SMS via Messages.app",
         "{\"to\": \"contact name, phone number, or email\", \"text\": \"message content\"}",
         {"send message", "text", "imessage", "send a text", "message to", "tell them",
          "saying", "send text", "message", "sms"},
         "communication",
         "Send a message to John saying I'll be 10 minutes late",
         "rcli action send_message '{\"to\": \"+1234567890\", \"text\": \"Running late!\"}'"},
        action_send_message);

}

} // namespace rcli
