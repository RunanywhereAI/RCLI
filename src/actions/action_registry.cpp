#include "actions/action_registry.h"
#include "actions/macos_actions.h"
#include "core/log.h"
#include <cstdio>
#include <algorithm>
#include <sstream>

namespace rcli {

ActionRegistry::ActionRegistry() = default;

void ActionRegistry::register_action(const ActionDef& def, ActionFunc fn) {
    actions_[def.name] = {def, std::move(fn)};
}

void ActionRegistry::register_defaults() {
    register_macos_actions(*this);
    LOG_DEBUG("Actions", "Registered %d macOS actions", num_actions());
}

std::string ActionRegistry::get_definitions_json() const {
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

bool ActionRegistry::needs_action(const std::string& query) const {
    return !match_action(query).empty();
}

void ActionRegistry::match_action_scored(const std::string& query,
                                          std::string& out_name,
                                          int& out_score) const {
    std::string q = query;
    for (auto& c : q) c = std::tolower(static_cast<unsigned char>(c));

    out_name.clear();
    out_score = 0;
    for (auto& [name, entry] : actions_) {
        int hits = 0;
        size_t longest = 0;
        for (auto& kw : entry.def.keywords) {
            if (q.find(kw) != std::string::npos) {
                hits++;
                if (kw.size() > longest) longest = kw.size();
            }
        }
        if (hits == 0) continue;
        int score = hits * 1000 + static_cast<int>(longest);
        if (score > out_score) {
            out_score = score;
            out_name = name;
        }
    }
}

std::string ActionRegistry::match_action(const std::string& query) const {
    std::string name;
    int score = 0;
    match_action_scored(query, name, score);
    return name;
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
