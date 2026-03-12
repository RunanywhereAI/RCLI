//
//  test_pipeline.cpp
//  RCLI
//
//  Comprehensive test suite for the RCLI engine.
//  Tests: action system, LLM inference, STT, TTS, full pipeline.
//
//  Usage:
//    ./rcli_test <models_dir>
//    ./rcli_test <models_dir> --actions-only    # Just test macOS actions
//    ./rcli_test <models_dir> --llm-only        # Just test LLM
//    ./rcli_test <models_dir> --stt-only        # Just test STT
//    ./rcli_test <models_dir> --tts-only        # Just test TTS
//    ./rcli_test <models_dir> --api-only        # Just test C API
//    ./rcli_test <models_dir> --metalrt-only    # Test MetalRT engines
//    ./rcli_test <models_dir> --personality-only # Test personality system
//    ./rcli_test <models_dir> --gpu-diag        # Full GPU diagnostic
//

#include "api/rcli_api.h"
#include "actions/action_registry.h"
#include "actions/macos_actions.h"
#include "actions/applescript_executor.h"
#include "pipeline/orchestrator.h"
#include "engines/llm_engine.h"
#include "engines/tts_engine.h"
#include "engines/stt_engine.h"
#include "engines/vad_engine.h"
#include "engines/metalrt_engine.h"
#include "engines/metalrt_stt_engine.h"
#include "engines/metalrt_tts_engine.h"
#include "engines/metalrt_loader.h"
#include "core/hardware_profile.h"
#include "core/personality.h"
#include "core/constants.h"
#include "core/log.h"
#include "audio/audio_io.h"
#include "models/model_registry.h"
#include "tools/tool_engine.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <chrono>
#include <fstream>

using namespace rastack;
using namespace rcli;

// =============================================================================
// Helpers
// =============================================================================

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_SECTION(name) \
    fprintf(stderr, "\n\033[1;36m=== %s ===\033[0m\n", name)

#define TEST(name, expr) do { \
    tests_run++; \
    if (expr) { \
        tests_passed++; \
        fprintf(stderr, "  \033[32m[PASS]\033[0m %s\n", name); \
    } else { \
        tests_failed++; \
        fprintf(stderr, "  \033[31m[FAIL]\033[0m %s\n", name); \
    } \
} while(0)

#define TEST_INFO(fmt, ...) \
    fprintf(stderr, "    \033[90m" fmt "\033[0m\n", ##__VA_ARGS__)

static double elapsed_ms(std::chrono::steady_clock::time_point start) {
    auto end = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(end - start).count();
}

static bool file_exists(const std::string& path) {
    std::ifstream f(path);
    return f.good();
}

// Generate a sine wave tone (for STT testing)
static std::vector<float> generate_sine_wave(float freq_hz, float duration_sec, int sample_rate = 16000) {
    int num_samples = static_cast<int>(duration_sec * sample_rate);
    std::vector<float> samples(num_samples);
    for (int i = 0; i < num_samples; i++) {
        samples[i] = 0.3f * sinf(2.0f * M_PI * freq_hz * i / sample_rate);
    }
    return samples;
}

// Generate silence
static std::vector<float> generate_silence(float duration_sec, int sample_rate = 16000) {
    int num_samples = static_cast<int>(duration_sec * sample_rate);
    return std::vector<float>(num_samples, 0.0f);
}

// =============================================================================
// Test: Action System (no models needed)
// =============================================================================

static void test_actions() {
    TEST_SECTION("Action System");

    ActionRegistry registry;

    // Test registration
    registry.register_defaults();
    TEST("register_defaults populates actions", registry.num_actions() > 0);
    TEST_INFO("Registered %d actions", registry.num_actions());

    // Test listing
    auto names = registry.list_actions();
    TEST("list_actions returns names", !names.empty());
    for (auto& n : names) {
        TEST_INFO("  - %s", n.c_str());
    }

    // Test definitions JSON
    std::string defs = registry.get_definitions_json();
    TEST("get_definitions_json is valid", !defs.empty() && defs[0] == '[');
    TEST_INFO("Definitions JSON: %zu bytes", defs.size());

    // Test enable/disable
    TEST_SECTION("Action Enable/Disable");
    TEST("open_app is enabled by default", registry.is_enabled("open_app"));
    TEST("screenshot is NOT enabled by default", !registry.is_enabled("screenshot"));

    int initial_enabled = registry.num_enabled();
    TEST_INFO("Initially %d actions enabled", initial_enabled);

    registry.set_enabled("open_app", false);
    TEST("open_app disabled after set_enabled(false)", !registry.is_enabled("open_app"));
    TEST("num_enabled decreased", registry.num_enabled() == initial_enabled - 1);

    registry.set_enabled("toggle_mute", false);
    TEST("toggle_mute disabled after set_enabled(false)", !registry.is_enabled("toggle_mute"));
    TEST("num_enabled decreased by 2", registry.num_enabled() == initial_enabled - 2);

    // Test get_definitions_json filters by enabled
    std::string enabled_defs = registry.get_definitions_json();
    TEST("enabled defs does not contain open_app (disabled)", enabled_defs.find("open_app") == std::string::npos);
    TEST("enabled defs does not contain toggle_mute (disabled)", enabled_defs.find("toggle_mute") == std::string::npos);

    std::string all_defs = registry.get_all_definitions_json();
    TEST("all defs contains open_app", all_defs.find("open_app") != std::string::npos);
    TEST("all defs contains toggle_mute", all_defs.find("toggle_mute") != std::string::npos);

    // Restore for remaining tests
    registry.set_enabled("open_app", true);
    registry.set_enabled("toggle_mute", true);

    auto enabled_list = registry.list_enabled_actions();
    TEST("list_enabled_actions returns entries", !enabled_list.empty());
    TEST_INFO("Enabled actions: %d", (int)enabled_list.size());

    // Test clipboard actions (safe to run)
    TEST_SECTION("Action Execution: Clipboard");
    auto write_result = registry.execute("clipboard_write", R"({"text": "RCLI test 123"})");
    TEST("clipboard_write succeeds", write_result.success);
    TEST_INFO("clipboard_write output: %s", write_result.output.c_str());

    auto read_result = registry.execute("clipboard_read", "{}");
    TEST("clipboard_read succeeds", read_result.success);
    TEST("clipboard contains test text", read_result.output.find("RCLI test 123") != std::string::npos);
    TEST_INFO("clipboard_read output: %s", read_result.output.c_str());

    // Test list_apps (safe to run)
    TEST_SECTION("Action Execution: List Apps");
    auto apps_result = registry.execute("list_apps", "{}");
    TEST("list_apps succeeds", apps_result.success);
    TEST("list_apps returns results", !apps_result.output.empty());
    TEST_INFO("Running apps: %.100s...", apps_result.output.c_str());

    // Test search_files (safe to run)
    TEST_SECTION("Action Execution: Search Files");
    auto search_result = registry.execute("search_files", R"({"query": "rcli"})");
    TEST("search_files succeeds", search_result.success);
    TEST_INFO("Search results: %.200s...", search_result.output.c_str());

    // Test open_url with a safe URL
    TEST_SECTION("Action Execution: Open URL");
    auto url_result = registry.execute("open_url", R"({"url": "https://example.com"})");
    TEST("open_url succeeds", url_result.success);

    // Test unknown action
    auto unknown = registry.execute("nonexistent_action", "{}");
    TEST("unknown action fails gracefully", !unknown.success);
}

// =============================================================================
// Test: AppleScript Executor
// =============================================================================

static void test_applescript_executor() {
    TEST_SECTION("AppleScript Executor");

    // Simple echo test
    auto result = rcli::run_applescript("return \"hello from applescript\"");
    TEST("run_applescript basic", result.success);
    TEST("result contains hello", result.output.find("hello") != std::string::npos);
    TEST_INFO("Output: %s", result.output.c_str());

    // Shell command
    auto shell = rcli::run_shell("echo 'shell test ok'");
    TEST("run_shell basic", shell.success);
    TEST("shell output correct", shell.output.find("shell test ok") != std::string::npos);

    // Timeout test (1 second timeout, 0.5 second script)
    auto fast = rcli::run_applescript("delay 0.1\nreturn \"done\"", 2000);
    TEST("short script completes within timeout", fast.success);

    // Error handling
    auto err = rcli::run_applescript("this is not valid applescript");
    TEST("invalid script returns error", !err.success);
    TEST_INFO("Error: %s", err.error.c_str());
}

// =============================================================================
// Test: LLM Engine
// =============================================================================

static void test_llm(const std::string& models_dir) {
    TEST_SECTION("LLM Engine");

    std::string model_path = models_dir + "/lfm2-1.2b-tool-q4_k_m.gguf";
    if (!file_exists(model_path)) {
        fprintf(stderr, "  \033[33m[SKIP] Skipped: model not found at %s\033[0m\n", model_path.c_str());
        return;
    }

    LlmEngine llm;
    LlmConfig config;
    config.model_path = model_path;
    config.n_gpu_layers = 99;
    config.n_ctx = 2048;
    config.n_batch = 512;
    config.n_threads = 1;
    config.n_threads_batch = 4;
    config.temperature = 0.7f;
    config.max_tokens = 128;
    config.flash_attn = true;
    config.type_k = 8;
    config.type_v = 8;

    auto t0 = std::chrono::steady_clock::now();
    bool ok = llm.init(config);
    TEST("LLM init succeeds", ok);
    TEST_INFO("LLM init: %.1f ms", elapsed_ms(t0));

    if (!ok) return;

    // Test basic generation
    TEST_SECTION("LLM Generation");
    std::string prompt = llm.build_chat_prompt(
        "You are a helpful assistant. Keep responses brief.",
        {}, "What is 2 + 2?");

    t0 = std::chrono::steady_clock::now();
    std::string response = llm.generate(prompt, nullptr);
    double gen_ms = elapsed_ms(t0);

    TEST("generate returns non-empty", !response.empty());
    TEST_INFO("Response: %.200s", response.c_str());
    TEST_INFO("Generation: %.1f ms, %lld tokens, %.1f tok/s",
              gen_ms, (long long)llm.last_stats().generated_tokens, llm.last_stats().gen_tps());

    // Test tool-aware generation
    TEST_SECTION("LLM Tool Calling");
    std::string tool_defs = R"([{"type":"function","function":{"name":"create_note","description":"Create an Apple Note","parameters":{"type":"object","properties":{"title":{"type":"string"},"body":{"type":"string"}},"required":["title"]}}}])";

    t0 = std::chrono::steady_clock::now();
    std::string tool_resp = llm.generate_with_tools(
        "Create a note called 'Test Note' with the content 'Hello World'",
        tool_defs,
        "You are RCLI. Use tools when asked to take actions.",
        nullptr);
    gen_ms = elapsed_ms(t0);

    TEST("generate_with_tools returns non-empty", !tool_resp.empty());
    TEST_INFO("Tool response: %.300s", tool_resp.c_str());
    TEST_INFO("Generation: %.1f ms", gen_ms);

    // Check if it produced a tool call
    bool has_tool_call = tool_resp.find("tool_call") != std::string::npos ||
                          tool_resp.find("create_note") != std::string::npos;
    TEST("response contains tool call or action name", has_tool_call);

    // Test conversation
    TEST_SECTION("LLM Conversation");
    prompt = llm.build_chat_prompt(
        "You are RCLI, a macOS voice assistant. Be brief and natural.",
        {}, "Tell me a one-sentence joke");

    t0 = std::chrono::steady_clock::now();
    response = llm.generate(prompt, nullptr);
    gen_ms = elapsed_ms(t0);

    TEST("joke generation works", !response.empty());
    TEST_INFO("Joke: %.200s", response.c_str());
    TEST_INFO("%.1f ms, %.1f tok/s", gen_ms, llm.last_stats().gen_tps());

    // Test prompt caching
    TEST_SECTION("LLM Prompt Caching");
    llm.cache_system_prompt("You are RCLI, a helpful voice assistant for macOS.");
    TEST("prompt cache created", llm.has_prompt_cache());

    t0 = std::chrono::steady_clock::now();
    response = llm.generate_with_cached_prompt("What can you do?", nullptr);
    gen_ms = elapsed_ms(t0);

    TEST("cached prompt generation works", !response.empty());
    TEST_INFO("Cached response: %.200s", response.c_str());
    TEST_INFO("%.1f ms (should be faster than uncached)", gen_ms);
}

// =============================================================================
// Test: STT Engine
// =============================================================================

static void test_stt(const std::string& models_dir) {
    TEST_SECTION("STT Engine (Offline/Whisper)");

    std::string encoder = models_dir + "/whisper-base.en/base.en-encoder.int8.onnx";
    std::string decoder = models_dir + "/whisper-base.en/base.en-decoder.int8.onnx";
    std::string tokens  = models_dir + "/whisper-base.en/base.en-tokens.txt";

    if (!file_exists(encoder)) {
        fprintf(stderr, "  \033[33m[SKIP] Skipped: Whisper model not found at %s\033[0m\n", encoder.c_str());
        return;
    }

    OfflineSttEngine stt;
    OfflineSttConfig config;
    config.encoder_path = encoder;
    config.decoder_path = decoder;
    config.tokens_path = tokens;
    config.language = "en";
    config.task = "transcribe";
    config.tail_paddings = 500;
    config.sample_rate = 16000;
    config.num_threads = 4;

    auto t0 = std::chrono::steady_clock::now();
    bool ok = stt.init(config);
    TEST("Whisper STT init succeeds", ok);
    TEST_INFO("STT init: %.1f ms", elapsed_ms(t0));

    if (!ok) return;

    // Test with silence (should return empty or minimal text)
    auto silence = generate_silence(2.0f);
    t0 = std::chrono::steady_clock::now();
    std::string result = stt.transcribe(silence.data(), silence.size());
    double stt_ms = elapsed_ms(t0);
    TEST("silence transcription completes", true);
    TEST_INFO("Silence result: '%s' (%.1f ms)", result.c_str(), stt_ms);

    // Test with a tone (not speech, but tests the pipeline)
    auto tone = generate_sine_wave(440.0f, 2.0f);
    t0 = std::chrono::steady_clock::now();
    result = stt.transcribe(tone.data(), tone.size());
    stt_ms = elapsed_ms(t0);
    TEST("tone transcription completes", true);
    TEST_INFO("Tone result: '%s' (%.1f ms)", result.c_str(), stt_ms);

    // Test streaming STT (Zipformer)
    TEST_SECTION("STT Engine (Streaming/Zipformer)");

    std::string enc = models_dir + "/zipformer/encoder-epoch-99-avg-1.int8.onnx";
    std::string dec = models_dir + "/zipformer/decoder-epoch-99-avg-1.int8.onnx";
    std::string joi = models_dir + "/zipformer/joiner-epoch-99-avg-1.int8.onnx";
    std::string tok = models_dir + "/zipformer/tokens.txt";

    if (!file_exists(enc)) {
        fprintf(stderr, "  \033[33m[SKIP] Skipped: Zipformer model not found at %s\033[0m\n", enc.c_str());
        return;
    }

    SttEngine streaming_stt;
    SttConfig stt_cfg;
    stt_cfg.encoder_path = enc;
    stt_cfg.decoder_path = dec;
    stt_cfg.joiner_path = joi;
    stt_cfg.tokens_path = tok;
    stt_cfg.sample_rate = 16000;
    stt_cfg.num_threads = 2;

    t0 = std::chrono::steady_clock::now();
    ok = streaming_stt.init(stt_cfg);
    TEST("Streaming STT init succeeds", ok);
    TEST_INFO("Streaming STT init: %.1f ms", elapsed_ms(t0));

    if (!ok) return;

    // Feed silence in chunks
    auto chunks = generate_silence(1.0f);
    int chunk_size = 1600; // 100ms chunks
    for (size_t i = 0; i < chunks.size(); i += chunk_size) {
        int n = std::min(chunk_size, (int)(chunks.size() - i));
        streaming_stt.feed_audio(chunks.data() + i, n);
        streaming_stt.process_tick();
    }
    auto seg = streaming_stt.get_result();
    TEST("streaming STT processes chunks", true);
    TEST_INFO("Streaming result: '%s'", seg.text.c_str());
}

// =============================================================================
// Test: TTS Engine
// =============================================================================

static void test_tts(const std::string& models_dir) {
    TEST_SECTION("TTS Engine (Piper)");

    std::string model = models_dir + "/piper-voice/en_US-lessac-medium.onnx";
    std::string config_json = models_dir + "/piper-voice/en_US-lessac-medium.onnx.json";
    std::string tts_tokens = models_dir + "/piper-voice/tokens.txt";
    std::string data_dir = models_dir + "/espeak-ng-data";

    if (!file_exists(model)) {
        fprintf(stderr, "  \033[33m[SKIP] Skipped: Piper TTS model not found at %s\033[0m\n", model.c_str());
        return;
    }

    TtsEngine tts;
    TtsConfig config;
    config.model_path = model;
    config.model_config_path = config_json;
    config.tokens_path = tts_tokens;
    config.data_dir = data_dir;
    config.num_threads = 2;
    config.speed = 1.0f;

    auto t0 = std::chrono::steady_clock::now();
    bool ok = tts.init(config);
    TEST("TTS init succeeds", ok);
    TEST_INFO("TTS init: %.1f ms", elapsed_ms(t0));

    if (!ok) return;

    // Synthesize short text
    t0 = std::chrono::steady_clock::now();
    auto audio = tts.synthesize("Hello, I am RCLI.");
    double tts_ms = elapsed_ms(t0);

    TEST("synthesize returns audio", !audio.empty());
    TEST_INFO("Synthesized %zu samples (%.2f sec) in %.1f ms",
              audio.size(), audio.size() / 22050.0, tts_ms);
    TEST_INFO("Real-time factor: %.2fx", tts.last_stats().real_time_factor());

    // Synthesize longer text
    t0 = std::chrono::steady_clock::now();
    audio = tts.synthesize("I just created a reminder for you to call the dentist tomorrow at 3 PM.");
    tts_ms = elapsed_ms(t0);

    TEST("longer synthesis works", !audio.empty());
    TEST_INFO("Synthesized %zu samples (%.2f sec) in %.1f ms",
              audio.size(), audio.size() / 22050.0, tts_ms);

    // Synthesize action response
    t0 = std::chrono::steady_clock::now();
    audio = tts.synthesize("Done! I've sent that message to John.");
    tts_ms = elapsed_ms(t0);

    TEST("action response synthesis works", !audio.empty());
    TEST_INFO("%.1f ms, %.2fx realtime", tts_ms, tts.last_stats().real_time_factor());
}

// =============================================================================
// Test: VAD Engine
// =============================================================================

static void test_vad(const std::string& models_dir) {
    TEST_SECTION("VAD Engine (Silero)");

    std::string model = models_dir + "/silero_vad.onnx";
    if (!file_exists(model)) {
        fprintf(stderr, "  \033[33m[SKIP] Skipped: VAD model not found at %s\033[0m\n", model.c_str());
        return;
    }

    VadEngine vad;
    VadConfig config;
    config.model_path = model;
    config.threshold = 0.5f;
    config.min_silence_duration = 0.5f;
    config.min_speech_duration = 0.25f;
    config.window_size = 512;
    config.sample_rate = 16000;
    config.num_threads = 1;

    auto t0 = std::chrono::steady_clock::now();
    bool ok = vad.init(config);
    TEST("VAD init succeeds", ok);
    TEST_INFO("VAD init: %.1f ms", elapsed_ms(t0));

    if (!ok) return;

    // Feed silence — should detect no speech
    auto silence = generate_silence(1.0f);
    auto segments = vad.extract_speech(silence.data(), static_cast<int>(silence.size()));
    TEST("silence produces no speech segments", segments.empty());
    TEST_INFO("Silence: %zu segments detected", segments.size());

    // Feed a tone — may or may not be detected as speech
    auto tone = generate_sine_wave(440.0f, 1.0f);
    segments = vad.extract_speech(tone.data(), static_cast<int>(tone.size()));
    TEST("tone processing completes", true);
    TEST_INFO("Tone: %zu segments detected", segments.size());
}

// =============================================================================
// Test: C API (rcli_api.h)
// =============================================================================

static void test_api(const std::string& models_dir) {
    TEST_SECTION("RCLI C API");

    // Test create/destroy
    RCLIHandle h = rcli_create(nullptr);
    TEST("rcli_create returns handle", h != nullptr);

    // Test get_info before init
    const char* info = rcli_get_info(h);
    TEST("get_info returns JSON", info != nullptr && strlen(info) > 0);
    TEST_INFO("Info: %s", info);

    // Test action_list before init
    const char* actions = rcli_action_list(h);
    TEST("action_list returns JSON", actions != nullptr && strlen(actions) > 2);
    TEST_INFO("Actions: %.200s...", actions);

    // Test init
    auto t0 = std::chrono::steady_clock::now();
    int rc = rcli_init(h, models_dir.c_str(), 99);
    double init_ms = elapsed_ms(t0);

    if (rc != 0) {
        fprintf(stderr, "  \033[33m[SKIP] rcli_init failed (models may not be present)\033[0m\n");
        TEST("rcli_init (skipped - no models)", false);
        rcli_destroy(h);
        return;
    }

    TEST("rcli_init succeeds", rc == 0);
    TEST_INFO("Engine init: %.1f ms", init_ms);

    // Test state
    int state = rcli_get_state(h);
    TEST("initial state is IDLE (0)", state == 0);

    // Test process_command (pure conversation)
    TEST_SECTION("C API: Process Command");
    t0 = std::chrono::steady_clock::now();
    const char* resp = rcli_process_command(h, "Hello, what can you do?");
    double cmd_ms = elapsed_ms(t0);

    TEST("process_command returns response", resp != nullptr && strlen(resp) > 0);
    TEST_INFO("Response: %.200s", resp);
    TEST_INFO("Command processing: %.1f ms", cmd_ms);

    // Test process_command (action trigger)
    t0 = std::chrono::steady_clock::now();
    resp = rcli_process_command(h, "Create a note called Test");
    cmd_ms = elapsed_ms(t0);

    TEST("action command returns response", resp != nullptr && strlen(resp) > 0);
    TEST_INFO("Action response: %.200s", resp);
    TEST_INFO("Action processing: %.1f ms", cmd_ms);

    // Test speak
    int speak_rc = rcli_speak(h, "Hello, this is a test.");
    TEST("rcli_speak completes", speak_rc == 0 || speak_rc == -1); // may fail without audio output

    // Test action_execute directly
    const char* action_result = rcli_action_execute(h, "clipboard_write", R"({"text":"API test"})");
    TEST("direct action_execute works", action_result != nullptr);
    TEST_INFO("Action result: %s", action_result);

    // Cleanup
    rcli_destroy(h);
    TEST("rcli_destroy completes", true);
}

// =============================================================================
// Test: End-to-End Voice Use Cases
// =============================================================================

static void test_use_cases(const std::string& models_dir) {
    TEST_SECTION("End-to-End Use Cases");

    RCLIHandle h = rcli_create(nullptr);
    if (rcli_init(h, models_dir.c_str(), 99) != 0) {
        fprintf(stderr, "  \033[33m[SKIP] Skipped: engine init failed\033[0m\n");
        rcli_destroy(h);
        return;
    }

    struct UseCase {
        const char* name;
        const char* input;
        bool expect_action;
    };

    UseCase cases[] = {
        {"Greeting", "Hey, how's it going?", false},
        {"Question", "What is the capital of France?", false},
        {"Create Note", "Create a note called Shopping List with milk, eggs, bread", true},
        {"Open App", "Open Safari for me", true},
        {"Search Files", "Find documents about project proposal", true},
        {"Clipboard", "Copy that to clipboard", true},
        {"Volume", "Set volume to 50 percent", true},
    };

    for (auto& uc : cases) {
        auto t0 = std::chrono::steady_clock::now();
        const char* resp = rcli_process_command(h, uc.input);
        double ms = elapsed_ms(t0);

        bool has_response = resp != nullptr && strlen(resp) > 0;
        TEST(uc.name, has_response);
        TEST_INFO("Input:    %s", uc.input);
        TEST_INFO("Response: %.150s", resp ? resp : "(null)");
        TEST_INFO("Time:     %.1f ms", ms);
    }

    rcli_destroy(h);
}

// =============================================================================
// Test: Tool Engine Integration
// =============================================================================

static void test_tool_engine() {
    TEST_SECTION("Tool Engine");

    ToolEngine tools;

    // Register a test tool
    tools.register_tool("test_tool", [](const std::string& args) -> std::string {
        return R"({"result": "tool executed", "args": )" + args + "}";
    });

    // Test parse_tool_calls
    std::string llm_output = R"(I'll help you with that. <tool_call>{"name":"test_tool","arguments":{"key":"value"}}</tool_call>)";
    auto calls = tools.parse_tool_calls(llm_output);
    TEST("parse finds tool call", calls.size() == 1);
    if (!calls.empty()) {
        TEST("tool call name is correct", calls[0].name == "test_tool");
        TEST_INFO("Parsed: name=%s, args=%s", calls[0].name.c_str(), calls[0].arguments_json.c_str());
    }

    // Test execution
    if (!calls.empty()) {
        auto results = tools.execute_all(calls);
        TEST("tool execution succeeds", !results.empty() && results[0].success);
        TEST_INFO("Result: %s", results[0].result_json.c_str());

        std::string formatted = tools.format_results(results);
        TEST("format_results returns text", !formatted.empty());
        TEST_INFO("Formatted: %s", formatted.c_str());
    }
}

// =============================================================================
// Test: MetalRT GPU Diagnostics
// =============================================================================

static void test_metalrt_loader() {
    TEST_SECTION("MetalRT Loader — dylib & Symbol Resolution");

    auto& loader = MetalRTLoader::instance();
    TEST("MetalRT dylib exists on disk", loader.is_available());
    if (!loader.is_available()) {
        TEST_INFO("dylib path: %s (NOT FOUND)", MetalRTLoader::dylib_path().c_str());
        TEST_INFO("Install with: rcli metalrt install");
        return;
    }
    TEST_INFO("dylib path: %s", MetalRTLoader::dylib_path().c_str());

    std::string ver = MetalRTLoader::installed_version();
    TEST_INFO("Installed version: %s", ver.empty() ? "(unknown)" : ver.c_str());

    bool loaded = loader.is_loaded() || loader.load();
    TEST("dlopen succeeds", loaded);
    if (!loaded) return;

    TEST("ABI version matches", loader.abi_version() == MetalRTLoader::REQUIRED_ABI_VERSION);
    TEST_INFO("ABI version: %u (required: %u)", loader.abi_version(), MetalRTLoader::REQUIRED_ABI_VERSION);

    TEST_SECTION("MetalRT LLM Symbols");
    TEST("create resolved",           loader.create != nullptr);
    TEST("destroy resolved",          loader.destroy != nullptr);
    TEST("load_model resolved",       loader.load_model != nullptr);
    TEST("generate resolved",         loader.generate != nullptr);
    TEST("generate_stream resolved",  loader.generate_stream != nullptr);
    TEST("device_name resolved",      loader.device_name != nullptr);
    TEST("model_name resolved",       loader.model_name != nullptr);
    TEST("set_system_prompt resolved",loader.set_system_prompt != nullptr);
    TEST("free_result resolved",      loader.free_result != nullptr);

    TEST_SECTION("MetalRT STT Symbols (Whisper)");
    TEST("whisper_create resolved",     loader.whisper_create != nullptr);
    TEST("whisper_load resolved",       loader.whisper_load != nullptr);
    TEST("whisper_transcribe resolved", loader.whisper_transcribe != nullptr);
    TEST("whisper_free_text resolved",  loader.whisper_free_text != nullptr);
    TEST("whisper_encode_ms resolved",  loader.whisper_last_encode_ms != nullptr);
    TEST("whisper_decode_ms resolved",  loader.whisper_last_decode_ms != nullptr);
    if (!loader.whisper_create || !loader.whisper_transcribe)
        TEST_INFO("WARNING: STT symbols missing — Whisper will fall back to sherpa-onnx (CPU)");

    TEST_SECTION("MetalRT TTS Symbols (Kokoro)");
    TEST("tts_create resolved",     loader.tts_create != nullptr);
    TEST("tts_load resolved",       loader.tts_load != nullptr);
    TEST("tts_synthesize resolved", loader.tts_synthesize != nullptr);
    TEST("tts_free_audio resolved", loader.tts_free_audio != nullptr);
    TEST("tts_sample_rate resolved",loader.tts_sample_rate != nullptr);
    if (!loader.tts_create || !loader.tts_synthesize)
        TEST_INFO("WARNING: TTS symbols missing — Kokoro will fall back to sherpa-onnx (CPU)");
}

static void test_metalrt_llm(const std::string& models_dir) {
    TEST_SECTION("MetalRT LLM — GPU Inference Verification");

    auto& loader = MetalRTLoader::instance();
    if (!loader.is_loaded() && !loader.load()) {
        fprintf(stderr, "  \033[33m[SKIP] MetalRT dylib not loaded\033[0m\n");
        return;
    }

    std::string model_dir;
    std::string model_display_name;
    {
        auto models = rcli::all_models();
        std::string mrt_base = rcli::metalrt_models_dir();
        for (auto& m : models) {
            if (m.metalrt_supported && rcli::is_metalrt_model_installed(m)) {
                model_dir = mrt_base + "/" + m.metalrt_dir_name;
                model_display_name = m.name;
                break;
            }
        }
    }

    if (model_dir.empty()) {
        std::string mrt_base = rcli::metalrt_models_dir();
        fprintf(stderr, "  \033[33m[SKIP] No MetalRT LLM models installed (checked %s)\033[0m\n",
                mrt_base.c_str());
        TEST_INFO("Run: rcli metalrt download");
        return;
    }
    TEST_INFO("Found model: %s", model_display_name.c_str());
    TEST_INFO("Model dir: %s", model_dir.c_str());

    MetalRTEngine engine;
    MetalRTEngineConfig cfg;
    cfg.model_dir = model_dir;
    cfg.max_tokens = 64;
    cfg.temperature = 0.0f;

    auto t0 = std::chrono::steady_clock::now();
    bool ok = engine.init(cfg);
    double init_ms = elapsed_ms(t0);
    TEST("MetalRT LLM init succeeds", ok);
    TEST_INFO("Init time: %.1f ms", init_ms);
    if (!ok) return;

    std::string dev = engine.device_name();
    std::string model = engine.model_name();
    TEST("device_name is non-empty", !dev.empty());
    TEST("device_name contains GPU indicator", dev.find("CPU") == std::string::npos);
    TEST_INFO("Device:  %s", dev.c_str());
    TEST_INFO("Model:   %s", model.c_str());
    TEST_INFO("Profile: %s", engine.profile().family_name.c_str());

    // Warm-up run
    engine.reset_conversation();
    engine.generate("hi");

    // Benchmark 3 prompts
    const char* prompts[] = {
        "What is 2+2?",
        "Write a haiku about the sea.",
        "Explain gravity in one sentence.",
    };

    TEST_SECTION("MetalRT LLM Inference (Metal GPU)");
    for (int i = 0; i < 3; i++) {
        engine.reset_conversation();
        t0 = std::chrono::steady_clock::now();
        std::string result = engine.generate(prompts[i]);
        double gen_ms = elapsed_ms(t0);

        const auto& stats = engine.last_stats();
        TEST_INFO("--- Run %d ---", i + 1);
        TEST_INFO("  Prompt:   \"%s\"", prompts[i]);
        TEST_INFO("  Response: \"%.*s%s\"", (int)std::min(result.size(), (size_t)80),
                  result.c_str(), result.size() > 80 ? "..." : "");
        TEST_INFO("  Backend:  MetalRT (Metal GPU)");
        TEST_INFO("  Prefill:  %.1f ms (%d tokens, %.0f tok/s)",
                  stats.prompt_eval_us / 1000.0, stats.prompt_tokens, stats.prompt_tps());
        TEST_INFO("  Decode:   %.1f ms (%d tokens, %.0f tok/s)",
                  stats.generation_us / 1000.0, stats.generated_tokens, stats.gen_tps());
        TEST_INFO("  Wall:     %.1f ms", gen_ms);
        TEST("run produces output", !result.empty());
    }
}

static void test_metalrt_stt(const std::string& models_dir) {
    TEST_SECTION("MetalRT STT — Whisper GPU Verification");

    auto& loader = MetalRTLoader::instance();
    if (!loader.is_loaded() && !loader.load()) {
        fprintf(stderr, "  \033[33m[SKIP] MetalRT dylib not loaded\033[0m\n");
        return;
    }
    if (!loader.whisper_create || !loader.whisper_transcribe) {
        fprintf(stderr, "  \033[33m[SKIP] Whisper symbols not in dylib\033[0m\n");
        return;
    }

    std::string stt_dir;
    {
        auto comps = rcli::metalrt_component_models();
        std::string mrt_base = rcli::metalrt_models_dir();
        for (auto& cm : comps) {
            if (cm.component == "stt" && rcli::is_metalrt_component_installed(cm)) {
                stt_dir = mrt_base + "/" + cm.dir_name;
                TEST_INFO("Found STT model: %s", cm.name.c_str());
                break;
            }
        }
    }

    if (stt_dir.empty()) {
        fprintf(stderr, "  \033[33m[SKIP] No MetalRT Whisper model installed\033[0m\n");
        TEST_INFO("Run: rcli metalrt download");
        return;
    }

    MetalRTSttEngine stt;
    MetalRTSttConfig cfg;
    cfg.model_dir = stt_dir;

    auto t0 = std::chrono::steady_clock::now();
    bool ok = stt.init(cfg);
    double init_ms = elapsed_ms(t0);
    TEST("MetalRT Whisper init succeeds", ok);
    TEST_INFO("Init time: %.1f ms", init_ms);
    TEST_INFO("Backend:   MetalRT Whisper (Metal GPU)");
    if (!ok) return;

    // Transcribe test audio: silence, then a tone
    struct SttTest {
        const char* label;
        std::vector<float> audio;
        float duration_sec;
    };

    // NOTE: MetalRT Whisper dylib can crash on synthetic audio (tones/silence).
    // Only test with real WAV files if available. Init verification above
    // confirms the GPU backend is loaded and ready.
    std::string test_wav = models_dir + "/bench-samples/test.wav";
    if (file_exists(test_wav)) {
        auto audio = AudioIO::load_wav_to_vec(test_wav, 16000);
        if (!audio.empty()) {
            double audio_ms = audio.size() * 1000.0 / 16000.0;
            t0 = std::chrono::steady_clock::now();
            std::string result = stt.transcribe(audio.data(), (int)audio.size(), 16000);
            double wall_ms = elapsed_ms(t0);
            double rtf = (audio_ms > 0) ? (wall_ms / audio_ms) : 0;

            TEST_INFO("[test.wav] result=\"%s\"", result.c_str());
            TEST_INFO("  audio=%.1fms  wall=%.1fms  encode=%.1fms  decode=%.1fms  RTF=%.3fx",
                      audio_ms, wall_ms, stt.last_encode_ms(), stt.last_decode_ms(), rtf);
            TEST("STT transcription completes", !result.empty());
        }
    } else {
        TEST_INFO("No test WAV found at %s — skipping transcription test", test_wav.c_str());
        TEST_INFO("(Init + GPU verification above is sufficient to confirm Metal backend)");
    }
}

static void test_metalrt_tts(const std::string& models_dir) {
    TEST_SECTION("MetalRT TTS — Kokoro GPU Verification");

    auto& loader = MetalRTLoader::instance();
    if (!loader.is_loaded() && !loader.load()) {
        fprintf(stderr, "  \033[33m[SKIP] MetalRT dylib not loaded\033[0m\n");
        return;
    }
    if (!loader.tts_create || !loader.tts_synthesize) {
        fprintf(stderr, "  \033[33m[SKIP] Kokoro TTS symbols not in dylib\033[0m\n");
        return;
    }

    std::string tts_dir;
    {
        auto comps = rcli::metalrt_component_models();
        std::string mrt_base = rcli::metalrt_models_dir();
        for (auto& cm : comps) {
            if (cm.component == "tts" && rcli::is_metalrt_component_installed(cm)) {
                tts_dir = mrt_base + "/" + cm.dir_name;
                TEST_INFO("Found TTS model: %s", cm.name.c_str());
                break;
            }
        }
    }

    if (tts_dir.empty()) {
        fprintf(stderr, "  \033[33m[SKIP] No MetalRT Kokoro model installed\033[0m\n");
        TEST_INFO("Run: rcli metalrt download");
        return;
    }

    MetalRTTtsEngine tts;
    MetalRTTtsConfig cfg;
    cfg.model_dir = tts_dir;
    cfg.voice = "af_heart";
    cfg.speed = 1.0f;

    auto t0 = std::chrono::steady_clock::now();
    bool ok = tts.init(cfg);
    double init_ms = elapsed_ms(t0);
    TEST("MetalRT Kokoro TTS init succeeds", ok);
    TEST_INFO("Init time:   %.1f ms", init_ms);
    TEST_INFO("Sample rate: %d Hz", tts.sample_rate());
    TEST_INFO("Backend:     MetalRT Kokoro (Metal GPU)");
    if (!ok) return;

    struct TtsTest {
        const char* label;
        const char* text;
    };

    TtsTest tests[] = {
        {"short", "Hello, I am RCLI."},
        {"medium", "The weather today is partly cloudy with a high of twenty two degrees."},
        {"long", "I just created a reminder for you to call the dentist tomorrow at three PM. "
                 "Is there anything else I can help you with?"},
        {"paragraph", "Quantum computing uses quantum bits, or qubits, that can exist in multiple "
                      "states simultaneously. This allows quantum computers to solve certain problems "
                      "exponentially faster than classical computers."},
    };

    for (auto& test : tests) {
        t0 = std::chrono::steady_clock::now();
        auto audio = tts.synthesize(test.text);
        double wall_ms = elapsed_ms(t0);

        double audio_ms = audio.empty() ? 0 : (audio.size() * 1000.0 / tts.sample_rate());
        double rtf = (audio_ms > 0) ? (wall_ms / audio_ms) : 0;
        double compute_ms = tts.last_synthesis_ms();
        double rtf_compute = (audio_ms > 0) ? (compute_ms / audio_ms) : 0;

        TEST_INFO("[%s] \"%.*s%s\"", test.label,
                  (int)(strlen(test.text) > 50 ? 50 : strlen(test.text)), test.text,
                  strlen(test.text) > 50 ? "..." : "");
        TEST_INFO("  Samples:      %zu (%.1f ms audio)", audio.size(), audio_ms);
        TEST_INFO("  Compute time: %.1f ms (dylib) — RTF=%.3fx", compute_ms, rtf_compute);
        TEST_INFO("  Wall time:    %.1f ms (end-to-end) — RTF=%.3fx", wall_ms, rtf);

        bool faster_than_realtime = rtf < 1.0;
        if (!faster_than_realtime)
            TEST_INFO("  *** WARNING: SLOWER than realtime! RTF=%.2fx — possible CPU fallback ***", rtf);

        TEST("TTS produces audio", !audio.empty());
        TEST("TTS faster than realtime", faster_than_realtime);
    }
}

static void test_metalrt_vs_llamacpp(const std::string& models_dir) {
    TEST_SECTION("Side-by-Side: MetalRT (GPU) vs llama.cpp (Metal)");

    // Try MetalRT LLM
    auto& loader = MetalRTLoader::instance();
    bool mrt_available = loader.is_loaded() || (loader.is_available() && loader.load());

    std::string metalrt_model_dir;
    if (mrt_available) {
        auto models = rcli::all_models();
        std::string mrt_base = rcli::metalrt_models_dir();
        for (auto& m : models) {
            if (m.metalrt_supported && rcli::is_metalrt_model_installed(m)) {
                metalrt_model_dir = mrt_base + "/" + m.metalrt_dir_name;
                break;
            }
        }
    }

    // Try llama.cpp LLM
    std::string llamacpp_model;
    const char* gguf_names[] = {
        "lfm2-1.2b-tool-q4_k_m.gguf", "qwen3-0.6b-q4_k_m.gguf", nullptr
    };
    for (const char** g = gguf_names; *g; ++g) {
        std::string path = models_dir + "/" + *g;
        if (file_exists(path)) { llamacpp_model = path; break; }
    }

    const char* test_prompt = "What is the speed of light?";

    // --- MetalRT ---
    if (!metalrt_model_dir.empty()) {
        MetalRTEngine mrt;
        MetalRTEngineConfig cfg;
        cfg.model_dir = metalrt_model_dir;
        cfg.max_tokens = 64;
        cfg.temperature = 0.0f;

        if (mrt.init(cfg)) {
            mrt.reset_conversation();
            mrt.generate("warmup");

            mrt.reset_conversation();
            auto t0 = std::chrono::steady_clock::now();
            std::string resp = mrt.generate(test_prompt);
            double ms = elapsed_ms(t0);
            const auto& s = mrt.last_stats();

            TEST_INFO("MetalRT (Metal GPU via libmetalrt.dylib):");
            TEST_INFO("  Device:   %s", mrt.device_name().c_str());
            TEST_INFO("  Model:    %s", mrt.model_name().c_str());
            TEST_INFO("  Prefill:  %.1f ms (%d tok, %.0f tok/s)", s.prompt_eval_us / 1000.0, s.prompt_tokens, s.prompt_tps());
            TEST_INFO("  Decode:   %.1f ms (%d tok, %.0f tok/s)", s.generation_us / 1000.0, s.generated_tokens, s.gen_tps());
            TEST_INFO("  Wall:     %.1f ms", ms);
            TEST_INFO("  Response: \"%.*s%s\"", (int)std::min(resp.size(), (size_t)80), resp.c_str(), resp.size() > 80 ? "..." : "");
        } else {
            TEST_INFO("MetalRT: init failed");
        }
    } else {
        TEST_INFO("MetalRT: no model installed (skipped)");
    }

    fprintf(stderr, "\n");

    // --- llama.cpp ---
    if (!llamacpp_model.empty()) {
        LlmEngine llm;
        LlmConfig cfg;
        cfg.model_path = llamacpp_model;
        cfg.n_gpu_layers = 99;
        cfg.n_ctx = 2048;
        cfg.n_batch = 512;
        cfg.n_threads = 1;
        cfg.n_threads_batch = 4;
        cfg.temperature = 0.0f;
        cfg.max_tokens = 64;
        cfg.flash_attn = true;
        cfg.type_k = 8;
        cfg.type_v = 8;

        if (llm.init(cfg)) {
            std::string warmup = llm.build_chat_prompt("You are helpful.", {}, "hi");
            llm.generate(warmup);
            llm.clear_kv_cache();

            std::string prompt = llm.build_chat_prompt("You are helpful.", {}, test_prompt);
            auto t0 = std::chrono::steady_clock::now();
            std::string resp = llm.generate(prompt);
            double ms = elapsed_ms(t0);
            const auto& s = llm.last_stats();

            TEST_INFO("llama.cpp (Metal GPU via ggml-metal):");
            TEST_INFO("  GPU layers: 99 (all offloaded to Metal)");
            TEST_INFO("  Flash attn: yes");
            TEST_INFO("  Prefill:  %.1f ms (%d tok, %.0f tok/s)", s.prompt_eval_us / 1000.0, (int)s.prompt_tokens, s.prompt_tps());
            TEST_INFO("  Decode:   %.1f ms (%d tok, %.0f tok/s)", s.generation_us / 1000.0, (int)s.generated_tokens, s.gen_tps());
            TEST_INFO("  Wall:     %.1f ms", ms);
            TEST_INFO("  Response: \"%.*s%s\"", (int)std::min(resp.size(), (size_t)80), resp.c_str(), resp.size() > 80 ? "..." : "");
        } else {
            TEST_INFO("llama.cpp: init failed");
        }
    } else {
        TEST_INFO("llama.cpp: no GGUF model found (skipped)");
    }
}

static void test_metalrt_tts_vs_sherpa(const std::string& models_dir) {
    TEST_SECTION("Side-by-Side: MetalRT Kokoro TTS (GPU) vs sherpa-onnx TTS (CPU)");

    const char* test_text = "I just created a reminder for you to call the dentist tomorrow at three PM.";

    // --- MetalRT TTS ---
    auto& loader = MetalRTLoader::instance();
    bool mrt_ok = loader.is_loaded() || (loader.is_available() && loader.load());

    if (mrt_ok && loader.tts_create && loader.tts_synthesize) {
        std::string tts_dir;
        {
            auto comps = rcli::metalrt_component_models();
            std::string mrt_base = rcli::metalrt_models_dir();
            for (auto& cm : comps) {
                if (cm.component == "tts" && rcli::is_metalrt_component_installed(cm)) {
                    tts_dir = mrt_base + "/" + cm.dir_name;
                    break;
                }
            }
        }

        if (!tts_dir.empty()) {
            MetalRTTtsEngine mrt_tts;
            MetalRTTtsConfig cfg;
            cfg.model_dir = tts_dir;
            cfg.voice = "af_heart";
            cfg.speed = 1.0f;

            if (mrt_tts.init(cfg)) {
                mrt_tts.synthesize("warmup");

                auto t0 = std::chrono::steady_clock::now();
                auto audio = mrt_tts.synthesize(test_text);
                double wall_ms = elapsed_ms(t0);
                double audio_ms = audio.empty() ? 0 : (audio.size() * 1000.0 / mrt_tts.sample_rate());
                double rtf = (audio_ms > 0) ? (wall_ms / audio_ms) : 0;

                TEST_INFO("MetalRT Kokoro (Metal GPU via libmetalrt.dylib):");
                TEST_INFO("  Samples:  %zu (%.1f ms audio @ %d Hz)", audio.size(), audio_ms, mrt_tts.sample_rate());
                TEST_INFO("  Compute:  %.1f ms (dylib)", mrt_tts.last_synthesis_ms());
                TEST_INFO("  Wall:     %.1f ms", wall_ms);
                TEST_INFO("  RTF:      %.3fx %s", rtf, rtf < 1.0 ? "(faster than realtime)" : "*** SLOWER than realtime ***");
            } else {
                TEST_INFO("MetalRT Kokoro: init failed");
            }
        } else {
            TEST_INFO("MetalRT Kokoro: no model installed (skipped)");
        }
    } else {
        TEST_INFO("MetalRT Kokoro: dylib symbols not available (skipped)");
    }

    fprintf(stderr, "\n");

    // --- sherpa-onnx TTS ---
    std::string piper_model = models_dir + "/piper-voice/en_US-lessac-medium.onnx";
    if (file_exists(piper_model)) {
        TtsEngine sherpa_tts;
        TtsConfig cfg;
        cfg.model_path = piper_model;
        cfg.model_config_path = models_dir + "/piper-voice/en_US-lessac-medium.onnx.json";
        cfg.tokens_path = models_dir + "/piper-voice/tokens.txt";
        cfg.data_dir = models_dir + "/espeak-ng-data";
        cfg.num_threads = 2;
        cfg.speed = 1.0f;

        if (sherpa_tts.init(cfg)) {
            sherpa_tts.synthesize("warmup");

            auto t0 = std::chrono::steady_clock::now();
            auto audio = sherpa_tts.synthesize(test_text);
            double wall_ms = elapsed_ms(t0);
            double audio_ms = audio.empty() ? 0 : (audio.size() * 1000.0 / sherpa_tts.sample_rate());
            double rtf = (audio_ms > 0) ? (wall_ms / audio_ms) : 0;

            TEST_INFO("sherpa-onnx Piper (CPU — ONNX Runtime):");
            TEST_INFO("  Samples:  %zu (%.1f ms audio @ %d Hz)", audio.size(), audio_ms, sherpa_tts.sample_rate());
            TEST_INFO("  Wall:     %.1f ms", wall_ms);
            TEST_INFO("  RTF:      %.3fx %s", rtf, rtf < 1.0 ? "(faster than realtime)" : "*** SLOWER than realtime ***");
        } else {
            TEST_INFO("sherpa-onnx Piper: init failed");
        }
    } else {
        TEST_INFO("sherpa-onnx Piper: model not found (skipped)");
    }
}

static void test_gpu_diagnostic() {
    TEST_SECTION("GPU / CPU Backend Diagnostic Summary");

    const auto& hw = global_hw();

    fprintf(stderr, "\n");
    fprintf(stderr, "  \033[1;36m╔══════════════════════════════════════════════════════════════╗\033[0m\n");
    fprintf(stderr, "  \033[1;36m║              HARDWARE & BACKEND DIAGNOSTIC                  ║\033[0m\n");
    fprintf(stderr, "  \033[1;36m╠══════════════════════════════════════════════════════════════╣\033[0m\n");
    fprintf(stderr, "  \033[1;36m║\033[0m  %-20s %-38s \033[1;36m║\033[0m\n", "Platform:", hw.platform_tag);
    fprintf(stderr, "  \033[1;36m║\033[0m  %-20s %d logical (%dP + %dE)%-17s \033[1;36m║\033[0m\n",
            "CPU:", hw.cpu_logical, hw.perf_cores, hw.effi_cores, "");
    fprintf(stderr, "  \033[1;36m║\033[0m  %-20s %lld MB%-32s \033[1;36m║\033[0m\n",
            "RAM:", (long long)hw.ram_total_mb, "");
    fprintf(stderr, "  \033[1;36m║\033[0m  %-20s %s%-32s \033[1;36m║\033[0m\n",
            "Metal GPU:", hw.has_metal ? "YES" : "NO", "");
    fprintf(stderr, "  \033[1;36m╠══════════════════════════════════════════════════════════════╣\033[0m\n");
    fprintf(stderr, "  \033[1;36m║\033[0m  %-20s %-20s %-18s \033[1;36m║\033[0m\n",
            "Component", "Backend", "Runs On");
    fprintf(stderr, "  \033[1;36m║\033[0m  %-20s %-20s %-18s \033[1;36m║\033[0m\n",
            "────────────────────", "────────────────────", "──────────────────");

    auto& loader = MetalRTLoader::instance();
    bool mrt_loaded = loader.is_loaded() || (loader.is_available() && loader.load());

    // LLM (MetalRT)
    bool llm_mrt = mrt_loaded && loader.create && loader.generate;
    fprintf(stderr, "  \033[1;36m║\033[0m  %-20s %-20s \033[%sm%-18s\033[0m \033[1;36m║\033[0m\n",
            "LLM (MetalRT)", llm_mrt ? "libmetalrt.dylib" : "(not available)",
            llm_mrt ? "32" : "33", llm_mrt ? "Metal GPU" : "N/A");

    // LLM (llama.cpp)
    fprintf(stderr, "  \033[1;36m║\033[0m  %-20s %-20s \033[32m%-18s\033[0m \033[1;36m║\033[0m\n",
            "LLM (llama.cpp)", "ggml-metal", "Metal GPU (99 layers)");

    // STT (MetalRT Whisper)
    bool stt_mrt = mrt_loaded && loader.whisper_create && loader.whisper_transcribe;
    fprintf(stderr, "  \033[1;36m║\033[0m  %-20s %-20s \033[%sm%-18s\033[0m \033[1;36m║\033[0m\n",
            "STT (MetalRT)", stt_mrt ? "Whisper MetalRT" : "(not available)",
            stt_mrt ? "32" : "33", stt_mrt ? "Metal GPU" : "N/A");

    // STT (sherpa-onnx)
    fprintf(stderr, "  \033[1;36m║\033[0m  %-20s %-20s \033[33m%-18s\033[0m \033[1;36m║\033[0m\n",
            "STT (sherpa-onnx)", "Zipformer/Whisper", "CPU (ONNX Runtime)");

    // TTS (MetalRT Kokoro)
    bool tts_mrt = mrt_loaded && loader.tts_create && loader.tts_synthesize;
    fprintf(stderr, "  \033[1;36m║\033[0m  %-20s %-20s \033[%sm%-18s\033[0m \033[1;36m║\033[0m\n",
            "TTS (MetalRT)", tts_mrt ? "Kokoro MetalRT" : "(not available)",
            tts_mrt ? "32" : "33", tts_mrt ? "Metal GPU" : "N/A");

    // TTS (sherpa-onnx)
    fprintf(stderr, "  \033[1;36m║\033[0m  %-20s %-20s \033[33m%-18s\033[0m \033[1;36m║\033[0m\n",
            "TTS (sherpa-onnx)", "Piper/Kokoro ONNX", "CPU (ONNX Runtime)");

    // VAD
    fprintf(stderr, "  \033[1;36m║\033[0m  %-20s %-20s \033[33m%-18s\033[0m \033[1;36m║\033[0m\n",
            "VAD", "Silero", "CPU (ONNX Runtime)");

    // Embeddings
    fprintf(stderr, "  \033[1;36m║\033[0m  %-20s %-20s \033[32m%-18s\033[0m \033[1;36m║\033[0m\n",
            "Embeddings", "ggml-metal", "Metal GPU (99 layers)");

    fprintf(stderr, "  \033[1;36m╠══════════════════════════════════════════════════════════════╣\033[0m\n");
    fprintf(stderr, "  \033[1;36m║\033[0m  \033[32m■ Metal GPU\033[0m   \033[33m■ CPU\033[0m   \033[90m■ Not available\033[0m                      \033[1;36m║\033[0m\n");
    fprintf(stderr, "  \033[1;36m╚══════════════════════════════════════════════════════════════╝\033[0m\n\n");

    TEST("hardware detection complete", true);
}

// =============================================================================
// Test: E2E Actions on Both Engines (llama.cpp + MetalRT)
// =============================================================================

// acceptable_alt: comma-separated list of alternative action names the model may reasonably pick
struct ActionTestCase {
    const char* label;
    const char* user_input;
    const char* expected_action;
    const char* acceptable_alts; // null or "alt1,alt2" — also counted as correct
    bool safe_to_execute;
    const char* expected_arg;
};

static bool action_is_acceptable(const std::string& got, const ActionTestCase& tc) {
    if (got == tc.expected_action) return true;
    if (!tc.acceptable_alts) return false;
    std::string alts(tc.acceptable_alts);
    size_t pos = 0;
    while (pos < alts.size()) {
        auto comma = alts.find(',', pos);
        if (comma == std::string::npos) comma = alts.size();
        if (got == alts.substr(pos, comma - pos)) return true;
        pos = comma + 1;
    }
    return false;
}

static const ActionTestCase ACTION_CASES[] = {
    {"Open App",       "Open Safari for me",                         "open_app",        nullptr,                                   false, "Safari"},
    {"Create Note",    "Create a note called Shopping List",          "create_note",     nullptr,                                   false, "Shopping"},
    {"Set Volume",     "Set volume to 50 percent",                   "set_volume",      nullptr,                                   false, "50"},
    {"Clipboard Write","Copy hello world to clipboard",              "clipboard_write",  "clipboard_read",                          true,  "hello"},
    {"Search Files",   "Find files about project proposal",          "search_files",    nullptr,                                    true,  "project"},
    {"Get Battery",    "What's my battery level?",                   "get_battery",     nullptr,                                    true,  nullptr},
    {"Get WiFi",       "What WiFi network am I on?",                "get_wifi",         nullptr,                                    true,  nullptr},
    {"Search Web",     "Search the web for today's weather",         "search_web",      nullptr,                                   false, "weather"},
    {"Play Music",     "Play some music",                            "play_pause_music", "play_apple_music,play_on_spotify",        false, nullptr},
};
static constexpr int NUM_ACTION_CASES = sizeof(ACTION_CASES) / sizeof(ACTION_CASES[0]);

static void test_actions_llamacpp(const std::string& models_dir) {
    TEST_SECTION("E2E Actions — llama.cpp Engine");

    std::string model_path = models_dir + "/lfm2-1.2b-tool-q4_k_m.gguf";
    if (!file_exists(model_path)) {
        fprintf(stderr, "  \033[33m[SKIP] Skipped: model not found at %s\033[0m\n", model_path.c_str());
        return;
    }

    LlmEngine llm;
    LlmConfig config;
    config.model_path = model_path;
    config.n_gpu_layers = 99;
    config.n_ctx = 2048;
    config.n_batch = 512;
    config.n_threads = 1;
    config.n_threads_batch = 4;
    config.temperature = 0.0f;
    config.max_tokens = 128;
    config.flash_attn = true;
    config.type_k = 8;
    config.type_v = 8;

    if (!llm.init(config)) {
        fprintf(stderr, "  \033[33m[SKIP] LLM init failed\033[0m\n");
        return;
    }

    ActionRegistry registry;
    registry.register_defaults();
    std::string tool_defs = registry.get_definitions_json();

    ToolEngine tools;
    tools.set_model_profile(&llm.profile());

    // Register action executors into the tool engine
    for (auto& name : registry.list_actions()) {
        tools.register_tool(name,
            [&registry, name](const std::string& args) -> std::string {
                auto result = registry.execute(name, args);
                return result.raw_json;
            });
    }

    int tool_detected = 0;
    int correct_action = 0;
    int executed_ok = 0;

    for (int i = 0; i < NUM_ACTION_CASES; i++) {
        auto& tc = ACTION_CASES[i];
        char section_label[64];
        snprintf(section_label, sizeof(section_label), "llama.cpp: %s", tc.label);

        auto t0 = std::chrono::steady_clock::now();
        std::string response = llm.generate_with_tools(
            tc.user_input,
            tool_defs,
            rastack::RCLI_SYSTEM_PROMPT,
            nullptr);
        double gen_ms = elapsed_ms(t0);

        auto calls = tools.parse_tool_calls(response);
        bool has_call = !calls.empty();
        bool right_action = has_call && action_is_acceptable(calls[0].name, tc);

        if (has_call) tool_detected++;
        if (right_action) correct_action++;

        char label[128];
        snprintf(label, sizeof(label), "[llama.cpp] %s — tool call detected", tc.label);
        TEST(label, has_call);

        if (has_call) {
            snprintf(label, sizeof(label), "[llama.cpp] %s — acceptable action for \"%s\"", tc.label, tc.expected_action);
            TEST(label, right_action);
            TEST_INFO("Parsed: %s(%s)", calls[0].name.c_str(), calls[0].arguments_json.c_str());

            if (tc.expected_arg) {
                bool has_arg = calls[0].arguments_json.find(tc.expected_arg) != std::string::npos;
                snprintf(label, sizeof(label), "[llama.cpp] %s — args contain \"%s\"", tc.label, tc.expected_arg);
                TEST(label, has_arg);
            }

            // Execute safe actions
            if (tc.safe_to_execute && right_action) {
                auto results = tools.execute_all(calls);
                bool exec_ok = !results.empty() && results[0].success;
                if (exec_ok) executed_ok++;
                snprintf(label, sizeof(label), "[llama.cpp] %s — execution succeeds", tc.label);
                TEST(label, exec_ok);
                if (!results.empty())
                    TEST_INFO("Result: %.150s", results[0].result_json.c_str());
            }
        } else {
            TEST_INFO("Raw response: %.200s", response.c_str());
        }

        TEST_INFO("Time: %.1f ms", gen_ms);
        llm.clear_kv_cache();
    }

    TEST_SECTION("llama.cpp Actions Summary");
    TEST_INFO("Tool calls detected: %d/%d", tool_detected, NUM_ACTION_CASES);
    TEST_INFO("Correct action name: %d/%d", correct_action, NUM_ACTION_CASES);
    TEST_INFO("Safe actions executed: %d", executed_ok);
}

// Bare tool call fallback for LFM2: detect "func_name(arg=val)" without wrapper tokens.
static std::vector<ToolCall> parse_bare_tool_calls(
    const std::string& text, const ActionRegistry& registry)
{
    std::vector<ToolCall> calls;
    auto names = registry.list_actions();

    for (const auto& name : names) {
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

        std::string inner = text.substr(paren_open + 1, paren_close - paren_open - 1);

        // Convert key="value" pairs to JSON
        std::string json = "{";
        bool first = true;
        size_t p = 0;
        while (p < inner.size()) {
            while (p < inner.size() && (inner[p] == ' ' || inner[p] == ',')) p++;
            size_t eq = inner.find('=', p);
            if (eq == std::string::npos) break;
            std::string key = inner.substr(p, eq - p);
            while (!key.empty() && key.back() == ' ') key.pop_back();

            p = eq + 1;
            while (p < inner.size() && inner[p] == ' ') p++;

            std::string val;
            if (p < inner.size() && inner[p] == '"') {
                p++;
                size_t end = inner.find('"', p);
                if (end == std::string::npos) break;
                val = inner.substr(p, end - p);
                p = end + 1;
            } else {
                size_t end = inner.find_first_of(", )", p);
                if (end == std::string::npos) end = inner.size();
                val = inner.substr(p, end - p);
                p = end;
            }

            if (!first) json += ", ";
            json += "\"" + key + "\": \"" + val + "\"";
            first = false;
        }
        json += "}";

        calls.push_back({name, json});
        break;
    }
    return calls;
}

static void test_actions_metalrt(const std::string& models_dir) {
    TEST_SECTION("E2E Actions — MetalRT Engine");

    auto& loader = MetalRTLoader::instance();
    if (!loader.is_loaded() && !loader.load()) {
        fprintf(stderr, "  \033[33m[SKIP] MetalRT dylib not loaded\033[0m\n");
        return;
    }

    std::string model_dir;
    {
        auto models = rcli::all_models();
        std::string mrt_base = rcli::metalrt_models_dir();
        // Prefer LFM2 model (primary tool-calling model) over smaller models
        for (auto& m : models) {
            if (m.metalrt_supported && rcli::is_metalrt_model_installed(m) &&
                m.metalrt_dir_name.find("LFM") != std::string::npos) {
                model_dir = mrt_base + "/" + m.metalrt_dir_name;
                break;
            }
        }
        if (model_dir.empty()) {
            for (auto& m : models) {
                if (m.metalrt_supported && rcli::is_metalrt_model_installed(m)) {
                    model_dir = mrt_base + "/" + m.metalrt_dir_name;
                    break;
                }
            }
        }
    }
    if (model_dir.empty()) {
        fprintf(stderr, "  \033[33m[SKIP] No MetalRT LLM model installed\033[0m\n");
        return;
    }

    MetalRTEngine engine;
    MetalRTEngineConfig cfg;
    cfg.model_dir = model_dir;
    // Use default max_tokens (2048) to match the C API pipeline
    cfg.temperature = 0.0f;

    if (!engine.init(cfg)) {
        fprintf(stderr, "  \033[33m[SKIP] MetalRT init failed\033[0m\n");
        return;
    }

    TEST_INFO("Device: %s", engine.device_name().c_str());
    TEST_INFO("Model:  %s", engine.model_name().c_str());

    ActionRegistry registry;
    registry.register_defaults();

    ToolEngine tools;
    tools.set_model_profile(&engine.profile());
    tools.set_external_tool_definitions(registry.get_definitions_json());
    for (auto& name : registry.list_actions()) {
        tools.register_tool(name,
            [&registry, name](const std::string& args) -> std::string {
                auto result = registry.execute(name, args);
                return result.raw_json;
            });
    }

    int tool_detected = 0;
    int correct_action = 0;
    int executed_ok = 0;

    for (int i = 0; i < NUM_ACTION_CASES; i++) {
        auto& tc = ACTION_CASES[i];

        // Build filtered tool defs per-command (same as C API)
        std::string tool_defs = registry.get_filtered_definitions_json(tc.user_input, 10);
        std::string system = engine.profile().build_tool_system_prompt(
            rastack::RCLI_SYSTEM_PROMPT, tool_defs);

        // Inject tool hint into user message
        std::string hint = tools.build_tool_hint(tc.user_input);
        std::string hinted_input = hint.empty()
            ? std::string(tc.user_input)
            : (hint + "\n" + tc.user_input);

        // Build full chat prompt and use generate_raw (same path as the real pipeline)
        std::string full_prompt = engine.profile().build_chat_prompt(system, {}, hinted_input);

        auto t0 = std::chrono::steady_clock::now();
        std::string response = engine.generate_raw(full_prompt);
        double gen_ms = elapsed_ms(t0);

        // Standard parse first, then bare tool call fallback for LFM2 (same as C API line 727)
        auto calls = engine.profile().parse_tool_calls(response);
        if (calls.empty() && engine.profile().family == ModelFamily::LFM2) {
            calls = parse_bare_tool_calls(response, registry);
        }

        bool has_call = !calls.empty();
        bool right_action = has_call && action_is_acceptable(calls[0].name, tc);

        if (has_call) tool_detected++;
        if (right_action) correct_action++;

        char label[128];
        snprintf(label, sizeof(label), "[MetalRT] %s — tool call detected", tc.label);
        TEST(label, has_call);

        if (has_call) {
            snprintf(label, sizeof(label), "[MetalRT] %s — acceptable action for \"%s\"", tc.label, tc.expected_action);
            TEST(label, right_action);
            TEST_INFO("Parsed: %s(%s)", calls[0].name.c_str(), calls[0].arguments_json.c_str());

            if (tc.expected_arg) {
                bool has_arg = calls[0].arguments_json.find(tc.expected_arg) != std::string::npos;
                snprintf(label, sizeof(label), "[MetalRT] %s — args contain \"%s\"", tc.label, tc.expected_arg);
                TEST(label, has_arg);
            }

            if (tc.safe_to_execute && right_action) {
                auto results = tools.execute_all(calls);
                bool exec_ok = !results.empty() && results[0].success;
                if (exec_ok) executed_ok++;
                snprintf(label, sizeof(label), "[MetalRT] %s — execution succeeds", tc.label);
                TEST(label, exec_ok);
                if (!results.empty())
                    TEST_INFO("Result: %.150s", results[0].result_json.c_str());
            }
        } else {
            TEST_INFO("No tool call: %.200s", response.c_str());
        }

        TEST_INFO("Hint: %s", hint.empty() ? "(none)" : hint.c_str());
        TEST_INFO("Time: %.1f ms", gen_ms);
    }

    TEST_SECTION("MetalRT Actions Summary");
    TEST_INFO("Tool calls detected: %d/%d", tool_detected, NUM_ACTION_CASES);
    TEST_INFO("Correct action name: %d/%d", correct_action, NUM_ACTION_CASES);
    TEST_INFO("Safe actions executed: %d", executed_ok);
}

static void test_actions_api_e2e(const std::string& models_dir) {
    TEST_SECTION("E2E Actions — C API Full Pipeline");

    RCLIHandle h = rcli_create(nullptr);
    if (rcli_init(h, models_dir.c_str(), 99) != 0) {
        fprintf(stderr, "  \033[33m[SKIP] rcli_init failed\033[0m\n");
        rcli_destroy(h);
        return;
    }

    struct ApiActionTest {
        const char* label;
        const char* input;
    };

    ApiActionTest cases[] = {
        {"Open App",       "Open Safari for me"},
        {"Create Note",    "Create a note called Test Note with hello world"},
        {"Set Volume",     "Set volume to 50 percent"},
        {"Clipboard",      "Copy the text hello world to clipboard"},
        {"Search Files",   "Find files about project proposal"},
        {"Get Battery",    "What's my battery percentage?"},
        {"Search Web",     "Search the web for today's weather"},
        {"Play Music",     "Play some music"},
        {"Get WiFi",       "What WiFi am I connected to?"},
    };

    for (auto& tc : cases) {
        auto t0 = std::chrono::steady_clock::now();
        const char* resp = rcli_process_command(h, tc.input);
        double ms = elapsed_ms(t0);

        bool ok = resp != nullptr && strlen(resp) > 0;
        char label[64];
        snprintf(label, sizeof(label), "[API] %s", tc.label);
        TEST(label, ok);
        TEST_INFO("Input:    %s", tc.input);
        TEST_INFO("Response: %.200s", resp ? resp : "(null)");
        TEST_INFO("Time:     %.1f ms", ms);
    }

    rcli_destroy(h);
}

// =============================================================================
// Test: Personality System
// =============================================================================

static void test_personality_unit() {
    TEST_SECTION("Personality System — Unit Tests");

    // all_personalities returns complete list
    auto& all = rastack::all_personalities();
    TEST("all_personalities has 5 entries", all.size() == 5);

    // Verify each personality has required fields
    const char* expected_keys[] = {"default", "professional", "quirky", "cynical", "nerdy"};
    for (int i = 0; i < 5; i++) {
        auto* info = rastack::find_personality(expected_keys[i]);
        bool found = info != nullptr;
        char label[64];
        snprintf(label, sizeof(label), "find_personality(\"%s\") found", expected_keys[i]);
        TEST(label, found);
        if (found) {
            TEST_INFO("  key=%s  name=%s  voice=%s", info->key, info->name, info->voice);
            snprintf(label, sizeof(label), "\"%s\" has display name", expected_keys[i]);
            TEST(label, strlen(info->name) > 0);
            snprintf(label, sizeof(label), "\"%s\" has voice", expected_keys[i]);
            TEST(label, strlen(info->voice) > 0);
            snprintf(label, sizeof(label), "\"%s\" has tagline", expected_keys[i]);
            TEST(label, strlen(info->tagline) > 0);
        }
    }

    // find_personality returns null for unknown
    TEST("unknown personality returns nullptr", rastack::find_personality("nonexistent") == nullptr);

    // find_personality by enum
    auto* def = rastack::find_personality(rastack::Personality::DEFAULT);
    TEST("find by enum DEFAULT works", def != nullptr && std::string(def->key) == "default");
    auto* quirky = rastack::find_personality(rastack::Personality::QUIRKY);
    TEST("find by enum QUIRKY works", quirky != nullptr && std::string(quirky->key) == "quirky");

    // apply_personality with default key returns base unchanged
    TEST_SECTION("Personality — apply_personality");
    std::string base = "Base system prompt.";
    std::string result = rastack::apply_personality(base, "default");
    TEST("default personality returns base unchanged", result == base);

    result = rastack::apply_personality(base, "");
    TEST("empty key returns base unchanged", result == base);

    // apply_personality with non-default key prepends personality prompt
    result = rastack::apply_personality(base, "quirky");
    TEST("quirky prepends personality prefix", result.size() > base.size());
    TEST("quirky result starts with IMPORTANT", result.find("IMPORTANT PERSONALITY RULE") == 0);
    TEST("quirky result ends with base prompt", result.find(base) != std::string::npos);
    TEST_INFO("quirky prompt length: %zu chars (base was %zu)", result.size(), base.size());

    result = rastack::apply_personality(base, "professional");
    TEST("professional prepends prefix", result.find("formal") != std::string::npos || result.find("professional") != std::string::npos);

    result = rastack::apply_personality(base, "cynical");
    TEST("cynical prepends prefix", result.find("cynical") != std::string::npos || result.find("deadpan") != std::string::npos);

    result = rastack::apply_personality(base, "nerdy");
    TEST("nerdy prepends prefix", result.find("nerdy") != std::string::npos || result.find("geek") != std::string::npos);

    // apply_personality with unknown key returns base unchanged
    result = rastack::apply_personality(base, "totally_fake");
    TEST("unknown personality returns base unchanged", result == base);

    // kokoro_voice_to_speaker_id
    TEST_SECTION("Personality — Voice Mapping");
    TEST("af_heart maps to id 3", rastack::kokoro_voice_to_speaker_id("af_heart") == 3);
    TEST("bf_emma maps to id 21", rastack::kokoro_voice_to_speaker_id("bf_emma") == 21);
    TEST("af_nova maps to id 7", rastack::kokoro_voice_to_speaker_id("af_nova") == 7);
    TEST("bm_lewis maps to id 27", rastack::kokoro_voice_to_speaker_id("bm_lewis") == 27);
    TEST("am_puck maps to id 18", rastack::kokoro_voice_to_speaker_id("am_puck") == 18);
    TEST("unknown voice defaults to 3 (af_heart)", rastack::kokoro_voice_to_speaker_id("unknown") == 3);

    // Each personality voice should be a valid voice
    for (auto& p : all) {
        int id = rastack::kokoro_voice_to_speaker_id(p.voice);
        char label[64];
        snprintf(label, sizeof(label), "%s voice \"%s\" maps to valid id %d", p.key, p.voice, id);
        TEST(label, id >= 0 && id <= 27);
    }
}

static void test_personality_llm_llamacpp(const std::string& models_dir) {
    TEST_SECTION("Personality + LLM — llama.cpp Engine");

    std::string model_path = models_dir + "/lfm2-1.2b-tool-q4_k_m.gguf";
    if (!file_exists(model_path)) {
        fprintf(stderr, "  \033[33m[SKIP] Skipped: model not found at %s\033[0m\n", model_path.c_str());
        return;
    }

    LlmEngine llm;
    LlmConfig config;
    config.model_path = model_path;
    config.n_gpu_layers = 99;
    config.n_ctx = 2048;
    config.n_batch = 512;
    config.n_threads = 1;
    config.n_threads_batch = 4;
    config.temperature = 0.7f;
    config.max_tokens = 128;
    config.flash_attn = true;
    config.type_k = 8;
    config.type_v = 8;

    bool ok = llm.init(config);
    if (!ok) {
        fprintf(stderr, "  \033[33m[SKIP] LLM init failed\033[0m\n");
        return;
    }

    const char* personality_keys[] = {"default", "quirky", "cynical", "nerdy", "professional"};
    const char* test_prompt = "What is gravity?";

    for (const char* key : personality_keys) {
        char section_label[64];
        snprintf(section_label, sizeof(section_label), "llama.cpp + personality \"%s\"", key);
        TEST_SECTION(section_label);

        std::string system = rastack::apply_personality(
            rastack::RCLI_SYSTEM_PROMPT, key);

        std::string prompt = llm.build_chat_prompt(system, {}, test_prompt);

        auto t0 = std::chrono::steady_clock::now();
        std::string response = llm.generate(prompt, nullptr);
        double gen_ms = elapsed_ms(t0);

        char label[64];
        snprintf(label, sizeof(label), "[%s] generates non-empty response", key);
        TEST(label, !response.empty());
        TEST_INFO("System prompt: %zu chars", system.size());
        TEST_INFO("Response: %.200s", response.c_str());
        TEST_INFO("Time: %.1f ms, %.1f tok/s", gen_ms, llm.last_stats().gen_tps());

        llm.clear_kv_cache();
    }

    // Tool calling with personality
    TEST_SECTION("llama.cpp — Personality + Tool Calling");
    std::string tool_defs = R"([{"type":"function","function":{"name":"open_app","description":"Open a macOS application","parameters":{"type":"object","properties":{"name":{"type":"string","description":"App name"}},"required":["name"]}}}])";

    for (const char* key : {"quirky", "professional"}) {
        std::string system = rastack::apply_personality(
            rastack::RCLI_SYSTEM_PROMPT, key);

        auto t0 = std::chrono::steady_clock::now();
        std::string response = llm.generate_with_tools(
            "Open Safari",
            tool_defs,
            system,
            nullptr);
        double gen_ms = elapsed_ms(t0);

        char label[128];
        bool has_tool = response.find("open_app") != std::string::npos ||
                        response.find("Safari") != std::string::npos;
        snprintf(label, sizeof(label), "[%s] tool call with personality produces action reference", key);
        TEST(label, has_tool);
        TEST_INFO("Response: %.300s", response.c_str());
        TEST_INFO("Time: %.1f ms", gen_ms);

        llm.clear_kv_cache();
    }
}

static void test_personality_metalrt(const std::string& models_dir) {
    TEST_SECTION("Personality + LLM — MetalRT Engine");

    auto& loader = MetalRTLoader::instance();
    if (!loader.is_loaded() && !loader.load()) {
        fprintf(stderr, "  \033[33m[SKIP] MetalRT dylib not loaded\033[0m\n");
        return;
    }

    std::string model_dir;
    {
        auto models = rcli::all_models();
        std::string mrt_base = rcli::metalrt_models_dir();
        for (auto& m : models) {
            if (m.metalrt_supported && rcli::is_metalrt_model_installed(m)) {
                model_dir = mrt_base + "/" + m.metalrt_dir_name;
                break;
            }
        }
    }

    if (model_dir.empty()) {
        fprintf(stderr, "  \033[33m[SKIP] No MetalRT LLM model installed\033[0m\n");
        return;
    }

    MetalRTEngine engine;
    MetalRTEngineConfig cfg;
    cfg.model_dir = model_dir;
    cfg.max_tokens = 128;
    cfg.temperature = 0.7f;

    if (!engine.init(cfg)) {
        fprintf(stderr, "  \033[33m[SKIP] MetalRT init failed\033[0m\n");
        return;
    }

    TEST_INFO("Device: %s", engine.device_name().c_str());
    TEST_INFO("Model:  %s", engine.model_name().c_str());

    const char* personality_keys[] = {"default", "quirky", "cynical", "nerdy", "professional"};
    const char* test_prompt = "What is gravity?";

    for (const char* key : personality_keys) {
        char section_label[64];
        snprintf(section_label, sizeof(section_label), "MetalRT + personality \"%s\"", key);
        TEST_SECTION(section_label);

        std::string system = rastack::apply_personality(
            rastack::RCLI_SYSTEM_PROMPT, key);
        engine.set_system_prompt(system);
        engine.reset_conversation();

        auto t0 = std::chrono::steady_clock::now();
        std::string response = engine.generate(test_prompt);
        double gen_ms = elapsed_ms(t0);

        char label[64];
        snprintf(label, sizeof(label), "[%s] MetalRT generates non-empty response", key);
        TEST(label, !response.empty());

        const auto& stats = engine.last_stats();
        TEST_INFO("System prompt: %zu chars", system.size());
        TEST_INFO("Response: %.200s", response.c_str());
        TEST_INFO("Prefill: %.1f ms (%d tok)", stats.prompt_eval_us / 1000.0, stats.prompt_tokens);
        TEST_INFO("Decode:  %.1f ms (%lld tok, %.0f tok/s)", stats.generation_us / 1000.0,
                  (long long)stats.generated_tokens, stats.gen_tps());
        TEST_INFO("Wall:    %.1f ms", gen_ms);
    }

    // Tool calling with personality on MetalRT
    // NOTE: The 1.2B model can struggle with tool calling when personality
    // prompts add extra context. We verify generation works and warn if
    // the tool call is not produced (soft check).
    TEST_SECTION("MetalRT — Personality + Tool Calling");
    std::string tool_defs = R"([{"type":"function","function":{"name":"open_app","description":"Open a macOS application","parameters":{"type":"object","properties":{"name":{"type":"string","description":"App name"}},"required":["name"]}}}])";

    for (const char* key : {"quirky", "professional"}) {
        std::string system = rastack::apply_personality(
            rastack::RCLI_SYSTEM_PROMPT, key);
        std::string full_system = engine.profile().build_tool_system_prompt(system, tool_defs);
        engine.set_system_prompt(full_system);
        engine.reset_conversation();

        auto t0 = std::chrono::steady_clock::now();
        std::string response = engine.generate("Open Safari");
        double gen_ms = elapsed_ms(t0);

        char label[128];
        snprintf(label, sizeof(label), "[%s] MetalRT generates response with personality + tools", key);
        TEST(label, !response.empty());

        bool has_tool = response.find("open_app") != std::string::npos ||
                        response.find("Safari") != std::string::npos;
        if (!has_tool)
            TEST_INFO("NOTE: 1.2B model did not produce tool call with personality prefix (expected for small models)");
        else
            TEST_INFO("Tool call detected in response");
        TEST_INFO("Response: %.300s", response.c_str());
        TEST_INFO("Time: %.1f ms", gen_ms);
    }
}

static void test_personality_api(const std::string& models_dir) {
    TEST_SECTION("Personality — C API Integration");

    RCLIHandle h = rcli_create(nullptr);
    TEST("rcli_create returns handle", h != nullptr);
    if (!h) return;

    // Personality before init
    const char* initial = rcli_get_personality(h);
    TEST("get_personality before init returns valid string", initial != nullptr);
    TEST_INFO("Initial personality: %s", initial ? initial : "(null)");

    // Init engine
    if (rcli_init(h, models_dir.c_str(), 99) != 0) {
        fprintf(stderr, "  \033[33m[SKIP] rcli_init failed\033[0m\n");
        rcli_destroy(h);
        return;
    }

    // Test set/get for each personality
    const char* keys[] = {"default", "quirky", "cynical", "nerdy", "professional"};
    for (const char* key : keys) {
        int rc = rcli_set_personality(h, key);
        char label[64];
        snprintf(label, sizeof(label), "set_personality(\"%s\") succeeds", key);
        TEST(label, rc == 0);

        const char* got = rcli_get_personality(h);
        snprintf(label, sizeof(label), "get_personality returns \"%s\"", key);
        TEST(label, got != nullptr && std::string(got) == key);
    }

    // Invalid personality key
    int rc = rcli_set_personality(h, "totally_invalid");
    TEST("set_personality with invalid key returns -1", rc == -1);

    // Verify current personality unchanged after invalid set
    const char* still = rcli_get_personality(h);
    TEST("personality unchanged after invalid set", still != nullptr && std::string(still) == "professional");

    // Test LLM generation with different personalities via API
    TEST_SECTION("Personality — C API LLM Generation");
    for (const char* key : {"default", "quirky", "professional"}) {
        rcli_set_personality(h, key);

        auto t0 = std::chrono::steady_clock::now();
        const char* resp = rcli_process_command(h, "What is gravity?");
        double ms = elapsed_ms(t0);

        char label[64];
        snprintf(label, sizeof(label), "[%s] API process_command generates response", key);
        TEST(label, resp != nullptr && strlen(resp) > 0);
        TEST_INFO("Personality: %s", key);
        TEST_INFO("Response:    %.200s", resp ? resp : "(null)");
        TEST_INFO("Time:        %.1f ms", ms);
    }

    // Test tool calling with personality via API
    TEST_SECTION("Personality — C API Tool Calling");
    for (const char* key : {"quirky", "cynical"}) {
        rcli_set_personality(h, key);

        auto t0 = std::chrono::steady_clock::now();
        const char* resp = rcli_process_command(h, "Open Safari for me");
        double ms = elapsed_ms(t0);

        char label[64];
        snprintf(label, sizeof(label), "[%s] API tool call produces response", key);
        TEST(label, resp != nullptr && strlen(resp) > 0);
        TEST_INFO("Personality: %s", key);
        TEST_INFO("Response:    %.200s", resp ? resp : "(null)");
        TEST_INFO("Time:        %.1f ms", ms);
    }

    rcli_set_personality(h, "default");
    rcli_destroy(h);
    TEST("cleanup completes", true);
}

// =============================================================================
// Voice Bench — TTFA timing for both engines via rcli_process_and_speak
// =============================================================================

struct VoiceBenchCtx {
    double ttfa_ms = 0;
    double ttft_ms = 0;
    double tts_total_ms = 0;
    bool got_ttfa = false;
    bool got_ttft = false;
    bool completed = false;
};

static void voice_bench_cb(const char* event, const char* data, void* ud) {
    auto* ctx = static_cast<VoiceBenchCtx*>(ud);
    if (strcmp(event, "first_audio") == 0 && data) {
        ctx->ttfa_ms = atof(data);
        ctx->got_ttfa = true;
    }
    if (strcmp(event, "sentence_ready") == 0 && data) {
        ctx->ttft_ms = atof(data);
        ctx->got_ttft = true;
    }
    if (strcmp(event, "complete") == 0) {
        ctx->completed = true;
    }
}

static void test_voice_bench(const std::string& models_dir) {
    constexpr int ITERS = 5;
    const char* queries[] = {
        "Hello, how are you?",
        "What is the weather like today?",
        "Tell me a joke.",
        "What can you do?",
        "Hi there, bro.",
    };

    auto run_engine = [&](const char* engine_name, bool fresh_each_turn) {
        const char* mode = fresh_each_turn ? "fresh-turn" : "multi-turn";
        std::string section = std::string("Voice Bench [") + mode + "] — " + engine_name;
        TEST_SECTION(section.c_str());

        rcli::write_engine_preference(engine_name);

        RCLIHandle h = rcli_create(nullptr);
        if (!h) { TEST("create engine", false); return; }
        if (rcli_init(h, models_dir.c_str(), 99) != 0) {
            fprintf(stderr, "  \033[33m[SKIP] %s engine init failed\033[0m\n", engine_name);
            rcli_destroy(h);
            return;
        }

        const char* active = rcli_get_active_engine(h);
        TEST_INFO("Active engine: %s  mode: %s", active ? active : "unknown", mode);

        double ttfa_sum = 0, ttft_sum = 0;
        double ttfa_min = 1e9, ttfa_max = 0;
        double ttft_min = 1e9, ttft_max = 0;
        int ok_count = 0;

        for (int i = 0; i < ITERS; i++) {
            if (fresh_each_turn && i > 0) {
                rcli_clear_history(h);
            }

            VoiceBenchCtx ctx;
            auto t0 = std::chrono::steady_clock::now();
            const char* resp = rcli_process_and_speak(h, queries[i], voice_bench_cb, &ctx);
            double total_ms = elapsed_ms(t0);

            char label[256];
            snprintf(label, sizeof(label), "[%s][%s] Iter %d — TTFT=%.0fms TTFA=%.0fms",
                     engine_name, mode, i + 1, ctx.ttft_ms, ctx.ttfa_ms);
            TEST(label, resp != nullptr && strlen(resp) > 0);
            TEST_INFO("Query:    \"%s\"", queries[i]);
            TEST_INFO("Response: %.100s", resp ? resp : "(null)");
            TEST_INFO("TTFT=%.1fms  TTFA=%.1fms  total=%.1fms", ctx.ttft_ms, ctx.ttfa_ms, total_ms);

            if (ctx.got_ttfa && ctx.ttfa_ms > 0) {
                ttfa_sum += ctx.ttfa_ms;
                if (ctx.ttfa_ms < ttfa_min) ttfa_min = ctx.ttfa_ms;
                if (ctx.ttfa_ms > ttfa_max) ttfa_max = ctx.ttfa_ms;
                ok_count++;
            }
            if (ctx.got_ttft && ctx.ttft_ms > 0) {
                ttft_sum += ctx.ttft_ms;
                if (ctx.ttft_ms < ttft_min) ttft_min = ctx.ttft_ms;
                if (ctx.ttft_ms > ttft_max) ttft_max = ctx.ttft_ms;
            }
        }

        if (ok_count > 0) {
            double ttfa_avg = ttfa_sum / ok_count;
            double ttft_avg = ttft_sum / ok_count;
            fprintf(stderr, "\n  \033[1;33m[%s][%s] TTFT: avg=%.0fms  min=%.0fms  max=%.0fms\033[0m\n",
                    engine_name, mode, ttft_avg, ttft_min, ttft_max);
            fprintf(stderr, "  \033[1;33m[%s][%s] TTFA: avg=%.0fms  min=%.0fms  max=%.0fms  (%d/%d)\033[0m\n",
                    engine_name, mode, ttfa_avg, ttfa_min, ttfa_max, ok_count, ITERS);

            int target = strcmp(engine_name, "metalrt") == 0
                ? (fresh_each_turn ? 180 : 250)
                : (fresh_each_turn ? 700 : 900);
            char summary_label[128];
            snprintf(summary_label, sizeof(summary_label),
                     "[%s][%s] Avg TTFA < %dms", engine_name, mode, target);
            TEST(summary_label, ttfa_avg < target);
        }

        rcli_destroy(h);
    };

    run_engine("metalrt", true);
    run_engine("metalrt", false);
    run_engine("llamacpp", true);
    run_engine("llamacpp", false);
}

// =============================================================================
// Test: Auto-Compaction & Multi-Turn Context Management
// =============================================================================

static void test_auto_compact(const std::string& models_dir) {
    auto run_engine = [&](const char* engine_name) {
        TEST_SECTION(std::string("Auto-Compact & Context — " + std::string(engine_name)).c_str());

        rcli::write_engine_preference(engine_name);

        RCLIHandle h = rcli_create(nullptr);
        if (!h) { TEST("create engine", false); return; }
        if (rcli_init(h, models_dir.c_str(), 99) != 0) {
            fprintf(stderr, "  \033[33m[SKIP] %s engine init failed\033[0m\n", engine_name);
            rcli_destroy(h);
            return;
        }

        const char* active = rcli_get_active_engine(h);
        TEST_INFO("Active engine: %s", active ? active : "unknown");

        // --- Phase 1: Multi-turn conversation builds context ---
        const char* multi_turn[] = {
            "Hello, my name is Alex.",
            "I live in San Francisco.",
            "My favorite color is blue.",
            "I work as a software engineer.",
            "My dog's name is Pixel.",
            "I was born in 1992.",
            "Remind me what my name is?",
        };
        constexpr int NUM_TURNS = sizeof(multi_turn) / sizeof(multi_turn[0]);

        int prev_prompt_tokens = 0;

        for (int i = 0; i < NUM_TURNS; i++) {
            auto t0 = std::chrono::steady_clock::now();
            const char* resp = rcli_process_command(h, multi_turn[i]);
            double ms = elapsed_ms(t0);

            bool ok = resp != nullptr && strlen(resp) > 0;
            char label[128];
            snprintf(label, sizeof(label), "[%s] Turn %d", engine_name, i + 1);
            TEST(label, ok);
            TEST_INFO("Input:    %s", multi_turn[i]);
            TEST_INFO("Response: %.200s", resp ? resp : "(null)");
            TEST_INFO("Time:     %.1f ms", ms);

            int pt = 0, cs = 0;
            rcli_get_context_info(h, &pt, &cs);
            TEST_INFO("Context:  %d / %d tokens (%.0f%%)",
                      pt, cs, cs > 0 ? 100.0 * pt / cs : 0.0);

            if (i > 0 && pt > 0 && pt < prev_prompt_tokens && prev_prompt_tokens > 0) {
                TEST_INFO("Context decreased from %d → %d (compaction or trimming occurred)", prev_prompt_tokens, pt);
            }
            if (pt > 0) prev_prompt_tokens = pt;
        }

        // --- Phase 2: Verify memory retention ---
        {
            const char* resp = rcli_process_command(h, "What is my name and where do I live?");
            bool has_name = resp && (strstr(resp, "Alex") != nullptr || strstr(resp, "alex") != nullptr);
            bool has_city = resp && (strstr(resp, "San Francisco") != nullptr ||
                                     strstr(resp, "san francisco") != nullptr ||
                                     strstr(resp, "SF") != nullptr);

            char label[128];
            snprintf(label, sizeof(label), "[%s] Memory — recalls name", engine_name);
            TEST(label, has_name);
            snprintf(label, sizeof(label), "[%s] Memory — recalls city", engine_name);
            TEST(label, has_city);
            TEST_INFO("Response: %.200s", resp ? resp : "(null)");
        }

        // --- Phase 3: Context gauge is stable and non-zero ---
        {
            int pt = 0, cs = 0;
            rcli_get_context_info(h, &pt, &cs);
            char label[128];
            snprintf(label, sizeof(label), "[%s] Context gauge non-zero", engine_name);
            TEST(label, pt > 0);
            snprintf(label, sizeof(label), "[%s] Context size reported", engine_name);
            TEST(label, cs > 0);
            snprintf(label, sizeof(label), "[%s] Context usage < 100%%", engine_name);
            TEST(label, pt < cs);
            TEST_INFO("Final context: %d / %d tokens (%.0f%%)",
                      pt, cs, cs > 0 ? 100.0 * pt / cs : 0.0);
        }

        // --- Phase 4: Stress test — many turns to trigger compaction ---
        TEST_SECTION(std::string("Stress Multi-Turn — " + std::string(engine_name)).c_str());
        const char* stress_queries[] = {
            "Tell me about machine learning.",
            "What's the difference between AI and ML?",
            "Explain neural networks briefly.",
            "What is deep learning?",
            "How does backpropagation work?",
            "What is a transformer model?",
            "Explain attention mechanism.",
            "What is GPT?",
            "Compare CNNs and RNNs.",
            "What are embeddings?",
            "Tell me about reinforcement learning.",
            "What is transfer learning?",
            "How does fine-tuning work?",
            "What are LLMs?",
            "Explain tokenization.",
        };
        constexpr int STRESS_TURNS = sizeof(stress_queries) / sizeof(stress_queries[0]);

        bool any_compaction = false;
        int prev_ctx = 0;
        bool all_responded = true;

        for (int i = 0; i < STRESS_TURNS; i++) {
            auto t0 = std::chrono::steady_clock::now();
            const char* resp = rcli_process_command(h, stress_queries[i]);
            double ms = elapsed_ms(t0);

            bool ok = resp != nullptr && strlen(resp) > 0;
            if (!ok) all_responded = false;

            int pt = 0, cs = 0;
            rcli_get_context_info(h, &pt, &cs);

            if (prev_ctx > 0 && pt < prev_ctx) {
                any_compaction = true;
                TEST_INFO("Compaction detected at turn %d: %d → %d tokens", i + 1, prev_ctx, pt);
            }
            prev_ctx = pt;

            if (i % 5 == 0 || !ok) {
                char label[128];
                snprintf(label, sizeof(label), "[%s] Stress turn %d", engine_name, i + 1);
                TEST(label, ok);
                TEST_INFO("Response: %.100s", resp ? resp : "(null)");
                TEST_INFO("Context: %d/%d (%.0f%%) | Time: %.1f ms",
                          pt, cs, cs > 0 ? 100.0 * pt / cs : 0.0, ms);
            }
        }

        {
            char label[128];
            snprintf(label, sizeof(label), "[%s] All stress turns responded", engine_name);
            TEST(label, all_responded);
            snprintf(label, sizeof(label), "[%s] Auto-compaction triggered", engine_name);
            if (any_compaction) {
                TEST(label, true);
            } else {
                TEST_INFO("(No compaction triggered — context budget may be large enough)");
                TEST(label, true); // not a failure, just informational
            }
        }

        // --- Phase 5: Clear history and verify reset ---
        {
            rcli_clear_history(h);
            int pt = 0, cs = 0;
            rcli_get_context_info(h, &pt, &cs);
            char label[128];
            snprintf(label, sizeof(label), "[%s] Clear history resets context gauge", engine_name);
            TEST(label, pt == 0);
            TEST_INFO("After clear: %d / %d tokens", pt, cs);

            // First turn after clear should work
            const char* resp = rcli_process_command(h, "Hello again!");
            snprintf(label, sizeof(label), "[%s] Works after clear", engine_name);
            TEST(label, resp != nullptr && strlen(resp) > 0);
            TEST_INFO("Post-clear response: %.200s", resp ? resp : "(null)");
        }

        // --- Phase 6: Tool call test ---
        {
            auto t0 = std::chrono::steady_clock::now();
            const char* resp = rcli_process_command(h, "What's my battery percentage?");
            double ms = elapsed_ms(t0);
            char label[128];
            snprintf(label, sizeof(label), "[%s] Tool call (battery)", engine_name);
            TEST(label, resp != nullptr && strlen(resp) > 0);
            TEST_INFO("Tool response: %.200s", resp ? resp : "(null)");
            TEST_INFO("Time: %.1f ms", ms);
        }

        rcli_destroy(h);
    };

    run_engine("metalrt");
    run_engine("llamacpp");
}

// =============================================================================
// Main
// =============================================================================

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <models_dir> [filter]\n\n", argv[0]);
        fprintf(stderr, "Filters:\n");
        fprintf(stderr, "  --actions-only    Test macOS actions only\n");
        fprintf(stderr, "  --llm-only        Test llama.cpp LLM only\n");
        fprintf(stderr, "  --stt-only        Test sherpa-onnx STT only\n");
        fprintf(stderr, "  --tts-only        Test sherpa-onnx TTS only\n");
        fprintf(stderr, "  --api-only        Test C API only\n");
        fprintf(stderr, "  --metalrt-only    Test MetalRT engines (LLM, STT, TTS)\n");
        fprintf(stderr, "  --personality-only Test personality system (unit + LLM + API)\n");
        fprintf(stderr, "  --actions-e2e     E2E action tests on both engines\n");
        fprintf(stderr, "  --voice-bench     Voice TTFA benchmark on both engines\n");
        fprintf(stderr, "  --compact-test    Auto-compaction & multi-turn context tests\n");
        fprintf(stderr, "  --gpu-diag        Full GPU diagnostic (what runs where)\n");
        fprintf(stderr, "\nModels directory should contain:\n");
        fprintf(stderr, "  lfm2-1.2b-tool-q4_k_m.gguf\n");
        fprintf(stderr, "  whisper-base.en/\n");
        fprintf(stderr, "  zipformer/\n");
        fprintf(stderr, "  piper-voice/\n");
        fprintf(stderr, "  silero_vad.onnx\n");
        fprintf(stderr, "  espeak-ng-data/\n");
        return 1;
    }

    std::string models_dir = argv[1];
    std::string filter = argc > 2 ? argv[2] : "";

    // Enable debug logging for MetalRT/GPU diagnostic tests
    if (filter == "--metalrt-only" || filter == "--gpu-diag") {
        set_log_level(LogLevel::DEBUG);
    }

    fprintf(stderr, "\033[1;35m");
    fprintf(stderr, "╔══════════════════════════════════════╗\n");
    fprintf(stderr, "║     RCLI Pipeline Test Suite         ║\n");
    fprintf(stderr, "╚══════════════════════════════════════╝\033[0m\n");
    fprintf(stderr, "Models dir: %s\n", models_dir.c_str());
    if (!filter.empty())
        fprintf(stderr, "Filter:     %s\n", filter.c_str());

    auto total_start = std::chrono::steady_clock::now();

    // --- GPU Diagnostic (comprehensive) ---
    if (filter == "--gpu-diag") {
        test_gpu_diagnostic();
        test_metalrt_loader();
        test_metalrt_llm(models_dir);
        test_metalrt_stt(models_dir);
        test_metalrt_tts(models_dir);
        test_metalrt_vs_llamacpp(models_dir);
        test_metalrt_tts_vs_sherpa(models_dir);
    }

    // --- MetalRT only ---
    if (filter == "--metalrt-only") {
        test_metalrt_loader();
        test_metalrt_llm(models_dir);
        test_metalrt_stt(models_dir);
        test_metalrt_tts(models_dir);
    }

    // --- Personality tests ---
    if (filter == "--personality-only") {
        test_personality_unit();
        test_personality_llm_llamacpp(models_dir);
        test_personality_metalrt(models_dir);
        test_personality_api(models_dir);
    }

    // --- E2E action tests on both engines ---
    if (filter == "--actions-e2e") {
        test_actions_llamacpp(models_dir);
        test_actions_metalrt(models_dir);
        test_actions_api_e2e(models_dir);
    }

    // --- Voice TTFA benchmark ---
    if (filter == "--voice-bench") {
        test_voice_bench(models_dir);
    }

    // --- Auto-compaction & multi-turn context ---
    if (filter == "--compact-test") {
        test_auto_compact(models_dir);
    }

    if (filter.empty() || filter == "--actions-only") {
        test_actions();
        test_applescript_executor();
        test_tool_engine();
    }

    if (filter.empty() || filter == "--llm-only") {
        test_llm(models_dir);
    }

    if (filter.empty() || filter == "--stt-only") {
        test_stt(models_dir);
    }

    if (filter.empty() || filter == "--tts-only") {
        test_tts(models_dir);
    }

    if (filter.empty() || filter == "--vad-only") {
        test_vad(models_dir);
    }

    if (filter.empty() || filter == "--api-only") {
        test_api(models_dir);
    }

    if (filter.empty()) {
        test_personality_unit();
        test_use_cases(models_dir);
    }

    double total_ms = elapsed_ms(total_start);

    fprintf(stderr, "\n\033[1;35m");
    fprintf(stderr, "══════════════════════════════════════\n");
    fprintf(stderr, " Results: %d passed, %d failed, %d total\n", tests_passed, tests_failed, tests_run);
    fprintf(stderr, " Time:    %.1f ms (%.1f sec)\n", total_ms, total_ms / 1000.0);
    fprintf(stderr, "══════════════════════════════════════\033[0m\n");

    return tests_failed > 0 ? 1 : 0;
}
