#include "actions/navigation_actions.h"
#include "actions/action_helpers.h"
#include "actions/applescript_executor.h"

namespace rcli {

static ActionResult action_open_maps(const std::string& args_json) {
    std::string query = json_get_string(args_json, "query");
    if (query.empty()) return {false, "", "Search query required", "{\"error\": \"missing query\"}"};

    std::string url = "maps://?q=" + url_encode(query);
    auto r = run_shell("open '" + escape_shell(url) + "'");
    if (r.success) return {true, "Opened Maps for: " + query, "",
        "{\"action\": \"open_maps\", \"query\": \"" + escape_applescript(query) + "\"}"};
    return {false, "", r.error, "{\"error\": \"" + r.error + "\"}"};
}

void register_navigation_actions(ActionRegistry& registry) {
    registry.register_action(
        {"open_maps", "Search for a place or address in Apple Maps",
         "{\"query\": \"place or address to search\"}",
         false,
         "navigation",
         "Show me coffee shops nearby",
         "rcli action open_maps '{\"query\": \"coffee shops\"}'"},
        action_open_maps);

}

} // namespace rcli
