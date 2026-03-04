#include "actions/clipboard_actions.h"
#include "actions/action_helpers.h"
#include "actions/applescript_executor.h"

namespace rcli {

static ActionResult action_clipboard_read(const std::string& args_json) {
    (void)args_json;
    auto r = run_shell("pbpaste");
    if (r.success) return {true, r.output, "", "{\"action\": \"clipboard_read\"}"};
    return {false, "", r.error, "{\"error\": \"" + r.error + "\"}"};
}

static ActionResult action_clipboard_write(const std::string& args_json) {
    std::string text = json_get_string(args_json, "text");
    if (text.empty()) return {false, "", "Text required", "{\"error\": \"missing text\"}"};
    auto r = run_shell("echo '" + escape_applescript(text) + "' | pbcopy");
    if (r.success) return {true, "Copied to clipboard", "", "{\"action\": \"clipboard_write\"}"};
    return {false, "", r.error, "{\"error\": \"" + r.error + "\"}"};
}

void register_clipboard_actions(ActionRegistry& registry) {
    registry.register_action(
        {"clipboard_read", "Read text from clipboard",
         "{}",
         {"read clipboard", "what's copied", "paste", "what's on my clipboard",
          "show clipboard", "clipboard contents", "in my clipboard", "on clipboard",
          "my clipboard", "the clipboard"},
         "system",
         "What's on my clipboard?",
         "rcli action clipboard_read '{}'"},
        action_clipboard_read);

    registry.register_action(
        {"clipboard_write", "Copy text to clipboard",
         "{\"text\": \"text to copy\"}",
         {"copy", "copy to clipboard", "copy that", "to clipboard", "clipboard"},
         "system",
         "Copy 'Hello World' to my clipboard",
         "rcli action clipboard_write '{\"text\": \"Hello World\"}'"},
        action_clipboard_write);
}

} // namespace rcli
