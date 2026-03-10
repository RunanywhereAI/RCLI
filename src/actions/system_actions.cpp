#include "actions/system_actions.h"
#include "actions/action_helpers.h"
#include "actions/applescript_executor.h"
#include <sstream>
#include <unordered_map>

namespace rcli {

static ActionResult action_screenshot(const std::string& args_json) {
    std::string path = json_get_string(args_json, "path");
    if (path.empty()) path = "/tmp/rcli_screenshot.png";
    auto r = run_shell("screencapture -x '" + escape_shell(path) + "'");
    if (r.success)
        return {true, "Screenshot saved to " + path, "",
                "{\"action\": \"screenshot\", \"path\": \"" + path + "\"}"};
    return {false, "", r.error, "{\"error\": \"" + r.error + "\"}"};
}

static ActionResult action_set_volume(const std::string& args_json) {
    std::string level = json_get_string(args_json, "level");
    if (level.empty())
        return {false, "", "Volume level required (0-100)", "{\"error\": \"missing level\"}"};
    auto r = run_applescript("set volume output volume " + level);
    if (r.success)
        return {true, "Volume set to " + level + "%", "",
                "{\"action\": \"set_volume\", \"level\": " + level + "}"};
    return {false, "", r.error, "{\"error\": \"" + r.error + "\"}"};
}

static ActionResult action_toggle_dark_mode(const std::string& args_json) {
    (void)args_json;
    auto r = run_applescript(
        "tell application \"System Events\" to tell appearance preferences to set dark mode to not dark mode");
    if (r.success) return {true, "Toggled dark mode", "", "{\"action\": \"toggle_dark_mode\"}"};
    return {false, "", r.error, "{\"error\": \"" + r.error + "\"}"};
}

static ActionResult action_lock_screen(const std::string& args_json) {
    (void)args_json;
    auto r = run_shell("pmset displaysleepnow");
    if (r.success) return {true, "Screen locked", "", "{\"action\": \"lock_screen\"}"};
    run_applescript(
        "tell application \"System Events\" to keystroke \"q\" using {control down, command down}");
    return {true, "Screen locked", "", "{\"action\": \"lock_screen\"}"};
}

static ActionResult action_get_battery(const std::string& args_json) {
    (void)args_json;
    auto r = run_shell("pmset -g batt | grep -Eo '[0-9]+%' | head -1");
    if (r.success && !r.output.empty()) {
        std::string pct = trim_output(r.output);
        auto r2 = run_shell("pmset -g batt | grep -o 'charging\\|discharging\\|charged\\|AC Power'");
        std::string status = trim_output(r2.output);
        return {true, "Battery: " + pct + " (" + status + ")", "",
                "{\"action\": \"get_battery\", \"percent\": \"" + pct + "\", \"status\": \"" + status + "\"}"};
    }
    return {false, "", "Could not read battery", "{\"error\": \"pmset failed\"}"};
}

static ActionResult action_get_wifi(const std::string& args_json) {
    (void)args_json;
    auto r = run_shell("networksetup -getairportnetwork en0 2>/dev/null | sed 's/Current Wi-Fi Network: //'");
    if (r.success && !r.output.empty()) {
        std::string name = trim_output(r.output);
        return {true, "Wi-Fi: " + name, "", "{\"action\": \"get_wifi\", \"network\": \"" + name + "\"}"};
    }
    return {false, "", "Not connected to Wi-Fi", "{\"error\": \"no wifi\"}"};
}

static const std::unordered_map<std::string, std::string>& settings_pane_map() {
    static const std::unordered_map<std::string, std::string> m = {
        {"wifi",        "com.apple.wifi-settings-extension"},
        {"bluetooth",   "com.apple.BluetoothSettings"},
        {"network",     "com.apple.Network-Settings.extension"},
        {"sound",       "com.apple.Sound-Settings.extension"},
        {"audio",       "com.apple.Sound-Settings.extension"},
        {"display",     "com.apple.Displays-Settings.extension"},
        {"displays",    "com.apple.Displays-Settings.extension"},
        {"wallpaper",   "com.apple.Wallpaper-Settings.extension"},
        {"battery",     "com.apple.Battery-Settings.extension"},
        {"keyboard",    "com.apple.Keyboard-Settings.extension"},
        {"trackpad",    "com.apple.Trackpad-Settings.extension"},
        {"mouse",       "com.apple.Mouse-Settings.extension"},
        {"printers",    "com.apple.Print-Scan-Settings.extension"},
        {"privacy",     "com.apple.settings.PrivacySecurity.extension"},
        {"security",    "com.apple.settings.PrivacySecurity.extension"},
        {"notifications","com.apple.Notifications-Settings.extension"},
        {"focus",       "com.apple.Focus-Settings.extension"},
        {"general",     "com.apple.General-Settings.extension"},
        {"appearance",  "com.apple.Appearance-Settings.extension"},
        {"accessibility","com.apple.Accessibility-Settings.extension"},
        {"storage",     "com.apple.settings.Storage"},
        {"siri",        "com.apple.Siri-Settings.extension"},
        {"spotlight",   "com.apple.Siri-Settings.extension"},
        {"passwords",   "com.apple.Passwords-Settings.extension"},
        {"users",       "com.apple.Users-Groups-Settings.extension"},
        {"accounts",    "com.apple.Users-Groups-Settings.extension"},
        {"apple id",    "com.apple.systempreferences.AppleIDSettings"},
        {"icloud",      "com.apple.systempreferences.AppleIDSettings"},
        {"desktop",     "com.apple.Desktop-Settings.extension"},
        {"dock",        "com.apple.Desktop-Settings.extension"},
        {"screen saver","com.apple.ScreenSaver-Settings.extension"},
        {"lock screen", "com.apple.Lock-Screen-Settings.extension"},
        {"time machine","com.apple.Time-Machine-Settings.extension"},
        {"sharing",     "com.apple.Sharing-Settings.extension"},
        {"vpn",         "com.apple.NetworkExtensionSettingsUI.NESettingsUIExtension"},
    };
    return m;
}

static ActionResult action_open_settings(const std::string& args_json) {
    std::string pane = json_get_string(args_json, "pane");

    if (pane.empty()) {
        run_shell("open 'x-apple.systempreferences:'");
        return {true, "Opened System Settings", "", "{\"action\": \"open_settings\"}"};
    }

    std::string lower = pane;
    for (auto& c : lower) c = std::tolower(static_cast<unsigned char>(c));

    auto& map = settings_pane_map();
    auto it = map.find(lower);
    if (it != map.end()) {
        std::string url = "x-apple.systempreferences:" + it->second;
        auto r = run_shell("open '" + url + "'");
        if (r.success) return {true, "Opened " + pane + " settings", "",
            "{\"action\": \"open_settings\", \"pane\": \"" + escape_applescript(pane) + "\"}"};
        return {false, "", r.error, "{\"error\": \"" + r.error + "\"}"};
    }

    run_shell("open 'x-apple.systempreferences:'");
    return {true, "Opened System Settings (pane '" + pane + "' not recognized)", "",
        "{\"action\": \"open_settings\", \"pane\": \"" + escape_applescript(pane) + "\", \"note\": \"pane not found\"}"};
}

static ActionResult action_get_disk_usage(const std::string& args_json) {
    (void)args_json;
    auto r = run_shell("df -h / | tail -1 | awk '{print \"Total: \" $2 \", Used: \" $3 \", Available: \" $4 \", Capacity: \" $5}'");
    if (r.success)
        return {true, trim_output(r.output), "", "{\"action\": \"get_disk_usage\"}"};
    return {false, "", r.error, "{\"error\": \"" + r.error + "\"}"};
}

static ActionResult action_get_uptime(const std::string& args_json) {
    (void)args_json;
    auto r = run_shell("uptime | sed 's/.*up /Up /' | sed 's/,.*//'");
    if (r.success)
        return {true, trim_output(r.output), "", "{\"action\": \"get_uptime\"}"};
    return {false, "", r.error, "{\"error\": \"" + r.error + "\"}"};
}

static ActionResult action_get_ip_address(const std::string& args_json) {
    (void)args_json;
    auto local = run_shell("ipconfig getifaddr en0 2>/dev/null || echo 'Not connected'");
    auto pub   = run_shell("curl -s --max-time 5 ifconfig.me 2>/dev/null || echo 'Unavailable'");
    std::string local_ip = trim_output(local.output);
    std::string pub_ip   = trim_output(pub.output);
    return {true, "Local IP: " + local_ip + ", Public IP: " + pub_ip, "",
            "{\"action\": \"get_ip_address\", \"local\": \"" + local_ip + "\", \"public\": \"" + pub_ip + "\"}"};
}

void register_system_actions(ActionRegistry& registry) {
    registry.register_action(
        {"screenshot", "Take a screenshot",
         "{\"path\": \"optional save path\"}",
         true,
         "system",
         "Take a screenshot",
         "rcli action screenshot '{}'"},
        action_screenshot);

    registry.register_action(
        {"set_volume", "Set system volume (0-100)",
         "{\"level\": \"0-100\"}",
         true,
         "system",
         "Set the volume to 50 percent",
         "rcli action set_volume '{\"level\": \"50\"}'"},
        action_set_volume);

    registry.register_action(
        {"toggle_dark_mode", "Toggle between light and dark appearance",
         "{}",
         true,
         "system",
         "Turn on dark mode",
         "rcli action toggle_dark_mode '{}'"},
        action_toggle_dark_mode);

    registry.register_action(
        {"lock_screen", "Lock the screen",
         "{}",
         true,
         "system",
         "Lock my screen",
         "rcli action lock_screen '{}'"},
        action_lock_screen);

    registry.register_action(
        {"get_battery", "Get battery percentage and charging status",
         "{}",
         true,
         "system",
         "What's my battery level?",
         "rcli action get_battery '{}'"},
        action_get_battery);

    registry.register_action(
        {"get_wifi", "Get the current Wi-Fi network name",
         "{}",
         true,
         "system",
         "What Wi-Fi am I connected to?",
         "rcli action get_wifi '{}'"},
        action_get_wifi);

    registry.register_action(
        {"open_settings", "Open System Settings (optionally to a specific pane: wifi, bluetooth, sound, display, battery, keyboard, privacy, notifications, general, appearance, etc.)",
         "{\"pane\": \"optional pane name\"}",
         true,
         "system",
         "Open Wi-Fi settings",
         "rcli action open_settings '{\"pane\": \"wifi\"}'"},
        action_open_settings);

    registry.register_action(
        {"get_disk_usage", "Show disk space usage",
         "{}",
         true,
         "system",
         "How much disk space do I have?",
         "rcli action get_disk_usage '{}'"},
        action_get_disk_usage);

    registry.register_action(
        {"get_uptime", "Show how long the system has been running",
         "{}",
         true,
         "system",
         "How long has my Mac been running?",
         "rcli action get_uptime '{}'"},
        action_get_uptime);

    registry.register_action(
        {"get_ip_address", "Show local and public IP addresses",
         "{}",
         true,
         "system",
         "What's my IP address?",
         "rcli action get_ip_address '{}'"},
        action_get_ip_address);
}

} // namespace rcli
