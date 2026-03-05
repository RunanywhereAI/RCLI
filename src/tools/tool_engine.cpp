#include "tools/tool_engine.h"
#include "tools/tool_defs.h"
#include "engines/model_profile.h"
#include "core/log.h"
#include <cstdio>
#include <ctime>
#include <cmath>
#include <sstream>
#include <future>

namespace rastack {

ToolEngine::ToolEngine() = default;

void ToolEngine::register_tool(const std::string& name, ToolFunction fn) {
    tools_[name] = std::move(fn);
}

void ToolEngine::register_defaults() {
    register_tool("get_current_time", [](const std::string&) -> std::string {
        auto now = std::time(nullptr);
        char buf[128];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S %Z", std::localtime(&now));
        return std::string("{\"time\": \"") + buf + "\"}";
    });

    register_tool("calculate", [](const std::string& args) -> std::string {
        std::string expr;
        auto pos = args.find("\"expression\"");
        if (pos != std::string::npos) {
            auto colon = args.find(':', pos);
            auto quote1 = args.find('"', colon + 1);
            auto quote2 = args.find('"', quote1 + 1);
            if (quote1 != std::string::npos && quote2 != std::string::npos) {
                expr = args.substr(quote1 + 1, quote2 - quote1 - 1);
            }
        }

        double result = 0;
        try {
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
    rebuild_tool_info_cache();
    LOG_DEBUG("Tools", "Set external tool definitions (%d bytes, %d tools parsed)",
              (int)json.size(), (int)tool_info_cache_.size());
}

void ToolEngine::rebuild_tool_info_cache() {
    tool_info_cache_.clear();
    const std::string& json = external_tool_defs_.empty() ? std::string(DEFAULT_TOOL_DEFS_JSON) : external_tool_defs_;

    // Lightweight JSON array parse: extract {"name": "...", "description": "..."} entries
    size_t pos = 0;
    while (pos < json.size()) {
        auto name_key = json.find("\"name\"", pos);
        if (name_key == std::string::npos) break;

        auto colon = json.find(':', name_key + 6);
        if (colon == std::string::npos) break;
        auto q1 = json.find('"', colon + 1);
        auto q2 = json.find('"', q1 + 1);
        if (q1 == std::string::npos || q2 == std::string::npos) break;
        std::string name = json.substr(q1 + 1, q2 - q1 - 1);

        std::string desc;
        auto desc_key = json.find("\"description\"", q2);
        // Only look for description within the same object (before next "name")
        auto next_name = json.find("\"name\"", q2 + 1);
        if (desc_key != std::string::npos && (next_name == std::string::npos || desc_key < next_name)) {
            auto dc = json.find(':', desc_key + 13);
            auto dq1 = json.find('"', dc + 1);
            auto dq2 = json.find('"', dq1 + 1);
            if (dq1 != std::string::npos && dq2 != std::string::npos)
                desc = json.substr(dq1 + 1, dq2 - dq1 - 1);
        }

        tool_info_cache_.push_back({std::move(name), std::move(desc)});
        pos = q2 + 1;
    }
}

std::string ToolEngine::build_tool_hint(const std::string& query, int top_k) const {
    if (tool_info_cache_.empty()) return "";

    // Tokenize query into lowercase words (skip short words)
    std::vector<std::string> words;
    {
        std::string word;
        for (char c : query) {
            if (std::isalnum(static_cast<unsigned char>(c))) {
                word += std::tolower(static_cast<unsigned char>(c));
            } else if (!word.empty()) {
                if (word.size() > 1) words.push_back(word);
                word.clear();
            }
        }
        if (word.size() > 1) words.push_back(word);
    }
    if (words.empty()) return "";

    // Score each tool by keyword overlap
    struct Scored { const ToolInfo* info; int score; };
    std::vector<Scored> scored;
    scored.reserve(tool_info_cache_.size());

    for (auto& ti : tool_info_cache_) {
        std::string haystack;
        for (char c : ti.name) haystack += std::tolower(static_cast<unsigned char>(c));
        haystack += ' ';
        for (char c : ti.description) haystack += std::tolower(static_cast<unsigned char>(c));

        // Replace underscores with spaces so "play_on_spotify" matches "play" and "spotify"
        for (auto& c : haystack) if (c == '_') c = ' ';

        int score = 0;
        for (auto& w : words) {
            if (haystack.find(w) != std::string::npos) score++;
        }
        if (score > 0) scored.push_back({&ti, score});
    }

    if (scored.empty()) return "";

    std::sort(scored.begin(), scored.end(),
              [](const Scored& a, const Scored& b) { return a.score > b.score; });

    std::string hint = "[Relevant tools: ";
    int n = std::min(top_k, (int)scored.size());
    for (int i = 0; i < n; i++) {
        if (i > 0) hint += ", ";
        hint += scored[i].info->name;
    }
    hint += "]";
    return hint;
}

std::vector<ToolCall> ToolEngine::parse_tool_calls(const std::string& llm_output) const {
    // Delegate to ModelProfile for format-aware parsing (single source of truth)
    if (model_profile_) {
        return model_profile_->parse_tool_calls(llm_output);
    }

    // Fallback: use a default ChatML profile if no model profile is set
    ModelProfile fallback = ModelProfile::from_family(ModelFamily::CHATML);
    return fallback.parse_tool_calls(llm_output);
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
