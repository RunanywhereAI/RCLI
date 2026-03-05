#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <functional>

namespace rcli {

struct ActionResult {
    bool        success;
    std::string output;
    std::string error;
    std::string raw_json;
};

struct ActionDef {
    std::string name;
    std::string description;
    std::string parameters_json;
    bool        default_enabled = true;
    std::string category;
    std::string example_voice;
    std::string example_cli;
};

using ActionFunc = std::function<ActionResult(const std::string& args_json)>;

class ActionRegistry {
public:
    ActionRegistry();
    ~ActionRegistry() = default;

    void register_action(const ActionDef& def, ActionFunc fn);
    void register_defaults();

    // Returns definitions JSON for enabled actions only (what the LLM sees)
    std::string get_definitions_json() const;

    // Returns definitions JSON for the top-k most relevant enabled actions for a query.
    // Uses keyword overlap scoring. Falls back to all enabled actions if fewer than
    // min_tools match. Built-in tools (time, calculate) are always included.
    std::string get_filtered_definitions_json(const std::string& query,
                                               int max_tools = 10) const;

    // Returns definitions JSON for ALL actions (for display/manual execution)
    std::string get_all_definitions_json() const;

    ActionResult execute(const std::string& name, const std::string& args_json);

    // Enable/disable actions for LLM visibility
    void set_enabled(const std::string& name, bool enabled);
    bool is_enabled(const std::string& name) const;
    std::vector<std::string> list_enabled_actions() const;
    int num_enabled() const;

    // Persistence
    void save_preferences(const std::string& path) const;
    void load_preferences(const std::string& path);

    std::vector<std::string> list_actions() const;
    std::vector<ActionDef> get_all_defs() const;
    const ActionDef* get_def(const std::string& name) const;
    int num_actions() const { return static_cast<int>(actions_.size()); }

private:
    struct RegisteredAction {
        ActionDef  def;
        ActionFunc fn;
    };

    std::unordered_map<std::string, RegisteredAction> actions_;
    std::unordered_set<std::string> enabled_;
};

} // namespace rcli
