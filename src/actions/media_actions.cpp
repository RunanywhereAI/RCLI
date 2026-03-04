#include "actions/media_actions.h"
#include "actions/action_helpers.h"
#include "actions/applescript_executor.h"

namespace rcli {

static ActionResult media_control(const std::string& command, const std::string& action_name) {
    auto r = run_applescript("tell application \"Music\" to " + command);
    if (r.success) return {true, action_name + " (Music.app)", "", "{\"action\": \"" + action_name + "\", \"app\": \"Music\"}"};
    auto r2 = run_applescript("tell application \"Spotify\" to " + command);
    if (r2.success) return {true, action_name + " (Spotify)", "", "{\"action\": \"" + action_name + "\", \"app\": \"Spotify\"}"};
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
    if (r2.success) {
        std::string info = trim_output(r2.output);
        return {true, "Now playing: " + info, "", "{\"action\": \"get_now_playing\", \"track\": \"" + escape_applescript(info) + "\"}"};
    }
    return {true, "Nothing playing", "", "{\"action\": \"get_now_playing\", \"track\": \"none\"}"};
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

static ActionResult action_set_music_volume(const std::string& args_json) {
    std::string level = json_get_string(args_json, "level");
    if (level.empty())
        return {false, "", "Volume level required (0-100)", "{\"error\": \"missing level\"}"};

    auto r = run_applescript("tell application \"Spotify\" to set sound volume to " + level);
    if (r.success)
        return {true, "Spotify volume set to " + level, "",
                "{\"action\": \"set_music_volume\", \"level\": " + level + ", \"app\": \"Spotify\"}"};

    auto r2 = run_applescript("tell application \"Music\" to set sound volume to " + level);
    if (r2.success)
        return {true, "Music volume set to " + level, "",
                "{\"action\": \"set_music_volume\", \"level\": " + level + ", \"app\": \"Music\"}"};
    return {false, "", "No music app running", "{\"error\": \"no music app\"}"};
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

    // Fallback: open Apple Music web search
    std::string url = "https://music.apple.com/search?term=" + url_encode(query);
    run_shell("open '" + url + "'");
    return {true, "Searching Apple Music for: " + query, "",
            "{\"action\": \"play_apple_music\", \"query\": \"" + escape_applescript(query) + "\"}"};
}

void register_media_actions(ActionRegistry& registry) {
    registry.register_action(
        {"play_pause_music", "Play or pause music (Music.app or Spotify)",
         "{}",
         {"play music", "pause music", "play pause", "resume music", "stop music",
          "pause", "resume", "stop playing"},
         "media",
         "Pause the music",
         "rcli action play_pause_music '{}'"},
        action_play_pause_music);

    registry.register_action(
        {"next_track", "Skip to the next track",
         "{}",
         {"next track", "next song", "skip song", "skip track"},
         "media",
         "Skip this song",
         "rcli action next_track '{}'"},
        action_next_track);

    registry.register_action(
        {"previous_track", "Go to the previous track",
         "{}",
         {"previous track", "previous song", "last song", "go back"},
         "media",
         "Play the previous song",
         "rcli action previous_track '{}'"},
        action_previous_track);

    registry.register_action(
        {"get_now_playing", "Get the currently playing song and artist",
         "{}",
         {"what's playing", "now playing", "current song", "what song"},
         "media",
         "What song is playing?",
         "rcli action get_now_playing '{}'"},
        action_get_now_playing);

    registry.register_action(
        {"play_on_spotify", "Play a song, artist, album, or playlist on Spotify",
         "{\"query\": \"song/artist/album name\", \"type\": \"track|artist|album|playlist (optional)\"}",
         {"play on spotify", "play spotify", "spotify play", "play song", "play artist",
          "play album", "play playlist", "listen to", "put on", "spotify", "on spotify"},
         "media",
         "Play Bohemian Rhapsody on Spotify",
         "rcli action play_on_spotify '{\"query\": \"Bohemian Rhapsody\"}'"},
        action_play_on_spotify);

    registry.register_action(
        {"set_music_volume", "Set music app volume (separate from system volume)",
         "{\"level\": \"0-100\"}",
         {"music volume", "spotify volume", "player volume"},
         "media",
         "Set Spotify volume to 50",
         "rcli action set_music_volume '{\"level\": \"50\"}'"},
        action_set_music_volume);

    registry.register_action(
        {"play_apple_music", "Play a song on Apple Music by name",
         "{\"query\": \"song or artist name\"}",
         {"play on apple music", "apple music play", "play in music app", "play"},
         "media",
         "Play Beatles on Apple Music",
         "rcli action play_apple_music '{\"query\": \"Beatles\"}'"},
        action_play_apple_music);
}

} // namespace rcli
