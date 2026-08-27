#include "anthropic/translate.h"

#include <string>
#include <utility>

namespace rcli::anthropic::translate {
namespace {

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
        if (block.value("type", std::string()) == "text") {
            text += block.value("text", std::string());
        }
    }
    return text;
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
    openai["stream"] = anthropic.value("stream", false);

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
            messages.push_back({{"role", message.value("role", std::string("user"))},
                                {"content", FlattenContent(message.value("content", Json()))}});
        }
    }
    openai["messages"] = std::move(messages);

    // max_tokens is required by Anthropic and optional for OpenAI, so it always
    // has a value to carry across.
    if (anthropic.contains("max_tokens")) {
        openai["max_tokens"] = anthropic["max_tokens"];
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
    const Json message = choice.value("message", Json::object());
    const std::string text = message.value("content", std::string());

    Json out;
    out["id"] = openai.value("id", std::string("msg_rcli"));
    out["type"] = "message";
    out["role"] = "assistant";
    out["model"] = model;
    out["content"] = Json::array({Json{{"type", "text"}, {"text", text}}});
    const std::string stop = StopReason(choice.value("finish_reason", std::string()));
    out["stop_reason"] = stop.empty() ? Json(nullptr) : Json(stop);
    out["stop_sequence"] = nullptr;

    const Json usage = openai.value("usage", Json::object());
    out["usage"] = Json{{"input_tokens", usage.value("prompt_tokens", 0)},
                        {"output_tokens", usage.value("completion_tokens", 0)}};
    return out;
}

std::string StreamChunkToAnthropic(const Json& chunk, StreamState* state) {
    if (state == nullptr) {
        return {};
    }
    std::string out;

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

    const std::string finish = choice.contains("finish_reason") &&
                                       choice["finish_reason"].is_string()
                                   ? choice["finish_reason"].get<std::string>()
                                   : std::string();
    if (!finish.empty()) {
        state->stop_reason = StopReason(finish);
    }
    if (chunk.contains("usage") && chunk["usage"].is_object()) {
        state->input_tokens = chunk["usage"].value("prompt_tokens", state->input_tokens);
        state->output_tokens = chunk["usage"].value("completion_tokens", state->output_tokens);
    }

    const Json delta = choice.contains("delta") && choice["delta"].is_object()
                           ? choice["delta"]
                           : Json::object();
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
    if (state == nullptr || !state->opened) {
        return {};
    }
    std::string out;
    if (state->block_open) {
        state->block_open = false;
        out += Event("content_block_stop", Json{{"type", "content_block_stop"}, {"index", 0}});
    }
    const std::string stop = state->stop_reason.empty() ? "end_turn" : state->stop_reason;
    out += Event("message_delta",
                 Json{{"type", "message_delta"},
                      {"delta", Json{{"stop_reason", stop}, {"stop_sequence", nullptr}}},
                      {"usage", Json{{"output_tokens", state->output_tokens}}}});
    out += Event("message_stop", Json{{"type", "message_stop"}});
    return out;
}

std::string ErrorBody(const std::string& type, const std::string& message) {
    return Json{{"type", "error"}, {"error", Json{{"type", type}, {"message", message}}}}.dump();
}

}  // namespace rcli::anthropic::translate
