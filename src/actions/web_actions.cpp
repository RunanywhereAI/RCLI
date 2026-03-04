#include "actions/web_actions.h"
#include "actions/action_helpers.h"
#include "actions/applescript_executor.h"

namespace rcli {

static ActionResult action_search_web(const std::string& args_json) {
    std::string query  = json_get_string(args_json, "query");
    std::string engine = json_get_string(args_json, "engine");
    if (query.empty()) return {false, "", "Search query required", "{\"error\": \"missing query\"}"};

    std::string url;
    std::string lower_engine = engine;
    for (auto& c : lower_engine) c = std::tolower(static_cast<unsigned char>(c));

    if (lower_engine == "duckduckgo" || lower_engine == "ddg")
        url = "https://duckduckgo.com/?q=" + url_encode(query);
    else if (lower_engine == "bing")
        url = "https://www.bing.com/search?q=" + url_encode(query);
    else
        url = "https://www.google.com/search?q=" + url_encode(query);

    auto r = run_shell("open '" + url + "'");
    if (r.success) return {true, "Searching for: " + query, "",
        "{\"action\": \"search_web\", \"query\": \"" + escape_applescript(query) + "\"}"};
    return {false, "", r.error, "{\"error\": \"" + r.error + "\"}"};
}

static ActionResult action_search_youtube(const std::string& args_json) {
    std::string query = json_get_string(args_json, "query");
    if (query.empty()) return {false, "", "Search query required", "{\"error\": \"missing query\"}"};

    std::string url = "https://www.youtube.com/results?search_query=" + url_encode(query);
    auto r = run_shell("open '" + url + "'");
    if (r.success) return {true, "Searching YouTube for: " + query, "",
        "{\"action\": \"search_youtube\", \"query\": \"" + escape_applescript(query) + "\"}"};
    return {false, "", r.error, "{\"error\": \"" + r.error + "\"}"};
}

void register_web_actions(ActionRegistry& registry) {
    registry.register_action(
        {"search_web", "Search the web using Google (or DuckDuckGo/Bing)",
         "{\"query\": \"search query\", \"engine\": \"optional: google|duckduckgo|bing\"}",
         {"search", "google", "search the web", "search for", "look up", "search google", "web search",
          "google search", "find online", "search online", "look it up", "search web"},
         "web",
         "Google how to make sourdough bread",
         "rcli action search_web '{\"query\": \"how to make sourdough bread\"}'"},
        action_search_web);

    registry.register_action(
        {"search_youtube", "Search YouTube for videos",
         "{\"query\": \"search query\"}",
         {"youtube", "search youtube", "watch video", "find video", "play video",
          "on youtube", "youtube video", "open youtube", "video of", "find on youtube"},
         "web",
         "Search YouTube for guitar tutorials",
         "rcli action search_youtube '{\"query\": \"guitar tutorials\"}'"},
        action_search_youtube);
}

} // namespace rcli
