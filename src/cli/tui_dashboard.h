#pragma once
// =============================================================================
// RCLI TUI Dashboard — Hardware monitoring for Apple Silicon
// =============================================================================

#include <string>
#include <vector>
#include <cstdint>
#include <mutex>
#include <atomic>
#include <thread>
#include <chrono>

#ifdef __APPLE__
#include <mach/mach.h>
#include <mach/mach_host.h>
#include <mach/host_info.h>
#include <mach/processor_info.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <IOKit/IOKitLib.h>
#include <CoreFoundation/CoreFoundation.h>
#endif

namespace rcli_tui {

struct ChipInfo {
    std::string chip_name;
    int         p_cores     = 0;
    int         e_cores     = 0;
    int         total_cores = 0;
    int         gpu_cores   = 0;
    int         ane_cores   = 16;
    int64_t     ram_gb      = 0;
};

struct SystemStats {
    float   cpu_percent     = 0.0f;
    float   gpu_percent     = 0.0f;
    float   ane_percent     = 0.0f;
    float   mem_used_gb     = 0.0f;
    float   mem_total_gb    = 0.0f;
    float   mem_percent     = 0.0f;
    float   power_watts     = 0.0f;
    int     battery_percent = -1;
    bool    battery_charging= false;
    float   net_rx_kbs      = 0.0f;
    float   net_tx_kbs      = 0.0f;
    std::vector<float> per_core_cpu;
    // RCLI process-specific metrics
    float   proc_cpu_percent = 0.0f;
    float   proc_mem_mb      = 0.0f;
};

inline ChipInfo detect_chip() {
    ChipInfo info;
#ifdef __APPLE__
    char buf[256] = {0};
    size_t len = sizeof(buf);
    if (sysctlbyname("machdep.cpu.brand_string", buf, &len, nullptr, 0) == 0)
        info.chip_name = buf;
    else
        info.chip_name = "Apple Silicon";

    int val = 0;
    len = sizeof(val);
    if (sysctlbyname("hw.perflevel0.physicalcpu", &val, &len, nullptr, 0) == 0)
        info.p_cores = val;
    if (sysctlbyname("hw.perflevel1.physicalcpu", &val, &len, nullptr, 0) == 0)
        info.e_cores = val;
    if (sysctlbyname("hw.physicalcpu", &val, &len, nullptr, 0) == 0)
        info.total_cores = val;
    if (info.total_cores == 0)
        info.total_cores = info.p_cores + info.e_cores;

    // Query actual GPU core count via IOKit (works for all Apple Silicon generations)
    {
        CFMutableDictionaryRef match = IOServiceMatching("AGXAccelerator");
        io_iterator_t iter = 0;
        if (match && IOServiceGetMatchingServices(kIOMainPortDefault, match, &iter) == KERN_SUCCESS) {
            io_service_t svc = IOIteratorNext(iter);
            if (svc) {
                CFTypeRef ref = IORegistryEntrySearchCFProperty(
                    svc, kIOServicePlane, CFSTR("gpu-core-count"),
                    kCFAllocatorDefault, kIORegistryIterateRecursively);
                if (ref) {
                    if (CFGetTypeID(ref) == CFNumberGetTypeID()) {
                        int cores = 0;
                        CFNumberGetValue((CFNumberRef)ref, kCFNumberIntType, &cores);
                        info.gpu_cores = cores;
                    }
                    CFRelease(ref);
                }
                IOObjectRelease(svc);
            }
            IOObjectRelease(iter);
        }
        if (info.gpu_cores == 0) {
            auto& cn = info.chip_name;
            if (cn.find("Ultra") != std::string::npos)       info.gpu_cores = 76;
            else if (cn.find("Max") != std::string::npos)     info.gpu_cores = 38;
            else if (cn.find("Pro") != std::string::npos)     info.gpu_cores = 18;
            else                                               info.gpu_cores = 10;
        }
    }

    int64_t mem = 0;
    len = sizeof(mem);
    if (sysctlbyname("hw.memsize", &mem, &len, nullptr, 0) == 0)
        info.ram_gb = mem / (1024 * 1024 * 1024);
#endif
    return info;
}

class HardwareMonitor {
public:
    void start(int poll_ms = 1000) {
        running_ = true;
        thread_ = std::thread([this, poll_ms]() { poll_loop(poll_ms); });
    }

    void stop() {
        running_ = false;
        if (thread_.joinable()) thread_.join();
    }

    ~HardwareMonitor() { stop(); }

    SystemStats snapshot() const {
        std::lock_guard<std::mutex> lock(mu_);
        return stats_;
    }

private:
    void poll_loop(int interval_ms) {
#ifdef __APPLE__
        uint64_t prev_rx = 0, prev_tx = 0;
        std::vector<uint64_t> prev_ticks;
        bool first = true;
        uint64_t prev_proc_utime = 0, prev_proc_stime = 0;
        auto prev_wall = std::chrono::steady_clock::now();

        while (running_) {
            SystemStats s;

            // --- Per-core CPU via host_processor_info ---
            natural_t n_cpus = 0;
            processor_info_array_t cpu_info = nullptr;
            mach_msg_type_number_t info_cnt = 0;

            if (host_processor_info(mach_host_self(), PROCESSOR_CPU_LOAD_INFO,
                                    &n_cpus, &cpu_info, &info_cnt) == KERN_SUCCESS) {
                std::vector<uint64_t> ticks(n_cpus * 4);
                for (natural_t i = 0; i < n_cpus; i++) {
                    auto* t = reinterpret_cast<processor_cpu_load_info_t>(cpu_info) + i;
                    ticks[i*4+0] = t->cpu_ticks[CPU_STATE_USER];
                    ticks[i*4+1] = t->cpu_ticks[CPU_STATE_SYSTEM];
                    ticks[i*4+2] = t->cpu_ticks[CPU_STATE_IDLE];
                    ticks[i*4+3] = t->cpu_ticks[CPU_STATE_NICE];
                }

                if (!first && prev_ticks.size() == ticks.size()) {
                    float total_active = 0, total_all = 0;
                    s.per_core_cpu.resize(n_cpus);
                    for (natural_t i = 0; i < n_cpus; i++) {
                        uint64_t u = ticks[i*4+0] - prev_ticks[i*4+0];
                        uint64_t sy= ticks[i*4+1] - prev_ticks[i*4+1];
                        uint64_t id= ticks[i*4+2] - prev_ticks[i*4+2];
                        uint64_t ni= ticks[i*4+3] - prev_ticks[i*4+3];
                        uint64_t tot = u + sy + id + ni;
                        float pct = tot > 0 ? 100.0f * (float)(u + sy + ni) / (float)tot : 0.0f;
                        s.per_core_cpu[i] = pct;
                        total_active += (float)(u + sy + ni);
                        total_all += (float)tot;
                    }
                    s.cpu_percent = total_all > 0 ? 100.0f * total_active / total_all : 0.0f;
                }
                prev_ticks = ticks;
                first = false;

                vm_deallocate(mach_task_self(), (vm_address_t)cpu_info,
                              info_cnt * sizeof(natural_t));
            }

            // --- System memory via host_statistics64 ---
            vm_statistics64_data_t vm_stat;
            mach_msg_type_number_t vm_count = HOST_VM_INFO64_COUNT;
            if (host_statistics64(mach_host_self(), HOST_VM_INFO64,
                                  (host_info64_t)&vm_stat, &vm_count) == KERN_SUCCESS) {
                uint64_t page_size = 0;
                size_t plen = sizeof(page_size);
                sysctlbyname("hw.pagesize", &page_size, &plen, nullptr, 0);
                if (page_size == 0) page_size = 4096;

                uint64_t active   = (uint64_t)vm_stat.active_count * page_size;
                uint64_t wired    = (uint64_t)vm_stat.wire_count * page_size;
                uint64_t compress = (uint64_t)vm_stat.compressor_page_count * page_size;
                uint64_t used     = active + wired + compress;

                int64_t total_mem = 0;
                size_t tlen = sizeof(total_mem);
                sysctlbyname("hw.memsize", &total_mem, &tlen, nullptr, 0);

                s.mem_used_gb  = (float)used / (1024.0f * 1024.0f * 1024.0f);
                s.mem_total_gb = (float)total_mem / (1024.0f * 1024.0f * 1024.0f);
                s.mem_percent  = s.mem_total_gb > 0 ? 100.0f * s.mem_used_gb / s.mem_total_gb : 0.0f;
            }

            // --- RCLI process-specific CPU + memory via task_info ---
            {
                struct mach_task_basic_info tinfo;
                mach_msg_type_number_t tcnt = MACH_TASK_BASIC_INFO_COUNT;
                if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                              (task_info_t)&tinfo, &tcnt) == KERN_SUCCESS) {
                    s.proc_mem_mb = (float)tinfo.resident_size / (1024.0f * 1024.0f);
                }

                task_thread_times_info_data_t times;
                mach_msg_type_number_t times_cnt = TASK_THREAD_TIMES_INFO_COUNT;
                if (task_info(mach_task_self(), TASK_THREAD_TIMES_INFO,
                              (task_info_t)&times, &times_cnt) == KERN_SUCCESS) {
                    uint64_t utime = (uint64_t)times.user_time.seconds * 1000000ULL + times.user_time.microseconds;
                    uint64_t stime = (uint64_t)times.system_time.seconds * 1000000ULL + times.system_time.microseconds;
                    auto now = std::chrono::steady_clock::now();
                    if (!first) {
                        uint64_t d_cpu = (utime - prev_proc_utime) + (stime - prev_proc_stime);
                        double d_wall = std::chrono::duration<double, std::micro>(now - prev_wall).count();
                        s.proc_cpu_percent = d_wall > 0 ? 100.0f * (float)d_cpu / (float)d_wall : 0.0f;
                    }
                    prev_proc_utime = utime;
                    prev_proc_stime = stime;
                    prev_wall = now;
                }
            }

            // --- Network throughput via getifaddrs ---
            {
                uint64_t rx = 0, tx = 0;
                struct ifaddrs* ifa = nullptr;
                if (getifaddrs(&ifa) == 0) {
                    for (auto* p = ifa; p; p = p->ifa_next) {
                        if (!p->ifa_data || !p->ifa_name) continue;
                        if (p->ifa_addr && p->ifa_addr->sa_family == AF_LINK) {
                            auto* d = reinterpret_cast<struct if_data*>(p->ifa_data);
                            rx += d->ifi_ibytes;
                            tx += d->ifi_obytes;
                        }
                    }
                    freeifaddrs(ifa);
                }
                if (prev_rx > 0) {
                    float dt = (float)interval_ms / 1000.0f;
                    s.net_rx_kbs = (float)(rx - prev_rx) / 1024.0f / dt;
                    s.net_tx_kbs = (float)(tx - prev_tx) / 1024.0f / dt;
                }
                prev_rx = rx;
                prev_tx = tx;
            }

            // --- Battery via pmset (lightweight shell) ---
            {
                FILE* fp = popen("pmset -g batt 2>/dev/null", "r");
                if (fp) {
                    char line[256];
                    while (fgets(line, sizeof(line), fp)) {
                        std::string l(line);
                        auto pct_pos = l.find('%');
                        if (pct_pos != std::string::npos && pct_pos >= 1) {
                            size_t start = pct_pos;
                            while (start > 0 && l[start-1] >= '0' && l[start-1] <= '9') start--;
                            s.battery_percent = std::atoi(l.c_str() + start);
                            s.battery_charging = (l.find("charging") != std::string::npos &&
                                                  l.find("discharging") == std::string::npos);
                        }
                    }
                    pclose(fp);
                }
            }

            // Read GPU utilization from IOKit AGXAccelerator PerformanceStatistics
            s.gpu_percent = 0.0f;
            s.ane_percent = 0.0f;
            {
                CFMutableDictionaryRef match = IOServiceMatching("AGXAccelerator");
                io_iterator_t iter = 0;
                if (match && IOServiceGetMatchingServices(kIOMainPortDefault, match, &iter) == KERN_SUCCESS) {
                    io_service_t svc = IOIteratorNext(iter);
                    if (svc) {
                        CFMutableDictionaryRef props = nullptr;
                        if (IORegistryEntryCreateCFProperties(svc, &props,
                                kCFAllocatorDefault, 0) == KERN_SUCCESS && props) {
                            CFDictionaryRef perf = (CFDictionaryRef)CFDictionaryGetValue(
                                props, CFSTR("PerformanceStatistics"));
                            if (perf && CFGetTypeID(perf) == CFDictionaryGetTypeID()) {
                                CFNumberRef util = (CFNumberRef)CFDictionaryGetValue(
                                    perf, CFSTR("Device Utilization %"));
                                if (util && CFGetTypeID(util) == CFNumberGetTypeID()) {
                                    int64_t pct = 0;
                                    CFNumberGetValue(util, kCFNumberSInt64Type, &pct);
                                    s.gpu_percent = (float)pct;
                                }
                            }
                            CFRelease(props);
                        }
                        IOObjectRelease(svc);
                    }
                    IOObjectRelease(iter);
                }
            }

            {
                std::lock_guard<std::mutex> lock(mu_);
                stats_ = s;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
        }
#endif
    }

    mutable std::mutex mu_;
    SystemStats stats_;
    std::atomic<bool> running_{false};
    std::thread thread_;
};

} // namespace rcli_tui
