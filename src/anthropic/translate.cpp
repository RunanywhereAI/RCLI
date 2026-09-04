#include "anthropic/translate.h"

#include <string>
#include <utility>

namespace rcli::anthropic::translate {
namespace {

/// A string field, or empty when it is absent or is something else.
///
/// `Json::value` throws when the key is there holding another type, and a
/// translator that throws on one unexpected field fails the whole request. A
/// client sending a shape we did not anticipate should lose that field, not its
/// turn.
std::string Field(const Json& object, const char* key) {
    if (!object.is_object()) {
        return {};
    }
    const auto found = object.find(key);
    return found != object.end() && found->is_string() ? found->get<std::string>()
                                                       : std::string();
}

/// A token count, or `fallback` when the field is absent or is not a number.
int Count(const Json& object, const char* key, int fallback = 0) {
    if (!object.is_object()) {
        return fallback;
    }
    const auto found = object.find(key);
    return found != object.end() && found->is_number_integer() ? found->get<int>() : fallback;
}

/// Anthropic lets content be a bare string or a list of typed blocks. Both mean
/// the same thing to an OpenAI endpoint, which only takes a string.
std::string FlattenContent(const Json& content) {
    if (content.is_string()) {
        return content.get<std::string>();
    }
    if (!content.is_array()) {
        return {};
    }
    std::string text;
    for (const Json& block : content) {
        if (!block.is_object()) {
            continue;
        }
        // Only text survives the trip. An image block would have to become an
        // OpenAI image_url part, and the local server does not serve vision at
        // all, so dropping it is honest where inventing a shape is not.
        if (Field(block, "type") == "text") {
            text += Field(block, "text");
        }
    }
    return text;
}

/// OpenAI carries a call's arguments as a JSON string; Anthropic wants the
/// object itself. A model that emits something unparseable here is common
/// enough that dropping the whole turn over it would be worse than a call with
/// no arguments, which the tool can at least reject on its own terms.
Json ParseArguments(const std::string& arguments) {
    if (arguments.empty()) {
        return Json::object();
    }
    try {
        const Json parsed = Json::parse(arguments);
        if (parsed.is_object()) {
            return parsed;
        }
    } catch (const Json::exception&) {
    }
    return Json::object();
}

/// Anthropic tool definitions, in OpenAI's shape.
///
/// Only client tools carry an `input_schema`. Anthropic's server-side tools —
/// web search and the rest — name a type we have nothing to run and have no
/// schema to forward, so they are left out rather than passed on as something
/// the endpoint would have to invent a meaning for.
Json ToolsToOpenAI(const Json& tools) {
    Json out = Json::array();
    if (!tools.is_array()) {
        return out;
    }
    for (const Json& tool : tools) {
        if (!tool.is_object() || !tool.contains("input_schema")) {
            continue;
        }
        Json function{{"name", Field(tool, "name")},
                      {"parameters", tool["input_schema"]}};
        if (tool.contains("description") && tool["description"].is_string()) {
            function["description"] = tool["description"];
        }
        out.push_back(Json{{"type", "function"}, {"function", std::move(function)}});
    }
    return out;
}

/// Anthropic's tool_choice, in OpenAI's vocabulary. Null when it says something
/// OpenAI has no way to express.
Json ToolChoiceToOpenAI(const Json& choice) {
    if (choice.is_string()) {
        return choice;
    }
    if (!choice.is_object()) {
        return {};
    }
    const std::string type = Field(choice, "type");
    if (type == "auto" || type == "none") {
        return type;
    }
    // "any" means the model has to call something, without saying what.
    if (type == "any") {
        return "required";
    }
    if (type == "tool") {
        return Json{{"type", "function"},
                    {"function", Json{{"name", Field(choice, "name")}}}};
    }
    return {};
}

/// Appends `message` to `out` as the OpenAI messages it implies.
///
/// One Anthropic turn can become several. Anthropic packs the results of a
/// round of tool calls into the user turn that follows them, while OpenAI wants
/// each result as its own `tool` message sitting directly after the assistant
/// turn that asked for it — so the results are written first, and whatever text
/// shared that turn follows as a message of its own.
void AppendMessage(const Json& message, Json* out) {
    std::string role = Field(message, "role");
    if (role.empty()) {
        role = "user";
    }
    const Json content = message.contains("content") ? message["content"] : Json();

    if (!content.is_array()) {
        out->push_back(Json{{"role", role}, {"content", FlattenContent(content)}});
        return;
    }

    for (const Json& block : content) {
        if (!block.is_object() || Field(block, "type") != "tool_result") {
            continue;
        }
        out->push_back(Json{
            {"role", "tool"},
            {"tool_call_id", Field(block, "tool_use_id")},
            {"content", FlattenContent(block.contains("content") ? block["content"] : Json())}});
    }

    Json calls = Json::array();
    for (const Json& block : content) {
        if (!block.is_object() || Field(block, "type") != "tool_use") {
            continue;
        }
        const Json input = block.contains("input") ? block["input"] : Json::object();
        calls.push_back(Json{{"id", Field(block, "id")},
                             {"type", "function"},
                             {"function", Json{{"name", Field(block, "name")},
                                               {"arguments", input.dump()}}}});
    }

    const std::string text = FlattenContent(content);
    if (!calls.empty()) {
        // An assistant turn that only called tools has no text to carry, and
        // OpenAI reads a null content there rather than an empty string.
        out->push_back(Json{{"role", role},
                            {"content", text.empty() ? Json(nullptr) : Json(text)},
                            {"tool_calls", std::move(calls)}});
        return;
    }
    if (!text.empty()) {
        out->push_back(Json{{"role", role}, {"content", text}});
    }
}

/// The reason a turn carrying `calls` tool calls ended, given what the endpoint
/// said in `finish`.
///
/// Endpoints disagree here: one closes a turn holding a tool call with
/// "tool_calls", another with a plain "stop". The second reads as end_turn,
/// which tells the client to show the answer and wait for the reader rather
/// than run the tool — so the call is written, ignored, and the agent narrates
/// what it was about to do instead of doing it. Truncation is the one thing
/// that still outranks it, because a call cut off mid-argument cannot be run.
std::string StopWithTools(const std::string& finish, bool calls) {
    if (!calls || finish == "max_tokens") {
        return finish;
    }
    return "tool_use";
}

/// OpenAI finish reasons, in Anthropic's vocabulary.
std::string StopReason(const std::string& finish) {
    if (finish == "length") {
        return "max_tokens";
    }
    if (finish == "tool_calls") {
        return "tool_use";
    }
    if (finish.empty()) {
        return {};
    }
    return "end_turn";
}

std::string Event(const std::string& name, const Json& data) {
    return "event: " + name + "\ndata: " + data.dump() + "\n\n";
}

}  // namespace

Json RequestToOpenAI(const Json& anthropic, const std::string& model) {
    Json openai;
    openai["model"] = model;
    const auto stream = anthropic.find("stream");
    openai["stream"] = stream != anthropic.end() && stream->is_boolean() && stream->get<bool>();

    Json messages = Json::array();
    // Anthropic carries the system prompt beside the conversation; OpenAI wants
    // it as the first message, so it is moved rather than dropped.
    if (anthropic.contains("system")) {
        const std::string system = FlattenContent(anthropic["system"]);
        if (!system.empty()) {
            messages.push_back({{"role", "system"}, {"content", system}});
        }
    }
    if (anthropic.contains("messages") && anthropic["messages"].is_array()) {
        for (const Json& message : anthropic["messages"]) {
            if (!message.is_object()) {
                continue;
            }
            AppendMessage(message, &messages);
        }
    }
    openai["messages"] = std::move(messages);

    // max_tokens is required by Anthropic and optional for OpenAI, so it always
    // has a value to carry across.
    if (anthropic.contains("max_tokens")) {
        openai["max_tokens"] = anthropic["max_tokens"];
    }
    if (anthropic.contains("tools")) {
        Json tools = ToolsToOpenAI(anthropic["tools"]);
        // An empty list is not the same as none: OpenAI rejects `tools: []`,
        // and a request whose only tools were server-side ones has nothing left
        // to send.
        if (!tools.empty()) {
            openai["tools"] = std::move(tools);
            if (anthropic.contains("tool_choice")) {
                const Json& asked = anthropic["tool_choice"];
                const Json choice = ToolChoiceToOpenAI(asked);
                if (!choice.is_null()) {
                    openai["tool_choice"] = choice;
                }
                const auto serial = asked.is_object()
                                       ? asked.find("disable_parallel_tool_use")
                                       : asked.end();
                if (serial != asked.end() && serial->is_boolean() && serial->get<bool>()) {
                    openai["parallel_tool_calls"] = false;
                }
            }
        }
    }

    for (const char* passthrough : {"temperature", "top_p", "stop_sequences"}) {
        if (anthropic.contains(passthrough)) {
            // stop_sequences is OpenAI's `stop`; the rest keep their names.
            const std::string key =
                std::string(passthrough) == "stop_sequences" ? "stop" : passthrough;
            openai[key] = anthropic[passthrough];
        }
    }
    return openai;
}

Json ResponseToAnthropic(const Json& openai, const std::string& model) {
    Json choice;
    if (openai.contains("choices") && openai["choices"].is_array() &&
        !openai["choices"].empty()) {
        choice = openai["choices"][0];
    }
    const Json message = choice.contains("message") && choice["message"].is_object()
                             ? choice["message"]
                             : Json::object();
    // Null, not absent, is what OpenAI sends for the content of a turn that only
    // called tools, and asking for it as a string there throws.
    const std::string text = message.contains("content") && message["content"].is_string()
                                 ? message["content"].get<std::string>()
                                 : std::string();

    Json out;
    const std::string reply_id = Field(openai, "id");
    out["id"] = reply_id.empty() ? std::string("msg_rcli") : reply_id;
    out["type"] = "message";
    out["role"] = "assistant";
    out["model"] = model;
    Json content = Json::array();
    if (!text.empty()) {
        content.push_back(Json{{"type", "text"}, {"text", text}});
    }
    if (message.contains("tool_calls") && message["tool_calls"].is_array()) {
        for (const Json& call : message["tool_calls"]) {
            if (!call.is_object()) {
                continue;
            }
            const Json function = call.contains("function") && call["function"].is_object()
                                      ? call["function"]
                                      : Json::object();
            const std::string name = Field(function, "name");
            if (name.empty()) {
                continue;
            }
            content.push_back(
                Json{{"type", "tool_use"},
                     {"id", Field(call, "id")},
                     {"name", name},
                     {"input", ParseArguments(Field(function, "arguments"))}});
        }
    }
    const bool tool_called = content.size() > (text.empty() ? 0u : 1u);
    // A turn that said nothing still needs a block: an empty content array reads
    // to some clients as a malformed message rather than an empty one.
    if (content.empty()) {
        content.push_back(Json{{"type", "text"}, {"text", ""}});
    }
    out["content"] = std::move(content);
    const std::string stop =
        StopWithTools(StopReason(Field(choice, "finish_reason")), tool_called);
    out["stop_reason"] = stop.empty() ? Json(nullptr) : Json(stop);
    out["stop_sequence"] = nullptr;

    // Streaming endpoints send a null usage on most chunks, and asking a null
    // for a count throws.
    const Json usage = openai.contains("usage") && openai["usage"].is_object()
                           ? openai["usage"]
                           : Json::object();
    out["usage"] = Json{{"input_tokens", Count(usage, "prompt_tokens")},
                        {"output_tokens", Count(usage, "completion_tokens")}};
    return out;
}

std::string StreamChunkToAnthropic(const Json& chunk, StreamState* state) {
    if (state == nullptr) {
        return {};
    }
    std::string out;

    // An endpoint is free to answer 200 and then report the failure in the
    // stream, which is how the console reports a request over quota. Skipping
    // the frame as unrecognised ends the stream with no content, and a client
    // handed an empty turn sits there waiting rather than saying it was
    // refused.
    std::string failure_type;
    std::string failure;
    if (PayloadError(chunk, &failure_type, &failure)) {
        state->failed = true;
        return "event: error\ndata: " + ErrorBody(failure_type, failure) + "\n\n";
    }

    if (!state->opened) {
        state->opened = true;
        // `value` throws when the key is present with another type, and an id
        // of null is exactly what some servers send.
        state->message_id = chunk.contains("id") && chunk["id"].is_string()
                                ? chunk["id"].get<std::string>()
                                : std::string("msg_rcli");
        Json start;
        start["type"] = "message_start";
        start["message"] = Json{{"id", state->message_id},
                                {"type", "message"},
                                {"role", "assistant"},
                                {"model", state->model},
                                {"content", Json::array()},
                                {"stop_reason", nullptr},
                                {"stop_sequence", nullptr},
                                {"usage", Json{{"input_tokens", 0}, {"output_tokens", 0}}}};
        out += Event("message_start", start);
    }

    Json choice;
    if (chunk.contains("choices") && chunk["choices"].is_array() && !chunk["choices"].empty()) {
        choice = chunk["choices"][0];
    }

    const std::string finish = Field(choice, "finish_reason");
    if (!finish.empty()) {
        state->stop_reason = StopReason(finish);
    }
    if (chunk.contains("usage") && chunk["usage"].is_object()) {
        state->input_tokens = Count(chunk["usage"], "prompt_tokens", state->input_tokens);
        state->output_tokens = Count(chunk["usage"], "completion_tokens", state->output_tokens);
    }

    const Json delta = choice.contains("delta") && choice["delta"].is_object()
                           ? choice["delta"]
                           : Json::object();
    // Gathered rather than written straight through. A call arrives as
    // fragments scattered across the stream, and a provider is free to advance
    // two of them at once; writing as they land would interleave two half-built
    // blocks, which Anthropic's stream cannot express. They go out whole in
    // StreamCloseToAnthropic instead.
    if (delta.contains("tool_calls") && delta["tool_calls"].is_array()) {
        for (const Json& call : delta["tool_calls"]) {
            if (!call.is_object()) {
                continue;
            }
            const bool numbered = call.contains("index") && call["index"].is_number_integer();
            const int index = numbered ? call["index"].get<int>() : 0;
            const std::string id =
                call.contains("id") && call["id"].is_string() ? call["id"].get<std::string>()
                                                              : std::string();

            int slot = -1;
            if (numbered) {
                const auto found = state->tool_slot_by_index.find(index);
                if (found != state->tool_slot_by_index.end()) {
                    slot = found->second;
                }
            } else if (!id.empty()) {
                const auto found = state->tool_slot_by_id.find(id);
                if (found != state->tool_slot_by_id.end()) {
                    slot = found->second;
                }
            } else if (state->next_tool_slot > 0) {
                // Nothing to identify it by, so the only reading left is that it
                // carries on the call already being assembled.
                slot = state->next_tool_slot - 1;
            }
            if (slot < 0) {
                slot = state->next_tool_slot++;
            }
            if (numbered) {
                state->tool_slot_by_index[index] = slot;
            }
            if (!id.empty()) {
                state->tool_slot_by_id[id] = slot;
            }

            StreamState::ToolCall& pending = state->tool_calls[slot];
            if (!id.empty()) {
                pending.id = id;
            }
            const Json function = call.contains("function") && call["function"].is_object()
                                      ? call["function"]
                                      : Json::object();
            if (function.contains("name") && function["name"].is_string()) {
                pending.name = function["name"].get<std::string>();
            }
            if (function.contains("arguments") && function["arguments"].is_string()) {
                pending.arguments += function["arguments"].get<std::string>();
            }
        }
    }

    // content is null on the chunk that only carries a finish reason.
    const std::string text = delta.contains("content") && delta["content"].is_string()
                                 ? delta["content"].get<std::string>()
                                 : std::string();
    if (text.empty()) {
        return out;
    }

    // The block opens on the first token rather than up front: a stream that
    // only ever carries a finish reason should not announce a text block that
    // never gets one.
    if (!state->block_open) {
        state->block_open = true;
        out += Event("content_block_start",
                     Json{{"type", "content_block_start"},
                          {"index", 0},
                          {"content_block", Json{{"type", "text"}, {"text", ""}}}});
    }
    out += Event("content_block_delta",
                 Json{{"type", "content_block_delta"},
                      {"index", 0},
                      {"delta", Json{{"type", "text_delta"}, {"text", text}}}});
    return out;
}

std::string StreamCloseToAnthropic(StreamState* state) {
    if (state == nullptr || !state->opened || state->failed) {
        return {};
    }
    std::string out;
    // Blocks are numbered in the order they are written, and the text — if the
    // turn had any — is always the one that came first.
    int index = 0;
    if (state->block_open) {
        state->block_open = false;
        out += Event("content_block_stop", Json{{"type", "content_block_stop"}, {"index", 0}});
        index = 1;
    }
    for (const auto& entry : state->tool_calls) {
        const StreamState::ToolCall& call = entry.second;
        // A call nobody ever named cannot be run, and a block naming nothing is
        // worse for the client than a call it never hears about.
        if (call.name.empty()) {
            continue;
        }
        // The client matches a result back to its call by this id, so a call
        // the endpoint never named still needs one it can quote.
        const std::string id =
            call.id.empty() ? "tool_" + std::to_string(entry.first) : call.id;
        out += Event("content_block_start",
                     Json{{"type", "content_block_start"},
                          {"index", index},
                          {"content_block", Json{{"type", "tool_use"},
                                                 {"id", id},
                                                 {"name", call.name},
                                                 {"input", Json::object()}}}});
        out += Event("content_block_delta",
                     Json{{"type", "content_block_delta"},
                          {"index", index},
                          {"delta", Json{{"type", "input_json_delta"},
                                         {"partial_json", call.arguments.empty()
                                                              ? std::string("{}")
                                                              : call.arguments}}}});
        out += Event("content_block_stop",
                     Json{{"type", "content_block_stop"}, {"index", index}});
        ++index;
    }
    const std::string stop = StopWithTools(
        state->stop_reason.empty() ? std::string("end_turn") : state->stop_reason,
        !state->tool_calls.empty());
    out += Event("message_delta",
                 Json{{"type", "message_delta"},
                      {"delta", Json{{"stop_reason", stop}, {"stop_sequence", nullptr}}},
                      {"usage", Json{{"output_tokens", state->output_tokens}}}});
    out += Event("message_stop", Json{{"type", "message_stop"}});
    return out;
}

bool PayloadError(const Json& payload, std::string* type, std::string* message) {
    if (!payload.is_object() || !payload.contains("error") || payload["error"].is_null()) {
        return false;
    }
    const Json& error = payload["error"];
    std::string text =
        error.is_object() ? Field(error, "message") : error.dump();
    if (text.empty()) {
        text = "the model endpoint reported an error it did not describe";
    }
    if (message != nullptr) {
        *message = text;
    }
    if (type != nullptr) {
        // Worth telling apart: a client that knows it was rate limited can back
        // off and try again, where a plain api_error reads as a dead endpoint.
        *type = text.find("RESOURCE_EXHAUSTED") != std::string::npos ||
                        text.find("exceeded your current quota") != std::string::npos
                    ? "rate_limit_error"
                    : "api_error";
    }
    return true;
}

std::string ErrorBody(const std::string& type, const std::string& message) {
    return Json{{"type", "error"}, {"error", Json{{"type", type}, {"message", message}}}}.dump();
}

}  // namespace rcli::anthropic::translate
