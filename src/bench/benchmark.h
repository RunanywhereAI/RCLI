#pragma once

#include "core/types.h"
#include "pipeline/orchestrator.h"
#include <string>
#include <vector>

namespace rastack {

struct BenchmarkResult {
    std::string name;
    std::string category;  // "stt", "llm", "tts", "e2e", "memory"

    double value;
    std::string unit;      // "ms", "tok/s", "MB", etc.

    // For comparison tests
    double baseline_value = 0;
    std::string baseline_label;
};

class Benchmark {
public:
    Benchmark(Orchestrator& pipeline);

    // Run all benchmarks
    void run_all(const std::string& test_wav_path);

    // Individual benchmarks
    void bench_stt(const std::string& wav_path);
    void bench_metalrt_stt(const std::string& wav_path);
    void bench_llm();
    void bench_llm_cached();
    void bench_llm_by_length();
    void bench_tool_calling();
    void bench_tts();
    void bench_metalrt_tts();
    void bench_e2e(const std::string& wav_path);
    void bench_e2e_long();
    void bench_e2e_by_length();
    void bench_memory();

    // Print results
    void print_results() const;

    // Export to JSON
    std::string to_json() const;

    const std::vector<BenchmarkResult>& results() const { return results_; }

private:
    void add_result(const std::string& name, const std::string& category,
                    double value, const std::string& unit);

    Orchestrator& pipeline_;
    std::vector<BenchmarkResult> results_;
};

} // namespace rastack
