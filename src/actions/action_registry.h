#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>

namespace rcli {

// Result from executing an action
struct ActionResult {
    bool        success;
    std::string output;       // Human-readable result
    std::string error;        // Error message if !success
    std::string raw_json;     // Machine-readable result for LLM context
};

// An action definition (what the LLM sees + what the CLI displays)
struct ActionDef {
    std::string name;
    std::string description;
    std::string parameters_json;  // JSON schema of parameters
    std::vector<std::string> keywords;  // Trigger keywords for needs_action()
    std::string category;         // e.g. "productivity", "communication", "system"
    std::string example_voice;    // e.g. "Create a note called Meeting Notes"
    std::string example_cli;      // e.g. "rcli action create_note '{\"title\": \"Meeting\"}'"
};

// Action function signature
using ActionFunc = std::function<ActionResult(const std::string& args_json)>;

// Registry of all available macOS actions
class ActionRegistry {
public:
    ActionRegistry();
    ~ActionRegistry() = default;

    // Register a new action
    void register_action(const ActionDef& def, ActionFunc fn);

    // Register all built-in macOS actions
    void register_defaults();

    // Get action definitions as JSON for LLM tool calling
    std::string get_definitions_json() const;

    // Execute an action by name
    ActionResult execute(const std::string& name, const std::string& args_json);

    // Check if a user query likely needs actions (keyword heuristic)
    bool needs_action(const std::string& query) const;

    // Return the best matching action name for the query, or "" if none
    std::string match_action(const std::string& query) const;

    // Same as match_action but also returns the score (hits * 1000 + longest_keyword_len)
    void match_action_scored(const std::string& query, std::string& out_name, int& out_score) const;

    // List all registered action names
    std::vector<std::string> list_actions() const;

    // Get all action definitions (for CLI display)
    std::vector<ActionDef> get_all_defs() const;

    // Get a single action definition by name (returns nullptr if not found)
    const ActionDef* get_def(const std::string& name) const;

    int num_actions() const { return static_cast<int>(actions_.size()); }

private:
    struct RegisteredAction {
        ActionDef  def;
        ActionFunc fn;
    };

    std::unordered_map<std::string, RegisteredAction> actions_;
};

} // namespace rcli
