#include "tools/tool_engine.h"
#include "tools/tool_defs.h"
#include "core/log.h"
#include <cstdio>
#include <ctime>
#include <cmath>
#include <sstream>
#include <regex>
#include <future>

namespace rastack {

ToolEngine::ToolEngine() = default;

void ToolEngine::register_tool(const std::string& name, ToolFunction fn) {
    tools_[name] = std::move(fn);
}

void ToolEngine::register_defaults() {
    // get_current_time
    register_tool("get_current_time", [](const std::string&) -> std::string {
        auto now = std::time(nullptr);
        char buf[128];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S %Z", std::localtime(&now));
        return std::string("{\"time\": \"") + buf + "\"}";
    });

    // get_weather (mock)
    register_tool("get_weather", [](const std::string& args) -> std::string {
        // Extract location from JSON (simple parsing)
        std::string location = "Unknown";
        auto pos = args.find("\"location\"");
        if (pos != std::string::npos) {
            auto colon = args.find(':', pos);
            auto quote1 = args.find('"', colon + 1);
            auto quote2 = args.find('"', quote1 + 1);
            if (quote1 != std::string::npos && quote2 != std::string::npos) {
                location = args.substr(quote1 + 1, quote2 - quote1 - 1);
            }
        }
        return "{\"location\": \"" + location + "\", \"temperature\": \"22°C\", "
               "\"condition\": \"Partly cloudy\", \"humidity\": \"45%\"}";
    });

    // calculate
    register_tool("calculate", [](const std::string& args) -> std::string {
        // Extract expression (simple parsing)
        std::string expr = "";
        auto pos = args.find("\"expression\"");
        if (pos != std::string::npos) {
            auto colon = args.find(':', pos);
            auto quote1 = args.find('"', colon + 1);
            auto quote2 = args.find('"', quote1 + 1);
            if (quote1 != std::string::npos && quote2 != std::string::npos) {
                expr = args.substr(quote1 + 1, quote2 - quote1 - 1);
            }
        }

        // Very simple evaluator for basic expressions
        // In production, use a proper expression parser
        double result = 0;
        try {
            // Handle simple "a op b" patterns
            std::istringstream iss(expr);
            double a, b;
            char op;
            if (iss >> a >> op >> b) {
                switch (op) {
                    case '+': result = a + b; break;
                    case '-': result = a - b; break;
                    case '*': result = a * b; break;
                    case '/': result = b != 0 ? a / b : 0; break;
                    default: result = 0;
                }
            } else {
                result = std::stod(expr);
            }
        } catch (...) {
            return "{\"error\": \"Could not evaluate: " + expr + "\"}";
        }

        std::ostringstream oss;
        oss << "{\"expression\": \"" << expr << "\", \"result\": " << result << "}";
        return oss.str();
    });

    LOG_DEBUG("Tools", "Registered %d tools", num_tools());
}

std::string ToolEngine::get_tool_definitions_json() const {
    if (!external_tool_defs_.empty()) return external_tool_defs_;
    return DEFAULT_TOOL_DEFS_JSON;
}

void ToolEngine::set_external_tool_definitions(const std::string& json) {
    external_tool_defs_ = json;
    LOG_DEBUG("Tools", "Set external tool definitions (%d bytes)", (int)json.size());
}

// Parse LFM2 native format: <|tool_call_start|>[func_name(key="val", key2="val2")]<|tool_call_end|>
static bool parse_lfm2_call(const std::string& content, ToolCall& call) {
    // content looks like: [func_name(key="val", key2="val2")]
    // or: func_name(key="val")
    std::string s = content;

    // Strip surrounding brackets
    while (!s.empty() && (s.front() == '[' || s.front() == ' ')) s.erase(s.begin());
    while (!s.empty() && (s.back() == ']' || s.back() == ' ')) s.pop_back();

    auto paren = s.find('(');
    if (paren == std::string::npos) return false;

    call.name = s.substr(0, paren);
    // Trim whitespace from name
    while (!call.name.empty() && call.name.back() == ' ') call.name.pop_back();

    auto close_paren = s.rfind(')');
    if (close_paren == std::string::npos || close_paren <= paren) return false;

    std::string params_str = s.substr(paren + 1, close_paren - paren - 1);

    // Parse key="value" pairs into JSON
    // Handle: key="value", key2="value2"
    std::string json = "{";
    bool first = true;
    size_t pos = 0;
    while (pos < params_str.size()) {
        // Skip whitespace and commas
        while (pos < params_str.size() && (params_str[pos] == ' ' || params_str[pos] == ','))
            pos++;
        if (pos >= params_str.size()) break;

        // Find key
        auto eq = params_str.find('=', pos);
        if (eq == std::string::npos) break;
        std::string key = params_str.substr(pos, eq - pos);
        while (!key.empty() && key.back() == ' ') key.pop_back();
        while (!key.empty() && key.front() == ' ') key.erase(key.begin());

        pos = eq + 1;
        // Skip whitespace after =
        while (pos < params_str.size() && params_str[pos] == ' ') pos++;

        // Extract value (quoted string or bare value)
        std::string value;
        if (pos < params_str.size() && params_str[pos] == '"') {
            pos++; // skip opening quote
            while (pos < params_str.size() && params_str[pos] != '"') {
                if (params_str[pos] == '\\' && pos + 1 < params_str.size()) {
                    value += params_str[pos + 1];
                    pos += 2;
                } else {
                    value += params_str[pos++];
                }
            }
            if (pos < params_str.size()) pos++; // skip closing quote
        } else {
            // Bare value up to comma or end
            auto comma = params_str.find(',', pos);
            if (comma == std::string::npos) comma = params_str.size();
            value = params_str.substr(pos, comma - pos);
            while (!value.empty() && value.back() == ' ') value.pop_back();
            pos = comma;
        }

        // Strip any junk tokens like /no_think
        if (value.size() > 9 && value.substr(value.size() - 9) == "/no_think")
            value = value.substr(0, value.size() - 9);
        while (!value.empty() && (value.back() == ' ' || value.back() == '/'))
            value.pop_back();

        if (!first) json += ", ";
        json += "\"" + key + "\": \"" + value + "\"";
        first = false;
    }
    json += "}";
    call.arguments_json = json;
    return !call.name.empty();
}

std::vector<ToolCall> ToolEngine::parse_tool_calls(const std::string& llm_output) const {
    std::vector<ToolCall> calls;

    // === Format 1: LFM2 native — <|tool_call_start|>[func(args)]<|tool_call_end|> ===
    {
        std::string start_tag = "<|tool_call_start|>";
        std::string end_tag = "<|tool_call_end|>";
        size_t pos = 0;
        while ((pos = llm_output.find(start_tag, pos)) != std::string::npos) {
            size_t content_start = pos + start_tag.length();
            size_t content_end = llm_output.find(end_tag, content_start);
            if (content_end == std::string::npos) break;
            std::string content = llm_output.substr(content_start, content_end - content_start);
            ToolCall call;
            if (parse_lfm2_call(content, call))
                calls.push_back(std::move(call));
            pos = content_end + end_tag.length();
        }
        if (!calls.empty()) return calls;
    }

    // === Format 2: Qwen3 / generic — <tool_call>{"name": "...", "arguments": {...}}</tool_call> ===
    {
        std::string start_tag = "<tool_call>";
        std::string end_tag = "</tool_call>";
        size_t pos = 0;
        while ((pos = llm_output.find(start_tag, pos)) != std::string::npos) {
            size_t content_start = pos + start_tag.length();
            size_t content_end = llm_output.find(end_tag, content_start);
            if (content_end == std::string::npos) break;

            std::string content = llm_output.substr(content_start, content_end - content_start);
            ToolCall call;

            auto name_pos = content.find("\"name\"");
            if (name_pos != std::string::npos) {
                auto colon = content.find(':', name_pos);
                auto q1 = content.find('"', colon + 1);
                auto q2 = content.find('"', q1 + 1);
                if (q1 != std::string::npos && q2 != std::string::npos)
                    call.name = content.substr(q1 + 1, q2 - q1 - 1);
            }

            auto args_pos = content.find("\"arguments\"");
            if (args_pos != std::string::npos) {
                auto brace_start = content.find('{', args_pos);
                if (brace_start != std::string::npos) {
                    int depth = 0;
                    size_t brace_end = brace_start;
                    for (size_t i = brace_start; i < content.size(); i++) {
                        if (content[i] == '{') depth++;
                        if (content[i] == '}') depth--;
                        if (depth == 0) { brace_end = i; break; }
                    }
                    call.arguments_json = content.substr(brace_start, brace_end - brace_start + 1);
                }
            }

            if (!call.name.empty())
                calls.push_back(std::move(call));
            pos = content_end + end_tag.length();
        }
    }

    return calls;
}

ToolResult ToolEngine::execute(const ToolCall& call) {
    ToolResult result;
    result.name = call.name;

    auto it = tools_.find(call.name);
    if (it == tools_.end()) {
        result.success = false;
        result.result_json = "{\"error\": \"Unknown tool: " + call.name + "\"}";
    } else {
        try {
            result.result_json = it->second(call.arguments_json);
            result.success = true;
        } catch (const std::exception& e) {
            result.success = false;
            result.result_json = "{\"error\": \"" + std::string(e.what()) + "\"}";
        }
    }

    return result;
}

std::vector<ToolResult> ToolEngine::execute_all(const std::vector<ToolCall>& calls) {
    if (calls.size() <= 1) {
        // Single call — no threading overhead needed
        std::vector<ToolResult> results;
        for (auto& call : calls) results.push_back(execute(call));
        return results;
    }

    // Multiple calls — execute in parallel, merge results
    std::vector<std::future<ToolResult>> futures;
    futures.reserve(calls.size());
    for (auto& call : calls) {
        futures.push_back(std::async(std::launch::async, [this, &call]() {
            return execute(call);
        }));
    }

    std::vector<ToolResult> results;
    results.reserve(calls.size());
    for (auto& fut : futures) results.push_back(fut.get());
    return results;
}

void ToolEngine::register_tool_keywords(const std::string& name,
                                         const std::vector<std::string>& keywords) {
    tool_keywords_[name] = keywords;
}

bool ToolEngine::needs_tools(const std::string& user_query) const {
    std::string q = user_query;
    for (auto& c : q) c = std::tolower(static_cast<unsigned char>(c));

    // Check all registered tool keywords (action-driven, not hardcoded)
    for (auto& [name, keywords] : tool_keywords_) {
        for (auto& kw : keywords) {
            if (q.find(kw) != std::string::npos) return true;
        }
    }
    // Fallback: check built-in tool keywords
    return needs_tools_static(user_query);
}

bool ToolEngine::needs_tools_static(const std::string& user_query) {
    std::string q = user_query;
    for (auto& c : q) c = std::tolower(static_cast<unsigned char>(c));

    static const char* keywords[] = {
        "what time", "current time", "what's the time", "what day is",
        "today's date", "weather", "forecast",
        "calculate", "compute", "how much is",
    };
    for (auto& kw : keywords) {
        if (q.find(kw) != std::string::npos) return true;
    }
    return false;
}

std::string ToolEngine::format_results(const std::vector<ToolResult>& results) const {
    std::string formatted;
    for (size_t i = 0; i < results.size(); i++) {
        if (i > 0) formatted += "\n";
        formatted += "{\"name\": \"" + results[i].name + "\", \"result\": "
                     + results[i].result_json + "}";
    }
    return formatted;
}

} // namespace rastack
