#include "pipeline/orchestrator.h"
#include "pipeline/text_sanitizer.h"
#include "core/base64.h"
#include "core/log.h"
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <future>

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

    if (!stt_.init(config.stt)) {
        LOG_ERROR("Pipeline", "STT init failed");
        return false;
    }

    if (!offline_stt_.init(config.offline_stt)) {
        LOG_WARN("Pipeline", "Offline STT init failed (will use streaming STT)");
    }

    if (!llm_.init(config.llm)) {
        LOG_ERROR("Pipeline", "LLM init failed");
        return false;
    }

    if (!tts_.init(config.tts)) {
        LOG_ERROR("Pipeline", "TTS init failed");
        return false;
    }

    if (!audio_.init(config.audio, capture_rb_.get(), playback_rb_.get())) {
        LOG_ERROR("Pipeline", "Audio init failed");
        return false;
    }

    if (!vad_.init(config.vad)) {
        LOG_WARN("Pipeline", "VAD init failed (will process all audio)");
    }

    tools_.register_defaults();

    llm_.cache_system_prompt(config.system_prompt);

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

    if (offline_stt_.is_initialized()) {
        stt_text = offline_stt_.transcribe(stt_audio, stt_audio_len);
        timings_.stt_latency_us = offline_stt_.last_latency_us();
    } else {
        // Fallback: streaming Zipformer
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
            auto audio = tts_.synthesize(sentence);
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

    std::string tool_defs = tools_.get_tool_definitions_json();
    std::string llm_response;
    bool use_tools = !tool_defs.empty() && tools_.needs_tools(stt_text);

    fprintf(stderr, "[Pipeline] Tool routing: %s (query: \"%s\")\n",
            use_tools ? "WITH tools" : "NO tools (knowledge query)", stt_text.c_str());

    if (use_tools) {
        // === TOOL-AWARE PATH ===
        // First pass: generate WITHOUT streaming to TTS
        llm_response = llm_.generate_with_tools(
            stt_text, tool_defs, config_.system_prompt, nullptr
        );

        fprintf(stderr, "[Pipeline] First pass: \"%s\"\n", llm_response.c_str());

        // Check for tool calls
        auto tool_calls = tools_.parse_tool_calls(llm_response);

        if (!tool_calls.empty()) {
            fprintf(stderr, "[Pipeline] Detected %zu tool call(s), executing...\n", tool_calls.size());
            auto results = tools_.execute_all(tool_calls);
            for (auto& r : results) {
                fprintf(stderr, "[Pipeline] Tool '%s': %s -> %s\n",
                        r.name.c_str(), r.success ? "OK" : "FAIL", r.result_json.c_str());
            }

            // Build continuation prompt with tool results
            std::string formatted_results = tools_.format_results(results);
            std::string continuation = llm_.build_tool_continuation_prompt(
                config_.system_prompt, stt_text, llm_response, formatted_results
            );

            // Clear KV cache for fresh second pass
            llm_.clear_kv_cache();

            // Second pass: generate WITH streaming to TTS
            SentenceDetector detector(queue_sentence, 3, 25, 7);
            llm_response = llm_.generate(continuation, [&](const TokenOutput& tok) {
                detector.feed(tok.text);
            });
            detector.flush();

            fprintf(stderr, "[Pipeline] Second pass: \"%s\"\n", llm_response.c_str());
        } else {
            // No tool calls — sanitize and feed first-pass text to TTS
            SentenceDetector detector(queue_sentence, 3, 25, 7);
            detector.feed(sanitize_for_tts(llm_response));
            detector.flush();
        }
    } else {
        // === NO-TOOLS PATH (knowledge queries) ===
        SentenceDetector detector(queue_sentence, 3, 25, 7);

        if (llm_.has_prompt_cache()) {
            // Use cached system prompt — only eval user portion
            std::string user_portion =
                "<|im_start|>user\n" + stt_text + " /no_think<|im_end|>\n"
                "<|im_start|>assistant\n";
            llm_response = llm_.generate_with_cached_prompt(user_portion,
                [&](const TokenOutput& tok) { detector.feed(tok.text); });
        } else {
            std::string prompt = llm_.build_chat_prompt(
                config_.system_prompt, {}, stt_text
            );
            llm_response = llm_.generate(prompt, [&](const TokenOutput& tok) {
                detector.feed(tok.text);
            });
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

    timings_.llm_first_token_us = llm_.last_stats().first_token_us;
    timings_.llm_total_us = now_us() - t_llm_start;

    // Strip <think>...</think> from response for logging/parsing
    std::string clean_response = llm_response;
    {
        size_t ts, te;
        while ((ts = clean_response.find("<think>")) != std::string::npos) {
            te = clean_response.find("</think>", ts);
            if (te != std::string::npos) {
                clean_response.erase(ts, te - ts + 8);
            } else {
                // Unclosed think block — remove everything from <think> onward
                clean_response.erase(ts);
                break;
            }
        }
        // Trim leading/trailing whitespace
        size_t f = clean_response.find_first_not_of(" \n\r\t");
        size_t l = clean_response.find_last_not_of(" \n\r\t");
        if (f != std::string::npos) {
            clean_response = clean_response.substr(f, l - f + 1);
        } else {
            clean_response.clear();
        }
    }

    fprintf(stderr, "[Pipeline] LLM response: \"%s\"\n", clean_response.c_str());
    fprintf(stderr, "[Pipeline] LLM stats: first_token=%.1fms, total=%.1fms, %.1f tok/s\n",
            timings_.llm_first_token_us / 1000.0,
            timings_.llm_total_us / 1000.0,
            llm_.last_stats().gen_tps());

    // 4. Collect TTS results
    set_state(PipelineState::SPEAKING);

    if (all_tts_audio.empty() && !clean_response.empty()) {
        auto audio = tts_.synthesize(sanitize_for_tts(clean_response));
        all_tts_audio = std::move(audio);
    }

    timings_.tts_first_sentence_us = (first_sentence_time > 0) ?
        (first_sentence_time - t_llm_start) : 0;

    // 5. Save output WAV
    if (!all_tts_audio.empty() && !output_wav.empty()) {
        AudioIO::save_wav(output_wav, all_tts_audio.data(),
                         all_tts_audio.size(), tts_.sample_rate());
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
    fprintf(stderr, "  LLM throughput:     %6.1f tok/s\n", llm_.last_stats().gen_tps());
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

    if (offline_stt_.is_initialized()) {
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

            auto audio = tts_.synthesize(sentence);
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

    std::string tool_defs = tools_.get_tool_definitions_json();
    std::string llm_response;
    bool use_tools = !tool_defs.empty() && tools_.needs_tools(stt_text);

    fprintf(stderr, "[Pipeline] Tool routing: %s\n", use_tools ? "WITH tools" : "NO tools (knowledge query)");

    if (use_tools) {
        // First pass: no TTS
        llm_response = llm_.generate_with_tools(
            stt_text, tool_defs, config_.system_prompt, nullptr
        );

        auto tool_calls = tools_.parse_tool_calls(llm_response);

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
            std::string continuation = llm_.build_tool_continuation_prompt(
                config_.system_prompt, stt_text, llm_response, formatted_results
            );

            llm_.clear_kv_cache();

            // Second pass: with TTS streaming
            SentenceDetector detector(queue_sentence, 3, 25, 7);
            llm_response = llm_.generate(continuation, [&](const TokenOutput& tok) {
                detector.feed(tok.text);
            });
            detector.flush();
        } else {
            // No tool calls — sanitize and feed first-pass text to TTS
            SentenceDetector detector(queue_sentence, 3, 25, 7);
            detector.feed(sanitize_for_tts(llm_response));
            detector.flush();
        }
    } else {
        // No-tools path (knowledge queries)
        SentenceDetector detector(queue_sentence, 3, 25, 7);

        if (llm_.has_prompt_cache()) {
            std::string user_portion =
                "<|im_start|>user\n" + stt_text + " /no_think<|im_end|>\n"
                "<|im_start|>assistant\n";
            llm_response = llm_.generate_with_cached_prompt(user_portion,
                [&](const TokenOutput& tok) { detector.feed(tok.text); });
        } else {
            std::string prompt = llm_.build_chat_prompt(
                config_.system_prompt, {}, stt_text
            );
            llm_response = llm_.generate(prompt, [&](const TokenOutput& tok) {
                detector.feed(tok.text);
            });
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

    timings_.llm_first_token_us = llm_.last_stats().first_token_us;
    timings_.llm_total_us = now_us() - t_llm_start;

    // Strip <think> tags for clean response
    std::string clean_response = llm_response;
    {
        size_t ts, te;
        while ((ts = clean_response.find("<think>")) != std::string::npos) {
            te = clean_response.find("</think>", ts);
            if (te != std::string::npos) {
                clean_response.erase(ts, te - ts + 8);
            } else {
                clean_response.erase(ts);
                break;
            }
        }
        size_t f = clean_response.find_first_not_of(" \n\r\t");
        size_t l = clean_response.find_last_not_of(" \n\r\t");
        if (f != std::string::npos) {
            clean_response = clean_response.substr(f, l - f + 1);
        } else {
            clean_response.clear();
        }
    }

    // If no audio was streamed but we have text, synthesize the clean response as fallback
    if (first_sentence_time == 0 && !clean_response.empty()) {
        auto audio = tts_.synthesize(sanitize_for_tts(clean_response));
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
                llm_.last_stats().gen_tps(),
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
    fprintf(stderr, "  LLM throughput:     %6.1f tok/s\n", llm_.last_stats().gen_tps());

    return true;
}

// --- Live mode ---
bool Orchestrator::start_live() {
    if (live_running_.load()) return false;
    live_running_.store(true, std::memory_order_release);

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
    if (offline_stt_.is_initialized()) {
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

void Orchestrator::stt_thread_fn() {
    pthread_setname_np("rastack.stt");

    std::vector<float> chunk_buf(1600);
    std::string last_partial;

    constexpr float ENERGY_FLOOR = 0.005f;

    while (live_running_.load(std::memory_order_relaxed)) {
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

            // Only feed audio to STT when there's actual energy above the
            // noise floor.  This prevents background noise from producing
            // phantom transcripts while still allowing push-to-talk speech
            // through (real speech easily exceeds the floor).
            bool has_energy = (rms > ENERGY_FLOOR);
            bool vad_speech = vad_.is_initialized() && vad_.is_speech();
            if (has_energy || vad_speech) {
                stt_.feed_audio(chunk_buf.data(), (int)to_read);
            }
        }

        stt_.process_tick();

        auto result = stt_.get_result();
        if (!result.text.empty()) {
            if (result.text != last_partial) {
                last_partial = result.text;
                if (transcript_cb_) {
                    transcript_cb_(result.text, result.is_final);
                }
            }

            if (result.is_final) {
                LOG_DEBUG("STT", "\"%s\"", result.text.c_str());
                {
                    std::lock_guard<std::mutex> lock(text_mutex_);
                    pending_text_ = result.text;
                    text_ready_ = true;
                }
                text_cv_.notify_one();
                stt_.reset();
                last_partial.clear();
            }
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

        set_state(PipelineState::PROCESSING);

        // --- Async TTS worker (same pattern as file/stream pipeline) ---
        std::mutex tts_queue_mutex;
        std::condition_variable tts_queue_cv;
        std::vector<std::string> tts_queue;
        bool llm_done = false;

        std::thread tts_worker([&]() {
            pthread_setname_np("rastack.tts.live");
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
                LOG_DEBUG("TTS", "Synthesizing: \"%s\"", sentence.c_str());
                set_state(PipelineState::SPEAKING);
                tts_.synthesize_to_ring_buffer(sentence, *playback_rb_);
            }
        });

        auto queue_sentence = [&](const std::string& sentence) {
            std::string clean = sanitize_for_tts(sentence);
            if (clean.empty()) return;
            {
                std::lock_guard<std::mutex> lock(tts_queue_mutex);
                tts_queue.push_back(std::move(clean));
            }
            tts_queue_cv.notify_one();
        };

        std::string tool_defs = tools_.get_tool_definitions_json();
        std::string response;
        bool use_tools = !tool_defs.empty() && tools_.needs_tools(user_text);

        if (use_tools) {
            // First pass: no TTS callback
            response = llm_.generate_with_tools(
                user_text, tool_defs, config_.system_prompt, nullptr
            );

            auto tool_calls = tools_.parse_tool_calls(response);

            if (!tool_calls.empty()) {
                LOG_DEBUG("Pipeline", "Tool calls detected, executing...");
                auto results = tools_.execute_all(tool_calls);
                for (auto& r : results) {
                    LOG_DEBUG("Pipeline", "Tool '%s': %s -> %s",
                            r.name.c_str(), r.success ? "OK" : "FAIL", r.result_json.c_str());
                }

                std::string formatted = tools_.format_results(results);
                std::string continuation = llm_.build_tool_continuation_prompt(
                    config_.system_prompt, user_text, response, formatted
                );

                llm_.clear_kv_cache();

                // Second pass: with TTS streaming
                SentenceDetector detector(queue_sentence, 3, 25, 7);
                response = llm_.generate(continuation, [&](const TokenOutput& tok) {
                    detector.feed(tok.text);
                });
                detector.flush();
            } else {
                // No tool calls — sanitize and send to TTS
                SentenceDetector detector(queue_sentence, 3, 25, 7);
                detector.feed(sanitize_for_tts(response));
                detector.flush();
            }
        } else {
            // No tools path (knowledge query) — stream directly
            SentenceDetector detector(queue_sentence, 3, 25, 7);

            if (llm_.has_prompt_cache()) {
                std::string user_portion =
                    "<|im_start|>user\n" + user_text + " /no_think<|im_end|>\n"
                    "<|im_start|>assistant\n";
                response = llm_.generate_with_cached_prompt(user_portion,
                    [&](const TokenOutput& tok) { detector.feed(tok.text); });
            } else {
                std::string prompt = llm_.build_chat_prompt(
                    config_.system_prompt, {}, user_text
                );
                response = llm_.generate(prompt, [&](const TokenOutput& tok) {
                    detector.feed(tok.text);
                });
            }
            detector.flush();
        }

        // Signal TTS worker done and wait for it
        {
            std::lock_guard<std::mutex> lock(tts_queue_mutex);
            llm_done = true;
        }
        tts_queue_cv.notify_one();
        tts_worker.join();

        LOG_DEBUG("LLM", "Response: \"%s\"", response.c_str());

        // Wait for playback to finish, then go back to listening
        while (playback_rb_->available_read() > 0 &&
               live_running_.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        set_state(PipelineState::LISTENING);
    }
}

void Orchestrator::set_state(PipelineState new_state) {
    PipelineState old = state_.exchange(new_state, std::memory_order_release);
    if (old != new_state) {
        LOG_DEBUG("State", "%s -> %s", pipeline_state_str(old), pipeline_state_str(new_state));
        if (state_cb_) state_cb_(old, new_state);
    }
}

} // namespace rastack
