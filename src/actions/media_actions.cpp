#include "actions/media_actions.h"
#include "actions/action_helpers.h"
#include "actions/applescript_executor.h"

namespace rcli {

static std::string friendly_media_result(const std::string& action_name, const std::string& app) {
    if (action_name == "play_pause_music") return "Toggled play pause on " + app;
    if (action_name == "next_track") return "Skipped to next track on " + app;
    if (action_name == "previous_track") return "Went back to previous track on " + app;
    return action_name + " done on " + app;
}

static ActionResult media_control(const std::string& command, const std::string& action_name) {
    auto r = run_applescript("tell application \"Music\" to " + command);
    if (r.success) return {true, friendly_media_result(action_name, "Music"), "", "{\"action\": \"" + action_name + "\", \"app\": \"Music\"}"};
    auto r2 = run_applescript("tell application \"Spotify\" to " + command);
    if (r2.success) return {true, friendly_media_result(action_name, "Spotify"), "", "{\"action\": \"" + action_name + "\", \"app\": \"Spotify\"}"};
    return {false, "", "No music app (Music or Spotify) is running", "{\"error\": \"no music app\"}"};
}

static ActionResult action_play_pause_music(const std::string& args_json) {
    (void)args_json;
    return media_control("playpause", "play_pause_music");
}

static ActionResult action_next_track(const std::string& args_json) {
    (void)args_json;
    return media_control("next track", "next_track");
}

static ActionResult action_previous_track(const std::string& args_json) {
    (void)args_json;
    return media_control("previous track", "previous_track");
}

static ActionResult action_get_now_playing(const std::string& args_json) {
    (void)args_json;
    auto r = run_applescript(
        "tell application \"Music\"\n"
        "  if player state is playing then\n"
        "    set t to name of current track\n"
        "    set a to artist of current track\n"
        "    return t & \" by \" & a\n"
        "  else\n"
        "    return \"Nothing playing\"\n"
        "  end if\n"
        "end tell");
    if (r.success && r.output.find("Nothing") == std::string::npos) {
        std::string info = trim_output(r.output);
        return {true, "Now playing: " + info, "", "{\"action\": \"get_now_playing\", \"track\": \"" + escape_applescript(info) + "\"}"};
    }
    auto r2 = run_applescript(
        "tell application \"Spotify\"\n"
        "  if player state is playing then\n"
        "    set t to name of current track\n"
        "    set a to artist of current track\n"
        "    return t & \" by \" & a\n"
        "  else\n"
        "    return \"Nothing playing\"\n"
        "  end if\n"
        "end tell");
    if (r2.success && r2.output.find("Nothing") == std::string::npos) {
        std::string info = trim_output(r2.output);
        return {true, "Now playing: " + info, "", "{\"action\": \"get_now_playing\", \"track\": \"" + escape_applescript(info) + "\"}"};
    }
    return {true, "Nothing playing right now", "", "{\"action\": \"get_now_playing\", \"track\": \"none\"}"};
}

static ActionResult action_play_on_spotify(const std::string& args_json) {
    std::string query = json_get_string(args_json, "query");
    std::string type  = json_get_string(args_json, "type");
    if (query.empty()) return {false, "", "Song or artist name required", "{\"error\": \"missing query\"}"};

    auto check = run_shell("osascript -e 'id of application \"Spotify\"' 2>/dev/null");
    if (!check.success || check.output.empty())
        return {false, "", "Spotify is not installed", "{\"error\": \"spotify not found\"}"};

    std::string encoded_query;
    for (char c : query) {
        if (c == ' ') encoded_query += "%20";
        else if (c == '"') encoded_query += "%22";
        else if (c == '\'') encoded_query += "%27";
        else encoded_query += c;
    }

    std::string lower_type = type;
    for (auto& c : lower_type) c = std::tolower(static_cast<unsigned char>(c));

    std::string uri = "spotify:search:" + encoded_query;
    if (lower_type == "artist") uri = "spotify:search:artist:" + encoded_query;
    else if (lower_type == "album") uri = "spotify:search:album:" + encoded_query;
    else if (lower_type == "playlist") uri = "spotify:search:playlist:" + encoded_query;

    std::string script =
        "tell application \"Spotify\"\n"
        "  activate\n"
        "  open location \"" + uri + "\"\n"
        "  delay 1.5\n"
        "  play\n"
        "end tell";

    auto r = run_applescript(script, 10000);
    if (r.success) return {true, "Playing " + query + " on Spotify", "",
        "{\"action\": \"play_on_spotify\", \"query\": \"" + escape_applescript(query) + "\"}"};
    return {false, "", r.error, "{\"error\": \"" + r.error + "\"}"};
}


static ActionResult action_play_apple_music(const std::string& args_json) {
    std::string query = json_get_string(args_json, "query");
    if (query.empty())
        return {false, "", "Song or artist name required", "{\"error\": \"missing query\"}"};

    // Search library first (faster than play track by name which can hang)
    std::string search_script =
        "tell application \"Music\"\n"
        "  activate\n"
        "  set results to search playlist \"Library\" for \"" + escape_applescript(query) + "\"\n"
        "  if length of results > 0 then\n"
        "    play item 1 of results\n"
        "    return name of item 1 of results\n"
        "  else\n"
        "    error \"Song not found in library\"\n"
        "  end if\n"
        "end tell";

    auto r = run_applescript(search_script, 8000);
    if (r.success)
        return {true, "Playing " + trim_output(r.output) + " on Apple Music", "",
                "{\"action\": \"play_apple_music\", \"query\": \"" + escape_applescript(query) + "\"}"};

    // Fallback: open Apple Music web search (not local playback)
    std::string url = "https://music.apple.com/search?term=" + url_encode(query);
    run_shell("open '" + escape_shell(url) + "'");
    return {true, "Could not find " + query + " in your library. Opened Apple Music search instead.", "",
            "{\"action\": \"play_apple_music\", \"query\": \"" + escape_applescript(query) +
            "\", \"fallback\": \"web_search\"}"};
}

void register_media_actions(ActionRegistry& registry) {
    registry.register_action(
        {"play_pause_music", "Play or pause music (Music.app or Spotify)",
         "{}",
         true,
         "media",
         "Pause the music",
         "rcli action play_pause_music '{}'"},
        action_play_pause_music);

    registry.register_action(
        {"next_track", "Skip to the next track",
         "{}",
         true,
         "media",
         "Skip this song",
         "rcli action next_track '{}'"},
        action_next_track);

    registry.register_action(
        {"previous_track", "Go to the previous track",
         "{}",
         false,
         "media",
         "Play the previous song",
         "rcli action previous_track '{}'"},
        action_previous_track);

    registry.register_action(
        {"get_now_playing", "Get the currently playing song and artist",
         "{}",
         true,
         "media",
         "What song is playing?",
         "rcli action get_now_playing '{}'"},
        action_get_now_playing);

    registry.register_action(
        {"play_on_spotify", "Search and play a specific song or artist on Spotify",
         "{\"query\": \"song or artist name\"}",
         true,
         "media",
         "Play Bohemian Rhapsody on Spotify",
         "rcli action play_on_spotify '{\"query\": \"Bohemian Rhapsody\"}'"},
        action_play_on_spotify);

    registry.register_action(
        {"play_apple_music", "Play a song on Apple Music by name",
         "{\"query\": \"song or artist name\"}",
         false,
         "media",
         "Play Beatles on Apple Music",
         "rcli action play_apple_music '{\"query\": \"Beatles\"}'"},
        action_play_apple_music);
}

} // namespace rcli
