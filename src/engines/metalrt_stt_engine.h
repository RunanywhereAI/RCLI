#pragma once

#include "engines/metalrt_loader.h"
#include <string>
#include <cstdint>

namespace rastack {

struct MetalRTSttConfig {
    std::string model_dir;
};

class MetalRTSttEngine {
public:
    MetalRTSttEngine() = default;
    ~MetalRTSttEngine() { shutdown(); }

    MetalRTSttEngine(const MetalRTSttEngine&) = delete;
    MetalRTSttEngine& operator=(const MetalRTSttEngine&) = delete;

    bool init(const MetalRTSttConfig& config);
    void shutdown();

    std::string transcribe(const float* samples, int num_samples, int sample_rate = 16000);

    bool is_initialized() const { return initialized_; }
    double last_encode_ms() const { return last_encode_ms_; }
    double last_decode_ms() const { return last_decode_ms_; }
    int64_t last_latency_us() const { return last_latency_us_; }

private:
    void* handle_ = nullptr;
    bool initialized_ = false;
    double last_encode_ms_ = 0;
    double last_decode_ms_ = 0;
    int64_t last_latency_us_ = 0;
};

} // namespace rastack
