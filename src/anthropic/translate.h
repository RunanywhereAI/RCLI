#ifndef RCLI_ANTHROPIC_TRANSLATE_H
#define RCLI_ANTHROPIC_TRANSLATE_H

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

/// The wire-format translation, with no sockets in it.
///
/// Separated from the server so the mapping can be tested by handing it JSON
/// and reading JSON back, which is the only part of this worth testing: the
/// HTTP plumbing is cpp-httplib's, and the interesting bugs are all in the
/// shapes.
namespace rcli::anthropic::translate {

using Json = nlohmann::json;

/// Anthropic Messages request -> OpenAI chat completion request.
///
/// `model` replaces whatever model the caller named, because Claude Code sends
/// its own model ids and the endpoint behind us has never heard of them.
Json RequestToOpenAI(const Json& anthropic, const std::string& model);

/// OpenAI chat completion -> a whole Anthropic message.
Json ResponseToAnthropic(const Json& openai, const std::string& model);

/// One OpenAI stream chunk, turned into the Anthropic SSE events it implies.
///
/// Anthropic's stream is a state machine — message_start, content_block_start,
/// deltas, content_block_stop, message_delta, message_stop — where OpenAI's is
/// a flat run of deltas. `state` carries what has already been emitted so the
/// opening events fire exactly once.
struct StreamState {
    bool opened = false;
    bool block_open = false;
    std::string message_id;
    std::string model;
    std::string stop_reason;
    int input_tokens = 0;
    int output_tokens = 0;
};

/// Returns the SSE text to write for `chunk`, or empty when it implies nothing.
std::string StreamChunkToAnthropic(const Json& chunk, StreamState* state);

/// The closing events, written once the upstream stream ends.
std::string StreamCloseToAnthropic(StreamState* state);

/// An Anthropic-shaped error body, so a failure reads as one to the client
/// rather than as a malformed message.
std::string ErrorBody(const std::string& type, const std::string& message);

}  // namespace rcli::anthropic::translate

#endif  // RCLI_ANTHROPIC_TRANSLATE_H
