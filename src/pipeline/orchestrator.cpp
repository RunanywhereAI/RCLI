#include "pipeline/orchestrator.h"
#include "pipeline/text_sanitizer.h"
#include "pipeline/wake_word_detector.h"
#include "core/base64.h"
#include "core/personality.h"
#include "core/log.h"
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <future>
#include <string>
#include <csignal>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>

// --- MetalRT crash recovery ---
// Before calling into the MetalRT dylib for init, we write a breadcrumb file.
// If the dylib segfaults (common on certain M3/M4 hardware), the breadcrumb
// persists.  On the next launch we detect it and skip MetalRT entirely,
// falling back to llama.cpp so the user isn't stuck in a crash loop.

static std::string metalrt_crash_breadcrumb_path() {
    const char* home = getenv("HOME");
    if (!home) return "/tmp/.rcli_metalrt_crash";
    return std::string(home) + "/.rcli/.metalrt_init_in_progress";
}

static bool metalrt_previously_crashed() {
    struct stat st;
    return stat(metalrt_crash_breadcrumb_path().c_str(), &st) == 0;
}

static void metalrt_breadcrumb_create() {
    std::string path = metalrt_crash_breadcrumb_path();
    // Ensure parent dir exists
    std::string dir = path.substr(0, path.rfind('/'));
    mkdir(dir.c_str(), 0755);
    FILE* f = fopen(path.c_str(), "w");
    if (f) { fprintf(f, "metalrt init in progress\n"); fclose(f); }
}

static void metalrt_breadcrumb_remove() {
    unlink(metalrt_crash_breadcrumb_path().c_str());
}

// Signal handler installed during MetalRT init to provide a helpful
// crash message instead of bare "segmentation fault".
static struct sigaction s_old_sigsegv, s_old_sigbus;

static void metalrt_crash_handler(int sig) {
    // Write directly to stderr (signal-safe)
    const char* msg =
        "\n\n  MetalRT GPU engine crashed during initialization.\n"
        "  This is a known issue on some M3/M4 Macs.\n\n"
        "  RCLI will automatically use llama.cpp on next launch.\n"
        "  Or run manually:  rcli engine llamacpp\n\n";
    (void)write(STDERR_FILENO, msg, strlen(msg));

    // Leave breadcrumb so next launch skips MetalRT
    // (breadcrumb was already created before init started)

    // Restore original handler and re-raise
    sigaction(SIGSEGV, &s_old_sigsegv, nullptr);
    sigaction(SIGBUS, &s_old_sigbus, nullptr);
    raise(sig);
}

namespace rastack {

Orchestrator::Orchestrator() = default;

Orchestrator::~Orchestrator() {
    stop_live();
}

bool Orchestrator::init(const PipelineConfig& config) {
    config_ = config;

    LOG_DEBUG("Pipeline", "Initializing...");

    // 1. Memory pool
    pool_ = std::make_unique<MemoryPool>(config.memory_pool_size);
    LOG_DEBUG("Pool", "Allocated %zuMB memory pool", config.memory_pool_size / (1024*1024));

    // 2. Ring buffers (allocated from pool)
    {
        float* cap_storage = pool_->alloc<float>(config.audio_ring_capacity);
        capture_rb_ = std::make_unique<RingBuffer<float>>(cap_storage, config.audio_ring_capacity);
        LOG_DEBUG("Pool", "Capture ring buffer: %zu samples", config.audio_ring_capacity);
    }
    {
        float* play_storage = pool_->alloc<float>(config.tts_ring_capacity);
        playback_rb_ = std::make_unique<RingBuffer<float>>(play_storage, config.tts_ring_capacity);
        LOG_DEBUG("Pool", "Playback ring buffer: %zu samples", config.tts_ring_capacity);
    }

    LOG_DEBUG("Pool", "Used: %.1fMB / %.1fMB (%.1f%%)",
            pool_->used_bytes() / (1024.0*1024.0),
            pool_->total_size() / (1024.0*1024.0),
            pool_->utilization_pct());

    // llama.cpp STT/LLM/TTS — required for llamacpp/auto, optional for metalrt-only
    bool need_llamacpp = (config.llm_backend != LlmBackend::METALRT);

    if (!stt_.init(config.stt)) {
        if (need_llamacpp) { LOG_ERROR("Pipeline", "STT init failed"); return false; }
        LOG_WARN("Pipeline", "llama.cpp STT not available (MetalRT will handle STT)");
    }

    if (!offline_stt_.init(config.offline_stt)) {
        LOG_WARN("Pipeline", "Offline STT init failed (will use streaming STT)");
    }

    if (!llm_.init(config.llm)) {
        if (need_llamacpp) { LOG_ERROR("Pipeline", "LLM init failed"); return false; }
        LOG_WARN("Pipeline", "llama.cpp LLM not available (MetalRT will handle LLM)");
    }

    if (!tts_.init(config.tts)) {
        if (need_llamacpp) { LOG_ERROR("Pipeline", "TTS init failed"); return false; }
        LOG_WARN("Pipeline", "llama.cpp TTS not available (MetalRT will handle TTS)");
    }

    if (!audio_.init(config.audio, capture_rb_.get(), playback_rb_.get())) {
        LOG_ERROR("Pipeline", "Audio init failed");
        return false;
    }

    if (!vad_.init(config.vad)) {
        LOG_WARN("Pipeline", "VAD init failed (will process all audio)");
    }

    tools_.register_defaults();

    if (llm_.is_initialized()) {
        tools_.set_model_profile(&llm_.profile());
        std::string tool_system = llm_.profile().build_tool_system_prompt(
            config.system_prompt, tools_.get_tool_definitions_json());
        llm_.cache_system_prompt(tool_system);
    }

    // --- MetalRT backend (optional) ---
    bool metalrt_skip_due_to_crash = false;
    if (config.llm_backend == LlmBackend::METALRT ||
        config.llm_backend == LlmBackend::AUTO) {
        // Check if MetalRT crashed on a previous launch
        if (metalrt_previously_crashed()) {
            LOG_WARN("Pipeline", "MetalRT crashed on a previous launch — skipping. "
                     "Remove %s to retry, or run: rcli engine llamacpp",
                     metalrt_crash_breadcrumb_path().c_str());
            fprintf(stderr, "\n  MetalRT crashed previously — using llama.cpp instead.\n"
                    "  To retry MetalRT: rm %s && rcli\n\n",
                    metalrt_crash_breadcrumb_path().c_str());
            metalrt_breadcrumb_remove();
            metalrt_skip_due_to_crash = true;
            if (config.llm_backend == LlmBackend::METALRT && !llm_.is_initialized()) {
                LOG_ERROR("Pipeline", "MetalRT skipped and llama.cpp LLM not available");
                return false;
            }
        }

        if (!metalrt_skip_due_to_crash && !config.metalrt.model_dir.empty()) {
            // Install crash handler + breadcrumb before MetalRT dylib calls
            metalrt_breadcrumb_create();
            struct sigaction sa = {};
            sa.sa_handler = metalrt_crash_handler;
            sigemptyset(&sa.sa_mask);
            sigaction(SIGSEGV, &sa, &s_old_sigsegv);
            sigaction(SIGBUS, &sa, &s_old_sigbus);

            if (metalrt_.init(config.metalrt)) {
                LOG_INFO("Pipeline", "MetalRT engine ready: %s on %s",
                         metalrt_.model_name().c_str(), metalrt_.device_name().c_str());
                // Cache system prompt + tool definitions into MetalRT KV cache.
                // Subsequent generate_raw_continue calls skip re-prefilling this.
                std::string mrt_system = metalrt_.profile().build_tool_system_prompt(
                    config.system_prompt, tools_.get_tool_definitions_json());
                std::string mrt_prefix = metalrt_.profile().build_system_prefix(mrt_system);
                metalrt_.cache_system_prompt(mrt_prefix);
                metalrt_.set_system_prompt(mrt_system);
                if (config.llm_backend == LlmBackend::METALRT) {
                    active_backend_ = LlmBackend::METALRT;
                    LOG_INFO("Pipeline", "Active LLM backend: MetalRT");
                }
            } else if (config.llm_backend == LlmBackend::METALRT) {
                metalrt_breadcrumb_remove();
                sigaction(SIGSEGV, &s_old_sigsegv, nullptr);
                sigaction(SIGBUS, &s_old_sigbus, nullptr);
                LOG_ERROR("Pipeline", "MetalRT LLM init FAILED — refusing to fall back to CPU. "
                          "Check that libmetalrt.dylib is installed and MetalRT models are present.");
                return false;
            }

            // Restore original signal handlers (breadcrumb stays until STT/TTS done)
            sigaction(SIGSEGV, &s_old_sigsegv, nullptr);
            sigaction(SIGBUS, &s_old_sigbus, nullptr);
        }
    }

    // --- MetalRT STT (Whisper) and TTS (Kokoro) — required when MetalRT is active ---
    if (active_backend_ == LlmBackend::METALRT) {
        bool stt_ok = false, tts_ok = false;

        // Re-install crash handler for STT/TTS init (dylib calls can segfault)
        {
            struct sigaction sa = {};
            sa.sa_handler = metalrt_crash_handler;
            sigemptyset(&sa.sa_mask);
            sigaction(SIGSEGV, &sa, &s_old_sigsegv);
            sigaction(SIGBUS, &sa, &s_old_sigbus);
        }

        if (!config.metalrt_stt.model_dir.empty()) {
            if (metalrt_stt_.init(config.metalrt_stt)) {
                metalrt_stt_initialized_ = true;
                stt_ok = true;
                LOG_INFO("Pipeline", "MetalRT Whisper STT ready");
            } else {
                LOG_ERROR("Pipeline", "MetalRT Whisper STT init FAILED at: %s",
                          config.metalrt_stt.model_dir.c_str());
            }
        } else {
            LOG_WARN("Pipeline", "MetalRT STT model path not configured");
        }

        if (!config.metalrt_tts.model_dir.empty()) {
            if (metalrt_tts_.init(config.metalrt_tts)) {
                metalrt_tts_initialized_ = true;
                tts_ok = true;
                LOG_INFO("Pipeline", "MetalRT Kokoro TTS ready (sample_rate=%d)",
                         metalrt_tts_.sample_rate());
            } else {
                LOG_ERROR("Pipeline", "MetalRT Kokoro TTS init FAILED at: %s",
                          config.metalrt_tts.model_dir.c_str());
            }
        } else {
            LOG_WARN("Pipeline", "MetalRT TTS model path not configured");
        }

        // Restore original signal handlers, remove breadcrumb (init survived)
        sigaction(SIGSEGV, &s_old_sigsegv, nullptr);
        sigaction(SIGBUS, &s_old_sigbus, nullptr);
        metalrt_breadcrumb_remove();

        if (!stt_ok || !tts_ok) {
            if (config.llm_backend == LlmBackend::METALRT) {
                LOG_ERROR("Pipeline", "MetalRT STT/TTS not available. "
                          "Install with: rcli setup --metalrt");
                return false;
            }
            // AUTO mode: fall back to llama.cpp gracefully
            LOG_WARN("Pipeline", "MetalRT STT/TTS incomplete — falling back to llama.cpp");
            active_backend_ = LlmBackend::LLAMACPP;
            metalrt_stt_initialized_ = false;
            metalrt_tts_initialized_ = false;
        }
    } else {
        // MetalRT not active — clean up breadcrumb if it was created
        metalrt_breadcrumb_remove();
    }

    // --- Final validation: at least one LLM backend must be working ---
    if (!llm_.is_initialized() && active_backend_ != LlmBackend::METALRT) {
        LOG_ERROR("Pipeline", "No LLM backend available — "
                  "neither llama.cpp nor MetalRT initialized successfully");
        return false;
    }

    LOG_INFO("Pipeline", "Ready");
    LOG_DEBUG("Pool", "Final usage: %.1fMB / %.1fMB (%.1f%%)",
            pool_->used_bytes() / (1024.0*1024.0),
            pool_->total_size() / (1024.0*1024.0),
            pool_->utilization_pct());

    set_state(PipelineState::IDLE);
    return true;
}

// --- File mode pipeline ---
bool Orchestrator::run_file_pipeline(const std::string& input_wav, const std::string& output_wav) {
    timings_ = PipelineTimings{};
    int64_t t_pipeline_start = now_us();

    fprintf(stderr, "\n=== File Pipeline: %s -> %s ===\n", input_wav.c_str(), output_wav.c_str());

    // 1. Load WAV
    auto audio_samples = AudioIO::load_wav_to_vec(input_wav, 16000);
    if (audio_samples.empty()) {
        fprintf(stderr, "[Pipeline] Failed to load input WAV\n");
        return false;
    }

    // 2. VAD pre-filter: extract speech segments to reduce STT work
    const float* stt_audio = audio_samples.data();
    int stt_audio_len = (int)audio_samples.size();
    std::vector<float> speech_only;

    if (vad_.is_initialized()) {
        auto segments = vad_.extract_speech(audio_samples.data(), (int)audio_samples.size());
        if (!segments.empty()) {
            for (auto& seg : segments) {
                speech_only.insert(speech_only.end(), seg.samples.begin(), seg.samples.end());
            }
            stt_audio = speech_only.data();
            stt_audio_len = (int)speech_only.size();
        }
    }

    // 3. STT — use Whisper offline if available, else fall back to streaming Zipformer
    set_state(PipelineState::LISTENING);
    int64_t t_stt_start = now_us();
    std::string stt_text;

    if (using_metalrt_stt()) {
        stt_text = metalrt_stt_.transcribe(stt_audio, stt_audio_len);
        timings_.stt_latency_us = metalrt_stt_.last_latency_us();
    } else if (offline_stt_.is_initialized()) {
        stt_text = offline_stt_.transcribe(stt_audio, stt_audio_len);
        timings_.stt_latency_us = offline_stt_.last_latency_us();
    } else {
        stt_.set_callback([&](const TextSegment& seg) {
            if (seg.is_final || !seg.text.empty()) {
                stt_text = seg.text;
            }
        });
        stt_.feed_audio(stt_audio, stt_audio_len);
        std::vector<float> silence(16000 * 3, 0.0f);
        stt_.feed_audio(silence.data(), silence.size());
        for (int i = 0; i < 500; i++) {
            stt_.process_tick();
        }
        if (stt_text.empty()) {
            auto seg = stt_.get_result();
            stt_text = seg.text;
        }
        stt_.set_callback(nullptr);
        timings_.stt_latency_us = now_us() - t_stt_start;
    }

    fprintf(stderr, "[Pipeline] STT result: \"%s\" (%.1fms)\n",
            stt_text.c_str(), timings_.stt_latency_us / 1000.0);

    if (stt_text.empty()) {
        fprintf(stderr, "[Pipeline] No speech detected\n");
        return false;
    }

    // Trim leading/trailing whitespace from STT result
    size_t start = stt_text.find_first_not_of(" \n\r\t");
    size_t end = stt_text.find_last_not_of(" \n\r\t");
    if (start != std::string::npos) {
        stt_text = stt_text.substr(start, end - start + 1);
    }

    // 3. LLM + Tool handling + TTS
    set_state(PipelineState::PROCESSING);
    int64_t t_llm_start = now_us();

    // --- Double-buffered TTS setup ---
    std::vector<float> all_tts_audio;
    std::mutex tts_queue_mutex;
    std::condition_variable tts_queue_cv;
    std::vector<std::string> tts_queue;
    bool llm_done = false;
    int64_t first_sentence_time = 0;

    // TTS worker thread
    std::thread tts_worker([&]() {
        pthread_setname_np("rastack.tts");
        while (true) {
            std::string sentence;
            {
                std::unique_lock<std::mutex> lock(tts_queue_mutex);
                tts_queue_cv.wait(lock, [&]() {
                    return !tts_queue.empty() || llm_done;
                });
                if (tts_queue.empty() && llm_done) break;
                if (tts_queue.empty()) continue;
                sentence = std::move(tts_queue.front());
                tts_queue.erase(tts_queue.begin());
            }
            auto audio = using_metalrt_tts()
                ? metalrt_tts_.synthesize(sentence)
                : tts_.synthesize(sentence);
            std::lock_guard<std::mutex> lock(tts_queue_mutex);
            all_tts_audio.insert(all_tts_audio.end(), audio.begin(), audio.end());
        }
    });

    auto queue_sentence = [&](const std::string& sentence) {
        std::string clean = sanitize_for_tts(sentence);
        if (clean.empty()) return;
        if (first_sentence_time == 0) {
            first_sentence_time = now_us();
            set_state(PipelineState::SPEAKING);
        }
        {
            std::lock_guard<std::mutex> lock(tts_queue_mutex);
            tts_queue.push_back(std::move(clean));
        }
        tts_queue_cv.notify_one();
    };

    std::string llm_response;
    fprintf(stderr, "[Pipeline] LLM-driven routing (query: \"%s\") [%s]\n",
            stt_text.c_str(), using_metalrt() ? "MetalRT" : "llama.cpp");

    // Speculative first-token detection: buffer initial tokens to detect tool calls.
    const auto& active_profile = using_metalrt() ? metalrt_.profile() : llm_.profile();
    const std::string& tc_start = active_profile.tool_call_start;

    std::string token_buffer;
    bool detected_tool_call = false;
    constexpr int SPECULATIVE_TOKENS = 30;
    int tokens_buffered = 0;
    SentenceDetector detector(queue_sentence, 6, 35, 20);

    auto speculative_callback = [&](const TokenOutput& tok) {
        if (detected_tool_call) return;
        token_buffer += tok.text;
        tokens_buffered++;
        if (tokens_buffered <= SPECULATIVE_TOKENS) {
            if (token_buffer.find(tc_start) != std::string::npos) {
                detected_tool_call = true;
            }
        } else if (!detected_tool_call) {
            bool partial_match = false;
            if (!tc_start.empty()) {
                for (size_t len = 1; len < tc_start.size() && len <= token_buffer.size(); len++) {
                    if (token_buffer.compare(token_buffer.size() - len, len, tc_start, 0, len) == 0) {
                        partial_match = true;
                        break;
                    }
                }
            }
            if (partial_match) return;
            detector.feed(token_buffer);
            token_buffer.clear();
        }
        if (tokens_buffered > SPECULATIVE_TOKENS && !detected_tool_call && token_buffer.empty()) {
            detector.feed(tok.text);
        }
    };

    if (using_metalrt()) {
        std::string hint = tools_.build_tool_hint(stt_text);
        std::string hinted = hint.empty() ? stt_text : (hint + "\n" + stt_text);
        if (metalrt_.has_prompt_cache()) {
            std::string user_turn = metalrt_.profile().build_user_turn(hinted);
            llm_response = metalrt_.generate_raw_continue(user_turn, speculative_callback);
        } else {
            std::string tool_defs = tools_.get_tool_definitions_json();
            std::string tool_system = metalrt_.profile().build_tool_system_prompt(
                config_.system_prompt, tool_defs);
            std::string full_prompt = metalrt_.profile().build_chat_prompt(tool_system, {}, hinted);
            llm_response = metalrt_.generate_raw(full_prompt, speculative_callback);
        }
    } else {
        std::string tool_hint = tools_.build_tool_hint(stt_text);
        std::string hinted_text = tool_hint.empty() ? stt_text : (tool_hint + "\n" + stt_text);

        std::string prompt;
        if (llm_.has_prompt_cache()) {
            prompt = llm_.profile().build_user_turn(hinted_text);
        } else {
            std::string tool_defs = tools_.get_tool_definitions_json();
            std::string tool_system = llm_.profile().build_tool_system_prompt(
                config_.system_prompt, tool_defs);
            prompt = llm_.build_chat_prompt(tool_system, {}, hinted_text);
        }

        if (llm_.has_prompt_cache()) {
            llm_response = llm_.generate_with_cached_prompt(prompt, speculative_callback);
        } else {
            llm_response = llm_.generate(prompt, speculative_callback);
        }
    }

    if (detected_tool_call) {
        auto tool_calls = active_profile.parse_tool_calls(llm_response);
        if (tool_calls.empty()) tool_calls = tools_.parse_tool_calls(llm_response);
        if (!tool_calls.empty()) {
            fprintf(stderr, "[Pipeline] Detected %zu tool call(s), executing...\n", tool_calls.size());
            auto results = tools_.execute_all(tool_calls);
            for (auto& r : results) {
                fprintf(stderr, "[Pipeline] Tool '%s': %s -> %s\n",
                        r.name.c_str(), r.success ? "OK" : "FAIL", r.result_json.c_str());
            }

            std::string formatted_results = tools_.format_results(results);
            SentenceDetector detector2(queue_sentence, 6, 35, 20);

            if (using_metalrt()) {
                std::string sum_sys = config_.system_prompt + " Do NOT output JSON.";
                std::string sum_msg = "Tool results: " + formatted_results + "\nSummarize briefly.";
                std::string sum_prompt = metalrt_.profile().build_chat_prompt(sum_sys, {}, sum_msg);
                llm_response = metalrt_.generate_raw(sum_prompt,
                    [&](const TokenOutput& tok) { detector2.feed(tok.text); });
            } else {
                std::string continuation = llm_.build_tool_continuation_prompt(
                    config_.system_prompt, stt_text, llm_response, formatted_results);
                llm_response = llm_.generate(continuation, [&](const TokenOutput& tok) {
                    detector2.feed(tok.text);
                });
            }
            detector2.flush();
            fprintf(stderr, "[Pipeline] Second pass: \"%s\"\n", llm_response.c_str());
        } else {
            detector.feed(sanitize_for_tts(llm_response));
            detector.flush();
        }
    } else {
        if (!token_buffer.empty()) {
            detector.feed(token_buffer);
        }
        detector.flush();
    }

    // Signal TTS thread done
    {
        std::lock_guard<std::mutex> lock(tts_queue_mutex);
        llm_done = true;
    }
    tts_queue_cv.notify_one();
    tts_worker.join();

    const auto& llm_stats = using_metalrt() ? metalrt_.last_stats() : llm_.last_stats();
    timings_.llm_first_token_us = llm_stats.first_token_us;
    timings_.llm_total_us = now_us() - t_llm_start;

    std::string clean_response = active_profile.clean_output(llm_response);

    fprintf(stderr, "[Pipeline] LLM response: \"%s\"\n", clean_response.c_str());
    fprintf(stderr, "[Pipeline] LLM stats: first_token=%.1fms, total=%.1fms, %.1f tok/s\n",
            timings_.llm_first_token_us / 1000.0,
            timings_.llm_total_us / 1000.0,
            llm_.last_stats().gen_tps());

    // 4. Collect TTS results
    set_state(PipelineState::SPEAKING);

    if (all_tts_audio.empty() && !clean_response.empty()) {
        auto audio = using_metalrt_tts()
            ? metalrt_tts_.synthesize(sanitize_for_tts(clean_response))
            : tts_.synthesize(sanitize_for_tts(clean_response));
        all_tts_audio = std::move(audio);
    }

    timings_.tts_first_sentence_us = (first_sentence_time > 0) ?
        (first_sentence_time - t_llm_start) : 0;

    int out_sample_rate = using_metalrt_tts()
        ? metalrt_tts_.sample_rate() : tts_.sample_rate();

    // 5. Save output WAV
    if (!all_tts_audio.empty() && !output_wav.empty()) {
        AudioIO::save_wav(output_wav, all_tts_audio.data(),
                         all_tts_audio.size(), out_sample_rate);
    }

    timings_.total_us = now_us() - t_pipeline_start;
    timings_.e2e_latency_us = timings_.stt_latency_us + timings_.llm_first_token_us +
                               timings_.tts_first_sentence_us;

    set_state(PipelineState::IDLE);

    // Print summary
    fprintf(stderr, "\n=== Pipeline Summary ===\n");
    fprintf(stderr, "  STT latency:        %6.1f ms\n", timings_.stt_latency_us / 1000.0);
    fprintf(stderr, "  LLM first token:    %6.1f ms\n", timings_.llm_first_token_us / 1000.0);
    fprintf(stderr, "  LLM total:          %6.1f ms\n", timings_.llm_total_us / 1000.0);
    fprintf(stderr, "  TTS first sentence: %6.1f ms\n", timings_.tts_first_sentence_us / 1000.0);
    fprintf(stderr, "  E2E latency:        %6.1f ms\n", timings_.e2e_latency_us / 1000.0);
    fprintf(stderr, "  Total pipeline:     %6.1f ms\n", timings_.total_us / 1000.0);
    fprintf(stderr, "  LLM throughput:     %6.1f tok/s\n", llm_stats.gen_tps());
    fprintf(stderr, "  Memory pool:        %6.1f MB used\n", pool_->high_water_mark() / (1024.0*1024.0));

    return true;
}

// --- Stream pipeline (outputs audio chunks to stdout) ---
bool Orchestrator::run_stream_pipeline(const std::string& input_wav) {
    // Disable stdout buffering so Python sees lines immediately
    setvbuf(stdout, nullptr, _IONBF, 0);

    timings_ = PipelineTimings{};
    int64_t t_pipeline_start = now_us();
    std::mutex stdout_mutex;

    fprintf(stderr, "\n=== Stream Pipeline: %s ===\n", input_wav.c_str());

    // 1. Load WAV
    auto audio_samples = AudioIO::load_wav_to_vec(input_wav, 16000);
    if (audio_samples.empty()) {
        fprintf(stderr, "[Pipeline] Failed to load input WAV\n");
        return false;
    }

    // 2. VAD pre-filter
    const float* stt_audio = audio_samples.data();
    int stt_audio_len = (int)audio_samples.size();
    std::vector<float> speech_only;

    if (vad_.is_initialized()) {
        auto segments = vad_.extract_speech(audio_samples.data(), (int)audio_samples.size());
        if (!segments.empty()) {
            for (auto& seg : segments) {
                speech_only.insert(speech_only.end(), seg.samples.begin(), seg.samples.end());
            }
            stt_audio = speech_only.data();
            stt_audio_len = (int)speech_only.size();
        }
    }

    // 3. STT
    set_state(PipelineState::LISTENING);
    int64_t t_stt_start = now_us();
    std::string stt_text;

    if (using_metalrt_stt()) {
        stt_text = metalrt_stt_.transcribe(stt_audio, stt_audio_len);
        timings_.stt_latency_us = metalrt_stt_.last_latency_us();
    } else if (offline_stt_.is_initialized()) {
        stt_text = offline_stt_.transcribe(stt_audio, stt_audio_len);
        timings_.stt_latency_us = offline_stt_.last_latency_us();
    } else {
        stt_.set_callback([&](const TextSegment& seg) {
            if (seg.is_final || !seg.text.empty()) {
                stt_text = seg.text;
            }
        });
        stt_.feed_audio(stt_audio, stt_audio_len);
        std::vector<float> silence(16000 * 3, 0.0f);
        stt_.feed_audio(silence.data(), silence.size());
        for (int i = 0; i < 500; i++) {
            stt_.process_tick();
        }
        if (stt_text.empty()) {
            auto seg = stt_.get_result();
            stt_text = seg.text;
        }
        stt_.set_callback(nullptr);
        timings_.stt_latency_us = now_us() - t_stt_start;
    }

    fprintf(stderr, "[Pipeline] STT result: \"%s\" (%.1fms)\n",
            stt_text.c_str(), timings_.stt_latency_us / 1000.0);

    if (stt_text.empty()) {
        fprintf(stderr, "[Pipeline] No speech detected\n");
        return false;
    }

    // Trim whitespace
    size_t start = stt_text.find_first_not_of(" \n\r\t");
    size_t end = stt_text.find_last_not_of(" \n\r\t");
    if (start != std::string::npos) {
        stt_text = stt_text.substr(start, end - start + 1);
    }

    // Emit STT result
    {
        std::lock_guard<std::mutex> lock(stdout_mutex);
        fprintf(stdout, "STT_RESULT:%s\n", stt_text.c_str());
    }

    // 3. LLM + Tool handling + streaming TTS
    set_state(PipelineState::PROCESSING);
    int64_t t_llm_start = now_us();

    // --- TTS worker that emits audio chunks to stdout ---
    std::mutex tts_queue_mutex;
    std::condition_variable tts_queue_cv;
    std::vector<std::string> tts_queue;
    bool llm_done = false;
    int64_t first_sentence_time = 0;

    std::thread tts_worker([&]() {
        pthread_setname_np("rastack.tts");
        while (true) {
            std::string sentence;
            {
                std::unique_lock<std::mutex> lock(tts_queue_mutex);
                tts_queue_cv.wait(lock, [&]() {
                    return !tts_queue.empty() || llm_done;
                });
                if (tts_queue.empty() && llm_done) break;
                if (tts_queue.empty()) continue;
                sentence = std::move(tts_queue.front());
                tts_queue.erase(tts_queue.begin());
            }

            auto audio = using_metalrt_tts()
                ? metalrt_tts_.synthesize(sentence)
                : tts_.synthesize(sentence);
            if (!audio.empty()) {
                const uint8_t* raw = reinterpret_cast<const uint8_t*>(audio.data());
                size_t raw_len = audio.size() * sizeof(float);
                std::string b64 = base64_encode(raw, raw_len);

                std::lock_guard<std::mutex> lock(stdout_mutex);
                fprintf(stdout, "AUDIO_CHUNK:%s\n", b64.c_str());
            }
        }
    });

    auto queue_sentence = [&](const std::string& sentence) {
        std::string clean = sanitize_for_tts(sentence);
        if (clean.empty()) return;
        if (first_sentence_time == 0) {
            first_sentence_time = now_us();
            set_state(PipelineState::SPEAKING);
        }
        {
            std::lock_guard<std::mutex> lock(tts_queue_mutex);
            tts_queue.push_back(std::move(clean));
        }
        tts_queue_cv.notify_one();
    };

    std::string llm_response;
    fprintf(stderr, "[Pipeline] LLM-driven routing [%s]\n", using_metalrt() ? "MetalRT" : "llama.cpp");

    const auto& stream_profile = using_metalrt() ? metalrt_.profile() : llm_.profile();
    const std::string& stream_tc_start = stream_profile.tool_call_start;

    std::string token_buffer;
    bool detected_tool_call = false;
    constexpr int SPECULATIVE_TOKENS = 30;
    int tokens_buffered = 0;
    SentenceDetector detector(queue_sentence, 6, 35, 20);

    auto speculative_callback = [&](const TokenOutput& tok) {
        if (detected_tool_call) return;
        token_buffer += tok.text;
        tokens_buffered++;
        if (tokens_buffered <= SPECULATIVE_TOKENS) {
            if (token_buffer.find(stream_tc_start) != std::string::npos) {
                detected_tool_call = true;
            }
        } else if (!detected_tool_call) {
            detector.feed(token_buffer);
            token_buffer.clear();
        }
        if (tokens_buffered > SPECULATIVE_TOKENS && !detected_tool_call) {
            detector.feed(tok.text);
        }
    };

    if (using_metalrt()) {
        std::string s_hint = tools_.build_tool_hint(stt_text);
        std::string s_hinted = s_hint.empty() ? stt_text : (s_hint + "\n" + stt_text);
        if (metalrt_.has_prompt_cache()) {
            std::string user_turn = metalrt_.profile().build_user_turn(s_hinted);
            llm_response = metalrt_.generate_raw_continue(user_turn, speculative_callback);
        } else {
            std::string s_tool_defs = tools_.get_tool_definitions_json();
            std::string s_tool_system = metalrt_.profile().build_tool_system_prompt(
                config_.system_prompt, s_tool_defs);
            std::string s_full = metalrt_.profile().build_chat_prompt(s_tool_system, {}, s_hinted);
            llm_response = metalrt_.generate_raw(s_full, speculative_callback);
        }
    } else {
        std::string stream_hint = tools_.build_tool_hint(stt_text);
        std::string hinted_stream = stream_hint.empty() ? stt_text : (stream_hint + "\n" + stt_text);

        std::string prompt;
        if (llm_.has_prompt_cache()) {
            prompt = llm_.profile().build_user_turn(hinted_stream);
        } else {
            std::string tool_defs = tools_.get_tool_definitions_json();
            std::string tool_system = llm_.profile().build_tool_system_prompt(
                config_.system_prompt, tool_defs);
            prompt = llm_.build_chat_prompt(tool_system, {}, hinted_stream);
        }

        if (llm_.has_prompt_cache()) {
            llm_response = llm_.generate_with_cached_prompt(prompt, speculative_callback);
        } else {
            llm_response = llm_.generate(prompt, speculative_callback);
        }
    }

    if (detected_tool_call) {
        auto tool_calls = stream_profile.parse_tool_calls(llm_response);
        if (tool_calls.empty()) tool_calls = tools_.parse_tool_calls(llm_response);
        if (!tool_calls.empty()) {
            fprintf(stderr, "[Pipeline] Detected %zu tool call(s), executing...\n", tool_calls.size());
            auto results = tools_.execute_all(tool_calls);
            for (auto& r : results) {
                fprintf(stderr, "[Pipeline] Tool '%s': %s -> %s\n",
                        r.name.c_str(), r.success ? "OK" : "FAIL", r.result_json.c_str());
                std::lock_guard<std::mutex> lock(stdout_mutex);
                fprintf(stdout, "TOOL_CALL:%s:%s:%s\n",
                        r.name.c_str(), r.success ? "success" : "fail", r.result_json.c_str());
            }

            std::string formatted_results = tools_.format_results(results);
            SentenceDetector detector2(queue_sentence, 6, 35, 20);

            if (using_metalrt()) {
                std::string s2_sys = config_.system_prompt + " Do NOT output JSON.";
                std::string s2_msg = "Tool results: " + formatted_results + "\nSummarize briefly.";
                std::string s2_prompt = metalrt_.profile().build_chat_prompt(s2_sys, {}, s2_msg);
                llm_response = metalrt_.generate_raw(s2_prompt,
                    [&](const TokenOutput& tok) { detector2.feed(tok.text); });
            } else {
                std::string continuation = llm_.build_tool_continuation_prompt(
                    config_.system_prompt, stt_text, llm_response, formatted_results);
                llm_.clear_kv_cache();
                llm_response = llm_.generate(continuation, [&](const TokenOutput& tok) {
                    detector2.feed(tok.text);
                });
            }
            detector2.flush();
        } else {
            detector.feed(sanitize_for_tts(llm_response));
            detector.flush();
        }
    } else {
        if (!token_buffer.empty()) {
            detector.feed(token_buffer);
        }
        detector.flush();
    }

    // Signal TTS thread done
    {
        std::lock_guard<std::mutex> lock(tts_queue_mutex);
        llm_done = true;
    }
    tts_queue_cv.notify_one();
    tts_worker.join();

    const auto& stream_stats = using_metalrt() ? metalrt_.last_stats() : llm_.last_stats();
    timings_.llm_first_token_us = stream_stats.first_token_us;
    timings_.llm_total_us = now_us() - t_llm_start;

    std::string clean_response = stream_profile.clean_output(llm_response);

    // If no audio was streamed but we have text, synthesize the clean response as fallback
    if (first_sentence_time == 0 && !clean_response.empty()) {
        auto audio = using_metalrt_tts()
            ? metalrt_tts_.synthesize(sanitize_for_tts(clean_response))
            : tts_.synthesize(sanitize_for_tts(clean_response));
        if (!audio.empty()) {
            const uint8_t* raw = reinterpret_cast<const uint8_t*>(audio.data());
            size_t raw_len = audio.size() * sizeof(float);
            std::string b64 = base64_encode(raw, raw_len);
            std::lock_guard<std::mutex> lock(stdout_mutex);
            fprintf(stdout, "AUDIO_CHUNK:%s\n", b64.c_str());
        }
    }

    timings_.tts_first_sentence_us = (first_sentence_time > 0) ?
        (first_sentence_time - t_llm_start) : 0;
    timings_.total_us = now_us() - t_pipeline_start;
    timings_.e2e_latency_us = timings_.stt_latency_us + timings_.llm_first_token_us +
                               timings_.tts_first_sentence_us;

    // Emit metadata and end
    {
        std::lock_guard<std::mutex> lock(stdout_mutex);
        fprintf(stdout, "LLM_TEXT:%s\n", clean_response.c_str());
        fprintf(stdout, "TIMINGS:{\"stt_ms\":%.1f,\"llm_first_token_ms\":%.1f,"
                "\"llm_total_ms\":%.1f,\"tts_first_ms\":%.1f,\"e2e_ms\":%.1f,"
                "\"tok_per_sec\":%.1f,\"total_ms\":%.1f}\n",
                timings_.stt_latency_us / 1000.0,
                timings_.llm_first_token_us / 1000.0,
                timings_.llm_total_us / 1000.0,
                timings_.tts_first_sentence_us / 1000.0,
                timings_.e2e_latency_us / 1000.0,
                stream_stats.gen_tps(),
                timings_.total_us / 1000.0);
        fprintf(stdout, "STREAM_END\n");
    }

    set_state(PipelineState::IDLE);

    fprintf(stderr, "\n=== Stream Pipeline Summary ===\n");
    fprintf(stderr, "  STT latency:        %6.1f ms\n", timings_.stt_latency_us / 1000.0);
    fprintf(stderr, "  LLM first token:    %6.1f ms\n", timings_.llm_first_token_us / 1000.0);
    fprintf(stderr, "  LLM total:          %6.1f ms\n", timings_.llm_total_us / 1000.0);
    fprintf(stderr, "  TTS first sentence: %6.1f ms\n", timings_.tts_first_sentence_us / 1000.0);
    fprintf(stderr, "  E2E latency:        %6.1f ms\n", timings_.e2e_latency_us / 1000.0);
    fprintf(stderr, "  Total pipeline:     %6.1f ms\n", timings_.total_us / 1000.0);
    fprintf(stderr, "  LLM throughput:     %6.1f tok/s\n", stream_stats.gen_tps());

    return true;
}

// --- Live mode ---
bool Orchestrator::start_live() {
    if (live_running_.load()) return false;
    live_running_.store(true, std::memory_order_release);
    live_history_.clear();

    // Start audio
    audio_.start();
    set_state(PipelineState::LISTENING);

    // STT thread
    stt_thread_ = std::thread([this]() { stt_thread_fn(); });

    // LLM+TTS thread
    llm_thread_ = std::thread([this]() { llm_thread_fn(); });

    LOG_DEBUG("Pipeline", "Live mode started");
    return true;
}

void Orchestrator::stop_live() {
    live_running_.store(false, std::memory_order_release);
    text_cv_.notify_all();

    if (stt_thread_.joinable()) stt_thread_.join();
    if (llm_thread_.joinable()) llm_thread_.join();

    audio_.stop();
    set_state(PipelineState::IDLE);
}

// --- Push-to-talk: capture only, no STT/LLM threads ---

bool Orchestrator::start_capture() {
    if (live_running_.load()) return false;
    // Drain any stale data in the ring buffer
    capture_rb_->clear();
    live_running_.store(true, std::memory_order_release);
    audio_.start();
    set_state(PipelineState::LISTENING);
    return true;
}

std::string Orchestrator::stop_capture_and_transcribe() {
    live_running_.store(false, std::memory_order_release);
    audio_.stop();

    // Drain the ring buffer into a contiguous vector
    size_t avail = capture_rb_->available_read();
    if (avail == 0) {
        set_state(PipelineState::IDLE);
        return "";
    }

    std::vector<float> audio_buf(avail);
    capture_rb_->read(audio_buf.data(), avail);

    // Transcribe with Whisper/Parakeet (offline, higher accuracy)
    set_state(PipelineState::PROCESSING);
    int64_t t_stt_start = now_us();
    std::string text;
    if (using_metalrt_stt()) {
        text = metalrt_stt_.transcribe(audio_buf.data(), static_cast<int>(audio_buf.size()));
    } else if (offline_stt_.is_initialized()) {
        text = offline_stt_.transcribe(audio_buf.data(), static_cast<int>(audio_buf.size()));
    } else {
        stt_.reset();
        stt_.feed_audio(audio_buf.data(), static_cast<int>(audio_buf.size()));
        stt_.process_tick();
        auto result = stt_.get_result();
        text = result.text;
    }
    timings_.stt_latency_us = now_us() - t_stt_start;
    timings_.stt_audio_samples = static_cast<int64_t>(avail);

    set_state(PipelineState::IDLE);

    // Trim whitespace
    auto first = text.find_first_not_of(" \t\n\r");
    if (first == std::string::npos) return "";
    auto last = text.find_last_not_of(" \t\n\r");
    return text.substr(first, last - first + 1);
}

void Orchestrator::check_barge_in(const float* audio, int num_samples) {
    if (!barge_in_enabled_.load(std::memory_order_relaxed)) return;

    auto cur_state = state_.load(std::memory_order_relaxed);
    if (cur_state != PipelineState::SPEAKING) return;

    // Compute mic RMS
    float sum_sq = 0.0f;
    for (int i = 0; i < num_samples; ++i) sum_sq += audio[i] * audio[i];
    float mic_rms = std::sqrtf(sum_sq / (float)num_samples);

    // Get playback RMS for comparison
    float pb_rms = audio_.playback_rms();

    // Barge-in requires:
    // 1. VAD detects speech (with normal threshold — audio energy gating handles echo)
    // 2. Mic energy significantly exceeds playback energy (not just hearing ourselves)
    // 3. Absolute mic energy above a floor (prevents triggering on silence)
    constexpr float BARGE_IN_ENERGY_FLOOR = 0.02f;   // higher than normal 0.005
    constexpr float BARGE_IN_ENERGY_RATIO = 2.5f;     // mic must be 2.5x louder than speaker

    bool vad_speech = vad_.is_initialized() && vad_.is_speech();
    bool strong_mic = (mic_rms > BARGE_IN_ENERGY_FLOOR);
    bool louder_than_speaker = (pb_rms < 0.001f) || (mic_rms > pb_rms * BARGE_IN_ENERGY_RATIO);

    if (vad_speech && strong_mic && louder_than_speaker) {
        LOG_DEBUG("BargeIn", "TRIGGERED: mic_rms=%.4f, pb_rms=%.4f, ratio=%.1f",
                  mic_rms, pb_rms, pb_rms > 0 ? mic_rms / pb_rms : 999.0f);

        // Signal barge-in
        barge_in_triggered_.store(true, std::memory_order_release);
        tts_cancel_flag_.store(true, std::memory_order_release);

        // Clear playback buffer to stop TTS audio immediately
        playback_rb_->clear();

        // Cancel in-flight LLM generation
        if (using_metalrt()) metalrt_.cancel();
        else llm_.cancel();

        set_state(PipelineState::BARGE_IN);

        if (barge_in_cb_) {
            std::lock_guard<std::mutex> lock(barge_in_mutex_);
            barge_in_cb_(interrupted_response_, interrupted_chars_spoken_);
        }
    }
}

void Orchestrator::stt_thread_fn() {
    pthread_setname_np("rastack.stt");

    std::vector<float> chunk_buf(1600);
    std::string last_partial;

    constexpr float ENERGY_FLOOR = 0.005f;

    // Barge-in: consecutive speech frames counter (debounce)
    int barge_in_speech_frames = 0;
    constexpr int BARGE_IN_DEBOUNCE = 3; // need 3 consecutive speech frames (~30ms)

    // Wake word detector for voice mode
    WakeWordDetector wake_detector;
    if (voice_mode_active_ && !wake_phrase_.empty()) {
        wake_detector.set_phrase(wake_phrase_);
        LOG_INFO("VoiceMode", "Wake word detector initialized: \"%s\"", wake_phrase_.c_str());
    }

    // (LISTENING is now fixed-duration, no timeout needed)

    // --- Voice mode: periodic Whisper wake word detection ---
    // Accumulate audio, run Whisper every ~1.5s, check for "friday". No VAD gate.
    std::vector<float> voice_audio_window;
    constexpr size_t VOICE_WINDOW_MAX = 16000 * 3;      // 3 seconds at 16kHz
    constexpr size_t VOICE_CHECK_INTERVAL = 16000 * 1.5; // run Whisper every 1.5s
    size_t voice_samples_since_last_check = 0;

    FILE* vdbg = nullptr;
#ifdef RCLI_DEBUG
    if (voice_mode_active_) {
        vdbg = fopen("/tmp/rcli_voice_debug.log", "w");
        if (vdbg) {
            fprintf(vdbg, "=== Voice mode started (Whisper wake word), phrase=\"%s\" ===\n",
                    wake_phrase_.c_str());
            fprintf(vdbg, "offline_stt_init=%d, metalrt_stt_init=%d, vad_init=%d\n",
                    offline_stt_.is_initialized() ? 1 : 0,
                    metalrt_stt_initialized_ ? 1 : 0,
                    vad_.is_initialized() ? 1 : 0);
            fflush(vdbg);
        }
    }
#endif
    if (voice_mode_active_) {
        voice_audio_window.reserve(VOICE_WINDOW_MAX);
    }

    int debug_tick_count = 0;
    int debug_feed_count = 0;
    int debug_result_count = 0;

    // Voice mode LISTENING: record until silence or max duration
    std::vector<float> voice_command_buf;
    if (voice_mode_active_) {
        voice_command_buf.reserve(16000 * 10);
    }
    constexpr size_t VOICE_CMD_MIN = 16000 * 2;   // minimum 2s before checking silence
    constexpr size_t VOICE_CMD_MAX = 16000 * 8;   // hard max 8s
    constexpr float  VOICE_CMD_SILENCE_RMS = 0.015f; // silence threshold (above ambient)
    int voice_cmd_silence_chunks = 0;              // consecutive quiet chunks
    constexpr int VOICE_CMD_SILENCE_NEEDED = 15;   // 15 chunks × 10ms = 150ms × 10 = 1.5s of quiet

    while (live_running_.load(std::memory_order_relaxed)) {
        auto cur_state = state_.load(std::memory_order_relaxed);

        // --- Voice mode LISTENING: transcribe when silence detected or max reached ---
        if (voice_mode_active_ && cur_state == PipelineState::LISTENING) {
            bool should_transcribe = false;

            // Hard max: always transcribe at 8s
            if (voice_command_buf.size() >= VOICE_CMD_MAX) {
                should_transcribe = true;
                if (vdbg) { fprintf(vdbg, "[cmd] Max 8s reached\n"); fflush(vdbg); }
            }
            // After minimum 2s, check for silence (1.5s of quiet chunks)
            else if (voice_command_buf.size() >= VOICE_CMD_MIN &&
                     voice_cmd_silence_chunks >= VOICE_CMD_SILENCE_NEEDED) {
                should_transcribe = true;
                if (vdbg) { fprintf(vdbg, "[cmd] Silence detected after %.1fs\n",
                        voice_command_buf.size() / 16000.0f); fflush(vdbg); }
            }

            if (should_transcribe) {
                if (vdbg) {
                    fprintf(vdbg, "[cmd] Transcribing %zu samples (%.1fs)...\n",
                            voice_command_buf.size(), voice_command_buf.size() / 16000.0f);
                    fflush(vdbg);
                }

                std::string command;
                if (metalrt_stt_initialized_) {
                    command = metalrt_stt_.transcribe(
                        voice_command_buf.data(),
                        (int)voice_command_buf.size(), 16000);
                } else if (offline_stt_.is_initialized()) {
                    command = offline_stt_.transcribe(
                        voice_command_buf.data(),
                        (int)voice_command_buf.size());
                }

                if (vdbg) {
                    fprintf(vdbg, "[cmd] Result: \"%s\"\n", command.c_str());
                    fflush(vdbg);
                }

                voice_command_buf.clear();
                voice_cmd_silence_chunks = 0;

                // Strip wake word if user repeated it
                if (!command.empty() && wake_detector.check(command))
                    command = wake_detector.strip_wake_word(command);

                if (!command.empty()) {
                    set_state(PipelineState::PROCESSING);
                    {
                        std::lock_guard<std::mutex> lock(text_mutex_);
                        pending_text_ = command;
                        text_ready_ = true;
                    }
                    text_cv_.notify_one();
                } else {
                    if (vdbg) { fprintf(vdbg, "[cmd] Empty, back to VOICE_IDLE\n"); fflush(vdbg); }
                    set_state(PipelineState::VOICE_IDLE);
                }
                continue;
            }
        }

        size_t avail = capture_rb_->available_read();
        size_t to_read = std::min(avail, (size_t)1600);
        if (to_read > 0) {
            capture_rb_->read(chunk_buf.data(), to_read);

            // Compute chunk energy (RMS)
            float sum_sq = 0.0f;
            for (size_t i = 0; i < to_read; ++i)
                sum_sq += chunk_buf[i] * chunk_buf[i];
            float rms = std::sqrtf(sum_sq / static_cast<float>(to_read));

            if (vad_.is_initialized()) {
                vad_.feed_audio(chunk_buf.data(), (int)to_read);
            }

            bool vad_speech = vad_.is_initialized() && vad_.is_speech();

            // --- VOICE_IDLE: periodic Whisper wake word detection (no VAD gate) ---
            if (cur_state == PipelineState::VOICE_IDLE) {
                debug_feed_count++;

                // Accumulate audio
                voice_audio_window.insert(voice_audio_window.end(),
                    chunk_buf.data(), chunk_buf.data() + to_read);
                voice_samples_since_last_check += to_read;

                // Trim to max 3s (keep most recent)
                if (voice_audio_window.size() > VOICE_WINDOW_MAX) {
                    size_t excess = voice_audio_window.size() - VOICE_WINDOW_MAX;
                    voice_audio_window.erase(voice_audio_window.begin(),
                        voice_audio_window.begin() + excess);
                }

                // Run Whisper every 1.5s — but only if window has real speech energy
                if (voice_samples_since_last_check >= VOICE_CHECK_INTERVAL) {
                    voice_samples_since_last_check = 0;

                    // Compute RMS energy of the window to skip silence/ambient noise
                    float win_sum_sq = 0.0f;
                    for (size_t i = 0; i < voice_audio_window.size(); ++i)
                        win_sum_sq += voice_audio_window[i] * voice_audio_window[i];
                    float win_rms = std::sqrtf(win_sum_sq / (float)voice_audio_window.size());

                    // Skip Whisper if window is just ambient noise (< 0.01 RMS)
                    constexpr float WAKE_ENERGY_THRESHOLD = 0.01f;
                    if (win_rms < WAKE_ENERGY_THRESHOLD) {
                        if (vdbg && (debug_tick_count % 200) < 15) {
                            fprintf(vdbg, "[wake] skip (rms=%.4f < %.3f)\n", win_rms, WAKE_ENERGY_THRESHOLD);
                            fflush(vdbg);
                        }
                    } else {
                        std::string transcript;
                        if (metalrt_stt_initialized_) {
                            transcript = metalrt_stt_.transcribe(
                                voice_audio_window.data(),
                                (int)voice_audio_window.size(), 16000);
                        } else if (offline_stt_.is_initialized()) {
                            transcript = offline_stt_.transcribe(
                                voice_audio_window.data(),
                                (int)voice_audio_window.size());
                        }

                        debug_result_count++;

                        // Filter out Whisper hallucinations: parenthesized noise descriptions
                        // like "(soft music)", "(fart)", "(scribbling)", "[BLANK_AUDIO]"
                        if (!transcript.empty()) {
                            // Trim leading whitespace
                            auto start = transcript.find_first_not_of(" \t");
                            if (start != std::string::npos)
                                transcript = transcript.substr(start);
                            // Drop if it starts with ( or [ — these are hallucinations
                            if (!transcript.empty() && (transcript[0] == '(' || transcript[0] == '['))
                                transcript.clear();
                        }

                        if (vdbg) {
                            fprintf(vdbg, "[wake] rms=%.4f samples=%zu transcript=\"%s\"\n",
                                    win_rms, voice_audio_window.size(), transcript.c_str());
                            fflush(vdbg);
                        }

                        if (!transcript.empty() && wake_detector.check(transcript)) {
                            std::string command = wake_detector.strip_wake_word(transcript);

                            // Filter punctuation-only commands ("!", "?", etc.)
                            bool has_alpha = false;
                            for (char c : command) {
                                if (std::isalpha(static_cast<unsigned char>(c))) {
                                    has_alpha = true;
                                    break;
                                }
                            }
                            if (!has_alpha) command.clear();

                            if (vdbg) {
                                fprintf(vdbg, "[wake] DETECTED! command=\"%s\"\n", command.c_str());
                                fflush(vdbg);
                            }

                            voice_audio_window.clear();
                            voice_samples_since_last_check = 0;

                            if (!command.empty()) {
                                set_state(PipelineState::PROCESSING);
                                {
                                    std::lock_guard<std::mutex> lock(text_mutex_);
                                    pending_text_ = command;
                                    text_ready_ = true;
                                }
                                text_cv_.notify_one();
                            } else {
                                voice_command_buf.clear();
                                voice_cmd_silence_chunks = 0;
                                set_state(PipelineState::LISTENING);
                            }
                        }
                    }
                }

                goto skip_stt_feed;
            }

            // --- VOICE LISTENING: accumulate audio + track silence ---
            if (voice_mode_active_ && cur_state == PipelineState::LISTENING) {
                voice_command_buf.insert(voice_command_buf.end(),
                    chunk_buf.data(), chunk_buf.data() + to_read);
                // Track consecutive quiet chunks for silence detection
                if (rms < VOICE_CMD_SILENCE_RMS) {
                    voice_cmd_silence_chunks++;
                } else {
                    voice_cmd_silence_chunks = 0;
                }
                goto skip_stt_feed;
            }

            // --- Voice mode: wake word interruption during SPEAKING/PROCESSING ---
            if (voice_mode_active_ &&
                (cur_state == PipelineState::SPEAKING || cur_state == PipelineState::PROCESSING)) {
                // Accumulate audio for wake word detection while speaking
                voice_audio_window.insert(voice_audio_window.end(),
                    chunk_buf.data(), chunk_buf.data() + to_read);
                voice_samples_since_last_check += to_read;

                if (voice_audio_window.size() > VOICE_WINDOW_MAX) {
                    size_t excess = voice_audio_window.size() - VOICE_WINDOW_MAX;
                    voice_audio_window.erase(voice_audio_window.begin(),
                        voice_audio_window.begin() + excess);
                }

                // Check for wake word every 1.5s with high energy threshold
                // (needs to be louder than TTS playback to trigger)
                if (voice_samples_since_last_check >= VOICE_CHECK_INTERVAL) {
                    voice_samples_since_last_check = 0;

                    float win_sum_sq = 0.0f;
                    for (size_t i = 0; i < voice_audio_window.size(); ++i)
                        win_sum_sq += voice_audio_window[i] * voice_audio_window[i];
                    float win_rms = std::sqrtf(win_sum_sq / (float)voice_audio_window.size());

                    // Higher threshold during speaking (voice must be louder than TTS)
                    if (win_rms > 0.02f) {
                        std::string transcript;
                        if (metalrt_stt_initialized_) {
                            transcript = metalrt_stt_.transcribe(
                                voice_audio_window.data(),
                                (int)voice_audio_window.size(), 16000);
                        } else if (offline_stt_.is_initialized()) {
                            transcript = offline_stt_.transcribe(
                                voice_audio_window.data(),
                                (int)voice_audio_window.size());
                        }

                        // Filter hallucinations
                        if (!transcript.empty()) {
                            auto start = transcript.find_first_not_of(" \t");
                            if (start != std::string::npos) transcript = transcript.substr(start);
                            if (!transcript.empty() && (transcript[0] == '(' || transcript[0] == '['))
                                transcript.clear();
                        }

                        if (vdbg) {
                            fprintf(vdbg, "[interrupt] rms=%.4f transcript=\"%s\"\n",
                                    win_rms, transcript.c_str());
                            fflush(vdbg);
                        }

                        if (!transcript.empty() && wake_detector.check(transcript)) {
                            if (vdbg) {
                                fprintf(vdbg, "[interrupt] WAKE WORD — interrupting!\n");
                                fflush(vdbg);
                            }
                            // Trigger barge-in
                            tts_cancel_flag_.store(true, std::memory_order_release);
                            barge_in_triggered_.store(true, std::memory_order_release);
                            playback_rb_->clear();
                            if (using_metalrt()) metalrt_.cancel();
                            else llm_.cancel();

                            voice_audio_window.clear();
                            voice_samples_since_last_check = 0;
                            voice_command_buf.clear();
                            voice_cmd_silence_chunks = 0;

                            // Go to LISTENING for the new command
                            set_state(PipelineState::LISTENING);
                        }
                    }
                }

                goto skip_stt_feed;
            }

            // --- Non-voice barge-in detection during SPEAKING ---
            if (!voice_mode_active_ && cur_state == PipelineState::SPEAKING &&
                barge_in_enabled_.load(std::memory_order_relaxed)) {
                check_barge_in(chunk_buf.data(), (int)to_read);

                if (barge_in_triggered_.load(std::memory_order_relaxed)) {
                    stt_.reset();
                    barge_in_speech_frames = 0;
                    goto skip_stt_feed;
                }

                goto skip_stt_feed;
            }

            // --- Normal STT feeding (non-voice-mode LISTENING / BARGE_IN states) ---
            {
                bool has_energy = (rms > ENERGY_FLOOR);
                if (has_energy || vad_speech) {
                    stt_.feed_audio(chunk_buf.data(), (int)to_read);
                }
            }
        }

        skip_stt_feed:

        // Periodic debug trace (every ~2 seconds = 200 ticks at 10ms)
        debug_tick_count++;
        if (vdbg && (debug_tick_count % 200) == 0) {
            auto cs = state_.load(std::memory_order_relaxed);
            fprintf(vdbg, "[%ds] tick=%d feeds=%d whisper_runs=%d state=%s window=%zu\n",
                    debug_tick_count / 100, debug_tick_count, debug_feed_count,
                    debug_result_count, pipeline_state_str(cs),
                    voice_audio_window.size());
            fflush(vdbg);
        }

        // --- Non-voice-mode: streaming Zipformer processing ---
        if (!voice_mode_active_) {
            stt_.process_tick();

            auto result = stt_.get_result();
            if (!result.text.empty()) {
                bool text_changed = (result.text != last_partial);
                if (text_changed) {
                    last_partial = result.text;
                }

                if (text_changed && transcript_cb_) {
                    transcript_cb_(result.text, result.is_final);
                }

                if (result.is_final) {
                    LOG_DEBUG("STT", "Final: \"%s\"", result.text.c_str());
                    {
                        std::lock_guard<std::mutex> lock(text_mutex_);
                        pending_text_ = result.text;
                        text_ready_ = true;
                    }
                    text_cv_.notify_one();
                    stt_.reset();
                    last_partial.clear();

                    // Clear barge-in trigger so LLM thread knows it's fresh input
                    barge_in_triggered_.store(false, std::memory_order_release);
                }
            }
        }

        // Voice mode: return to VOICE_IDLE after PROCESSING/SPEAKING/BARGE_IN completes
        if (voice_mode_active_ &&
            (cur_state == PipelineState::IDLE || cur_state == PipelineState::BARGE_IN)) {
            voice_audio_window.clear();
            voice_samples_since_last_check = 0;
            voice_command_buf.clear();
            set_state(PipelineState::VOICE_IDLE);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Flush: emit last partial as final so push-to-talk callers get the transcript
    if (!last_partial.empty()) {
        LOG_DEBUG("STT", "(flush) \"%s\"", last_partial.c_str());
        if (transcript_cb_) {
            transcript_cb_(last_partial, true);
        }
    }

    if (vdbg) {
        fprintf(vdbg, "=== Voice mode ended. ticks=%d feeds=%d whisper_runs=%d ===\n",
                debug_tick_count, debug_feed_count, debug_result_count);
        fclose(vdbg);
    }
}

void Orchestrator::llm_thread_fn() {
    pthread_setname_np("rastack.llm");

    while (live_running_.load(std::memory_order_relaxed)) {
        // Wait for text from STT
        std::string user_text;
        {
            std::unique_lock<std::mutex> lock(text_mutex_);
            text_cv_.wait(lock, [this]() {
                return text_ready_ || !live_running_.load(std::memory_order_relaxed);
            });
            if (!live_running_.load()) break;
            user_text = pending_text_;
            text_ready_ = false;
        }

        if (user_text.empty()) continue;

        // --- Continuation detection: check if user wants to resume interrupted response ---
        bool is_continuation = false;
        {
            std::string lower;
            lower.reserve(user_text.size());
            for (char c : user_text) lower += std::tolower(static_cast<unsigned char>(c));

            // Check for continuation phrases
            static const char* cont_phrases[] = {
                "continue", "go on", "keep going", "carry on", "go ahead",
                "yes continue", "yes go on", "yeah continue", "yeah go on",
                "please continue", "keep talking"
            };

            std::lock_guard<std::mutex> lock(barge_in_mutex_);
            if (!interrupted_response_.empty()) {
                for (auto& phrase : cont_phrases) {
                    if (lower.find(phrase) != std::string::npos) {
                        is_continuation = true;
                        break;
                    }
                }
                // Expire interrupted context after 60 seconds
                if (interrupted_at_us_ > 0 && (now_us() - interrupted_at_us_) > 60'000'000) {
                    interrupted_response_.clear();
                    interrupted_query_.clear();
                    interrupted_chars_spoken_ = 0;
                    is_continuation = false;
                }
            }
        }

        // Reset barge-in flags for this turn
        barge_in_triggered_.store(false, std::memory_order_release);
        tts_cancel_flag_.store(false, std::memory_order_release);

        // Clear stale audio from previous turn before new TTS output
        playback_rb_->clear();

        set_state(PipelineState::PROCESSING);

        // --- Pre-LLM action matching (Tier 1 keyword match) ---
        // Try to match common voice commands directly without going through LLM.
        // This handles "open X", "play music", "set volume", etc.
        {
            std::string lower_text;
            lower_text.reserve(user_text.size());
            for (char c : user_text) lower_text += std::tolower(static_cast<unsigned char>(c));

            // "open X" → open_app action
            std::string action_name, args_json;
            if (lower_text.find("open ") != std::string::npos) {
                auto pos = lower_text.find("open ");
                std::string app = user_text.substr(pos + 5);
                // Trim trailing punctuation/whitespace
                while (!app.empty() && (app.back() == '.' || app.back() == '!' ||
                       app.back() == '?' || app.back() == ' ')) app.pop_back();
                // Trim "please", "for me" etc from the app name
                for (const char* suffix : {" please", " for me", " from there"}) {
                    std::string ls;
                    for (char c : app) ls += std::tolower(static_cast<unsigned char>(c));
                    auto sp = ls.rfind(suffix);
                    if (sp != std::string::npos && sp + std::strlen(suffix) == ls.size()) {
                        app = app.substr(0, sp);
                        while (!app.empty() && app.back() == ' ') app.pop_back();
                    }
                }
                if (!app.empty()) {
                    action_name = "open_app";
                    args_json = "{\"app\": \"" + app + "\"}";
                }
            }
            // "play/pause music" → play_pause_music
            else if (lower_text.find("play music") != std::string::npos ||
                     lower_text.find("pause music") != std::string::npos ||
                     lower_text == "play" || lower_text == "pause") {
                action_name = "play_pause_music";
                args_json = "{}";
            }
            // "next song/track" → next_track
            else if (lower_text.find("next song") != std::string::npos ||
                     lower_text.find("next track") != std::string::npos ||
                     lower_text.find("skip") != std::string::npos) {
                action_name = "next_track";
                args_json = "{}";
            }
            // "search (for) X" / "google X" / "look up X" → search_web
            else if (lower_text.find("search") != std::string::npos ||
                     lower_text.find("google") != std::string::npos ||
                     lower_text.find("look up") != std::string::npos) {
                // Extract query: everything after "search (for)", "google", "look up"
                std::string query;
                for (const char* prefix : {"search for ", "search ", "google ", "look up "}) {
                    auto pos = lower_text.find(prefix);
                    if (pos != std::string::npos) {
                        query = user_text.substr(pos + std::strlen(prefix));
                        break;
                    }
                }
                // Trim filler from query
                for (const char* filler : {"on the browser", "on browser", "on google",
                        "on the web", "please", "for me"}) {
                    std::string lq;
                    for (char c : query) lq += std::tolower(static_cast<unsigned char>(c));
                    auto fp = lq.find(filler);
                    if (fp != std::string::npos) {
                        query.erase(fp, std::strlen(filler));
                    }
                }
                // Trim whitespace/punctuation
                while (!query.empty() && (query.back() == ' ' || query.back() == '.' ||
                       query.back() == '!' || query.back() == '?')) query.pop_back();
                while (!query.empty() && query.front() == ' ') query.erase(query.begin());
                if (!query.empty()) {
                    action_name = "search_web";
                    std::string escaped;
                    for (char c : query) {
                        if (c == '"') escaped += "\\\"";
                        else escaped += c;
                    }
                    args_json = "{\"query\": \"" + escaped + "\"}";
                }
            }
            // "make a note" / "create a note" → create_note
            else if (lower_text.find("make a note") != std::string::npos ||
                     lower_text.find("create a note") != std::string::npos ||
                     lower_text.find("write a note") != std::string::npos) {
                std::string content;
                for (const char* prefix : {"make a note that ", "make a note ", "create a note that ",
                        "create a note ", "write a note that ", "write a note "}) {
                    auto pos = lower_text.find(prefix);
                    if (pos != std::string::npos) {
                        content = user_text.substr(pos + std::strlen(prefix));
                        break;
                    }
                }
                while (!content.empty() && (content.back() == '.' || content.back() == ' ')) content.pop_back();
                if (!content.empty() && tools_.has_tool("create_note")) {
                    action_name = "create_note";
                    std::string escaped;
                    for (char c : content) {
                        if (c == '"') escaped += "\\\"";
                        else escaped += c;
                    }
                    args_json = "{\"title\": \"Note\", \"text\": \"" + escaped + "\"}";
                }
            }

            if (!action_name.empty() && tools_.has_tool(action_name)) {
                LOG_DEBUG("VoiceCmd", "Tier 1 match: %s %s", action_name.c_str(), args_json.c_str());
                ToolCall call{action_name, args_json};
                auto result = tools_.execute(call);

                std::string reply = result.success
                    ? result.result_json
                    : "Sorry, that didn't work.";
                // Use the friendly display text if available
                if (result.success && !result.result_json.empty()) {
                    // Try to get a nicer message — result_json has the structured data
                    reply = "Done! " + action_name;
                    if (action_name == "open_app") {
                        auto ap = args_json.find("\"app\": \"");
                        if (ap != std::string::npos) {
                            auto ae = args_json.find('"', ap + 8);
                            reply = "Opened " + args_json.substr(ap + 8, ae - ap - 8) + " for you.";
                        }
                    }
                }

                if (response_cb_) response_cb_(reply);
                live_history_.emplace_back("user", user_text);
                live_history_.emplace_back("assistant", reply);

                // Speak the response
                std::vector<float> samples;
                if (using_metalrt_tts())
                    samples = metalrt_tts_.synthesize(reply);
                else
                    samples = tts_.synthesize(reply);

                set_state(PipelineState::SPEAKING);
                size_t offset = 0;
                while (offset < samples.size()) {
                    size_t written = playback_rb_->write(
                        samples.data() + offset, samples.size() - offset);
                    offset += written;
                    if (offset < samples.size())
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }

                // Wait for playback
                while (playback_rb_->available_read() > 0 &&
                       live_running_.load(std::memory_order_relaxed)) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }

                if (voice_mode_active_)
                    set_state(PipelineState::VOICE_IDLE);
                else
                    set_state(PipelineState::IDLE);
                continue;
            }
        }

        // --- Async TTS worker with barge-in cancel support ---
        std::mutex tts_queue_mutex;
        std::condition_variable tts_queue_cv;
        std::vector<std::string> tts_queue;
        bool llm_done = false;
        std::atomic<int> chars_spoken{0};  // track how many chars of response were spoken

        std::thread tts_worker([&]() {
            pthread_setname_np("rastack.tts.live");
            while (true) {
                // Check cancel flag before waiting
                if (tts_cancel_flag_.load(std::memory_order_relaxed)) break;

                std::string sentence;
                {
                    std::unique_lock<std::mutex> lock(tts_queue_mutex);
                    tts_queue_cv.wait(lock, [&]() {
                        return !tts_queue.empty() || llm_done ||
                               tts_cancel_flag_.load(std::memory_order_relaxed);
                    });
                    if (tts_cancel_flag_.load(std::memory_order_relaxed)) break;
                    if (tts_queue.empty() && llm_done) break;
                    if (tts_queue.empty()) continue;
                    sentence = std::move(tts_queue.front());
                    tts_queue.erase(tts_queue.begin());
                }

                // Check cancel again before synthesis
                if (tts_cancel_flag_.load(std::memory_order_relaxed)) break;

                LOG_DEBUG("TTS", "Synthesizing: \"%s\"", sentence.c_str());
                set_state(PipelineState::SPEAKING);

                // Synthesize to samples, then write with backpressure
                std::vector<float> samples;
                if (using_metalrt_tts())
                    samples = metalrt_tts_.synthesize(sentence);
                else
                    samples = tts_.synthesize(sentence);

                // Write with backpressure (like rcli_speak_streaming)
                size_t offset = 0;
                while (offset < samples.size() &&
                       !tts_cancel_flag_.load(std::memory_order_relaxed)) {
                    size_t written = playback_rb_->write(
                        samples.data() + offset, samples.size() - offset);
                    offset += written;
                    if (offset < samples.size()) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    }
                }

                // Track spoken chars (if not cancelled mid-synthesis)
                if (!tts_cancel_flag_.load(std::memory_order_relaxed)) {
                    chars_spoken.fetch_add(static_cast<int>(sentence.size()), std::memory_order_relaxed);
                }
            }
        });

        auto queue_sentence = [&](const std::string& sentence) {
            if (tts_cancel_flag_.load(std::memory_order_relaxed)) return;
            std::string clean = sanitize_for_tts(sentence);
            if (clean.empty()) return;
            {
                std::lock_guard<std::mutex> lock(tts_queue_mutex);
                tts_queue.push_back(std::move(clean));
            }
            tts_queue_cv.notify_one();
        };

        const auto& live_profile = using_metalrt() ? metalrt_.profile() : llm_.profile();
        const std::string& live_tc_start = live_profile.tool_call_start;
        std::string response;

        // --- Handle continuation: resume from where we left off ---
        if (is_continuation) {
            std::string remaining;
            {
                std::lock_guard<std::mutex> lock(barge_in_mutex_);
                if (interrupted_chars_spoken_ < (int)interrupted_response_.size()) {
                    remaining = interrupted_response_.substr(interrupted_chars_spoken_);
                }
                // Clear interrupted state
                interrupted_response_.clear();
                interrupted_query_.clear();
                interrupted_chars_spoken_ = 0;
            }

            if (!remaining.empty()) {
                LOG_DEBUG("LLM", "Resuming interrupted response (%zu chars remaining)", remaining.size());
                // Feed remaining text directly to TTS
                SentenceDetector cont_detector(queue_sentence, 6, 35, 20);
                cont_detector.feed(remaining);
                cont_detector.flush();
                response = remaining;
            }
        } else {
            // --- Normal LLM generation ---
            // Speculative first-token detection
            std::string token_buffer;
            bool detected_tool_call = false;
            constexpr int SPECULATIVE_TOKENS = 30;
            int tokens_buffered = 0;
            SentenceDetector detector(queue_sentence, 6, 35, 20);

            auto speculative_callback = [&](const TokenOutput& tok) {
                // Abort early if barge-in triggered during generation
                if (barge_in_triggered_.load(std::memory_order_relaxed)) return;
                if (detected_tool_call) return;
                token_buffer += tok.text;
                tokens_buffered++;
                if (tokens_buffered <= SPECULATIVE_TOKENS) {
                    // Still in speculative window — check for tool call markers
                    if (token_buffer.find(live_tc_start) != std::string::npos) {
                        detected_tool_call = true;
                    }
                } else if (!detected_tool_call) {
                    // Past speculative window — flush entire buffer to TTS
                    // (token_buffer already includes tok.text from above)
                    detector.feed(token_buffer);
                    token_buffer.clear();
                }
            };

            if (using_metalrt()) {
                std::string l_tool_defs = tools_.get_tool_definitions_json();
                std::string l_tool_system = metalrt_.profile().build_tool_system_prompt(
                    config_.system_prompt, l_tool_defs);

                std::vector<std::pair<std::string, std::string>> l_trimmed;
                int l_ctx = metalrt_.context_size();
                if (l_ctx > 0) {
                    int l_sys_tok = metalrt_.count_tokens(l_tool_system);
                    int l_usr_tok = metalrt_.count_tokens(user_text);
                    int l_budget = l_ctx - 512 - l_sys_tok - l_usr_tok - 50;

                    if (!live_history_.empty() && l_budget > 0) {
                        int total = 0;
                        for (int i = static_cast<int>(live_history_.size()) - 1; i >= 0; i--) {
                            int entry_tok = metalrt_.count_tokens(
                                live_history_[i].first + ": " + live_history_[i].second);
                            if (total + entry_tok > l_budget) break;
                            total += entry_tok;
                            l_trimmed.insert(l_trimmed.begin(), live_history_[i]);
                        }
                    }
                }

                std::string l_hint = tools_.build_tool_hint(user_text);
                std::string l_hinted = l_hint.empty() ? user_text : (l_hint + "\n" + user_text);

                if (metalrt_.has_prompt_cache()) {
                    std::string user_turn = metalrt_.profile().build_user_turn(l_hinted);
                    response = metalrt_.generate_raw_continue(user_turn, speculative_callback);
                } else {
                    std::string l_full = metalrt_.profile().build_chat_prompt(
                        l_tool_system, l_trimmed, l_hinted);
                    response = metalrt_.generate_raw(l_full, speculative_callback);
                }
            } else {
                std::string tool_defs = tools_.get_tool_definitions_json();
                std::string tool_system = llm_.profile().build_tool_system_prompt(
                    config_.system_prompt, tool_defs);

                int ctx_size = llm_.context_size();
                int system_tokens = llm_.count_tokens(tool_system);
                int user_tokens = llm_.count_tokens(user_text);
                int history_budget = ctx_size - 512 - system_tokens - user_tokens - 50;

                std::vector<std::pair<std::string, std::string>> trimmed;
                if (!live_history_.empty() && history_budget > 0) {
                    int total = 0;
                    for (int i = static_cast<int>(live_history_.size()) - 1; i >= 0; i--) {
                        int entry_tokens = llm_.count_tokens(
                            live_history_[i].first + ": " + live_history_[i].second);
                        if (total + entry_tokens > history_budget) break;
                        total += entry_tokens;
                        trimmed.insert(trimmed.begin(), live_history_[i]);
                    }
                }

                std::string live_hint = tools_.build_tool_hint(user_text);
                std::string hinted_user = live_hint.empty() ? user_text : (live_hint + "\n" + user_text);

                if (llm_.has_prompt_cache() && trimmed.empty()) {
                    std::string user_portion = llm_.profile().build_user_turn(hinted_user);
                    response = llm_.generate_with_cached_prompt(user_portion, speculative_callback);
                } else {
                    std::string prompt = llm_.build_chat_prompt(tool_system, trimmed, hinted_user);
                    response = llm_.generate(prompt, speculative_callback);
                }
            }

            // If barge-in happened during generation, skip tool handling / flush
            if (!barge_in_triggered_.load(std::memory_order_relaxed)) {
                if (detected_tool_call) {
                    auto tool_calls = live_profile.parse_tool_calls(response);
                    if (tool_calls.empty()) tool_calls = tools_.parse_tool_calls(response);
                    if (!tool_calls.empty()) {
                        LOG_DEBUG("Pipeline", "Tool calls detected, executing...");
                        auto results = tools_.execute_all(tool_calls);
                        for (auto& r : results) {
                            LOG_DEBUG("Pipeline", "Tool '%s': %s -> %s",
                                    r.name.c_str(), r.success ? "OK" : "FAIL", r.result_json.c_str());
                        }

                        std::string formatted = tools_.format_results(results);
                        SentenceDetector detector2(queue_sentence, 6, 35, 20);

                        if (using_metalrt()) {
                            std::string l2_sys = config_.system_prompt + " Do NOT output JSON.";
                            std::string l2_msg = "Tool results: " + formatted + "\nSummarize briefly.";
                            std::string l2_prompt = metalrt_.profile().build_chat_prompt(l2_sys, {}, l2_msg);
                            response = metalrt_.generate_raw(l2_prompt,
                                [&](const TokenOutput& tok) { detector2.feed(tok.text); });
                        } else {
                            std::string tool_system_for_cont = llm_.profile().build_tool_system_prompt(
                                config_.system_prompt, tools_.get_tool_definitions_json());
                            std::string continuation = llm_.build_tool_continuation_prompt(
                                config_.system_prompt, user_text, response, formatted);
                            llm_.clear_kv_cache();
                            response = llm_.generate(continuation, [&](const TokenOutput& tok) {
                                detector2.feed(tok.text);
                            });
                        }
                        detector2.flush();
                    } else {
                        detector.feed(sanitize_for_tts(response));
                        detector.flush();
                    }
                } else {
                    if (!token_buffer.empty()) {
                        detector.feed(token_buffer);
                    }
                    detector.flush();
                }
            }
        }

        // Signal TTS worker done and wait for it
        {
            std::lock_guard<std::mutex> lock(tts_queue_mutex);
            llm_done = true;
        }
        tts_queue_cv.notify_one();
        tts_worker.join();

        LOG_DEBUG("LLM", "Response: \"%s\"", response.c_str());

        std::string clean_response = live_profile.clean_output(response);

        // --- Store interrupted state if barge-in happened ---
        if (barge_in_triggered_.load(std::memory_order_relaxed) && !clean_response.empty()) {
            std::lock_guard<std::mutex> lock(barge_in_mutex_);
            interrupted_response_ = clean_response;
            interrupted_query_ = user_text;
            interrupted_chars_spoken_ = chars_spoken.load(std::memory_order_relaxed);
            interrupted_at_us_ = now_us();
            LOG_DEBUG("BargeIn", "Stored interrupted response (%d/%zu chars spoken)",
                      interrupted_chars_spoken_, clean_response.size());
        }

        if (!clean_response.empty()) {
            // Fire response callback so TUI/consumers can display the response
            if (response_cb_) response_cb_(clean_response);

            live_history_.emplace_back("user", user_text);
            live_history_.emplace_back("assistant", clean_response);
            // Cap at 20 entries (10 turns)
            while (live_history_.size() > 20) {
                live_history_.erase(live_history_.begin());
                if (!live_history_.empty()) live_history_.erase(live_history_.begin());
            }
        }

        // Wait for playback to finish, with barge-in awareness
        while (playback_rb_->available_read() > 0 &&
               live_running_.load(std::memory_order_relaxed) &&
               !barge_in_triggered_.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        // If barge-in triggered, stay in BARGE_IN state (stt_thread handles transition)
        // Otherwise go back to listening (or VOICE_IDLE if in voice mode)
        if (!barge_in_triggered_.load(std::memory_order_relaxed)) {
            if (voice_mode_active_) {
                set_state(PipelineState::VOICE_IDLE);
            } else {
                set_state(PipelineState::LISTENING);
            }
        }
    }
}

void Orchestrator::recache_system_prompt() {
    std::string tool_defs = tools_.get_tool_definitions_json();
    std::string tool_system = llm_.profile().build_tool_system_prompt(
        config_.system_prompt, tool_defs);
    llm_.cache_system_prompt(tool_system);

    if (metalrt_.is_initialized()) {
        std::string mrt_system = metalrt_.profile().build_tool_system_prompt(
            config_.system_prompt, tool_defs);
        std::string mrt_prefix = metalrt_.profile().build_system_prefix(mrt_system);
        metalrt_.cache_system_prompt(mrt_prefix);
        metalrt_.set_system_prompt(mrt_system);
    }

    LOG_DEBUG("Pipeline", "Re-cached system prompt with updated tool defs");
}

void Orchestrator::set_tts_voice(const std::string& voice_name) {
    if (voice_name.empty()) return;

    if (metalrt_tts_initialized_ && metalrt_tts_.is_initialized()) {
        metalrt_tts_.set_voice(voice_name);
        LOG_DEBUG("Pipeline", "MetalRT TTS voice changed to: %s", voice_name.c_str());
    }

    // For sherpa-onnx Kokoro fallback, map voice name to speaker_id
    if (tts_.is_initialized() && config_.tts.architecture == "kokoro") {
        int sid = kokoro_voice_to_speaker_id(voice_name);
        tts_.set_speaker_id(sid);
        LOG_DEBUG("Pipeline", "Sherpa-onnx TTS speaker_id changed to: %d (%s)", sid, voice_name.c_str());
    }
}

bool Orchestrator::switch_backend(LlmBackend backend) {
    auto cur = state_.load(std::memory_order_acquire);
    if (cur != PipelineState::IDLE) {
        LOG_ERROR("Pipeline", "Cannot switch backend while pipeline is %s",
                  pipeline_state_str(cur));
        return false;
    }

    if (backend == LlmBackend::METALRT) {
        if (!metalrt_.is_initialized()) {
            LOG_ERROR("Pipeline", "MetalRT engine not initialized");
            return false;
        }
        active_backend_ = LlmBackend::METALRT;
        LOG_INFO("Pipeline", "Switched to MetalRT backend");
    } else {
        active_backend_ = LlmBackend::LLAMACPP;
        LOG_INFO("Pipeline", "Switched to llama.cpp backend");
    }
    return true;
}

bool Orchestrator::reload_llm(const LlmConfig& new_config) {
    auto cur = state_.load(std::memory_order_acquire);
    if (cur != PipelineState::IDLE) {
        LOG_ERROR("Pipeline", "Cannot reload LLM while pipeline is %s",
                  pipeline_state_str(cur));
        return false;
    }

    LOG_INFO("Pipeline", "Hot-swapping LLM model: %s", new_config.model_path.c_str());
    llm_.shutdown();

    if (!llm_.init(new_config)) {
        LOG_ERROR("Pipeline", "Failed to init new LLM model");
        return false;
    }

    config_.llm = new_config;
    tools_.set_model_profile(&llm_.profile());
    recache_system_prompt();
    LOG_INFO("Pipeline", "LLM hot-swap complete (%s profile)", llm_.profile().family_name.c_str());
    return true;
}

bool Orchestrator::start_voice_mode(const std::string& wake_phrase) {
    if (live_running_.load()) {
        LOG_ERROR("Pipeline", "Cannot start voice mode while live mode is running");
        return false;
    }

    wake_phrase_ = wake_phrase;
    voice_mode_active_ = true;
    barge_in_enabled_.store(true, std::memory_order_release);

    live_running_.store(true, std::memory_order_release);
    live_history_.clear();

    // Drain stale audio and reset STT state
    capture_rb_->clear();
    stt_.reset();

    // Start audio
    audio_.start();
    set_state(PipelineState::VOICE_IDLE);

    // STT thread (handles wake word detection in VOICE_IDLE state)
    stt_thread_ = std::thread([this]() { stt_thread_fn(); });

    // LLM+TTS thread
    llm_thread_ = std::thread([this]() { llm_thread_fn(); });

    LOG_INFO("Pipeline", "Voice mode started (wake phrase: \"%s\")", wake_phrase.c_str());
    return true;
}

void Orchestrator::stop_voice_mode() {
    voice_mode_active_ = false;
    barge_in_enabled_.store(false, std::memory_order_release);
    stop_live();
    LOG_INFO("Pipeline", "Voice mode stopped");
}

void Orchestrator::set_state(PipelineState new_state) {
    PipelineState old = state_.exchange(new_state, std::memory_order_release);
    if (old != new_state) {
        LOG_DEBUG("State", "%s -> %s", pipeline_state_str(old), pipeline_state_str(new_state));
        if (state_cb_) state_cb_(old, new_state);
    }
}

} // namespace rastack
