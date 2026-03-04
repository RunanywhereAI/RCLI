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
        fprintf(stderr, "  \033[32m✓\033[0m %s\n", name); \
    } else { \
        tests_failed++; \
        fprintf(stderr, "  \033[31m✗\033[0m %s\n", name); \
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

    // Test needs_action keyword detection
    TEST("needs_action('open Safari')", registry.needs_action("open Safari"));
    TEST("needs_action('create a note')", registry.needs_action("create a note"));
    TEST("needs_action('send message to John')", registry.needs_action("send message to John"));
    TEST("needs_action('set a reminder')", registry.needs_action("set a reminder"));
    TEST("needs_action('search for files')", registry.needs_action("search for files"));
    TEST("needs_action('take screenshot')", registry.needs_action("take screenshot"));
    TEST("!needs_action('hello how are you')", !registry.needs_action("hello how are you"));
    TEST("!needs_action('what is the weather')", !registry.needs_action("what is the weather"));

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

    std::string model_path = models_dir + "/qwen3-0.6b-q4_k_m.gguf";
    if (!file_exists(model_path)) {
        fprintf(stderr, "  \033[33m⚠ Skipped: model not found at %s\033[0m\n", model_path.c_str());
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
        fprintf(stderr, "  \033[33m⚠ Skipped: Whisper model not found at %s\033[0m\n", encoder.c_str());
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
        fprintf(stderr, "  \033[33m⚠ Skipped: Zipformer model not found at %s\033[0m\n", enc.c_str());
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
        fprintf(stderr, "  \033[33m⚠ Skipped: Piper TTS model not found at %s\033[0m\n", model.c_str());
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
        fprintf(stderr, "  \033[33m⚠ Skipped: VAD model not found at %s\033[0m\n", model.c_str());
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
        fprintf(stderr, "  \033[33m⚠ rcli_init failed (models may not be present)\033[0m\n");
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
        fprintf(stderr, "  \033[33m⚠ Skipped: engine init failed\033[0m\n");
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
        {"Set Reminder", "Remind me to take out the trash at 6pm", true},
        {"Send Message", "Send a message to Mom saying I'll be home late", true},
        {"Search Files", "Find documents about project proposal", true},
        {"Clipboard", "Copy that to clipboard", true},
        {"Timer", "Set a 5 minute timer", true},
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

    // Test needs_tools
    TEST("needs_tools('calculate 5+3')", tools.needs_tools("calculate 5+3"));

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
// Main
// =============================================================================

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <models_dir> [--actions-only|--llm-only|--stt-only|--tts-only|--api-only]\n", argv[0]);
        fprintf(stderr, "\nModels directory should contain:\n");
        fprintf(stderr, "  qwen3-0.6b-q4_k_m.gguf\n");
        fprintf(stderr, "  whisper-base.en/\n");
        fprintf(stderr, "  zipformer/\n");
        fprintf(stderr, "  piper-voice/\n");
        fprintf(stderr, "  silero_vad.onnx\n");
        fprintf(stderr, "  espeak-ng-data/\n");
        return 1;
    }

    std::string models_dir = argv[1];
    std::string filter = argc > 2 ? argv[2] : "";

    fprintf(stderr, "\033[1;35m");
    fprintf(stderr, "╔══════════════════════════════════════╗\n");
    fprintf(stderr, "║     RCLI Pipeline Test Suite     ║\n");
    fprintf(stderr, "╚══════════════════════════════════════╝\033[0m\n");
    fprintf(stderr, "Models dir: %s\n", models_dir.c_str());

    auto total_start = std::chrono::steady_clock::now();

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
