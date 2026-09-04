#include "anthropic/messages.h"

#include <atomic>
#include <memory>
#include <string>
#include <thread>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include "anthropic/translate.h"
#include "io/output.h"

namespace wally::anthropic {
namespace {

using Json = nlohmann::json;

/// Split "http://host:port/v1" into the host root and the path prefix httplib
/// wants separately.
bool SplitBaseUrl(const std::string& base_url, std::string* origin, std::string* prefix) {
    const std::string scheme = base_url.rfind("https://", 0) == 0 ? "https://" : "http://";
    const size_t start = base_url.find(scheme);
    if (start != 0) {
        return false;
    }
    const size_t slash = base_url.find('/', scheme.size());
    if (slash == std::string::npos) {
        *origin = base_url;
        *prefix = "";
    } else {
        *origin = base_url.substr(0, slash);
        *prefix = base_url.substr(slash);
    }
    return !origin->empty();
}

struct Runtime {
    httplib::Server server;
    std::thread thread;
    std::string origin;
    std::string prefix;
    std::string api_key;
    std::string model;
    std::string advertised;
    bool verbose = false;
};

std::unique_ptr<Runtime> g_runtime;

void ApplyAuth(httplib::Client& client, const std::string& api_key) {
    if (!api_key.empty()) {
        client.set_bearer_token_auth(api_key);
    }
}

void HandleNonStreaming(Runtime& runtime, const Json& request, httplib::Response& response) {
    httplib::Client client(runtime.origin);
    client.set_read_timeout(600, 0);
    ApplyAuth(client, runtime.api_key);

    const Json upstream = translate::RequestToOpenAI(request, runtime.model);
    const httplib::Result reply =
        client.Post(runtime.prefix + "/chat/completions", upstream.dump(), "application/json");
    if (!reply || reply->status < 200 || reply->status >= 300) {
        response.status = reply ? reply->status : 502;
        response.set_content(
            translate::ErrorBody("api_error",
                                 reply ? reply->body : std::string("the model endpoint did not answer")),
            "application/json");
        return;
    }
    Json parsed;
    try {
        parsed = Json::parse(reply->body);
    } catch (const Json::exception& error) {
        response.status = 502;
        response.set_content(translate::ErrorBody("api_error", error.what()), "application/json");
        return;
    }
    std::string failure_type;
    std::string failure;
    if (translate::PayloadError(parsed, &failure_type, &failure)) {
        response.status = failure_type == "rate_limit_error" ? 429 : 502;
        response.set_content(translate::ErrorBody(failure_type, failure), "application/json");
        return;
    }
    response.set_content(translate::ResponseToAnthropic(parsed, runtime.model).dump(),
                         "application/json");
}

void HandleStreaming(Runtime& runtime, const Json& request, httplib::Response& response) {
    // The upstream body is built here rather than in the sink: the sink runs
    // after this function returns, and everything it touches has to outlive it.
    auto upstream = std::make_shared<std::string>(
        translate::RequestToOpenAI(request, runtime.model).dump());
    auto origin = std::make_shared<std::string>(runtime.origin);
    auto path = std::make_shared<std::string>(runtime.prefix + "/chat/completions");
    auto api_key = std::make_shared<std::string>(runtime.api_key);
    auto model = std::make_shared<std::string>(runtime.model);

    response.set_chunked_content_provider(
        "text/event-stream",
        [upstream, origin, path, api_key, model](size_t /*offset*/, httplib::DataSink& sink) {
            httplib::Client client(*origin);
            client.set_read_timeout(600, 0);
            ApplyAuth(client, *api_key);

            translate::StreamState state;
            state.model = *model;
            std::string pending;

            const httplib::Result reply = client.Post(
                *path, httplib::Headers(), *upstream, "application/json",
                [&](const char* data, size_t length) {
                    pending.append(data, length);
                    // SSE frames are separated by a blank line, and a chunk can
                    // split one in half, so only whole frames are consumed.
                    size_t split = 0;
                    while ((split = pending.find("\n\n")) != std::string::npos) {
                        const std::string frame = pending.substr(0, split);
                        pending.erase(0, split + 2);
                        const size_t field = frame.find("data:");
                        if (field == std::string::npos) {
                            continue;
                        }
                        std::string payload = frame.substr(field + 5);
                        while (!payload.empty() && (payload.front() == ' ' || payload.front() == '\r')) {
                            payload.erase(payload.begin());
                        }
                        if (payload == "[DONE]") {
                            continue;
                        }
                        Json chunk;
                        try {
                            chunk = Json::parse(payload);
                        } catch (const Json::exception&) {
                            continue;
                        }
                        std::string events;
                        try {
                            events = translate::StreamChunkToAnthropic(chunk, &state);
                        } catch (const std::exception&) {
                            // A chunk in a shape the mapping did not expect is
                            // a chunk to skip, not a reason to kill the run.
                            continue;
                        }
                        if (!events.empty() && !sink.write(events.data(), events.size())) {
                            return false;
                        }
                    }
                    return true;
                });

            if (!reply) {
                const std::string body =
                    "event: error\ndata: " +
                    translate::ErrorBody("api_error", "the model endpoint stopped answering") +
                    "\n\n";
                sink.write(body.data(), body.size());
                sink.done();
                return false;
            }
            try {
                const std::string closing = translate::StreamCloseToAnthropic(&state);
                if (!closing.empty()) {
                    sink.write(closing.data(), closing.size());
                }
            } catch (const std::exception&) {
                // Nothing useful left to say; ending the stream cleanly beats
                // aborting the process holding the reader's editor open.
            }
            sink.done();
            return true;
        });
}

}  // namespace

bool Start(const harness::Endpoint& upstream, const std::string& model, Shim* shim,
           bool verbose, const std::string& advertised) {
    if (shim == nullptr) {
        return false;
    }
    Stop(shim);

    auto runtime = std::make_unique<Runtime>();
    if (!SplitBaseUrl(upstream.base_url, &runtime->origin, &runtime->prefix)) {
        out::error_line("could not read the model endpoint: " + upstream.base_url);
        return false;
    }
    runtime->api_key = upstream.api_key;
    runtime->model = model;
    runtime->advertised = advertised.empty() ? model : advertised;
    runtime->verbose = verbose;

    Runtime* raw = runtime.get();
    raw->server.Post("/v1/messages", [raw](const httplib::Request& request,
                                           httplib::Response& response) {
        if (raw->verbose) {
            out::status_line("anthropic: POST /v1/messages, " +
                        std::to_string(request.body.size()) + " bytes");
        }
        Json parsed;
        try {
            parsed = Json::parse(request.body);
        } catch (const Json::exception& error) {
            response.status = 400;
            response.set_content(translate::ErrorBody("invalid_request_error", error.what()),
                                 "application/json");
            return;
        }
        try {
            if (parsed.value("stream", false)) {
                HandleStreaming(*raw, parsed, response);
            } else {
                HandleNonStreaming(*raw, parsed, response);
            }
        } catch (const std::exception& error) {
            // httplib does not catch, and an exception leaving here reaches
            // std::terminate: the editor's model call would abort wally.
            if (raw->verbose) {
                out::status_line(std::string("anthropic: request failed: ") + error.what());
            }
            response.status = 500;
            response.set_content(translate::ErrorBody("api_error", error.what()),
                                 "application/json");
        }
    });

    // Discovery, in Anthropic's shape rather than OpenAI's.
    //
    // Claude Desktop probes this before it will use a gateway at all, and an
    // OpenAI-shaped list fails it with "Gateway returned no usable models":
    // the entries need `display_name` and `created_at`, and the envelope needs
    // the paging fields, or nothing in the list counts as usable.
    raw->server.Get("/v1/models", [raw](const httplib::Request&, httplib::Response& response) {
        if (raw->verbose) {
            out::status_line("anthropic: GET /v1/models -> " + raw->advertised + " (serving " +
                        raw->model + ")");
        }
        // The shape claude.com/docs/third-party/claude-desktop documents for a
        // gateway, which is OpenAI's list envelope rather than Anthropic's.
        // Guessing the Anthropic shape here is what produced "Gateway returned
        // no usable models".
        const Json entry{{"id", raw->advertised}, {"object", "model"}};
        response.set_content(
            Json{{"object", "list"}, {"data", Json::array({entry})}}.dump(),
            "application/json");
    });

    // Claude Code probes this before it sends anything and treats a failure as
    // an endpoint that is not there. Answering it is what makes the translator
    // look like a gateway rather than a wrong address.
    raw->server.Get("/api/hello", [](const httplib::Request&, httplib::Response& response) {
        response.set_content(Json{{"ok", true}}.dump(), "application/json");
    });
    // httplib has no HEAD route, and Claude Code probes with HEAD, so it is
    // answered ahead of routing rather than left to fall through to the 404.
    raw->server.set_pre_routing_handler(
        [](const httplib::Request& request, httplib::Response& response) {
            if (request.method == "HEAD" && request.path == "/api/hello") {
                response.status = 200;
                return httplib::Server::HandlerResponse::Handled;
            }
            return httplib::Server::HandlerResponse::Unhandled;
        });

    // A route we do not translate should say so, not 404 into a silence the
    // reader has to guess at.
    raw->server.set_error_handler([raw](const httplib::Request& request,
                                        httplib::Response& response) {
        if (raw->verbose) {
            out::status_line("anthropic: " + request.method + " " + request.path + " -> " +
                        std::to_string(response.status));
        }
        if (response.body.empty()) {
            response.set_content(
                translate::ErrorBody("not_found_error",
                                     request.method + " " + request.path +
                                         " is not something wally translates"),
                "application/json");
        }
    });

    const int port = raw->server.bind_to_any_port("127.0.0.1");
    if (port <= 0) {
        out::error_line("could not open a port for the Anthropic translator");
        return false;
    }

    g_runtime = std::move(runtime);
    Runtime* started = g_runtime.get();
    started->thread = std::thread([started] { started->server.listen_after_bind(); });

    shim->base_url = "http://127.0.0.1:" + std::to_string(port);
    // Never the upstream key: the client only has to send something, and
    // handing it a real console token would put it in that process's
    // environment where it does not belong.
    shim->auth_token = "wally-local";
    shim->running = true;
    return true;
}

void Stop(Shim* shim) {
    if (g_runtime) {
        g_runtime->server.stop();
        if (g_runtime->thread.joinable()) {
            g_runtime->thread.join();
        }
        g_runtime.reset();
    }
    if (shim != nullptr) {
        shim->running = false;
        shim->base_url.clear();
        shim->auth_token.clear();
    }
}

}  // namespace wally::anthropic
