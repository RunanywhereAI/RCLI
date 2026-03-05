#pragma once

#include "core/types.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>

namespace rastack {

struct ModelProfile;

class ToolEngine {
public:
    using ToolFunction = std::function<std::string(const std::string& args_json)>;

    ToolEngine();
    ~ToolEngine() = default;

    void register_tool(const std::string& name, ToolFunction fn);
    void register_defaults();

    // Returns external definitions if set, otherwise the built-in defaults.
    std::string get_tool_definitions_json() const;

    // Set external tool definitions JSON (e.g., from ActionRegistry).
    void set_external_tool_definitions(const std::string& json);

    // Set the active model profile for format-aware parsing (non-owning pointer)
    void set_model_profile(const ModelProfile* profile) { model_profile_ = profile; }

    // Delegates to ModelProfile::parse_tool_calls() when a profile is set
    std::vector<ToolCall> parse_tool_calls(const std::string& llm_output) const;
    ToolResult execute(const ToolCall& call);
    std::vector<ToolResult> execute_all(const std::vector<ToolCall>& calls);
    std::string format_results(const std::vector<ToolResult>& results) const;

    // Build a lightweight tool focus hint for injection into the user turn.
    // Returns e.g. "[Relevant tools: create_reminder, open_app]" or "" if no strong matches.
    // Does not invalidate the KV cache -- the hint goes in the user turn, not the system prompt.
    std::string build_tool_hint(const std::string& query, int top_k = 5) const;

    int num_tools() const { return static_cast<int>(tools_.size()); }
    bool has_tool(const std::string& name) const { return tools_.count(name) > 0; }
    std::vector<std::string> list_tool_names() const {
        std::vector<std::string> names;
        for (auto& [n, _] : tools_) names.push_back(n);
        return names;
    }

private:
    // Parsed name+description pairs from external_tool_defs_ (rebuilt on set)
    struct ToolInfo { std::string name; std::string description; };
    std::vector<ToolInfo> tool_info_cache_;
    void rebuild_tool_info_cache();

    std::unordered_map<std::string, ToolFunction> tools_;
    std::string external_tool_defs_;
    const ModelProfile* model_profile_ = nullptr;
};

} // namespace rastack
