#include "actions/communication_actions.h"
#include "actions/action_helpers.h"
#include "actions/applescript_executor.h"

namespace rcli {

std::string resolve_contact(const std::string& input) {
    if (input.find('@') != std::string::npos) return input;
    bool has_digit = false;
    for (char c : input) if (std::isdigit(static_cast<unsigned char>(c))) has_digit = true;
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

static ActionResult action_facetime_call(const std::string& args_json) {
    std::string contact = json_get_string(args_json, "contact");
    if (contact.empty()) return {false, "", "Contact required", "{\"error\": \"missing contact\"}"};

    std::string resolved = resolve_contact(contact);
    std::string url = "facetime://" + url_encode(resolved);
    auto r = run_shell("open '" + escape_shell(url) + "'");
    if (r.success) return {true, "Starting FaceTime video call with " + contact, "",
        "{\"action\": \"facetime_call\", \"contact\": \"" + escape_applescript(contact) +
        "\", \"resolved\": \"" + escape_applescript(resolved) + "\"}"};
    return {false, "", r.error, "{\"error\": \"" + r.error + "\"}"};
}

static ActionResult action_facetime_audio(const std::string& args_json) {
    std::string contact = json_get_string(args_json, "contact");
    if (contact.empty()) return {false, "", "Contact required", "{\"error\": \"missing contact\"}"};

    std::string resolved = resolve_contact(contact);
    std::string url = "facetime-audio://" + url_encode(resolved);
    auto r = run_shell("open '" + escape_shell(url) + "'");
    if (r.success) return {true, "Starting FaceTime audio call with " + contact, "",
        "{\"action\": \"facetime_audio\", \"contact\": \"" + escape_applescript(contact) +
        "\", \"resolved\": \"" + escape_applescript(resolved) + "\"}"};
    return {false, "", r.error, "{\"error\": \"" + r.error + "\"}"};
}

static ActionResult action_run_shortcut(const std::string& args_json) {
    std::string name  = json_get_string(args_json, "name");
    std::string input = json_get_string(args_json, "input");
    if (name.empty()) return {false, "", "Shortcut name required", "{\"error\": \"missing name\"}"};
    std::string url = "shortcuts://run-shortcut?name=" + url_encode(name);
    if (!input.empty()) url += "&input=text&text=" + url_encode(input);
    auto r = run_shell("open '" + escape_shell(url) + "'");
    if (r.success) return {true, "Running shortcut: " + name, "",
        "{\"action\": \"run_shortcut\", \"name\": \"" + escape_applescript(name) + "\"}"};
    return {false, "", r.error, "{\"error\": \"" + r.error + "\"}"};
}

void register_communication_actions(ActionRegistry& registry) {
    registry.register_action(
        {"facetime_call", "Start a FaceTime video call",
         "{\"contact\": \"name, phone number, or email\"}",
         true,
         "communication",
         "FaceTime John",
         "rcli action facetime_call '{\"contact\": \"john@example.com\"}'"},
        action_facetime_call);

    registry.register_action(
        {"facetime_audio", "Start a FaceTime audio call (phone call)",
         "{\"contact\": \"name, phone number, or email\"}",
         true,
         "communication",
         "Call Mom",
         "rcli action facetime_audio '{\"contact\": \"+1234567890\"}'"},
        action_facetime_audio);

    registry.register_action(
        {"run_shortcut", "Run an Apple Shortcut by name",
         "{\"name\": \"shortcut name\", \"input\": \"optional input text\"}",
         true,
         "productivity",
         "Run my Morning Routine shortcut",
         "rcli action run_shortcut '{\"name\": \"Morning Routine\"}'"},
        action_run_shortcut);
}

} // namespace rcli
