#include "engines/model_profile.h"
#include "core/log.h"
#include "llama.h"
#include <algorithm>
#include <cctype>
#include <string_view>

namespace rastack {

// ---------------------------------------------------------------------------
// Family presets
// ---------------------------------------------------------------------------

static ModelProfile make_qwen3() {
    ModelProfile p;
    p.family           = ModelFamily::QWEN3;
    p.family_name      = "Qwen3";
    p.msg_start        = "<|im_start|>";
    p.msg_end          = "<|im_end|>";
    p.role_separator   = "\n";
    p.no_think_suffix  = " /no_think";
    p.think_start      = "<think>";
    p.think_end        = "</think>";
    p.tool_call_start  = "<tool_call>";
    p.tool_call_end    = "</tool_call>";
    p.tool_call_json_format = true;
    p.tool_response_start = "<tool_response>";
    p.tool_response_end   = "</tool_response>";
    return p;
}

static ModelProfile make_lfm2() {
    ModelProfile p;
    p.family           = ModelFamily::LFM2;
    p.family_name      = "LFM2";
    p.bos_token        = "<|startoftext|>";
    p.msg_start        = "<|im_start|>";
    p.msg_end          = "<|im_end|>";
    p.role_separator   = "\n";
    p.no_think_suffix  = "";         // LFM2 doesn't use /no_think
    p.think_start      = "";
    p.think_end        = "";
    p.tool_call_start  = "<|tool_call_start|>";
    p.tool_call_end    = "<|tool_call_end|>";
    p.tool_call_json_format = false; // func(key="val") format
    p.tool_response_start = "<|tool_response_start|>";
    p.tool_response_end   = "<|tool_response_end|>";

    // Embed the LFM2.5 GGUF chat template so build_chat_prompt uses
    // llama_chat_apply_template — identical to the llama.cpp path.
    p.has_gguf_template = true;
    p.gguf_template =
        "{{- bos_token -}}"
        "{%- set keep_past_thinking = keep_past_thinking | default(false) -%}"
        "{%- set ns = namespace(system_prompt=\"\") -%}"
        "{%- if messages[0][\"role\"] == \"system\" -%}"
        "    {%- set ns.system_prompt = messages[0][\"content\"] -%}"
        "    {%- set messages = messages[1:] -%}"
        "{%- endif -%}"
        "{%- if tools -%}"
        "    {%- set ns.system_prompt = ns.system_prompt + (\"\\n\" if ns.system_prompt else \"\") + \"List of tools: [\" -%}"
        "    {%- for tool in tools -%}"
        "        {%- if tool is not string -%}"
        "            {%- set tool = tool | tojson -%}"
        "        {%- endif -%}"
        "        {%- set ns.system_prompt = ns.system_prompt + tool -%}"
        "        {%- if not loop.last -%}"
        "            {%- set ns.system_prompt = ns.system_prompt + \", \" -%}"
        "        {%- endif -%}"
        "    {%- endfor -%}"
        "    {%- set ns.system_prompt = ns.system_prompt + \"]\" -%}"
        "{%- endif -%}"
        "{%- if ns.system_prompt -%}"
        "    {{- \"<|im_start|>system\\n\" + ns.system_prompt + \"<|im_end|>\\n\" -}}"
        "{%- endif -%}"
        "{%- set ns.last_assistant_index = -1 -%}"
        "{%- for message in messages -%}"
        "    {%- if message[\"role\"] == \"assistant\" -%}"
        "        {%- set ns.last_assistant_index = loop.index0 -%}"
        "    {%- endif -%}"
        "{%- endfor -%}"
        "{%- for message in messages -%}"
        "    {{- \"<|im_start|>\" + message[\"role\"] + \"\\n\" -}}"
        "    {%- set content = message[\"content\"] -%}"
        "    {%- if content is not string -%}"
        "        {%- set content = content | tojson -%}"
        "    {%- endif -%}"
        "    {%- if message[\"role\"] == \"assistant\" and not keep_past_thinking and loop.index0 != ns.last_assistant_index -%}"
        "        {%- if \"</think>\" in content -%}"
        "            {%- set content = content.split(\"</think>\")[-1] | trim -%}"
        "        {%- endif -%}"
        "    {%- endif -%}"
        "    {{- content + \"<|im_end|>\\n\" -}}"
        "{%- endfor -%}"
        "{%- if add_generation_prompt -%}"
        "    {{- \"<|im_start|>assistant\\n\" -}}"
        "{%- endif -%}";

    return p;
}

static ModelProfile make_llama3() {
    ModelProfile p;
    p.family           = ModelFamily::LLAMA3;
    p.family_name      = "Llama3";
    p.msg_start        = "<|start_header_id|>";
    p.msg_end          = "<|eot_id|>";
    p.role_separator   = "<|end_header_id|>\n\n";
    p.no_think_suffix  = "";
    p.think_start      = "";
    p.think_end        = "";
    p.tool_call_start  = "<|python_tag|>";
    p.tool_call_end    = "<|eom_id|>";
    p.tool_call_json_format = true;
    p.tool_response_start = "<tool_response>";
    p.tool_response_end   = "</tool_response>";
    return p;
}

static ModelProfile make_gemma() {
    ModelProfile p;
    p.family           = ModelFamily::GEMMA;
    p.family_name      = "Gemma";
    p.msg_start        = "<start_of_turn>";
    p.msg_end          = "<end_of_turn>";
    p.role_separator   = "\n";
    p.no_think_suffix  = "";
    p.think_start      = "";
    p.think_end        = "";
    p.tool_call_start  = "```tool_call";
    p.tool_call_end    = "```";
    p.tool_call_json_format = true;
    p.tool_response_start = "<tool_response>";
    p.tool_response_end   = "</tool_response>";
    return p;
}

static ModelProfile make_mistral() {
    ModelProfile p;
    p.family           = ModelFamily::MISTRAL;
    p.family_name      = "Mistral";
    p.msg_start        = "[INST]";
    p.msg_end          = "[/INST]";
    p.role_separator   = " ";
    p.no_think_suffix  = "";
    p.think_start      = "";
    p.think_end        = "";
    p.tool_call_start  = "[TOOL_CALLS]";
    p.tool_call_end    = "";           // Mistral uses newline as end
    p.tool_call_json_format = true;
    p.tool_response_start = "[TOOL_RESULTS]";
    p.tool_response_end   = "[/TOOL_RESULTS]";
    return p;
}

static ModelProfile make_phi() {
    ModelProfile p;
    p.family           = ModelFamily::PHI;
    p.family_name      = "Phi";
    p.msg_start        = "<|";
    p.msg_end          = "<|end|>";
    p.role_separator   = "|>\n";
    p.no_think_suffix  = "";
    p.think_start      = "";
    p.think_end        = "";
    p.tool_call_start  = "<tool_call>";
    p.tool_call_end    = "</tool_call>";
    p.tool_call_json_format = true;
    p.tool_response_start = "<tool_response>";
    p.tool_response_end   = "</tool_response>";
    return p;
}

static ModelProfile make_chatml() {
    ModelProfile p;
    p.family           = ModelFamily::CHATML;
    p.family_name      = "ChatML";
    p.msg_start        = "<|im_start|>";
    p.msg_end          = "<|im_end|>";
    p.role_separator   = "\n";
    p.no_think_suffix  = "";
    p.think_start      = "";
    p.think_end        = "";
    p.tool_call_start  = "<tool_call>";
    p.tool_call_end    = "</tool_call>";
    p.tool_call_json_format = true;
    p.tool_response_start = "<tool_response>";
    p.tool_response_end   = "</tool_response>";
    return p;
}

ModelProfile ModelProfile::from_family(ModelFamily family) {
    switch (family) {
        case ModelFamily::QWEN3:   return make_qwen3();
        case ModelFamily::LFM2:    return make_lfm2();
        case ModelFamily::LLAMA3:  return make_llama3();
        case ModelFamily::GEMMA:   return make_gemma();
        case ModelFamily::MISTRAL: return make_mistral();
        case ModelFamily::PHI:     return make_phi();
        case ModelFamily::CHATML:  return make_chatml();
        case ModelFamily::UNKNOWN: return make_chatml(); // safe default
    }
    return make_chatml();
}

// ---------------------------------------------------------------------------
// Auto-detection
// ---------------------------------------------------------------------------

static std::string to_lower(const std::string& s) {
    std::string r = s;
    for (auto& c : r) c = std::tolower(static_cast<unsigned char>(c));
    return r;
}

ModelProfile ModelProfile::detect(const llama_model* model, const std::string& model_path) {
    ModelProfile profile;

    // 1. Try reading the GGUF-embedded chat template
    if (model) {
        const char* tmpl = llama_model_chat_template(model, nullptr);
        if (tmpl) {
            profile.gguf_template = tmpl;
            profile.has_gguf_template = true;
        }
    }

    // 2. Detect family from filename (most reliable for our use case)
    std::string path_lower = to_lower(model_path);

    if (path_lower.find("lfm2") != std::string::npos ||
        path_lower.find("liquid") != std::string::npos) {
        profile = make_lfm2();
    } else if (path_lower.find("qwen3") != std::string::npos ||
               path_lower.find("qwen2") != std::string::npos ||
               path_lower.find("qwen-") != std::string::npos) {
        profile = make_qwen3();
    } else if (path_lower.find("llama-3") != std::string::npos ||
               path_lower.find("llama3") != std::string::npos ||
               path_lower.find("meta-llama") != std::string::npos) {
        profile = make_llama3();
    } else if (path_lower.find("gemma") != std::string::npos) {
        profile = make_gemma();
    } else if (path_lower.find("mistral") != std::string::npos ||
               path_lower.find("mixtral") != std::string::npos) {
        profile = make_mistral();
    } else if (path_lower.find("phi-") != std::string::npos ||
               path_lower.find("phi3") != std::string::npos ||
               path_lower.find("phi4") != std::string::npos) {
        profile = make_phi();
    } else if (profile.has_gguf_template) {
        // 3. Fall back to GGUF template heuristics
        std::string tmpl_lower = to_lower(profile.gguf_template);
        if (tmpl_lower.find("<|im_start|>") != std::string::npos)
            profile = make_chatml();
        else if (tmpl_lower.find("<|start_header_id|>") != std::string::npos)
            profile = make_llama3();
        else if (tmpl_lower.find("<start_of_turn>") != std::string::npos)
            profile = make_gemma();
        else if (tmpl_lower.find("[inst]") != std::string::npos)
            profile = make_mistral();
        else
            profile = make_chatml();
    } else {
        // 4. Ultimate fallback: ChatML (most widely supported)
        profile = make_chatml();
    }

    // Preserve the GGUF template if we found one
    if (model) {
        const char* tmpl = llama_model_chat_template(model, nullptr);
        if (tmpl) {
            profile.gguf_template = tmpl;
            profile.has_gguf_template = true;
        }
    }

    LOG_INFO("ModelProfile", "Detected: %s (gguf_template: %s)",
             profile.family_name.c_str(),
             profile.has_gguf_template ? "yes" : "no");

    return profile;
}

// ---------------------------------------------------------------------------
// Prompt building — uses llama_chat_apply_template when available,
// falls back to manual formatting with family-specific tokens
// ---------------------------------------------------------------------------

static std::string apply_gguf_template(
    const std::string& tmpl,
    const std::vector<std::pair<std::string, std::string>>& messages,
    bool add_assistant)
{
    std::vector<llama_chat_message> msgs;
    msgs.reserve(messages.size());
    for (auto& [role, content] : messages) {
        msgs.push_back({role.c_str(), content.c_str()});
    }

    // First call to get required buffer size
    int32_t needed = llama_chat_apply_template(
        tmpl.c_str(), msgs.data(), msgs.size(), add_assistant, nullptr, 0);
    if (needed <= 0) return "";

    std::string buf(needed + 1, '\0');
    llama_chat_apply_template(
        tmpl.c_str(), msgs.data(), msgs.size(), add_assistant, buf.data(), buf.size());
    buf.resize(needed);
    return buf;
}

std::string ModelProfile::build_chat_prompt(
    const std::string& system_prompt,
    const std::vector<std::pair<std::string, std::string>>& history,
    const std::string& user_message) const
{
    // Try GGUF template first (most accurate)
    if (has_gguf_template) {
        std::vector<std::pair<std::string, std::string>> messages;
        if (!system_prompt.empty())
            messages.emplace_back("system", system_prompt);
        for (auto& h : history)
            messages.push_back(h);

        std::string user_msg = user_message + no_think_suffix;
        messages.emplace_back("user", user_msg);

        std::string result = apply_gguf_template(gguf_template, messages, true);
        if (!result.empty()) return result;
    }

    // Manual fallback using family-specific tokens
    std::string prompt;

    if (family == ModelFamily::LLAMA3) {
        // Llama 3 format
        prompt += "<|begin_of_text|>";
        if (!system_prompt.empty()) {
            prompt += "<|start_header_id|>system<|end_header_id|>\n\n";
            prompt += system_prompt + "<|eot_id|>";
        }
        for (auto& [role, content] : history) {
            prompt += "<|start_header_id|>" + role + "<|end_header_id|>\n\n";
            prompt += content + "<|eot_id|>";
        }
        prompt += "<|start_header_id|>user<|end_header_id|>\n\n";
        prompt += user_message + no_think_suffix + "<|eot_id|>";
        prompt += "<|start_header_id|>assistant<|end_header_id|>\n\n";
    } else if (family == ModelFamily::GEMMA) {
        if (!system_prompt.empty()) {
            prompt += "<start_of_turn>user\n[System: " + system_prompt + "]\n\n";
        } else {
            prompt += "<start_of_turn>user\n";
        }
        prompt += user_message + "<end_of_turn>\n";
        prompt += "<start_of_turn>model\n";
    } else if (family == ModelFamily::MISTRAL) {
        prompt += "[INST] ";
        if (!system_prompt.empty())
            prompt += system_prompt + "\n\n";
        prompt += user_message + " [/INST]";
    } else {
        // ChatML-family (Qwen3, LFM2, generic ChatML, Phi, etc.)
        if (!bos_token.empty()) prompt += bos_token;
        prompt += msg_start + "system" + role_separator + system_prompt + msg_end + "\n";
        for (auto& [role, content] : history) {
            prompt += msg_start + role + role_separator + content + msg_end + "\n";
        }
        prompt += msg_start + "user" + role_separator + user_message + no_think_suffix + msg_end + "\n";
        prompt += msg_start + "assistant" + role_separator;
    }

    return prompt;
}

std::string ModelProfile::build_system_prefix(const std::string& system_prompt) const {
    if (has_gguf_template) {
        std::vector<std::pair<std::string, std::string>> messages;
        messages.emplace_back("system", system_prompt);
        std::string result = apply_gguf_template(gguf_template, messages, false);
        if (!result.empty()) return result;
    }

    if (family == ModelFamily::LLAMA3)
        return "<|begin_of_text|><|start_header_id|>system<|end_header_id|>\n\n" + system_prompt + "<|eot_id|>";
    if (family == ModelFamily::GEMMA)
        return "";  // Gemma doesn't have a system role; it's prepended to user
    if (family == ModelFamily::MISTRAL)
        return "";  // Mistral v1 doesn't have a system role

    return bos_token + msg_start + "system" + role_separator + system_prompt + msg_end + "\n";
}

std::string ModelProfile::build_user_turn(const std::string& user_message) const {
    if (family == ModelFamily::LLAMA3) {
        return "<|start_header_id|>user<|end_header_id|>\n\n"
               + user_message + no_think_suffix + "<|eot_id|>"
               + "<|start_header_id|>assistant<|end_header_id|>\n\n";
    }
    if (family == ModelFamily::GEMMA) {
        return "<start_of_turn>user\n" + user_message + "<end_of_turn>\n<start_of_turn>model\n";
    }
    if (family == ModelFamily::MISTRAL) {
        return "[INST] " + user_message + " [/INST]";
    }

    return msg_start + "user" + role_separator + user_message + no_think_suffix + msg_end + "\n"
         + msg_start + "assistant" + role_separator;
}

std::string ModelProfile::build_tool_continuation(
    const std::string& system_prompt,
    const std::string& user_message,
    const std::string& assistant_tool_call_text,
    const std::string& tool_results_text) const
{
    // Wrap results in model-specific tool response tags
    std::string wrapped_results = tool_results_text;
    if (!tool_response_start.empty()) {
        wrapped_results = tool_response_start + tool_results_text + tool_response_end;
    }

    if (has_gguf_template) {
        std::vector<std::pair<std::string, std::string>> messages;
        if (!system_prompt.empty())
            messages.emplace_back("system", system_prompt);
        messages.emplace_back("user", user_message + no_think_suffix);
        messages.emplace_back("assistant", assistant_tool_call_text);
        messages.emplace_back("tool", wrapped_results);
        std::string result = apply_gguf_template(gguf_template, messages, true);
        if (!result.empty()) {
            if (!think_start.empty())
                result += think_start + "\n" + think_end + "\n";
            return result;
        }
    }

    // Manual fallback (ChatML family)
    std::string prompt;
    if (!bos_token.empty()) prompt += bos_token;
    prompt += msg_start + "system" + role_separator + system_prompt + msg_end + "\n";
    prompt += msg_start + "user" + role_separator + user_message + no_think_suffix + msg_end + "\n";
    prompt += msg_start + "assistant" + role_separator + assistant_tool_call_text + msg_end + "\n";
    prompt += msg_start + "tool" + role_separator + wrapped_results + msg_end + "\n";
    prompt += msg_start + "assistant" + role_separator;
    if (!think_start.empty())
        prompt += think_start + "\n" + think_end + "\n";
    return prompt;
}

std::string ModelProfile::build_tool_system_prompt(
    const std::string& base_system_prompt,
    const std::string& tool_defs_json) const
{
    std::string prompt = base_system_prompt;
    if (tool_defs_json.empty()) return prompt;

    if (family == ModelFamily::QWEN3) {
        // Matches Qwen3's training format: tools wrapped in <tools></tools> XML
        prompt += "\n\n# Tools\n\n"
                  "You may call one or more functions to assist with the user query.\n\n"
                  "You are provided with function signatures within <tools></tools> XML tags:\n"
                  "<tools>\n" + tool_defs_json + "\n</tools>\n\n"
                  "For each function call, return a json object with function name and arguments "
                  "within <tool_call></tool_call> XML tags:\n"
                  "<tool_call>\n"
                  "{\"name\": <function-name>, \"arguments\": <args-json-object>}\n"
                  "</tool_call>";
    } else if (family == ModelFamily::LFM2) {
        prompt += "\n\nYou have access to the following tools: "
                  "<|tool_list_start|>" + tool_defs_json + "<|tool_list_end|>\n\n"
                  "When the user asks you to perform an action, you MUST call the appropriate tool. "
                  "Use this exact format:\n"
                  "<|tool_call_start|>[function_name(arg1=\"value1\", arg2=\"value2\")]<|tool_call_end|>\n\n"
                  "Example: if the user says \"open Safari\", call open_app(app=\"Safari\").\n"
                  "If no tool is needed, respond conversationally.";
    } else {
        // Generic format using the profile's tool_call_start/end tokens
        prompt += "\n\n# Tools\n\nYou have these tools:\n" + tool_defs_json + "\n\n"
                  "To use a tool, respond EXACTLY like this:\n"
                  + tool_call_start + "\n{\"name\": \"tool_name\", \"arguments\": {\"key\": \"value\"}}\n"
                  + tool_call_end + "\n"
                  "Only call a tool when the user asks you to perform an action. "
                  "Output ONLY the tool call, nothing else.";
    }
    return prompt;
}

// ---------------------------------------------------------------------------
// Tool call parsing
// ---------------------------------------------------------------------------

// Parse LFM2 native: [func_name(key="val", key2="val2")]
static bool parse_lfm2_tool_call(const std::string& content, ToolCall& call) {
    std::string s = content;
    while (!s.empty() && (s.front() == '[' || s.front() == ' ')) s.erase(s.begin());
    while (!s.empty() && (s.back() == ']' || s.back() == ' ')) s.pop_back();

    auto paren = s.find('(');
    if (paren == std::string::npos) return false;

    call.name = s.substr(0, paren);
    while (!call.name.empty() && call.name.back() == ' ') call.name.pop_back();

    auto close_paren = s.rfind(')');
    if (close_paren == std::string::npos || close_paren <= paren) return false;

    std::string params_str = s.substr(paren + 1, close_paren - paren - 1);

    std::string json = "{";
    bool first = true;
    size_t pos = 0;
    while (pos < params_str.size()) {
        while (pos < params_str.size() && (params_str[pos] == ' ' || params_str[pos] == ','))
            pos++;
        if (pos >= params_str.size()) break;

        auto eq = params_str.find('=', pos);
        if (eq == std::string::npos) break;
        std::string key = params_str.substr(pos, eq - pos);
        while (!key.empty() && key.back() == ' ') key.pop_back();
        while (!key.empty() && key.front() == ' ') key.erase(key.begin());

        pos = eq + 1;
        while (pos < params_str.size() && params_str[pos] == ' ') pos++;

        std::string value;
        if (pos < params_str.size() && params_str[pos] == '"') {
            pos++;
            while (pos < params_str.size() && params_str[pos] != '"') {
                if (params_str[pos] == '\\' && pos + 1 < params_str.size()) {
                    value += params_str[pos + 1];
                    pos += 2;
                } else {
                    value += params_str[pos++];
                }
            }
            if (pos < params_str.size()) pos++;
        } else {
            auto comma = params_str.find(',', pos);
            if (comma == std::string::npos) comma = params_str.size();
            value = params_str.substr(pos, comma - pos);
            while (!value.empty() && value.back() == ' ') value.pop_back();
            pos = comma;
        }

        // Strip /no_think artifacts
        if (value.size() > 9 && value.substr(value.size() - 9) == "/no_think")
            value = value.substr(0, value.size() - 9);
        while (!value.empty() && (value.back() == ' ' || value.back() == '/'))
            value.pop_back();

        if (!first) json += ", ";
        // Escape quotes in value for valid JSON
        std::string escaped;
        for (char c : value) {
            if (c == '"') escaped += "\\\"";
            else if (c == '\\') escaped += "\\\\";
            else escaped += c;
        }
        json += "\"" + key + "\": \"" + escaped + "\"";
        first = false;
    }
    json += "}";
    call.arguments_json = json;
    return !call.name.empty();
}

// Parse JSON-based: {"name": "...", "arguments": {...}}
static bool parse_json_tool_call(const std::string& content, ToolCall& call) {
    auto name_pos = content.find("\"name\"");
    if (name_pos == std::string::npos) return false;

    auto colon = content.find(':', name_pos);
    auto q1 = content.find('"', colon + 1);
    auto q2 = content.find('"', q1 + 1);
    if (q1 == std::string::npos || q2 == std::string::npos) return false;
    call.name = content.substr(q1 + 1, q2 - q1 - 1);

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
    return !call.name.empty();
}

std::vector<ToolCall> ModelProfile::parse_tool_calls(const std::string& llm_output) const {
    std::vector<ToolCall> calls;

    // Helper: extract content between start/end tags.
    // If end tag is missing (common when llama.cpp consumes it as EOS), use rest of string.
    auto extract_between = [](const std::string& text, const std::string& start,
                              const std::string& end, size_t& pos) -> std::string {
        size_t cs = pos + start.length();
        size_t ce = end.empty() ? std::string::npos : text.find(end, cs);
        if (ce == std::string::npos) {
            pos = text.length();
            return text.substr(cs);
        }
        pos = ce + end.length();
        return text.substr(cs, ce - cs);
    };

    // Try the model's native tool-call tags first
    if (!tool_call_start.empty()) {
        size_t pos = 0;
        while ((pos = llm_output.find(tool_call_start, pos)) != std::string::npos) {
            std::string content = extract_between(llm_output, tool_call_start, tool_call_end, pos);
            ToolCall call;
            bool ok = tool_call_json_format
                ? parse_json_tool_call(content, call)
                : parse_lfm2_tool_call(content, call);
            if (ok) calls.push_back(std::move(call));
        }
        if (!calls.empty()) return calls;
    }

    // Fallback: try all known formats in case model used a different one
    // LFM2 format
    {
        std::string s1 = "<|tool_call_start|>", e1 = "<|tool_call_end|>";
        size_t pos = 0;
        while ((pos = llm_output.find(s1, pos)) != std::string::npos) {
            std::string content = extract_between(llm_output, s1, e1, pos);
            ToolCall call;
            if (parse_lfm2_tool_call(content, call))
                calls.push_back(std::move(call));
        }
        if (!calls.empty()) return calls;
    }

    // Qwen3 / ChatML format
    {
        std::string s2 = "<tool_call>", e2 = "</tool_call>";
        size_t pos = 0;
        while ((pos = llm_output.find(s2, pos)) != std::string::npos) {
            std::string content = extract_between(llm_output, s2, e2, pos);
            ToolCall call;
            if (parse_json_tool_call(content, call))
                calls.push_back(std::move(call));
        }
    }

    return calls;
}

// ---------------------------------------------------------------------------
// Output cleaning
// ---------------------------------------------------------------------------

std::string ModelProfile::clean_output(const std::string& raw) const {
    std::string r = raw;

    // Strip think blocks
    if (!think_start.empty()) {
        while (true) {
            auto a = r.find(think_start);
            if (a == std::string::npos) break;
            auto b = r.find(think_end, a);
            if (b == std::string::npos) { r.erase(a); break; }
            r.erase(a, b + think_end.length() - a);
        }
    }

    // Strip tool call blocks (both native and all known formats)
    auto strip_tag_blocks = [&](const std::string& start, const std::string& end) {
        if (start.empty()) return;
        while (true) {
            auto a = r.find(start);
            if (a == std::string::npos) break;
            auto b = end.empty() ? r.find('\n', a + start.length()) : r.find(end, a);
            if (b == std::string::npos) { r.erase(a); break; }
            r.erase(a, b + (end.empty() ? 1 : end.length()) - a);
        }
    };

    strip_tag_blocks(tool_call_start, tool_call_end);
    strip_tag_blocks("<|tool_call_start|>", "<|tool_call_end|>");
    strip_tag_blocks("<tool_call>", "</tool_call>");

    // Strip tool response wrappers — models sometimes echo these in summaries
    auto strip_wrapper_tags = [&](const std::string& start, const std::string& end) {
        if (start.empty()) return;
        size_t a = r.find(start);
        if (a != std::string::npos) {
            r.erase(a, start.length());
            size_t b = r.find(end);
            if (b != std::string::npos) r.erase(b, end.length());
        }
    };
    strip_wrapper_tags(tool_response_start, tool_response_end);
    strip_wrapper_tags("<tool_response>", "</tool_response>");
    strip_wrapper_tags("<|tool_response_start|>", "<|tool_response_end|>");

    // Strip /no_think artifacts
    size_t pos;
    while ((pos = r.find("/no_think")) != std::string::npos)
        r.erase(pos, 9);

    // Trim leading whitespace
    auto first = r.find_first_not_of(" \t\n\r");
    return (first == std::string::npos) ? "" : r.substr(first);
}

bool ModelProfile::should_suppress_token(
    const std::string& accumulated, bool& in_think, bool& in_tool) const
{
    // Only search the tail of the accumulated string to avoid O(N^2) over the full output.
    // Tags are short (<25 chars) and only appear at the point of new token emission.
    constexpr size_t TAIL_WINDOW = 80;
    size_t tail_start = (accumulated.size() > TAIL_WINDOW) ? accumulated.size() - TAIL_WINDOW : 0;
    auto tail = std::string_view(accumulated).substr(tail_start);

    if (!think_start.empty()) {
        if (!in_think && tail.find(think_start) != std::string_view::npos)
            in_think = true;
        if (in_think && tail.find(think_end) != std::string_view::npos)
            in_think = false;
    }

    auto check_tool = [&](const std::string& start, const std::string& end) {
        if (start.empty()) return;
        if (!in_tool && tail.find(start) != std::string_view::npos)
            in_tool = true;
        if (in_tool && !end.empty() && tail.find(end) != std::string_view::npos)
            in_tool = false;
    };

    check_tool(tool_call_start, tool_call_end);
    check_tool("<|tool_call_start|>", "<|tool_call_end|>");
    check_tool("<tool_call>", "</tool_call>");

    return in_think || in_tool;
}

} // namespace rastack
