#include "bench/benchmark.h"
#include "audio/audio_io.h"
#include "core/constants.h"
#include "pipeline/sentence_detector.h"
#include "pipeline/text_sanitizer.h"
#include "tools/tool_defs.h"
#include <cstdio>
#include <cmath>
#include <numeric>
#include <algorithm>
#include <thread>
#include <mutex>
#include <condition_variable>

namespace rastack {

Benchmark::Benchmark(Orchestrator& pipeline) : pipeline_(pipeline) {}

void Benchmark::run_all(const std::string& test_wav_path) {
    results_.clear();
    fprintf(stderr, "\n");
    fprintf(stderr, "╔══════════════════════════════════════════╗\n");
    fprintf(stderr, "║        RASTACK POC BENCHMARK SUITE         ║\n");
    fprintf(stderr, "╚══════════════════════════════════════════╝\n\n");

    bench_memory();
    bench_stt(test_wav_path);
    bench_llm();
    bench_llm_cached();
    bench_llm_by_length();
    bench_tool_calling();
    bench_tts();
    bench_e2e(test_wav_path);
    bench_e2e_by_length();

    print_results();
}

void Benchmark::bench_stt(const std::string& wav_path) {
    fprintf(stderr, "--- STT Benchmark ---\n");

    auto audio = AudioIO::load_wav_to_vec(wav_path, 16000);
    if (audio.empty()) {
        fprintf(stderr, "[Bench] No test audio available, skipping STT\n");
        return;
    }

    // Warm up
    pipeline_.stt().reset();
    pipeline_.stt().feed_audio(audio.data(), audio.size());
    for (int i = 0; i < 100; i++) pipeline_.stt().process_tick();
    pipeline_.stt().get_result();

    // Benchmark (3 runs)
    std::vector<double> latencies;
    for (int run = 0; run < 3; run++) {
        pipeline_.stt().reset();

        int64_t t_start = now_us();
        pipeline_.stt().feed_audio(audio.data(), audio.size());
        for (int i = 0; i < 100; i++) pipeline_.stt().process_tick();
        auto result = pipeline_.stt().get_result();
        int64_t t_end = now_us();

        double latency = (t_end - t_start) / 1000.0;
        latencies.push_back(latency);
        fprintf(stderr, "  Run %d: %.1fms - \"%s\"\n", run + 1, latency, result.text.c_str());
    }

    double avg = std::accumulate(latencies.begin(), latencies.end(), 0.0) / latencies.size();
    double audio_duration_ms = audio.size() * 1000.0 / 16000.0;
    double rtf = avg / audio_duration_ms;

    add_result("STT Latency (avg)", "stt", avg, "ms");
    add_result("STT Audio Duration", "stt", audio_duration_ms, "ms");
    add_result("STT Real-Time Factor", "stt", rtf, "x");
}

void Benchmark::bench_llm() {
    fprintf(stderr, "\n--- LLM Benchmark ---\n");

    std::vector<std::string> prompts = {
        "What is the capital of France?",
        "Explain quantum computing in one sentence.",
        "Write a haiku about programming."
    };

    std::vector<double> first_token_latencies;
    std::vector<double> throughputs;

    for (auto& prompt : prompts) {
        pipeline_.llm().clear_kv_cache();

        std::string full_prompt = pipeline_.llm().build_chat_prompt(
            "You are a helpful assistant. Keep responses brief.", {}, prompt
        );

        std::string response = pipeline_.llm().generate(full_prompt);

        auto& stats = pipeline_.llm().last_stats();
        first_token_latencies.push_back(stats.first_token_us / 1000.0);
        throughputs.push_back(stats.gen_tps());

        fprintf(stderr, "  Prompt: \"%s\"\n", prompt.c_str());
        fprintf(stderr, "  Response: \"%s\"\n", response.substr(0, 100).c_str());
        fprintf(stderr, "  First token: %.1fms, %.1f tok/s, %lld tokens\n\n",
                stats.first_token_us / 1000.0, stats.gen_tps(), stats.generated_tokens);
    }

    double avg_ftl = std::accumulate(first_token_latencies.begin(),
                                      first_token_latencies.end(), 0.0) / first_token_latencies.size();
    double avg_tps = std::accumulate(throughputs.begin(),
                                      throughputs.end(), 0.0) / throughputs.size();

    add_result("LLM First Token (avg)", "llm", avg_ftl, "ms");
    add_result("LLM Throughput (avg)", "llm", avg_tps, "tok/s");
    add_result("LLM Prompt Eval", "llm",
               pipeline_.llm().last_stats().prompt_tps(), "tok/s");
}

void Benchmark::bench_llm_cached() {
    fprintf(stderr, "\n--- LLM Cached Prompt Benchmark ---\n");

    if (!pipeline_.llm().has_prompt_cache()) {
        fprintf(stderr, "  Skipping — no cached system prompt\n");
        return;
    }

    std::vector<std::string> prompts = {
        "What is the capital of France?",
        "Explain quantum computing in one sentence.",
        "Write a haiku about programming."
    };

    std::vector<double> first_token_latencies;
    std::vector<double> throughputs;

    for (auto& prompt : prompts) {
        // Build only the user portion (system prompt stays cached)
        std::string user_portion =
            "<|im_start|>user\n" + prompt + " /no_think<|im_end|>\n"
            "<|im_start|>assistant\n";

        std::string response = pipeline_.llm().generate_with_cached_prompt(user_portion);

        auto& stats = pipeline_.llm().last_stats();
        first_token_latencies.push_back(stats.first_token_us / 1000.0);
        throughputs.push_back(stats.gen_tps());

        fprintf(stderr, "  Prompt: \"%s\"\n", prompt.c_str());
        fprintf(stderr, "  Response: \"%s\"\n", response.substr(0, 100).c_str());
        fprintf(stderr, "  First token: %.1fms, %.1f tok/s, %lld tokens\n\n",
                stats.first_token_us / 1000.0, stats.gen_tps(), stats.generated_tokens);
    }

    double avg_ftl = std::accumulate(first_token_latencies.begin(),
                                      first_token_latencies.end(), 0.0) / first_token_latencies.size();
    double avg_tps = std::accumulate(throughputs.begin(),
                                      throughputs.end(), 0.0) / throughputs.size();

    add_result("LLM Cached TTFT (avg)", "llm_cached", avg_ftl, "ms");
    add_result("LLM Cached Throughput (avg)", "llm_cached", avg_tps, "tok/s");
}

void Benchmark::bench_llm_by_length() {
    fprintf(stderr, "\n--- LLM By Length Benchmark ---\n");

    if (!pipeline_.llm().has_prompt_cache()) {
        fprintf(stderr, "  Skipping — no cached system prompt\n");
        return;
    }

    struct LengthTier {
        const char* label;
        std::vector<std::string> prompts;
    };

    LengthTier tiers[] = {
        {"Short", {
            "What is the capital of France?",
            "What color is the sky?"
        }},
        {"Medium", {
            "Explain how a car engine works.",
            "Describe the water cycle in detail."
        }},
        {"Long", {
            "Explain 5 important laws of physics.",
            "Describe how computers process information from input to output."
        }},
        {"XLong", {
            "Give a detailed overview of the solar system, describing each planet.",
            "Explain the causes and effects of climate change in detail."
        }},
        {"Longest", {
            "Write a comprehensive guide to healthy eating, covering macronutrients, micronutrients, meal planning, hydration, and common dietary myths."
        }},
    };

    for (auto& tier : tiers) {
        fprintf(stderr, "\n  [%s]\n", tier.label);

        std::vector<double> ttfts;
        std::vector<double> tpss;
        std::vector<int64_t> token_counts;

        for (auto& prompt : tier.prompts) {
            std::string user_portion =
                "<|im_start|>user\n" + prompt + " /no_think<|im_end|>\n"
                "<|im_start|>assistant\n";

            std::string response = pipeline_.llm().generate_with_cached_prompt(user_portion);

            auto& stats = pipeline_.llm().last_stats();
            ttfts.push_back(stats.first_token_us / 1000.0);
            tpss.push_back(stats.gen_tps());
            token_counts.push_back(stats.generated_tokens);

            fprintf(stderr, "    \"%s\"\n", prompt.c_str());
            fprintf(stderr, "    TTFT: %.1fms, %.1f tok/s, %lld tokens\n",
                    stats.first_token_us / 1000.0, stats.gen_tps(), stats.generated_tokens);
        }

        double avg_ttft = std::accumulate(ttfts.begin(), ttfts.end(), 0.0) / ttfts.size();
        double avg_tps = std::accumulate(tpss.begin(), tpss.end(), 0.0) / tpss.size();
        double avg_tokens = 0;
        for (auto t : token_counts) avg_tokens += t;
        avg_tokens /= token_counts.size();

        std::string cat = std::string("llm_") + tier.label;
        add_result(std::string(tier.label) + " TTFT", cat, avg_ttft, "ms");
        add_result(std::string(tier.label) + " Throughput", cat, avg_tps, "tok/s");
        add_result(std::string(tier.label) + " Tokens", cat, avg_tokens, "tokens");
    }
}

void Benchmark::bench_tool_calling() {
    fprintf(stderr, "\n--- Tool Calling Benchmark ---\n");

    std::string system_prompt = RCLI_SYSTEM_PROMPT;

    std::string tool_defs = pipeline_.tools().get_tool_definitions_json();

    struct ToolTest {
        const char* query;
        const char* expected_tool;
    };

    ToolTest tests[] = {
        {"What time is it right now?", "get_current_time"},
        {"Calculate 42 plus 17", "calculate"},
    };

    int successes = 0;

    for (auto& test : tests) {
        fprintf(stderr, "\n  Query: \"%s\"\n", test.query);
        pipeline_.llm().clear_kv_cache();

        // First pass — generate tool call
        int64_t t0 = now_us();
        std::string first_pass = pipeline_.llm().generate_with_tools(
            test.query, tool_defs, system_prompt);
        int64_t t1 = now_us();
        double first_pass_ms = (t1 - t0) / 1000.0;

        // Parse tool calls
        auto calls = pipeline_.tools().parse_tool_calls(first_pass);
        bool detected = !calls.empty() && calls[0].name == test.expected_tool;

        fprintf(stderr, "    First pass: %.1fms, detected=%s", first_pass_ms,
                detected ? "yes" : "NO");
        if (!calls.empty()) fprintf(stderr, " (tool=%s)", calls[0].name.c_str());
        fprintf(stderr, "\n");

        if (!detected) {
            add_result(std::string("Tool ") + test.expected_tool + " FirstPass", "tools",
                       first_pass_ms, "ms");
            add_result(std::string("Tool ") + test.expected_tool + " Success", "tools", 0.0, "bool");
            continue;
        }

        // Execute tool
        int64_t t2 = now_us();
        auto results = pipeline_.tools().execute_all(calls);
        int64_t t3 = now_us();
        double exec_ms = (t3 - t2) / 1000.0;

        fprintf(stderr, "    Tool exec: %.1fms, result=%s\n", exec_ms,
                results[0].result_json.substr(0, 60).c_str());

        // Second pass — natural language response
        std::string continuation = pipeline_.llm().build_tool_continuation_prompt(
            system_prompt, test.query, first_pass,
            pipeline_.tools().format_results(results));

        int64_t t4 = now_us();
        std::string final_response = pipeline_.llm().generate(continuation);
        int64_t t5 = now_us();
        double second_pass_ms = (t5 - t4) / 1000.0;

        double total_ms = (t5 - t0) / 1000.0;
        successes++;

        fprintf(stderr, "    Second pass: %.1fms\n", second_pass_ms);
        fprintf(stderr, "    Response: \"%s\"\n", sanitize_for_tts(final_response).substr(0, 80).c_str());
        fprintf(stderr, "    Total: %.1fms\n", total_ms);

        std::string prefix = std::string("Tool ") + test.expected_tool;
        add_result(prefix + " FirstPass", "tools", first_pass_ms, "ms");
        add_result(prefix + " Exec", "tools", exec_ms, "ms");
        add_result(prefix + " SecondPass", "tools", second_pass_ms, "ms");
        add_result(prefix + " Total", "tools", total_ms, "ms");
        add_result(prefix + " Success", "tools", 1.0, "bool");
    }

    add_result("Tool Success Rate", "tools",
               (double)successes / (sizeof(tests) / sizeof(tests[0])) * 100.0, "%");
}

void Benchmark::bench_tts() {
    fprintf(stderr, "\n--- TTS Benchmark ---\n");

    std::vector<std::string> texts = {
        "Hello, I am your AI assistant.",
        "The weather today is partly cloudy with a high of twenty two degrees.",
        "Quantum computing uses quantum bits to perform calculations."
    };

    std::vector<double> latencies;
    std::vector<double> rtfs;

    for (auto& text : texts) {
        int64_t t_start = now_us();
        auto audio = pipeline_.tts().synthesize(text);
        int64_t t_end = now_us();

        double latency = (t_end - t_start) / 1000.0;
        double audio_duration_ms = audio.size() * 1000.0 / pipeline_.tts().sample_rate();
        double rtf = latency / audio_duration_ms;

        latencies.push_back(latency);
        rtfs.push_back(rtf);

        fprintf(stderr, "  \"%s\"\n", text.c_str());
        fprintf(stderr, "  Latency: %.1fms, Audio: %.1fms, RTF: %.2f\n\n",
                latency, audio_duration_ms, rtf);
    }

    double avg_lat = std::accumulate(latencies.begin(), latencies.end(), 0.0) / latencies.size();
    double avg_rtf = std::accumulate(rtfs.begin(), rtfs.end(), 0.0) / rtfs.size();

    add_result("TTS Latency (avg)", "tts", avg_lat, "ms");
    add_result("TTS Real-Time Factor (avg)", "tts", avg_rtf, "x");
}

void Benchmark::bench_e2e(const std::string& wav_path) {
    fprintf(stderr, "\n--- E2E Pipeline Benchmark ---\n");

    // Run file pipeline
    std::string output = "/tmp/rastack_bench_output.wav";
    pipeline_.run_file_pipeline(wav_path, output);

    auto& t = pipeline_.last_timings();
    add_result("E2E STT", "e2e", t.stt_latency_us / 1000.0, "ms");
    add_result("E2E LLM First Token", "e2e", t.llm_first_token_us / 1000.0, "ms");
    add_result("E2E LLM Total", "e2e", t.llm_total_us / 1000.0, "ms");
    add_result("E2E TTS First Sentence", "e2e", t.tts_first_sentence_us / 1000.0, "ms");
    add_result("E2E Latency (STT+LLM_FT+TTS)", "e2e", t.e2e_latency_us / 1000.0, "ms");
    add_result("E2E Total", "e2e", t.total_us / 1000.0, "ms");
}

void Benchmark::bench_e2e_long() {
    fprintf(stderr, "\n--- E2E Long-Form Benchmark (LLM+TTS, parallel) ---\n");
    fprintf(stderr, "  Prompt: \"Explain 5 important laws of physics.\"\n");

    pipeline_.llm().clear_kv_cache();

    std::string system_prompt =
        "You are a helpful voice assistant. Your responses will be spoken aloud, "
        "so keep them natural and conversational.";
    std::string user_msg = "Explain 5 important laws of physics.";

    std::string full_prompt = pipeline_.llm().build_chat_prompt(
        system_prompt, {}, user_msg
    );

    // --- Parallel TTS worker (same architecture as orchestrator) ---
    std::vector<float> all_tts_audio;
    std::mutex tts_mutex;
    std::condition_variable tts_cv;
    std::vector<std::string> tts_queue;
    bool llm_done = false;
    int64_t first_sentence_time = 0;

    int64_t t_start = now_us();

    std::thread tts_worker([&]() {
        while (true) {
            std::string sentence;
            {
                std::unique_lock<std::mutex> lock(tts_mutex);
                tts_cv.wait(lock, [&]() { return !tts_queue.empty() || llm_done; });
                if (tts_queue.empty() && llm_done) break;
                if (tts_queue.empty()) continue;
                sentence = std::move(tts_queue.front());
                tts_queue.erase(tts_queue.begin());
            }
            auto audio = pipeline_.tts().synthesize(sentence);
            std::lock_guard<std::mutex> lock(tts_mutex);
            all_tts_audio.insert(all_tts_audio.end(), audio.begin(), audio.end());
        }
    });

    auto queue_sentence = [&](const std::string& sentence) {
        if (first_sentence_time == 0) {
            first_sentence_time = now_us();
        }
        {
            std::lock_guard<std::mutex> lock(tts_mutex);
            tts_queue.push_back(sentence);
        }
        tts_cv.notify_one();
    };

    // Generate with sentence-level streaming to TTS
    SentenceDetector detector(queue_sentence, 3, 25, 7);
    std::string response = pipeline_.llm().generate(full_prompt, [&](const TokenOutput& tok) {
        detector.feed(tok.text);
    });
    detector.flush();

    int64_t llm_done_time = now_us();

    // Signal TTS worker done
    {
        std::lock_guard<std::mutex> lock(tts_mutex);
        llm_done = true;
    }
    tts_cv.notify_one();
    tts_worker.join();

    int64_t t_end = now_us();

    auto& stats = pipeline_.llm().last_stats();
    double llm_total_ms = (llm_done_time - t_start) / 1000.0;
    double tts_first_ms = first_sentence_time > 0 ? (first_sentence_time - t_start) / 1000.0 : 0;
    double e2e_latency_ms = stats.first_token_us / 1000.0 + tts_first_ms;
    double total_ms = (t_end - t_start) / 1000.0;

    fprintf(stderr, "  LLM tokens: %lld at %.1f tok/s\n", stats.generated_tokens, stats.gen_tps());
    fprintf(stderr, "  LLM first token: %.1fms\n", stats.first_token_us / 1000.0);
    fprintf(stderr, "  LLM total: %.1fms\n", llm_total_ms);
    fprintf(stderr, "  TTS first sentence ready: %.1fms\n", tts_first_ms);
    fprintf(stderr, "  E2E latency (first audio): %.1fms\n", e2e_latency_ms);
    fprintf(stderr, "  Total (LLM+TTS complete): %.1fms\n\n", total_ms);

    add_result("Long LLM Tokens", "e2e_long", (double)stats.generated_tokens, "tokens");
    add_result("Long LLM Throughput", "e2e_long", stats.gen_tps(), "tok/s");
    add_result("Long LLM First Token", "e2e_long", stats.first_token_us / 1000.0, "ms");
    add_result("Long LLM Total", "e2e_long", llm_total_ms, "ms");
    add_result("Long TTS First Sentence", "e2e_long", tts_first_ms, "ms");
    add_result("Long E2E Latency", "e2e_long", e2e_latency_ms, "ms");
    add_result("Long Total", "e2e_long", total_ms, "ms");
}

void Benchmark::bench_e2e_by_length() {
    fprintf(stderr, "\n--- E2E By Length Benchmark (LLM+TTS, parallel) ---\n");

    std::string system_prompt =
        "You are a helpful voice assistant. Your responses will be spoken aloud, "
        "so keep them natural and conversational. "
        "IMPORTANT: Never use asterisks, bullet points, numbered lists, markdown formatting, "
        "or any special symbols in your response. Write in plain conversational sentences only.";

    struct LengthTier {
        const char* label;
        const char* prompt;
    };

    LengthTier tiers[] = {
        {"Short",   "What is two plus two?"},
        {"Medium",  "Explain how rain forms."},
        {"Long",    "Explain 5 important laws of physics."},
        {"XLong",   "Give a detailed overview of the solar system, describing each planet."},
        {"Longest", "Write a comprehensive guide to healthy eating, covering macronutrients, micronutrients, meal planning, hydration, and common dietary myths."},
    };

    for (auto& tier : tiers) {
        fprintf(stderr, "\n  [%s] \"%s\"\n", tier.label, tier.prompt);

        pipeline_.llm().clear_kv_cache();

        std::string full_prompt = pipeline_.llm().build_chat_prompt(
            system_prompt, {}, tier.prompt
        );

        // Parallel TTS worker
        std::vector<float> all_tts_audio;
        std::mutex tts_mutex;
        std::condition_variable tts_cv;
        std::vector<std::string> tts_queue;
        bool llm_done = false;
        int64_t first_sentence_time = 0;
        int sentence_count = 0;

        int64_t t_start = now_us();

        std::thread tts_worker([&]() {
            while (true) {
                std::string sentence;
                {
                    std::unique_lock<std::mutex> lock(tts_mutex);
                    tts_cv.wait(lock, [&]() { return !tts_queue.empty() || llm_done; });
                    if (tts_queue.empty() && llm_done) break;
                    if (tts_queue.empty()) continue;
                    sentence = std::move(tts_queue.front());
                    tts_queue.erase(tts_queue.begin());
                }
                auto audio = pipeline_.tts().synthesize(sentence);
                std::lock_guard<std::mutex> lock(tts_mutex);
                all_tts_audio.insert(all_tts_audio.end(), audio.begin(), audio.end());
            }
        });

        auto queue_sentence = [&](const std::string& sentence) {
            std::string clean = sanitize_for_tts(sentence);
            if (clean.empty()) return;
            if (first_sentence_time == 0) {
                first_sentence_time = now_us();
            }
            sentence_count++;
            {
                std::lock_guard<std::mutex> lock(tts_mutex);
                tts_queue.push_back(clean);
            }
            tts_cv.notify_one();
        };

        SentenceDetector detector(queue_sentence, 3, 25, 7);
        std::string response = pipeline_.llm().generate(full_prompt, [&](const TokenOutput& tok) {
            detector.feed(tok.text);
        });
        detector.flush();

        int64_t llm_done_time = now_us();

        {
            std::lock_guard<std::mutex> lock(tts_mutex);
            llm_done = true;
        }
        tts_cv.notify_one();
        tts_worker.join();

        int64_t t_end = now_us();

        auto& stats = pipeline_.llm().last_stats();
        double ttft_ms = stats.first_token_us / 1000.0;
        double llm_total_ms = (llm_done_time - t_start) / 1000.0;
        double first_audio_ms = first_sentence_time > 0 ? (first_sentence_time - t_start) / 1000.0 : 0;
        double total_ms = (t_end - t_start) / 1000.0;

        fprintf(stderr, "    LLM: %lld tok, %.1f tok/s, TTFT %.1fms, total %.1fms\n",
                stats.generated_tokens, stats.gen_tps(), ttft_ms, llm_total_ms);
        fprintf(stderr, "    TTS: %d sentences, first audio %.1fms\n", sentence_count, first_audio_ms);
        fprintf(stderr, "    Total: %.1fms\n", total_ms);

        std::string cat = std::string("e2e_") + tier.label;
        add_result(std::string("E2E ") + tier.label + " TTFT", cat, ttft_ms, "ms");
        add_result(std::string("E2E ") + tier.label + " FirstAudio", cat, first_audio_ms, "ms");
        add_result(std::string("E2E ") + tier.label + " LLM Total", cat, llm_total_ms, "ms");
        add_result(std::string("E2E ") + tier.label + " Throughput", cat, stats.gen_tps(), "tok/s");
        add_result(std::string("E2E ") + tier.label + " Tokens", cat, (double)stats.generated_tokens, "tokens");
        add_result(std::string("E2E ") + tier.label + " Sentences", cat, (double)sentence_count, "count");
        add_result(std::string("E2E ") + tier.label + " Total", cat, total_ms, "ms");
    }
}

void Benchmark::bench_memory() {
    fprintf(stderr, "--- Memory Benchmark ---\n");
    // Memory stats will be populated after init — grab from pool if available
    // For now, report process RSS
    add_result("Pool Size", "memory", 64.0, "MB");
    fprintf(stderr, "  Pool allocated: 64 MB\n\n");
}

void Benchmark::print_results() const {
    fprintf(stderr, "\n");
    fprintf(stderr, "╔════════════════════════════════════════════════════════╗\n");
    fprintf(stderr, "║                 BENCHMARK RESULTS                      ║\n");
    fprintf(stderr, "╠════════════════════════════════════════════════════════╣\n");

    std::string last_category;
    for (auto& r : results_) {
        if (r.category != last_category) {
            fprintf(stderr, "║ %-54s ║\n", ("[ " + r.category + " ]").c_str());
            last_category = r.category;
        }
        char line[80];
        snprintf(line, sizeof(line), "  %-35s %10.1f %s",
                 r.name.c_str(), r.value, r.unit.c_str());
        fprintf(stderr, "║ %-54s ║\n", line);
    }

    fprintf(stderr, "╚════════════════════════════════════════════════════════╝\n\n");
}

std::string Benchmark::to_json() const {
    std::string json = "{\n  \"results\": [\n";
    for (size_t i = 0; i < results_.size(); i++) {
        auto& r = results_[i];
        json += "    {\"name\": \"" + r.name + "\", "
                "\"category\": \"" + r.category + "\", "
                "\"value\": " + std::to_string(r.value) + ", "
                "\"unit\": \"" + r.unit + "\"}";
        if (i + 1 < results_.size()) json += ",";
        json += "\n";
    }
    json += "  ]\n}\n";
    return json;
}

void Benchmark::add_result(const std::string& name, const std::string& category,
                            double value, const std::string& unit) {
    results_.push_back({name, category, value, unit});
}

} // namespace rastack
