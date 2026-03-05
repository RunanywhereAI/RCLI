#include "api/rcli_api.h"
#include "models/model_registry.h"
#include "models/tts_model_registry.h"
#include "models/stt_model_registry.h"
#include "pipeline/orchestrator.h"
#include "audio/audio_io.h"
#include "core/constants.h"
#include "core/log.h"
#include "bench/benchmark.h"
#include "engines/embedding_engine.h"
#include "rag/vector_index.h"
#include "rag/bm25_index.h"
#include "rag/hybrid_retriever.h"
#include "rag/document_processor.h"
#include "rag/index_builder.h"
#include "pipeline/text_sanitizer.h"
#include <atomic>
#include <cctype>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <mutex>
#include <thread>
#include <memory>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "actions/action_registry.h"
#include "actions/macos_actions.h"

using namespace rastack;

// Internal engine state behind the opaque handle
struct RCLIEngine {
    Orchestrator     pipeline;
    std::string      models_dir;

    rcli::ActionRegistry actions;

    // Config overrides from rcli_create() config_json
    std::string config_system_prompt;
    int config_gpu_layers = -1;
    int config_ctx_size   = -1;

    // Which offline STT backend is active
    bool using_parakeet = false;
    std::string llm_model_name = "Qwen3 0.6B";
    std::string tts_model_name = "Piper Lessac";
    std::string stt_model_name = "Whisper base.en";

    // RAG subsystem
    std::unique_ptr<EmbeddingEngine> rag_embedding;
    std::unique_ptr<HybridRetriever> rag_retriever;
    std::string rag_index_path;
    bool rag_ready = false;
    std::string last_rag_result;

    // TTS playback PID (for interruption)
    std::atomic<pid_t> tts_pid{0};

    // Response buffers (owned by engine, returned to callers)
    std::string last_transcript;
    std::string last_response;
    std::string last_action_result;
    std::string last_action_list;
    std::string last_info;

    // Callbacks
    RCLITranscriptCallback transcript_cb = nullptr;
    void* transcript_ud = nullptr;
    RCLIStateCallback state_cb = nullptr;
    void* state_ud = nullptr;
    RCLIActionCallback action_cb = nullptr;
    void* action_ud = nullptr;
    // Tool trace callback — separate from action_cb because action_cb only fires
    // for ActionRegistry actions, while trace covers both actions and built-in tools
    // (get_current_time, calculate), giving a complete picture of tool dispatch.
    RCLIToolTraceCallback tool_trace_cb = nullptr;
    void* tool_trace_ud = nullptr;

    std::atomic<float> audio_rms{0.0f};

    // Conversation memory (session-scoped, for multi-turn chat)
    std::vector<std::pair<std::string, std::string>> conversation_history;

    std::mutex mutex;
    bool initialized = false;
};

extern "C" {

// =============================================================================
// Lifecycle
// =============================================================================

// Simple JSON string value extractor (for config parsing)
static std::string config_get_string(const std::string& json, const std::string& key) {
    auto pos = json.find("\"" + key + "\"");
    if (pos == std::string::npos) return "";
    auto colon = json.find(':', pos);
    if (colon == std::string::npos) return "";
    auto q1 = json.find('"', colon + 1);
    if (q1 == std::string::npos) return "";
    auto q2 = json.find('"', q1 + 1);
    if (q2 == std::string::npos) return "";
    return json.substr(q1 + 1, q2 - q1 - 1);
}

static int config_get_int(const std::string& json, const std::string& key, int default_val) {
    auto pos = json.find("\"" + key + "\"");
    if (pos == std::string::npos) return default_val;
    auto colon = json.find(':', pos);
    if (colon == std::string::npos) return default_val;
    size_t start = colon + 1;
    while (start < json.size() && (json[start] == ' ' || json[start] == '\t')) start++;
    if (start >= json.size()) return default_val;
    return std::atoi(json.c_str() + start);
}

RCLIHandle rcli_create(const char* config_json) {
    auto* engine = new (std::nothrow) RCLIEngine();
    if (!engine) return nullptr;

    if (config_json && config_json[0]) {
        std::string cfg(config_json);
        std::string dir = config_get_string(cfg, "models_dir");
        if (!dir.empty()) engine->models_dir = dir;
        std::string prompt = config_get_string(cfg, "system_prompt");
        if (!prompt.empty()) engine->config_system_prompt = prompt;
        engine->config_gpu_layers = config_get_int(cfg, "gpu_layers", -1);
        engine->config_ctx_size = config_get_int(cfg, "ctx_size", -1);
    }

    engine->actions.register_defaults();

    return static_cast<RCLIHandle>(engine);
}

void rcli_destroy(RCLIHandle handle) {
    if (!handle) return;
    auto* engine = static_cast<RCLIEngine*>(handle);
    if (engine->initialized) {
        engine->pipeline.stop_live();
    }
    delete engine;
}

int rcli_init(RCLIHandle handle, const char* models_dir, int gpu_layers) {
    if (!handle || !models_dir) return -1;
    auto* engine = static_cast<RCLIEngine*>(handle);

    std::string dir(models_dir);
    engine->models_dir = dir;

    PipelineConfig config;

    // --- STT (Zipformer streaming — always active for live mic) ---
    config.stt.encoder_path = dir + "/zipformer/encoder-epoch-99-avg-1.int8.onnx";
    config.stt.decoder_path = dir + "/zipformer/decoder-epoch-99-avg-1.int8.onnx";
    config.stt.joiner_path  = dir + "/zipformer/joiner-epoch-99-avg-1.int8.onnx";
    config.stt.tokens_path  = dir + "/zipformer/tokens.txt";
    config.stt.sample_rate  = 16000;
    config.stt.num_threads  = 2;

    // --- Offline STT (resolve: user preference > auto-detect highest priority) ---
    {
        auto stt_models = rcli::all_stt_models();
        const auto* active_stt = rcli::resolve_active_stt(dir, stt_models);
        if (!active_stt) active_stt = rcli::get_default_offline_stt(stt_models);

        if (active_stt && active_stt->backend == "nemo_transducer") {
            std::string base = dir + "/" + active_stt->dir_name;
            config.offline_stt.backend = OfflineSttBackend::NEMO_TRANSDUCER;
            config.offline_stt.transducer_encoder_path = base + "/" + active_stt->encoder_file;
            config.offline_stt.transducer_decoder_path = base + "/" + active_stt->decoder_file;
            config.offline_stt.transducer_joiner_path  = base + "/" + active_stt->joiner_file;
            config.offline_stt.tokens_path  = base + "/" + active_stt->tokens_file;
            config.offline_stt.sample_rate  = 16000;
            config.offline_stt.num_threads  = 4;
            engine->using_parakeet = true;
            engine->stt_model_name = active_stt->name;
            LOG_DEBUG("RCLI", "Using %s for offline STT", active_stt->name.c_str());
        } else if (active_stt) {
            std::string base = dir + "/" + active_stt->dir_name;
            config.offline_stt.backend = OfflineSttBackend::WHISPER;
            config.offline_stt.encoder_path = base + "/" + active_stt->encoder_file;
            config.offline_stt.decoder_path = base + "/" + active_stt->decoder_file;
            config.offline_stt.tokens_path  = base + "/" + active_stt->tokens_file;
            config.offline_stt.language     = "en";
            config.offline_stt.task         = "transcribe";
            config.offline_stt.tail_paddings = 500;
            config.offline_stt.sample_rate  = 16000;
            config.offline_stt.num_threads  = 4;
            engine->using_parakeet = false;
            engine->stt_model_name = active_stt->name;
            LOG_DEBUG("RCLI", "Using %s for offline STT", active_stt->name.c_str());
        } else {
            config.offline_stt.backend = OfflineSttBackend::WHISPER;
            config.offline_stt.encoder_path = dir + "/whisper-base.en/base.en-encoder.int8.onnx";
            config.offline_stt.decoder_path = dir + "/whisper-base.en/base.en-decoder.int8.onnx";
            config.offline_stt.tokens_path  = dir + "/whisper-base.en/base.en-tokens.txt";
            config.offline_stt.language     = "en";
            config.offline_stt.task         = "transcribe";
            config.offline_stt.tail_paddings = 500;
            config.offline_stt.sample_rate  = 16000;
            config.offline_stt.num_threads  = 4;
            engine->using_parakeet = false;
        }
    }

    // --- LLM (resolve: user preference > auto-detect highest priority) ---
    {
        auto models = rcli::all_models();
        const auto* active = rcli::resolve_active_model(dir, models);
        if (active) {
            config.llm.model_path = dir + "/" + active->filename;
            engine->llm_model_name = active->name;
            LOG_DEBUG("RCLI", "Using %s for LLM (priority %d)",
                      active->name.c_str(), active->priority);
        } else {
            const auto* def = rcli::get_default_model(models);
            config.llm.model_path = dir + "/" + (def ? def->filename : "lfm2-1.2b-tool-q4_k_m.gguf");
            engine->llm_model_name = def ? def->name : "Liquid LFM2 1.2B Tool";
        }
    }
    config.llm.n_gpu_layers = (engine->config_gpu_layers >= 0) ? engine->config_gpu_layers : gpu_layers;
    config.llm.n_ctx        = (engine->config_ctx_size > 0) ? engine->config_ctx_size : 4096;
    config.llm.n_batch      = 512;
    config.llm.n_threads       = 1;
    config.llm.temperature  = 0.7f;
    config.llm.max_tokens   = 2048;
    config.llm.flash_attn   = true;
    config.llm.type_k       = 8;  // Q8_0 KV cache
    config.llm.type_v       = 8;

    config.llm.n_threads_batch = 8;

    // --- TTS ---
    {
        auto tts_models = rcli::all_tts_models();
        const auto* active_tts = rcli::resolve_active_tts(dir, tts_models);
        if (!active_tts) {
            active_tts = rcli::get_default_tts(tts_models);
        }
        if (active_tts) {
            std::string base = dir + "/" + active_tts->dir_name;
            config.tts.architecture     = active_tts->architecture;
            config.tts.model_path       = base + "/" + active_tts->model_file;
            config.tts.tokens_path      = base + "/" + active_tts->tokens_file;
            config.tts.data_dir         = dir + "/espeak-ng-data";
            if (!active_tts->config_file.empty())
                config.tts.model_config_path = base + "/" + active_tts->config_file;
            if (!active_tts->voices_file.empty())
                config.tts.voices_path = base + "/" + active_tts->voices_file;
            if (!active_tts->vocoder_file.empty())
                config.tts.vocoder_path = base + "/" + active_tts->vocoder_file;
            if (!active_tts->lexicon_file.empty())
                config.tts.lexicon_path = base + "/" + active_tts->lexicon_file;
            if (!active_tts->lang.empty())
                config.tts.lang = active_tts->lang;
            engine->tts_model_name = active_tts->name;
            LOG_DEBUG("RCLI", "Using TTS: %s (%s)", active_tts->name.c_str(),
                      active_tts->architecture.c_str());
        } else {
            config.tts.architecture      = "vits";
            config.tts.model_path        = dir + "/piper-voice/en_US-lessac-medium.onnx";
            config.tts.model_config_path = dir + "/piper-voice/en_US-lessac-medium.onnx.json";
            config.tts.tokens_path       = dir + "/piper-voice/tokens.txt";
            config.tts.data_dir          = dir + "/espeak-ng-data";
        }
        config.tts.num_threads = 2;
        config.tts.speed       = 1.1f;
    }

    // --- VAD ---
    config.vad.model_path           = dir + "/silero_vad.onnx";
    config.vad.threshold            = 0.5f;
    config.vad.min_silence_duration = 0.5f;
    config.vad.min_speech_duration  = 0.25f;
    config.vad.window_size          = 512;
    config.vad.sample_rate          = 16000;
    config.vad.num_threads          = 1;

    // --- Audio mode ---
    config.audio.capture_rate  = 16000;
    config.audio.playback_rate = 22050;
#if defined(RASTACK_FILE_AUDIO_ONLY)
    config.audio.mode = AudioMode::FILE_MODE;
#else
    config.audio.mode = AudioMode::LIVE_MODE;
#endif

    // --- System prompt (shared constant from core/constants.h) ---
    config.system_prompt = rastack::RCLI_SYSTEM_PROMPT;

    // Load user action preferences (enable/disable state)
    {
        std::string prefs_dir;
        if (const char* home = getenv("HOME"))
            prefs_dir = std::string(home) + "/.rcli";
        else
            prefs_dir = "/tmp/.rcli";
        struct stat st;
        if (stat(prefs_dir.c_str(), &st) != 0)
            mkdir(prefs_dir.c_str(), 0755);
        engine->actions.load_preferences(prefs_dir + "/actions.json");
    }

    // Register ALL actions as tool implementations (so any can be executed)
    for (auto& name : engine->actions.list_actions()) {
        engine->pipeline.tools().register_tool(name,
            [&engine_ref = *engine, name](const std::string& args) -> std::string {
                auto result = engine_ref.actions.execute(name, args);
                return result.raw_json;
            });
    }

    // Expose only enabled action definitions to the LLM
    engine->pipeline.tools().set_external_tool_definitions(
        engine->actions.get_definitions_json());

    LOG_DEBUG("RCLI", "Initializing pipeline...");
    if (!engine->pipeline.init(config)) {
        LOG_ERROR("RCLI", "Failed to initialize pipeline");
        return -1;
    }

    // Wire up state callback
    if (engine->state_cb) {
        engine->pipeline.set_state_callback(
            [engine](PipelineState old_s, PipelineState new_s) {
                engine->state_cb(static_cast<int>(old_s), static_cast<int>(new_s), engine->state_ud);
            });
    }

    // Wire up transcript callback (STT partials + finals in live mode)
    if (engine->transcript_cb) {
        engine->pipeline.set_transcript_callback(
            [engine](const std::string& text, bool is_final) {
                engine->last_transcript = text;
                engine->transcript_cb(text.c_str(), is_final ? 1 : 0, engine->transcript_ud);
            });
    }

    engine->initialized = true;
    LOG_DEBUG("RCLI", "Initialized with %d actions", engine->actions.num_actions());
    return 0;
}

int rcli_is_ready(RCLIHandle handle) {
    if (!handle) return 0;
    auto* engine = static_cast<RCLIEngine*>(handle);
    return engine->initialized ? 1 : 0;
}

// =============================================================================
// Live Voice Pipeline (macOS)
// =============================================================================

int rcli_start_listening(RCLIHandle handle) {
    if (!handle) return -1;
    auto* engine = static_cast<RCLIEngine*>(handle);
    if (!engine->initialized) return -1;
    return engine->pipeline.start_live() ? 0 : -1;
}

int rcli_stop_listening(RCLIHandle handle) {
    if (!handle) return -1;
    auto* engine = static_cast<RCLIEngine*>(handle);
    engine->pipeline.stop_live();
    return 0;
}

int rcli_start_capture(RCLIHandle handle) {
    if (!handle) return -1;
    auto* engine = static_cast<RCLIEngine*>(handle);
    if (!engine->initialized) return -1;
    return engine->pipeline.start_capture() ? 0 : -1;
}

const char* rcli_stop_capture_and_transcribe(RCLIHandle handle) {
    if (!handle) return "";
    auto* engine = static_cast<RCLIEngine*>(handle);
    engine->last_transcript = engine->pipeline.stop_capture_and_transcribe();
    return engine->last_transcript.c_str();
}

// Clean LLM output using the model's profile (strips think blocks, tool tokens, junk)
static std::string clean_llm_output(RCLIEngine* engine, const std::string& s) {
    return engine->pipeline.llm().profile().clean_output(s);
}

// Truncate conversation history to fit within a token budget.
// Walks backward from most recent, keeping complete user/assistant pairs.
static std::vector<std::pair<std::string, std::string>> truncate_history(
    RCLIEngine* engine,
    const std::vector<std::pair<std::string, std::string>>& history,
    int token_budget)
{
    if (history.empty() || token_budget <= 0) return {};

    std::vector<std::pair<std::string, std::string>> result;
    int total_tokens = 0;

    // Walk backward, keeping pairs (assistant then user going back)
    for (int i = static_cast<int>(history.size()) - 1; i >= 0; i--) {
        int entry_tokens = engine->pipeline.llm().count_tokens(
            history[i].first + ": " + history[i].second);
        if (total_tokens + entry_tokens > token_budget) break;
        total_tokens += entry_tokens;
        result.insert(result.begin(), history[i]);
    }
    return result;
}

// Compute trimmed history for a given user input and system prompt.
static std::vector<std::pair<std::string, std::string>> get_trimmed_history(
    RCLIEngine* engine,
    const std::string& system_prompt,
    const std::string& input)
{
    if (engine->conversation_history.empty()) return {};

    int ctx_size = engine->pipeline.llm().context_size();
    int system_tokens = engine->pipeline.llm().count_tokens(system_prompt);
    int user_tokens = engine->pipeline.llm().count_tokens(input);
    int history_budget = ctx_size - 512 - system_tokens - user_tokens - 50;

    return truncate_history(engine, engine->conversation_history,
                            std::max(0, history_budget));
}

// Cap conversation history to last N entries (must be even to keep pairs aligned)
static void cap_history(std::vector<std::pair<std::string, std::string>>& history,
                        size_t max_entries = 20)
{
    while (history.size() > max_entries) {
        history.erase(history.begin());
        if (!history.empty()) history.erase(history.begin());
    }
}

// Fallback: detect bare function calls like "func_name(arg=val)" without tool-call tags.
// Matches against known action names so we don't false-positive on normal text.
static std::vector<rastack::ToolCall> try_parse_bare_tool_calls(
    RCLIEngine* engine, const std::string& text)
{
    std::vector<rastack::ToolCall> calls;
    auto all_names = engine->actions.list_actions();
    for (auto& tool_name : engine->pipeline.tools().list_tool_names())
        all_names.push_back(tool_name);

    for (const auto& name : all_names) {
        std::string lower_text = text;
        for (auto& c : lower_text) c = std::tolower(static_cast<unsigned char>(c));
        std::string lower_name = name;
        for (auto& c : lower_name) c = std::tolower(static_cast<unsigned char>(c));

        size_t pos = lower_text.find(lower_name + "(");
        if (pos == std::string::npos) continue;
        if (pos > 0 && std::isalnum(static_cast<unsigned char>(text[pos - 1]))) continue;

        size_t paren_open = pos + name.size();
        int depth = 0;
        size_t paren_close = std::string::npos;
        for (size_t i = paren_open; i < text.size(); i++) {
            if (text[i] == '(') depth++;
            if (text[i] == ')') { depth--; if (depth == 0) { paren_close = i; break; } }
        }
        if (paren_close == std::string::npos) continue;

        std::string params_str = text.substr(paren_open + 1, paren_close - paren_open - 1);

        std::string json = "{";
        bool first = true;
        size_t p = 0;
        while (p < params_str.size()) {
            while (p < params_str.size() && (params_str[p] == ' ' || params_str[p] == ',')) p++;
            if (p >= params_str.size()) break;

            auto eq = params_str.find('=', p);
            if (eq == std::string::npos) break;
            std::string key = params_str.substr(p, eq - p);
            while (!key.empty() && key.back() == ' ') key.pop_back();
            while (!key.empty() && key.front() == ' ') key.erase(key.begin());

            p = eq + 1;
            while (p < params_str.size() && params_str[p] == ' ') p++;
            if (p >= params_str.size()) break;

            std::string val;
            if (params_str[p] == '"' || params_str[p] == '\'') {
                char q = params_str[p++];
                while (p < params_str.size() && params_str[p] != q) val += params_str[p++];
                if (p < params_str.size()) p++;
            } else {
                while (p < params_str.size() && params_str[p] != ',' && params_str[p] != ')')
                    val += params_str[p++];
                while (!val.empty() && val.back() == ' ') val.pop_back();
            }

            if (!first) json += ", ";
            json += "\"" + key + "\": \"" + val + "\"";
            first = false;
        }
        json += "}";

        rastack::ToolCall tc;
        tc.name = name;
        tc.arguments_json = json;
        calls.push_back(std::move(tc));
        break;
    }
    return calls;
}

const char* rcli_process_command(RCLIHandle handle, const char* text) {
    if (!handle || !text) return "";
    auto* engine = static_cast<RCLIEngine*>(handle);
    if (!engine->initialized) return "";

    std::lock_guard<std::mutex> lock(engine->mutex);
    std::string input(text);

    // === Single LLM-driven path: tool definitions in system prompt ===
    // Pre-filter tools by query relevance to avoid overwhelming small models
    std::string tool_defs = engine->actions.get_filtered_definitions_json(input, 10);
    std::string system_prompt = engine->pipeline.llm().profile().build_tool_system_prompt(
        rastack::RCLI_SYSTEM_PROMPT, tool_defs);

    auto history = get_trimmed_history(engine, system_prompt, input);
    std::string raw_llm_output = engine->pipeline.llm().generate(
        engine->pipeline.llm().build_chat_prompt(system_prompt, history, input),
        nullptr);

    // Parse tool calls from RAW output first (before cleaning strips the tags)
    auto tool_calls = engine->pipeline.llm().profile().parse_tool_calls(raw_llm_output);

    // Fallback: bare function-call detection only for LFM2 family (which uses func(k="v") format).
    // Qwen3 should always emit proper <tool_call> tags; bare matching causes false positives.
    if (tool_calls.empty() &&
        engine->pipeline.llm().profile().family == rastack::ModelFamily::LFM2) {
        tool_calls = try_parse_bare_tool_calls(engine, raw_llm_output);
    }

    std::string llm_output = clean_llm_output(engine, raw_llm_output);

    if (!tool_calls.empty()) {
        // Fire "detected" events before execution so the trace consumer sees what
        // the LLM decided to call, even if execution fails or the tool isn't found.
        // This two-phase approach (detected → result) lets the UI show a "calling..."
        // state before the potentially slow AppleScript execution completes.
        if (engine->tool_trace_cb) {
            for (auto& call : tool_calls) {
                engine->tool_trace_cb("detected", call.name.c_str(),
                    call.arguments_json.c_str(), 0, engine->tool_trace_ud);
            }
        }

        bool any_valid = false;
        std::string combined_response;
        for (auto& call : tool_calls) {
            const auto* def = engine->actions.get_def(call.name);
            if (def && engine->actions.is_enabled(call.name)) {
                any_valid = true;
                // Inlined execute_and_summarize() here so trace events can fire
                // between execution and the error-recovery LLM call.
                auto action_result = engine->actions.execute(call.name, call.arguments_json);

                if (engine->action_cb) {
                    engine->action_cb(call.name.c_str(), action_result.raw_json.c_str(),
                                      action_result.success ? 1 : 0, engine->action_ud);
                }

                if (engine->tool_trace_cb) {
                    engine->tool_trace_cb("result", call.name.c_str(),
                        action_result.raw_json.c_str(),
                        action_result.success ? 1 : 0, engine->tool_trace_ud);
                }

                if (action_result.success) {
                    combined_response += action_result.output;
                } else {
                    std::string summary = engine->pipeline.llm().generate(
                        engine->pipeline.llm().build_chat_prompt(
                            "You are RCLI, a macOS voice assistant. The user asked: \"" + input + "\". "
                            "You tried the action \"" + call.name + "\" but it failed with: " + action_result.error + "\n"
                            "Explain what went wrong in one short sentence and suggest what the user can do. "
                            "Do NOT say you can't do things. Do NOT output JSON. Do NOT use <think> tags.",
                            {}, ""),
                        nullptr);
                    combined_response += clean_llm_output(engine, summary);
                }
            } else if (engine->pipeline.tools().has_tool(call.name)) {
                any_valid = true;
                auto result = engine->pipeline.tools().execute(call);

                // Trace: notify built-in tool result
                if (engine->tool_trace_cb) {
                    engine->tool_trace_cb("result", call.name.c_str(),
                        result.result_json.c_str(),
                        result.success ? 1 : 0, engine->tool_trace_ud);
                }

                combined_response += result.success ? result.result_json : ("Error: " + result.result_json);
            }
            if (!combined_response.empty() && &call != &tool_calls.back()) combined_response += " ";
        }
        if (any_valid && !combined_response.empty()) {
            engine->last_response = combined_response;
            engine->conversation_history.emplace_back("user", input);
            engine->conversation_history.emplace_back("assistant", engine->last_response);
            cap_history(engine->conversation_history);
            return engine->last_response.c_str();
        }
    }

    // No valid tool calls — use LLM output as conversational response
    if (!llm_output.empty() && llm_output.find("<|tool_call") == std::string::npos) {
        engine->last_response = llm_output;
    } else {
        engine->last_response = engine->pipeline.llm().generate(
            engine->pipeline.llm().build_chat_prompt(
                "You are RCLI, a friendly macOS voice assistant. "
                "Keep responses brief and natural. Do NOT use <think> tags.",
                history, input),
            nullptr);
        engine->last_response = clean_llm_output(engine, engine->last_response);
    }

    engine->last_response = clean_llm_output(engine, engine->last_response);

    if (engine->last_response.empty()) {
        auto retry_history = get_trimmed_history(engine,
            "You are RCLI. Answer the user directly in one sentence. "
            "Do NOT use <think> tags.", input);
        std::string retry = engine->pipeline.llm().generate(
            engine->pipeline.llm().build_chat_prompt(
                "You are RCLI. Answer the user directly in one sentence. "
                "Do NOT use <think> tags.",
                retry_history, input + " Answer directly."),
            nullptr);
        engine->last_response = clean_llm_output(engine, retry);
    }

    if (!engine->last_response.empty()) {
        engine->conversation_history.emplace_back("user", input);
        engine->conversation_history.emplace_back("assistant", engine->last_response);
        cap_history(engine->conversation_history);
    }

    return engine->last_response.c_str();
}

int rcli_speak(RCLIHandle handle, const char* text) {
    if (!handle || !text) return -1;
    auto* engine = static_cast<RCLIEngine*>(handle);
    if (!engine->initialized) return -1;

    // Kill any currently playing TTS first
    rcli_stop_speaking(handle);

    std::string clean_text = rastack::sanitize_for_tts(std::string(text));
    if (clean_text.empty()) return 0;
    auto audio = engine->pipeline.tts().synthesize(clean_text);
    if (audio.empty()) return -1;

    int sample_rate = engine->pipeline.tts().sample_rate();

    std::string tmp_path = "/tmp/rcli_speak_" + std::to_string(getpid()) + ".wav";
    if (!AudioIO::save_wav(tmp_path, audio.data(), static_cast<int>(audio.size()), sample_rate)) {
        LOG_ERROR("RCLI", "Failed to write TTS audio to %s", tmp_path.c_str());
        return -1;
    }

    // Fork afplay so we get a PID we can kill for interruption
    pid_t pid = fork();
    if (pid == 0) {
        // Child: exec afplay, it will exit when done
        execl("/usr/bin/afplay", "afplay", tmp_path.c_str(), nullptr);
        _exit(127);
    } else if (pid > 0) {
        engine->tts_pid.store(pid, std::memory_order_release);
        // Reaper thread: wait for afplay to finish, then clean up
        std::thread([engine, pid, tmp_path]() {
            int status = 0;
            waitpid(pid, &status, 0);
            pid_t expected = pid;
            engine->tts_pid.compare_exchange_strong(expected, 0, std::memory_order_release);
            unlink(tmp_path.c_str());
        }).detach();
    }

    return 0;
}

void rcli_stop_speaking(RCLIHandle handle) {
    if (!handle) return;
    auto* engine = static_cast<RCLIEngine*>(handle);
    pid_t pid = engine->tts_pid.exchange(0, std::memory_order_acq_rel);
    if (pid > 0) {
        kill(pid, SIGTERM);
    }
}

int rcli_is_speaking(RCLIHandle handle) {
    if (!handle) return 0;
    auto* engine = static_cast<RCLIEngine*>(handle);
    return engine->tts_pid.load(std::memory_order_relaxed) > 0 ? 1 : 0;
}

void rcli_stop_processing(RCLIHandle handle) {
    if (!handle) return;
    auto* engine = static_cast<RCLIEngine*>(handle);
    if (engine->initialized) {
        engine->pipeline.llm().cancel();
    }
    rcli_stop_speaking(handle);
    rcli_stop_listening(handle);
}

void rcli_clear_history(RCLIHandle handle) {
    if (!handle) return;
    auto* engine = static_cast<RCLIEngine*>(handle);
    std::lock_guard<std::mutex> lock(engine->mutex);
    engine->conversation_history.clear();
}

const char* rcli_get_transcript(RCLIHandle handle) {
    if (!handle) return "";
    auto* engine = static_cast<RCLIEngine*>(handle);
    return engine->last_transcript.c_str();
}

// =============================================================================
// File Pipeline (iOS, testing)
// =============================================================================

int rcli_process_wav(RCLIHandle handle,
                          const char* input_wav,
                          const char* output_wav,
                          RCLIEventCallback callback,
                          void* user_data) {
    if (!handle || !input_wav || !output_wav) return -1;
    auto* engine = static_cast<RCLIEngine*>(handle);
    if (!engine->initialized) return -1;

    // Set state callback to forward events
    engine->pipeline.set_state_callback([callback, user_data](PipelineState /*old_state*/, PipelineState new_state) {
        if (!callback) return;
        const char* state_str = "unknown";
        switch (new_state) {
            case PipelineState::IDLE:       state_str = "idle"; break;
            case PipelineState::LISTENING:  state_str = "listening"; break;
            case PipelineState::PROCESSING: state_str = "processing"; break;
            case PipelineState::SPEAKING:   state_str = "speaking"; break;
            case PipelineState::INTERRUPTED: state_str = "interrupted"; break;
        }
        callback("state_change", state_str, user_data);
    });

    LOG_DEBUG("RCLI", "Processing: %s -> %s", input_wav, output_wav);

    bool ok = engine->pipeline.run_file_pipeline(input_wav, output_wav);

    // Send timings
    if (callback) {
        auto& t = engine->pipeline.last_timings();
        char buf[512];
        snprintf(buf, sizeof(buf),
                 "{\"stt_ms\":%.1f,\"llm_ttft_ms\":%.1f,\"llm_total_ms\":%.1f,"
                 "\"tts_first_ms\":%.1f,\"e2e_ms\":%.1f,\"total_ms\":%.1f}",
                 t.stt_latency_us / 1000.0,
                 t.llm_first_token_us / 1000.0,
                 t.llm_total_us / 1000.0,
                 t.tts_first_sentence_us / 1000.0,
                 t.e2e_latency_us / 1000.0,
                 t.total_us / 1000.0);
        callback("timings", buf, user_data);
    }

    engine->pipeline.set_state_callback(nullptr);
    return ok ? 0 : -1;
}

char* rcli_get_timings(RCLIHandle handle) {
    if (!handle) return nullptr;
    auto* engine = static_cast<RCLIEngine*>(handle);
    if (!engine->initialized) return nullptr;

    auto& t = engine->pipeline.last_timings();
    char buf[512];
    snprintf(buf, sizeof(buf),
             "{\"stt_ms\":%.1f,\"llm_ttft_ms\":%.1f,\"llm_total_ms\":%.1f,"
             "\"tts_first_ms\":%.1f,\"e2e_ms\":%.1f,\"total_ms\":%.1f}",
             t.stt_latency_us / 1000.0,
             t.llm_first_token_us / 1000.0,
             t.llm_total_us / 1000.0,
             t.tts_first_sentence_us / 1000.0,
             t.e2e_latency_us / 1000.0,
             t.total_us / 1000.0);

    return strdup(buf);
}

// =============================================================================
// Benchmark
// =============================================================================

int rcli_benchmark(RCLIHandle handle,
                        const char* test_wav,
                        int iterations,
                        RCLIEventCallback callback,
                        void* user_data) {
    if (!handle || !test_wav || iterations <= 0) return -1;
    auto* engine = static_cast<RCLIEngine*>(handle);
    if (!engine->initialized) return -1;

    std::string tmp_out = engine->models_dir + "/../tmp/bench_out.wav";

    // Accumulators for min/max/sum
    struct Metric { double min_v, max_v, sum; int count; };
    auto make_metric = []() -> Metric { return {1e9, 0, 0, 0}; };
    auto add_sample = [](Metric& m, double v) {
        if (v < m.min_v) m.min_v = v;
        if (v > m.max_v) m.max_v = v;
        m.sum += v;
        m.count++;
    };

    Metric m_stt = make_metric(), m_ttft = make_metric(), m_llm = make_metric();
    Metric m_tts = make_metric(), m_e2e = make_metric(), m_total = make_metric();

    for (int i = 0; i < iterations; i++) {
        if (callback) {
            char prog[64];
            snprintf(prog, sizeof(prog), "%d/%d", i + 1, iterations);
            callback("benchmark_progress", prog, user_data);
        }

        bool ok = engine->pipeline.run_file_pipeline(test_wav, tmp_out);
        if (!ok) {
            fprintf(stderr, "[Benchmark] Run %d/%d failed\n", i + 1, iterations);
            continue;
        }

        auto& t = engine->pipeline.last_timings();
        double stt   = t.stt_latency_us / 1000.0;
        double ttft  = t.llm_first_token_us / 1000.0;
        double llm   = t.llm_total_us / 1000.0;
        double tts   = t.tts_first_sentence_us / 1000.0;
        double e2e   = t.e2e_latency_us / 1000.0;
        double total = t.total_us / 1000.0;

        add_sample(m_stt, stt);
        add_sample(m_ttft, ttft);
        add_sample(m_llm, llm);
        add_sample(m_tts, tts);
        add_sample(m_e2e, e2e);
        add_sample(m_total, total);

        if (callback) {
            char buf[512];
            snprintf(buf, sizeof(buf),
                     "{\"run\":%d,\"stt_ms\":%.1f,\"llm_ttft_ms\":%.1f,\"llm_total_ms\":%.1f,"
                     "\"tts_first_ms\":%.1f,\"e2e_ms\":%.1f,\"total_ms\":%.1f}",
                     i + 1, stt, ttft, llm, tts, e2e, total);
            callback("benchmark_run", buf, user_data);
        }
    }

    // Send aggregate results
    if (callback && m_stt.count > 0) {
        int n = m_stt.count;
        char buf[2048];
        snprintf(buf, sizeof(buf),
                 "{\"iterations\":%d,"
                 "\"stt_ms\":{\"min\":%.1f,\"avg\":%.1f,\"max\":%.1f},"
                 "\"llm_ttft_ms\":{\"min\":%.1f,\"avg\":%.1f,\"max\":%.1f},"
                 "\"llm_total_ms\":{\"min\":%.1f,\"avg\":%.1f,\"max\":%.1f},"
                 "\"tts_first_ms\":{\"min\":%.1f,\"avg\":%.1f,\"max\":%.1f},"
                 "\"e2e_ms\":{\"min\":%.1f,\"avg\":%.1f,\"max\":%.1f},"
                 "\"total_ms\":{\"min\":%.1f,\"avg\":%.1f,\"max\":%.1f}}",
                 n,
                 m_stt.min_v, m_stt.sum/n, m_stt.max_v,
                 m_ttft.min_v, m_ttft.sum/n, m_ttft.max_v,
                 m_llm.min_v, m_llm.sum/n, m_llm.max_v,
                 m_tts.min_v, m_tts.sum/n, m_tts.max_v,
                 m_e2e.min_v, m_e2e.sum/n, m_e2e.max_v,
                 m_total.min_v, m_total.sum/n, m_total.max_v);
        callback("benchmark_result", buf, user_data);
    }

    return 0;
}

// =============================================================================
// Full Benchmark Suite
// =============================================================================

struct BenchSample {
    std::string file;
    std::string transcript;
    std::string category;
};

static std::vector<BenchSample> get_bench_samples() {
    return {
        {"sample_short.wav",    "Open Safari for me.",
         "short_command"},
        {"sample_medium.wav",   "Can you tell me what the weather is like in San Francisco today?",
         "question"},
        {"sample_long.wav",     "Create a new note called Project Ideas with the following content we need to review the design documents and schedule a meeting for next Thursday.",
         "long_command"},
        {"sample_question.wav", "What is the capital of France and what language do they speak there?",
         "factual"},
        {"sample_action.wav",   "Set my volume to fifty percent and then open the calculator app.",
         "multi_action"},
    };
}

static bool generate_bench_samples(const std::string& models_dir, TtsEngine& tts) {
    std::string dir = models_dir + "/bench-samples";
    struct stat st;
    if (stat(dir.c_str(), &st) == 0) return true;

    std::string cmd = "mkdir -p '" + dir + "'";
    system(cmd.c_str());

    auto samples = get_bench_samples();
    int sr = tts.sample_rate();

    for (auto& s : samples) {
        std::string path = dir + "/" + s.file;
        auto audio = tts.synthesize(s.transcript);
        if (audio.empty()) {
            fprintf(stderr, "  Warning: TTS failed for \"%s\"\n", s.transcript.substr(0, 40).c_str());
            continue;
        }
        AudioIO::save_wav(path, audio.data(), (int)audio.size(), sr);
    }

    // Write manifest
    FILE* mf = fopen((dir + "/manifest.json").c_str(), "w");
    if (mf) {
        fprintf(mf, "{\n  \"samples\": [\n");
        for (size_t i = 0; i < samples.size(); i++) {
            fprintf(mf, "    {\"file\": \"%s\", \"transcript\": \"%s\", \"category\": \"%s\"}%s\n",
                    samples[i].file.c_str(), samples[i].transcript.c_str(),
                    samples[i].category.c_str(), i + 1 < samples.size() ? "," : "");
        }
        fprintf(mf, "  ]\n}\n");
        fclose(mf);
    }

    return true;
}

static int word_error_rate_percent(const std::string& reference, const std::string& hypothesis) {
    auto tokenize = [](const std::string& s) {
        std::vector<std::string> words;
        std::string w;
        for (char c : s) {
            if (c == ' ' || c == '\t' || c == '\n') {
                if (!w.empty()) { words.push_back(w); w.clear(); }
            } else {
                w += (char)tolower((unsigned char)c);
            }
        }
        if (!w.empty()) words.push_back(w);
        return words;
    };

    auto ref = tokenize(reference);
    auto hyp = tokenize(hypothesis);
    int n = (int)ref.size(), m = (int)hyp.size();
    if (n == 0) return m == 0 ? 0 : 100;

    std::vector<std::vector<int>> dp(n + 1, std::vector<int>(m + 1, 0));
    for (int i = 0; i <= n; i++) dp[i][0] = i;
    for (int j = 0; j <= m; j++) dp[0][j] = j;
    for (int i = 1; i <= n; i++) {
        for (int j = 1; j <= m; j++) {
            int cost = (ref[i - 1] == hyp[j - 1]) ? 0 : 1;
            dp[i][j] = std::min({dp[i - 1][j] + 1, dp[i][j - 1] + 1, dp[i - 1][j - 1] + cost});
        }
    }
    return (int)(dp[n][m] * 100.0 / n + 0.5);
}

static bool suite_requested(const std::string& suite, const std::string& name) {
    if (suite == "all") return true;
    if (suite == name) return true;
    if (suite.find(name) != std::string::npos) return true;
    return false;
}

int rcli_run_full_benchmark(RCLIHandle handle, const char* suite_str,
                                  int runs, const char* output_json) {
    if (!handle) return -1;
    auto* engine = static_cast<RCLIEngine*>(handle);
    if (!engine->initialized) return -1;

    std::string suite = suite_str ? suite_str : "all";
    if (runs <= 0) runs = 3;

    std::string samples_dir = engine->models_dir + "/bench-samples";

    fprintf(stderr, "\n  Generating bench samples...");
    fflush(stderr);
    generate_bench_samples(engine->models_dir, engine->pipeline.tts());
    fprintf(stderr, " done.\n");

    auto samples = get_bench_samples();
    std::string primary_wav = samples_dir + "/" + samples[0].file;

    Benchmark bench(engine->pipeline);

    // ── STT ────────────────────────────────────────────────
    if (suite_requested(suite, "stt")) {
        fprintf(stderr, "\n  \033[1m\033[36m── STT Benchmark ──\033[0m\n");
        bench.bench_stt(primary_wav);

        fprintf(stderr, "\n  \033[1m\033[36m── STT Accuracy (WER) ──\033[0m\n");
        for (auto& s : samples) {
            std::string wav_path = samples_dir + "/" + s.file;
            auto audio = AudioIO::load_wav_to_vec(wav_path, 16000);
            if (audio.empty()) continue;

            std::string hyp = engine->pipeline.offline_stt().transcribe(
                audio.data(), (int)audio.size());

            int wer = word_error_rate_percent(s.transcript, hyp);
            fprintf(stderr, "    %-18s WER: %3d%%  \"%s\"\n",
                    s.category.c_str(), wer, hyp.substr(0, 50).c_str());
        }
    }

    // ── LLM ────────────────────────────────────────────────
    if (suite_requested(suite, "llm")) {
        fprintf(stderr, "\n  \033[1m\033[36m── LLM Benchmark ──\033[0m\n");
        bench.bench_llm();
        bench.bench_tool_calling();
    }

    // ── TTS ────────────────────────────────────────────────
    if (suite_requested(suite, "tts")) {
        fprintf(stderr, "\n  \033[1m\033[36m── TTS Benchmark ──\033[0m\n");
        bench.bench_tts();
    }

    // ── E2E ────────────────────────────────────────────────
    if (suite_requested(suite, "e2e")) {
        fprintf(stderr, "\n  \033[1m\033[36m── E2E Pipeline Benchmark ──\033[0m\n");
        bench.bench_e2e(primary_wav);
        bench.bench_e2e_long();
    }

    // ── Tools / Actions ────────────────────────────────────
    if (suite_requested(suite, "tools") || suite_requested(suite, "actions")) {
        fprintf(stderr, "\n  \033[1m\033[36m── Actions Info ──\033[0m\n");
        fprintf(stderr, "    Registered: %d actions (%d enabled for LLM)\n",
                engine->actions.num_actions(), engine->actions.num_enabled());
        fprintf(stderr, "    Tool routing: fully LLM-driven (no keyword heuristics)\n");
        auto enabled = engine->actions.list_enabled_actions();
        fprintf(stderr, "    Enabled:");
        for (size_t i = 0; i < enabled.size() && i < 10; i++)
            fprintf(stderr, " %s", enabled[i].c_str());
        if (enabled.size() > 10) fprintf(stderr, " ...(+%d more)", (int)enabled.size() - 10);
        fprintf(stderr, "\n");
    }

    // ── RAG ────────────────────────────────────────────────
    if (suite_requested(suite, "rag") && engine->rag_ready) {
        fprintf(stderr, "\n  \033[1m\033[36m── RAG Benchmark ──\033[0m\n");

        std::string test_query = "What were the key decisions from the meeting?";

        // Embedding latency
        std::vector<double> embed_times;
        for (int r = 0; r < runs; r++) {
            int64_t t0 = now_us();
            auto emb = engine->rag_embedding->embed(test_query);
            int64_t t1 = now_us();
            embed_times.push_back((t1 - t0) / 1000.0);
        }
        double avg_embed = 0;
        for (auto t : embed_times) avg_embed += t;
        avg_embed /= embed_times.size();
        fprintf(stderr, "    Embedding latency (avg):  %.1f ms\n", avg_embed);

        // Retrieval latency
        auto query_emb = engine->rag_embedding->embed(test_query);
        std::vector<double> retrieve_times;
        for (int r = 0; r < runs; r++) {
            int64_t t0 = now_us();
            auto results = engine->rag_retriever->retrieve(test_query, query_emb, 5);
            int64_t t1 = now_us();
            retrieve_times.push_back((t1 - t0) / 1000.0);
        }
        double avg_retrieve = 0;
        for (auto t : retrieve_times) avg_retrieve += t;
        avg_retrieve /= retrieve_times.size();
        fprintf(stderr, "    Retrieval latency (avg):  %.1f ms (%d results)\n",
                avg_retrieve, 5);

        // Full RAG query (embedding + retrieval + LLM)
        std::vector<double> rag_times;
        for (int r = 0; r < runs; r++) {
            int64_t t0 = now_us();
            rcli_rag_query(handle, test_query.c_str());
            int64_t t1 = now_us();
            rag_times.push_back((t1 - t0) / 1000.0);
        }
        double avg_rag = 0;
        for (auto t : rag_times) avg_rag += t;
        avg_rag /= rag_times.size();
        fprintf(stderr, "    Full RAG query (avg):     %.1f ms\n", avg_rag);
    } else if (suite_requested(suite, "rag") && !engine->rag_ready) {
        fprintf(stderr, "\n  \033[33mSkipping RAG benchmark — no index loaded. Use --rag <index>\033[0m\n");
    }

    // ── Memory ─────────────────────────────────────────────
    if (suite_requested(suite, "memory")) {
        fprintf(stderr, "\n  \033[1m\033[36m── Memory Benchmark ──\033[0m\n");
        bench.bench_memory();
    }

    // ── Results table ──────────────────────────────────────
    bench.print_results();

    // ── JSON export ────────────────────────────────────────
    if (output_json) {
        FILE* f = fopen(output_json, "w");
        if (f) {
            std::string json = bench.to_json();
            fwrite(json.data(), 1, json.size(), f);
            fclose(f);
            fprintf(stderr, "  Results saved to: %s\n\n", output_json);
        } else {
            fprintf(stderr, "  Error: Could not write to %s\n\n", output_json);
        }
    }

    return 0;
}

// =============================================================================
// RAG
// =============================================================================

int rcli_rag_ingest(RCLIHandle handle, const char* dir_path) {
    if (!handle || !dir_path) return -1;
    auto* engine = static_cast<RCLIEngine*>(handle);
    if (!engine->initialized) return -1;

    std::string index_path = engine->rag_index_path;
    if (index_path.empty()) {
        if (const char* home = getenv("HOME"))
            index_path = std::string(home) + "/Library/RCLI/index";
        else
            index_path = "/tmp/rcli_index";
    }

    std::string embedding_model = engine->models_dir + "/snowflake-arctic-embed-s-q8_0.gguf";
    LOG_DEBUG("RCLI", "RAG ingest: %s -> %s", dir_path, index_path.c_str());

    EmbeddingConfig embed_cfg;
    embed_cfg.model_path = embedding_model;
    embed_cfg.n_gpu_layers = 99;
    embed_cfg.n_threads = 2;
    embed_cfg.n_batch = 512;
    embed_cfg.embedding_dim = 384;

    ProcessorConfig proc_cfg;
    proc_cfg.max_chunk_tokens = 512;
    proc_cfg.overlap_tokens = 50;

    IndexBuilder builder;
    IndexBuilder::BuildConfig build_cfg;
    build_cfg.docs_path = dir_path;
    build_cfg.output_path = index_path;
    build_cfg.embed_config = embed_cfg;
    build_cfg.proc_config = proc_cfg;
    build_cfg.batch_size = 32;

    bool ok = builder.build(build_cfg, [](int done, int total) {
        int bar_width = 30;
        float pct = total > 0 ? (float)done / total : 0.0f;
        int filled = (int)(pct * bar_width);
        fprintf(stderr, "\r  ");
        for (int i = 0; i < bar_width; i++)
            fprintf(stderr, "%s", i < filled ? "\xe2\x96\x88" : "\xc2\xb7");
        fprintf(stderr, "  %d/%d chunks (%d%%)", done, total, (int)(pct * 100));
        fflush(stderr);
    });
    fprintf(stderr, "\n");

    if (!ok) {
        LOG_ERROR("RCLI", "RAG ingest failed");
        return -1;
    }

    engine->rag_index_path = index_path;
    LOG_DEBUG("RCLI", "RAG ingest complete: %s", index_path.c_str());

    // Load the index for querying
    RAGConfig rag_cfg;
    rag_cfg.index_path = index_path;
    rag_cfg.embedding_model_path = embedding_model;

    engine->rag_retriever = std::make_unique<HybridRetriever>();
    if (!engine->rag_retriever->init(index_path, rag_cfg)) {
        LOG_ERROR("RCLI", "Failed to load RAG index for querying");
        engine->rag_retriever.reset();
        return -1;
    }

    engine->rag_embedding = std::make_unique<EmbeddingEngine>();
    if (!engine->rag_embedding->init(embed_cfg)) {
        LOG_ERROR("RCLI", "Failed to init embedding engine");
        engine->rag_embedding.reset();
        engine->rag_retriever.reset();
        return -1;
    }

    engine->rag_ready = true;
    return 0;
}

int rcli_rag_load_index(RCLIHandle handle, const char* index_path) {
    if (!handle || !index_path) return -1;
    auto* engine = static_cast<RCLIEngine*>(handle);
    if (!engine->initialized) return -1;

    std::lock_guard<std::mutex> lock(engine->mutex);

    std::string embedding_model = engine->models_dir + "/snowflake-arctic-embed-s-q8_0.gguf";
    if (access(embedding_model.c_str(), F_OK) != 0) {
        LOG_ERROR("RCLI", "Embedding model not found: %s  Run: rcli setup", embedding_model.c_str());
        return -1;
    }

    EmbeddingConfig embed_cfg;
    embed_cfg.model_path = embedding_model;
    embed_cfg.n_gpu_layers = 99;
    embed_cfg.n_threads = 2;
    embed_cfg.n_batch = 512;
    embed_cfg.embedding_dim = 384;

    RAGConfig rag_cfg;
    rag_cfg.index_path = index_path;
    rag_cfg.embedding_model_path = embedding_model;

    engine->rag_retriever = std::make_unique<HybridRetriever>();
    if (!engine->rag_retriever->init(std::string(index_path), rag_cfg)) {
        LOG_ERROR("RCLI", "Failed to load RAG index at %s", index_path);
        engine->rag_retriever.reset();
        return -1;
    }

    engine->rag_embedding = std::make_unique<EmbeddingEngine>();
    if (!engine->rag_embedding->init(embed_cfg)) {
        LOG_ERROR("RCLI", "Failed to init embedding engine");
        engine->rag_embedding.reset();
        engine->rag_retriever.reset();
        return -1;
    }

    engine->rag_index_path = index_path;
    engine->rag_ready = true;
    LOG_DEBUG("RCLI", "RAG index loaded: %s", index_path);
    return 0;
}

const char* rcli_rag_query(RCLIHandle handle, const char* query) {
    if (!handle || !query) return "";
    auto* engine = static_cast<RCLIEngine*>(handle);
    if (!engine->rag_ready || !engine->rag_retriever || !engine->rag_embedding) {
        return "{\"error\": \"RAG not initialized. Run rcli_rag_ingest() first.\"}";
    }

    std::lock_guard<std::mutex> lock(engine->mutex);

    auto query_embedding = engine->rag_embedding->embed(std::string(query));
    if (query_embedding.empty()) {
        return "{\"error\": \"Failed to embed query\"}";
    }

    auto results = engine->rag_retriever->retrieve(
        std::string(query), query_embedding, 5);

    if (results.empty()) {
        engine->last_rag_result = "{\"results\": [], \"answer\": \"No relevant documents found.\"}";
        return engine->last_rag_result.c_str();
    }

    // Build context from retrieved chunks
    std::string context;
    for (size_t i = 0; i < results.size(); i++) {
        context += "[" + std::to_string(i + 1) + "] " + std::string(results[i].text) + "\n\n";
    }

    // Generate answer using LLM with retrieved context
    std::string rag_prompt =
        "Based on the following context, answer the question. "
        "Only use information from the context. If the answer isn't in the context, say so.\n\n"
        "Context:\n" + context + "\n"
        "Question: " + std::string(query);

    std::string answer = engine->pipeline.llm().generate(
        engine->pipeline.llm().build_chat_prompt(
            "You are a helpful assistant answering questions based on provided documents. "
            "Do NOT use <think> tags or internal reasoning. Answer directly.",
            {}, rag_prompt),
        nullptr);

    engine->last_rag_result = clean_llm_output(engine, answer);

    // Retry once if output cleaned to empty (some models produce only think tokens)
    if (engine->last_rag_result.empty()) {
        std::string retry = engine->pipeline.llm().generate(
            engine->pipeline.llm().build_chat_prompt(
                "Answer questions using the context provided. Be direct and concise.",
                {}, "Context:\n" + context + "\nQuestion: " + std::string(query) + "\nAnswer directly:"),
            nullptr);
        engine->last_rag_result = clean_llm_output(engine, retry);
    }
    return engine->last_rag_result.c_str();
}

void rcli_rag_clear(RCLIHandle handle) {
    if (!handle) return;
    auto* engine = static_cast<RCLIEngine*>(handle);
    std::lock_guard<std::mutex> lock(engine->mutex);
    engine->rag_retriever.reset();
    engine->rag_embedding.reset();
    engine->rag_index_path.clear();
    engine->rag_ready = false;
    engine->last_rag_result.clear();
    LOG_DEBUG("RCLI", "RAG cleared");
}

// =============================================================================
// Actions (macOS only)
// =============================================================================

const char* rcli_action_execute(RCLIHandle handle, const char* action_name, const char* args_json) {
    if (!handle || !action_name) return "";
    auto* engine = static_cast<RCLIEngine*>(handle);

    std::lock_guard<std::mutex> lock(engine->mutex);
    auto result = engine->actions.execute(action_name, args_json ? args_json : "{}");

    engine->last_action_result = result.raw_json;

    if (engine->action_cb) {
        engine->action_cb(action_name, result.raw_json.c_str(), result.success ? 1 : 0, engine->action_ud);
    }

    return engine->last_action_result.c_str();
}

const char* rcli_action_list(RCLIHandle handle) {
    if (!handle) return "[]";
    auto* engine = static_cast<RCLIEngine*>(handle);
    engine->last_action_list = engine->actions.get_all_definitions_json();
    return engine->last_action_list.c_str();
}

int rcli_set_action_enabled(RCLIHandle handle, const char* name, int enabled) {
    if (!handle || !name) return -1;
    auto* engine = static_cast<RCLIEngine*>(handle);
    std::lock_guard<std::mutex> lock(engine->mutex);
    engine->actions.set_enabled(name, enabled != 0);
    // Re-sync tool definitions visible to the LLM
    engine->pipeline.tools().set_external_tool_definitions(
        engine->actions.get_definitions_json());
    // Re-cache system prompt with updated tool definitions
    if (engine->initialized)
        engine->pipeline.recache_system_prompt();
    return 0;
}

int rcli_is_action_enabled(RCLIHandle handle, const char* name) {
    if (!handle || !name) return 0;
    auto* engine = static_cast<RCLIEngine*>(handle);
    return engine->actions.is_enabled(name) ? 1 : 0;
}

int rcli_save_action_preferences(RCLIHandle handle) {
    if (!handle) return -1;
    auto* engine = static_cast<RCLIEngine*>(handle);
    std::string prefs_path;
    if (const char* home = getenv("HOME"))
        prefs_path = std::string(home) + "/.rcli/actions.json";
    else
        prefs_path = "/tmp/.rcli/actions.json";
    engine->actions.save_preferences(prefs_path);
    return 0;
}

// =============================================================================
// Model Hot-Swap
// =============================================================================

int rcli_switch_llm(RCLIHandle handle, const char* model_id) {
    if (!handle || !model_id) return -1;
    auto* engine = static_cast<RCLIEngine*>(handle);
    if (!engine->initialized) return -1;

    std::lock_guard<std::mutex> lock(engine->mutex);

    auto models = rcli::all_models();
    const auto* m = rcli::find_model_by_id(model_id, models);
    if (!m) {
        LOG_ERROR("RCLI", "Unknown model id: %s", model_id);
        return -1;
    }

    std::string model_path = engine->models_dir + "/" + m->filename;
    FILE* f = fopen(model_path.c_str(), "r");
    if (!f) {
        LOG_ERROR("RCLI", "Model file not found: %s", model_path.c_str());
        return -1;
    }
    fclose(f);

    LlmConfig new_config;
    new_config.model_path      = model_path;
    new_config.n_gpu_layers    = (engine->config_gpu_layers >= 0) ? engine->config_gpu_layers : 99;
    new_config.n_ctx           = (engine->config_ctx_size > 0) ? engine->config_ctx_size : 4096;
    new_config.n_batch         = 512;
    new_config.n_threads       = 1;
    new_config.n_threads_batch = 8;
    new_config.temperature     = 0.7f;
    new_config.max_tokens      = 2048;
    new_config.flash_attn      = true;
    new_config.type_k          = 8;
    new_config.type_v          = 8;

    LOG_INFO("RCLI", "Switching LLM to %s (%s)", m->name.c_str(), m->filename.c_str());

    if (!engine->pipeline.reload_llm(new_config)) {
        LOG_ERROR("RCLI", "Failed to hot-swap LLM to %s", m->name.c_str());
        return -1;
    }

    engine->llm_model_name = m->name;
    rcli::write_selected_model_id(model_id);

    engine->pipeline.tools().set_external_tool_definitions(
        engine->actions.get_definitions_json());
    engine->pipeline.recache_system_prompt();

    LOG_INFO("RCLI", "LLM switched to %s (profile: %s)",
             m->name.c_str(), engine->pipeline.llm().profile().family_name.c_str());
    return 0;
}

// =============================================================================
// Callbacks
// =============================================================================

void rcli_set_transcript_callback(RCLIHandle handle, RCLITranscriptCallback cb, void* user_data) {
    if (!handle) return;
    auto* engine = static_cast<RCLIEngine*>(handle);
    engine->transcript_cb = cb;
    engine->transcript_ud = user_data;
}

void rcli_set_state_callback(RCLIHandle handle, RCLIStateCallback cb, void* user_data) {
    if (!handle) return;
    auto* engine = static_cast<RCLIEngine*>(handle);
    engine->state_cb = cb;
    engine->state_ud = user_data;
}

void rcli_set_action_callback(RCLIHandle handle, RCLIActionCallback cb, void* user_data) {
    if (!handle) return;
    auto* engine = static_cast<RCLIEngine*>(handle);
    engine->action_cb = cb;
    engine->action_ud = user_data;
}

void rcli_set_tool_trace_callback(RCLIHandle handle, RCLIToolTraceCallback cb, void* user_data) {
    if (!handle) return;
    auto* engine = static_cast<RCLIEngine*>(handle);
    engine->tool_trace_cb = cb;
    engine->tool_trace_ud = user_data;
}

// =============================================================================
// State / Info
// =============================================================================

int rcli_get_state(RCLIHandle handle) {
    if (!handle) return 0;
    auto* engine = static_cast<RCLIEngine*>(handle);
    return static_cast<int>(engine->pipeline.state());
}

const char* rcli_get_info(RCLIHandle handle) {
    if (!handle) return "{}";
    auto* engine = static_cast<RCLIEngine*>(handle);

    int num_actions = engine->actions.num_actions();

    engine->last_info = "{"
        "\"name\": \"RCLI\","
        "\"version\": \"0.1.0\","
        "\"engine\": \"llama.cpp + sherpa-onnx\","
        "\"actions\": " + std::to_string(num_actions) + ","
        "\"on_device\": true"
    "}";

    return engine->last_info.c_str();
}

float rcli_get_audio_level(RCLIHandle handle) {
    if (!handle) return 0.0f;
    auto* engine = static_cast<RCLIEngine*>(handle);
    return engine->pipeline.audio().get_rms();
}

int rcli_is_using_parakeet(RCLIHandle handle) {
    if (!handle) return 0;
    auto* engine = static_cast<RCLIEngine*>(handle);
    return engine->using_parakeet ? 1 : 0;
}

const char* rcli_get_llm_model(RCLIHandle handle) {
    if (!handle) return "unknown";
    auto* engine = static_cast<RCLIEngine*>(handle);
    return engine->llm_model_name.c_str();
}

const char* rcli_get_tts_model(RCLIHandle handle) {
    if (!handle) return "unknown";
    auto* engine = static_cast<RCLIEngine*>(handle);
    return engine->tts_model_name.c_str();
}

const char* rcli_get_stt_model(RCLIHandle handle) {
    if (!handle) return "unknown";
    auto* engine = static_cast<RCLIEngine*>(handle);
    return engine->stt_model_name.c_str();
}

void rcli_get_last_llm_perf(RCLIHandle handle,
                                  int* out_tokens,
                                  double* out_tok_per_sec,
                                  double* out_ttft_ms,
                                  double* out_total_ms) {
    if (!handle) return;
    auto* engine = static_cast<RCLIEngine*>(handle);
    if (!engine->initialized) return;
    auto& s = engine->pipeline.llm().last_stats();
    if (out_tokens)      *out_tokens = static_cast<int>(s.generated_tokens);
    if (out_tok_per_sec) *out_tok_per_sec = s.gen_tps();
    if (out_ttft_ms)     *out_ttft_ms = s.first_token_us / 1000.0;
    if (out_total_ms)    *out_total_ms = s.generation_us / 1000.0;
}

void rcli_get_last_tts_perf(RCLIHandle handle,
                                  int* out_samples,
                                  double* out_synthesis_ms,
                                  double* out_rtf) {
    if (!handle) return;
    auto* engine = static_cast<RCLIEngine*>(handle);
    if (!engine->initialized) return;
    auto& s = engine->pipeline.tts().last_stats();
    if (out_samples)      *out_samples = static_cast<int>(s.num_samples);
    if (out_synthesis_ms) *out_synthesis_ms = s.synthesis_us / 1000.0;
    if (out_rtf)          *out_rtf = s.real_time_factor();
}

void rcli_get_last_stt_perf(RCLIHandle handle,
                                  double* out_audio_ms,
                                  double* out_transcribe_ms) {
    if (!handle) return;
    auto* engine = static_cast<RCLIEngine*>(handle);
    if (!engine->initialized) return;
    auto& t = engine->pipeline.last_timings();
    if (out_audio_ms) {
        double samples = static_cast<double>(t.stt_audio_samples);
        *out_audio_ms = samples > 0 ? (samples / 16000.0) * 1000.0 : 0;
    }
    if (out_transcribe_ms) *out_transcribe_ms = t.stt_latency_us / 1000.0;
}

} // extern "C"

std::vector<rcli::ActionDef> rcli_get_all_action_defs(RCLIHandle handle) {
    if (!handle) return {};
    auto* engine = static_cast<RCLIEngine*>(handle);
    return engine->actions.get_all_defs();
}
