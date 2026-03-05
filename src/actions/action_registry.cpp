#include "actions/action_registry.h"
#include "actions/macos_actions.h"
#include "core/log.h"
#include <cstdio>
#include <algorithm>
#include <sstream>
#include <fstream>

namespace rcli {

ActionRegistry::ActionRegistry() = default;

void ActionRegistry::register_action(const ActionDef& def, ActionFunc fn) {
    actions_[def.name] = {def, std::move(fn)};
    if (def.default_enabled)
        enabled_.insert(def.name);
}

void ActionRegistry::register_defaults() {
    register_macos_actions(*this);
    LOG_DEBUG("Actions", "Registered %d macOS actions (%d enabled)",
              num_actions(), num_enabled());
}

std::string ActionRegistry::get_definitions_json() const {
    std::ostringstream oss;
    oss << "[\n";
    bool first = true;

    // Built-in tools (time, calculate)
    oss << "  {\"name\": \"get_current_time\", \"description\": \"Get the current date and time\", \"parameters\": {}}";
    first = false;
    oss << ",\n  {\"name\": \"calculate\", \"description\": \"Evaluate a math expression\", \"parameters\": {\"expression\": \"math expression like 2 + 2\"}}";

    for (auto& [name, entry] : actions_) {
        if (enabled_.count(name) == 0) continue;
        if (!first) oss << ",\n";
        first = false;
        oss << "  {\"name\": \"" << entry.def.name
            << "\", \"description\": \"" << entry.def.description
            << "\", \"parameters\": " << entry.def.parameters_json << "}";
    }
    oss << "\n]";
    return oss.str();
}

std::string ActionRegistry::get_filtered_definitions_json(
    const std::string& query, int max_tools) const
{
    // Tokenize query into lowercase words
    std::vector<std::string> query_words;
    {
        std::string word;
        for (char c : query) {
            if (std::isalnum(static_cast<unsigned char>(c))) {
                word += std::tolower(static_cast<unsigned char>(c));
            } else if (!word.empty()) {
                if (word.size() > 1) query_words.push_back(word);
                word.clear();
            }
        }
        if (word.size() > 1) query_words.push_back(word);
    }

    // Score each enabled action by keyword overlap with name + description
    struct ScoredAction {
        std::string name;
        std::string description;
        std::string parameters_json;
        int score;
    };
    std::vector<ScoredAction> scored;

    for (auto& [name, entry] : actions_) {
        if (enabled_.count(name) == 0) continue;

        std::string haystack;
        for (char c : entry.def.name)
            haystack += std::tolower(static_cast<unsigned char>(c));
        haystack += ' ';
        for (char c : entry.def.description)
            haystack += std::tolower(static_cast<unsigned char>(c));
        haystack += ' ';
        for (char c : entry.def.category)
            haystack += std::tolower(static_cast<unsigned char>(c));

        int score = 0;
        for (auto& w : query_words) {
            if (haystack.find(w) != std::string::npos) score++;
        }

        scored.push_back({entry.def.name, entry.def.description,
                          entry.def.parameters_json, score});
    }

    // Sort by score descending
    std::sort(scored.begin(), scored.end(),
              [](const ScoredAction& a, const ScoredAction& b) { return a.score > b.score; });

    // If fewer than max_tools actions are enabled, just return all of them
    if ((int)scored.size() <= max_tools) {
        return get_definitions_json();
    }

    // Take top-k scoring actions (always include those with score > 0)
    std::ostringstream oss;
    oss << "[\n";

    // Always include built-in tools
    oss << "  {\"name\": \"get_current_time\", \"description\": \"Get the current date and time\", \"parameters\": {}}";
    oss << ",\n  {\"name\": \"calculate\", \"description\": \"Evaluate a math expression\", \"parameters\": {\"expression\": \"math expression like 2 + 2\"}}";

    int included = 0;
    for (auto& sa : scored) {
        if (included >= max_tools) break;
        oss << ",\n  {\"name\": \"" << sa.name
            << "\", \"description\": \"" << sa.description
            << "\", \"parameters\": " << sa.parameters_json << "}";
        included++;
    }
    oss << "\n]";
    return oss.str();
}

std::string ActionRegistry::get_all_definitions_json() const {
    std::ostringstream oss;
    oss << "[\n";
    bool first = true;
    for (auto& [name, entry] : actions_) {
        if (!first) oss << ",\n";
        first = false;
        oss << "  {\"name\": \"" << entry.def.name
            << "\", \"description\": \"" << entry.def.description
            << "\", \"parameters\": " << entry.def.parameters_json << "}";
    }
    oss << "\n]";
    return oss.str();
}

ActionResult ActionRegistry::execute(const std::string& name, const std::string& args_json) {
    auto it = actions_.find(name);
    if (it == actions_.end()) {
        return {false, "", "Unknown action: " + name, "{\"error\": \"unknown action\"}"};
    }

    try {
        return it->second.fn(args_json);
    } catch (const std::exception& e) {
        return {false, "", e.what(), "{\"error\": \"" + std::string(e.what()) + "\"}"};
    }
}

void ActionRegistry::set_enabled(const std::string& name, bool enabled) {
    if (actions_.find(name) == actions_.end()) return;
    if (enabled)
        enabled_.insert(name);
    else
        enabled_.erase(name);
}

bool ActionRegistry::is_enabled(const std::string& name) const {
    return enabled_.count(name) > 0;
}

std::vector<std::string> ActionRegistry::list_enabled_actions() const {
    std::vector<std::string> names;
    names.reserve(enabled_.size());
    for (auto& n : enabled_) names.push_back(n);
    std::sort(names.begin(), names.end());
    return names;
}

int ActionRegistry::num_enabled() const {
    return static_cast<int>(enabled_.size());
}

void ActionRegistry::save_preferences(const std::string& path) const {
    std::ofstream f(path);
    if (!f.is_open()) {
        LOG_ERROR("Actions", "Cannot write preferences to %s", path.c_str());
        return;
    }
    f << "{\n  \"enabled\": [";
    bool first = true;
    auto sorted = list_enabled_actions();
    for (auto& n : sorted) {
        if (!first) f << ", ";
        first = false;
        f << "\"" << n << "\"";
    }
    f << "],\n  \"disabled\": [";
    first = true;
    auto all = list_actions();
    for (auto& n : all) {
        if (enabled_.count(n) > 0) continue;
        if (!first) f << ", ";
        first = false;
        f << "\"" << n << "\"";
    }
    f << "]\n}\n";
    LOG_DEBUG("Actions", "Saved preferences to %s (%d enabled)", path.c_str(), num_enabled());
}

void ActionRegistry::load_preferences(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return;

    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    if (content.empty()) return;

    // Parse the "enabled" array from JSON
    auto parse_array = [&](const std::string& key) -> std::vector<std::string> {
        std::vector<std::string> result;
        auto pos = content.find("\"" + key + "\"");
        if (pos == std::string::npos) return result;
        auto bracket = content.find('[', pos);
        if (bracket == std::string::npos) return result;
        auto end_bracket = content.find(']', bracket);
        if (end_bracket == std::string::npos) return result;
        std::string arr = content.substr(bracket + 1, end_bracket - bracket - 1);
        size_t p = 0;
        while (p < arr.size()) {
            auto q1 = arr.find('"', p);
            if (q1 == std::string::npos) break;
            auto q2 = arr.find('"', q1 + 1);
            if (q2 == std::string::npos) break;
            result.push_back(arr.substr(q1 + 1, q2 - q1 - 1));
            p = q2 + 1;
        }
        return result;
    };

    auto enabled_list = parse_array("enabled");
    auto disabled_list = parse_array("disabled");

    if (enabled_list.empty() && disabled_list.empty()) return;

    // Apply: enable listed ones, disable listed ones
    for (auto& n : enabled_list) {
        if (actions_.count(n)) enabled_.insert(n);
    }
    for (auto& n : disabled_list) {
        enabled_.erase(n);
    }

    LOG_DEBUG("Actions", "Loaded preferences from %s (%d enabled)", path.c_str(), num_enabled());
}

std::vector<std::string> ActionRegistry::list_actions() const {
    std::vector<std::string> names;
    names.reserve(actions_.size());
    for (auto& [name, _] : actions_) {
        names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

std::vector<ActionDef> ActionRegistry::get_all_defs() const {
    std::vector<ActionDef> defs;
    defs.reserve(actions_.size());
    for (auto& [name, entry] : actions_) {
        defs.push_back(entry.def);
    }
    std::sort(defs.begin(), defs.end(),
              [](const ActionDef& a, const ActionDef& b) {
                  if (a.category != b.category) return a.category < b.category;
                  return a.name < b.name;
              });
    return defs;
}

const ActionDef* ActionRegistry::get_def(const std::string& name) const {
    auto it = actions_.find(name);
    if (it == actions_.end()) return nullptr;
    return &it->second.def;
}

} // namespace rcli
