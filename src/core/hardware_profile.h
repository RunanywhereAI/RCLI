#pragma once

// hardware_profile.h
// Runtime hardware detection and optimal configuration for rastack RAG pipelines.
//
// Detects: CPU topology (P/E cores), RAM, GPU availability
// Produces: optimal llama.cpp params (n_gpu_layers, n_threads, n_batch, flash_attn)
//           optimal memory pool sizing
//           cross-platform thread naming helper
//
// Usage:
//   const auto& hw = rastack::global_hw();   // cached singleton, free after first call
//   LlmConfig cfg  = LlmConfig::from_hw(model_path);
//   pool_ = std::make_unique<MemoryPool>(hw.pool_size_bytes);

#include <cstdint>
#include <cstdio>
#include <algorithm>
#include <thread>

#if defined(__APPLE__)
#   include <TargetConditionals.h>
#   include <sys/sysctl.h>
#elif defined(__linux__)
#   include <unistd.h>
#   include <sys/sysinfo.h>
#endif

#if defined(__linux__)
#   include <pthread.h>
#elif defined(__APPLE__)
#   include <pthread.h>
#endif

namespace rastack {

// ---------------------------------------------------------------------------
// HardwareProfile — POD struct describing the current device
// ---------------------------------------------------------------------------
struct HardwareProfile {
    // CPU topology
    int     cpu_logical   = 1;   // total logical CPUs (includes HT/SMT)
    int     cpu_physical  = 1;   // physical cores
    int     perf_cores    = 1;   // "big" / P-cores (used for LLM decode)
    int     effi_cores    = 0;   // "little" / E-cores (background tasks)

    // Memory
    int64_t ram_total_mb  = 0;
    int64_t ram_avail_mb  = 0;

    // GPU
    bool    has_metal     = false;
    bool    has_cuda      = false;

    // --- Recommended llama.cpp LLM params ---
    int     llm_gpu_layers    = 0;     // 0 = CPU-only, 99 = all to GPU
    int     llm_n_threads     = 4;     // decode threads (CPU path: physical / 2)
    int     llm_n_threads_batch = 4;   // prompt-eval threads (all cores)
    int     llm_n_batch       = 512;   // prompt batch size
    int     llm_n_ubatch      = 512;   // micro-batch (match n_batch)
    bool    llm_flash_attn    = false; // Flash Attention (Metal/CUDA only today)
    bool    llm_use_mlock     = false; // Pin model weights in RAM

    // --- Recommended embedding params ---
    int     emb_gpu_layers    = 0;
    int     emb_n_threads     = 2;
    int     emb_n_batch       = 512;

    // --- Memory pool ---
    size_t  pool_bytes        = 64 * 1024 * 1024;  // 64 MB default

    // Human-readable platform tag (for logging / benchmark results)
    const char* platform_tag  = "unknown";
};

// ---------------------------------------------------------------------------
// detect_hardware() — fills a HardwareProfile for the current device
// ---------------------------------------------------------------------------
inline HardwareProfile detect_hardware() {
    HardwareProfile p;

#if defined(__APPLE__)
    // ------------------------------------------------------------------
    // macOS / iOS — sysctl + TargetConditionals
    // ------------------------------------------------------------------
    {
        int32_t val = 0; size_t len = sizeof(val);
        if (sysctlbyname("hw.physicalcpu", &val, &len, nullptr, 0) == 0)
            p.cpu_physical = val;
        len = sizeof(val);
        if (sysctlbyname("hw.logicalcpu",  &val, &len, nullptr, 0) == 0)
            p.cpu_logical  = val;

        // Apple Silicon: hw.perflevel0 = P-cores, hw.perflevel1 = E-cores
        int32_t pc = 0, ec = 0;
        size_t pc_len = sizeof(pc), ec_len = sizeof(ec);
        if (sysctlbyname("hw.perflevel0.physicalcpu", &pc, &pc_len, nullptr, 0) == 0 &&
            sysctlbyname("hw.perflevel1.physicalcpu", &ec, &ec_len, nullptr, 0) == 0) {
            p.perf_cores = pc;
            p.effi_cores = ec;
        } else {
            // Intel Mac or detection failed — all cores are "performance"
            p.perf_cores = p.cpu_physical;
        }

        int64_t mem = 0; size_t mem_len = sizeof(mem);
        if (sysctlbyname("hw.memsize", &mem, &mem_len, nullptr, 0) == 0)
            p.ram_total_mb = mem / (1024 * 1024);
    }

    p.has_metal          = true;
    p.llm_gpu_layers     = 99;
    p.llm_flash_attn     = true;
    p.llm_n_threads      = 1;              // GPU-bound decode: 1 thread is optimal
    p.llm_n_threads_batch = p.perf_cores;  // prompt eval uses all P-cores
    p.emb_gpu_layers     = 99;
    p.emb_n_threads      = 2;

#   if TARGET_OS_IOS
    // iOS: tighter memory budget, no mlock
    p.platform_tag       = "ios";
    p.llm_use_mlock      = false;
    p.pool_bytes         = 64  * 1024 * 1024;
    p.llm_n_batch        = 512;
    p.llm_n_ubatch       = 512;
    p.emb_n_batch        = 256;
#   else
    // macOS: use RAM headroom for larger batches and pool
    p.platform_tag       = "macos";
    p.llm_use_mlock      = (p.ram_total_mb >= 16 * 1024); // mlock if >=16 GB
    if (p.ram_total_mb >= 64 * 1024) {        // Mac Studio / Mac Pro
        p.pool_bytes       = 256 * 1024 * 1024;
        p.llm_n_batch      = 4096;
        p.llm_n_ubatch     = 2048;
        p.emb_n_batch      = 2048;
    } else if (p.ram_total_mb >= 32 * 1024) { // M3 Max 36/48 GB
        p.pool_bytes       = 128 * 1024 * 1024;
        p.llm_n_batch      = 2048;
        p.llm_n_ubatch     = 1024;
        p.emb_n_batch      = 1024;
    } else {                                   // M3 / M2 / M1 16-24 GB
        p.pool_bytes       = 64  * 1024 * 1024;
        p.llm_n_batch      = 1024;
        p.llm_n_ubatch     = 512;
        p.emb_n_batch      = 512;
    }
#   endif

#elif defined(__ANDROID__)
    // ------------------------------------------------------------------
    // Android ARM64 — /proc/meminfo + heuristic P/E core split
    // ------------------------------------------------------------------
    p.platform_tag    = "android";
    p.cpu_logical     = static_cast<int>(std::thread::hardware_concurrency());
    p.cpu_physical    = p.cpu_logical;

    // Heuristic: modern Android SoCs have 4 big + 4 small (or 1+3+4)
    // Read /sys/.../cpuinfo_max_freq to find true big-core count
    {
        int big_count = 0;
        long max_freq = 0;
        // Pass 1: find global max freq
        for (int i = 0; i < p.cpu_logical; i++) {
            char path[128];
            snprintf(path, sizeof(path),
                     "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", i);
            FILE* f = fopen(path, "r");
            if (f) {
                long freq = 0;
                if (fscanf(f, "%ld", &freq) == 1 && freq > max_freq)
                    max_freq = freq;
                fclose(f);
            }
        }
        // Pass 2: count cores running at >=90% of max freq
        if (max_freq > 0) {
            for (int i = 0; i < p.cpu_logical; i++) {
                char path[128];
                snprintf(path, sizeof(path),
                         "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", i);
                FILE* f = fopen(path, "r");
                if (f) {
                    long freq = 0;
                    if (fscanf(f, "%ld", &freq) == 1 &&
                        freq >= (long)(max_freq * 9 / 10))
                        big_count++;
                    fclose(f);
                }
            }
        }
        p.perf_cores = (big_count > 0) ? big_count
                                       : std::max(1, p.cpu_logical / 2);
        p.effi_cores = p.cpu_logical - p.perf_cores;
    }

    // RAM from /proc/meminfo
    {
        FILE* f = fopen("/proc/meminfo", "r");
        if (f) {
            char line[128];
            while (fgets(line, sizeof(line), f)) {
                long v = 0;
                if      (sscanf(line, "MemTotal:     %ld kB", &v) == 1) p.ram_total_mb = v / 1024;
                else if (sscanf(line, "MemAvailable: %ld kB", &v) == 1) p.ram_avail_mb = v / 1024;
            }
            fclose(f);
        }
    }

    // CPU-only on Android (no Metal, no CUDA)
    p.has_metal           = false;
    p.llm_gpu_layers      = 0;
    p.llm_flash_attn      = false;
    p.llm_n_threads       = p.perf_cores;
    p.llm_n_threads_batch = p.cpu_logical;    // prompt eval: all cores
    p.emb_gpu_layers      = 0;
    p.emb_n_threads       = std::min(4, p.perf_cores);

    // Scale batch + pool to available RAM
    if (p.ram_total_mb >= 8 * 1024) {
        p.pool_bytes   = 128 * 1024 * 1024;
        p.llm_n_batch  = 1024; p.llm_n_ubatch = 512;
        p.emb_n_batch  = 512;
    } else if (p.ram_total_mb >= 4 * 1024) {
        p.pool_bytes   = 64  * 1024 * 1024;
        p.llm_n_batch  = 512; p.llm_n_ubatch = 256;
        p.emb_n_batch  = 256;
    } else {
        p.pool_bytes   = 32  * 1024 * 1024;
        p.llm_n_batch  = 256; p.llm_n_ubatch = 128;
        p.emb_n_batch  = 128;
    }

#elif defined(__linux__)
    // ------------------------------------------------------------------
    // Linux (x86_64 or aarch64)
    // ------------------------------------------------------------------
    p.platform_tag  = "linux";
    p.cpu_logical   = static_cast<int>(std::thread::hardware_concurrency());
    p.cpu_physical  = p.cpu_logical;
    p.perf_cores    = p.cpu_logical;  // treat all vCPUs as performance cores

    {
        struct sysinfo si;
        if (sysinfo(&si) == 0) {
            p.ram_total_mb = static_cast<int64_t>(si.totalram) *
                             si.mem_unit / (1024 * 1024);
            p.ram_avail_mb = static_cast<int64_t>(si.freeram) *
                             si.mem_unit / (1024 * 1024);
        }
    }

    // CPU-only inference
    p.has_metal          = false;
    p.has_cuda           = false;
    p.llm_gpu_layers     = 0;
    p.llm_flash_attn     = false;
    // Decode: physical/2 threads (hyperthreading hurts LLM decode latency)
    // Prompt: all vCPUs (embarrassingly parallel matrix ops)
    p.llm_n_threads       = std::max(1, p.cpu_logical / 2);
    p.llm_n_threads_batch = p.cpu_logical;
    p.emb_gpu_layers      = 0;
    p.emb_n_threads       = std::min(8, p.llm_n_threads);
    // mlock: pin weights in RAM (avoid swap overhead)
    p.llm_use_mlock       = (p.ram_total_mb > 0);

    // Scale batch + pool by RAM tier
    if (p.ram_total_mb >= 128 * 1024) {
        p.pool_bytes   = 512 * 1024 * 1024;
        p.llm_n_batch  = 4096; p.llm_n_ubatch = 2048;
        p.emb_n_batch  = 2048;
    } else if (p.ram_total_mb >= 32 * 1024) {
        p.pool_bytes   = 256 * 1024 * 1024;
        p.llm_n_batch  = 2048; p.llm_n_ubatch = 1024;
        p.emb_n_batch  = 1024;
    } else if (p.ram_total_mb >= 16 * 1024) {
        p.pool_bytes   = 128 * 1024 * 1024;
        p.llm_n_batch  = 1024; p.llm_n_ubatch = 512;
        p.emb_n_batch  = 512;
    } else {
        p.pool_bytes   = 64  * 1024 * 1024;
        p.llm_n_batch  = 512; p.llm_n_ubatch = 256;
        p.emb_n_batch  = 256;
    }

#else
    // ------------------------------------------------------------------
    // Fallback (Windows, unknown)
    // ------------------------------------------------------------------
    p.platform_tag        = "unknown";
    p.cpu_logical         = static_cast<int>(std::thread::hardware_concurrency());
    p.perf_cores          = p.cpu_logical;
    p.llm_n_threads       = p.cpu_logical;
    p.llm_n_threads_batch = p.cpu_logical;
#endif

    fprintf(stderr,
        "[HW] platform=%s cpu=%d(p=%d e=%d) ram=%lldMB "
        "gpu_layers=%d flash_attn=%d n_threads=%d/%d n_batch=%d pool=%zuMB\n",
        p.platform_tag,
        p.cpu_logical, p.perf_cores, p.effi_cores,
        (long long)p.ram_total_mb,
        p.llm_gpu_layers, (int)p.llm_flash_attn,
        p.llm_n_threads, p.llm_n_threads_batch,
        p.llm_n_batch,
        p.pool_bytes / (1024 * 1024));

    return p;
}

// Singleton — detected once at program start, zero-cost afterwards
inline const HardwareProfile& global_hw() {
    static HardwareProfile hw = detect_hardware();
    return hw;
}

// ---------------------------------------------------------------------------
// Cross-platform thread naming (pthread_setname_np signature differs)
// ---------------------------------------------------------------------------
inline void set_thread_name(const char* name) {
#if defined(__APPLE__)
    // macOS/iOS: sets current thread only, no thread argument
    pthread_setname_np(name);
#elif defined(__linux__)
    // Linux/Android: requires explicit thread handle, max 15 chars
    pthread_setname_np(pthread_self(), name);
#endif
}

} // namespace rastack
