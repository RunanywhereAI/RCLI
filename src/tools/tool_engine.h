#pragma once

#include "core/types.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>

namespace rastack {

class ToolEngine {
public:
    using ToolFunction = std::function<std::string(const std::string& args_json)>;

    ToolEngine();
    ~ToolEngine() = default;

    // Register a tool
    void register_tool(const std::string& name, ToolFunction fn);

    // Register default built-in tools
    void register_defaults();

    // Get tool definitions as JSON (for LLM system prompt).
    // Returns external definitions if set, otherwise the built-in defaults.
    std::string get_tool_definitions_json() const;

    // Set external tool definitions JSON (e.g., from ActionRegistry).
    // When set, get_tool_definitions_json() returns this instead of defaults.
    void set_external_tool_definitions(const std::string& json);

    // Parse tool calls from LLM output
    std::vector<ToolCall> parse_tool_calls(const std::string& llm_output) const;

    // Execute a single tool
    ToolResult execute(const ToolCall& call);

    // Execute multiple tools (could be parallelized)
    std::vector<ToolResult> execute_all(const std::vector<ToolCall>& calls);

    // Format tool results for injection back into LLM context
    std::string format_results(const std::vector<ToolResult>& results) const;

    int num_tools() const { return static_cast<int>(tools_.size()); }

    // Check if a tool exists by name
    bool has_tool(const std::string& name) const { return tools_.count(name) > 0; }

    // Lightweight heuristic: does the user query likely need tool calls?
    // Uses registered tool keywords (not hardcoded lists) to decide.
    // This avoids injecting tool defs for pure knowledge queries.
    bool needs_tools(const std::string& user_query) const;

    // Register keywords for a tool (used by ActionRegistry bridge).
    void register_tool_keywords(const std::string& name, const std::vector<std::string>& keywords);

    // Legacy static version (uses built-in tool keywords only).
    static bool needs_tools_static(const std::string& user_query);

private:
    std::unordered_map<std::string, ToolFunction> tools_;
    std::unordered_map<std::string, std::vector<std::string>> tool_keywords_;
    std::string external_tool_defs_;
};

} // namespace rastack
