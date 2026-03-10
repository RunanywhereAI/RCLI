#include "actions/files_actions.h"
#include "actions/action_helpers.h"
#include "actions/applescript_executor.h"

namespace rcli {

static ActionResult action_search_files(const std::string& args_json) {
    std::string query = json_get_string(args_json, "query");
    if (query.empty())
        return {false, "", "Query required", "{\"error\": \"missing query\"}"};

    auto r = run_shell("mdfind '" + escape_shell(query) + "' | head -20", 15000);
    if (r.success)
        return {true, r.output.empty() ? "No files found" : r.output, "",
                "{\"action\": \"search_files\", \"query\": \"" + query + "\"}"};
    return {false, "", r.error, "{\"error\": \"" + r.error + "\"}"};
}

void register_files_actions(ActionRegistry& registry) {
    registry.register_action(
        {"search_files", "Search for files using Spotlight",
         "{\"query\": \"search term\"}",
         false,
         "system",
         "Find files about project plan",
         "rcli action search_files '{\"query\": \"project plan\"}'"},
        action_search_files);

}

} // namespace rcli
