#pragma once

#include "engines/metalrt_loader.h"
#include "core/ring_buffer.h"
#include <string>
#include <vector>
#include <cstdint>

namespace rastack {

struct MetalRTTtsConfig {
    std::string model_dir;
    float speed = 1.0f;
    std::string voice;
};

class MetalRTTtsEngine {
public:
    MetalRTTtsEngine() = default;
    ~MetalRTTtsEngine() { shutdown(); }

    MetalRTTtsEngine(const MetalRTTtsEngine&) = delete;
    MetalRTTtsEngine& operator=(const MetalRTTtsEngine&) = delete;

    bool init(const MetalRTTtsConfig& config);
    void shutdown();

    std::vector<float> synthesize(const std::string& text);
    bool synthesize_to_ring_buffer(const std::string& text, RingBuffer<float>& rb);

    int sample_rate() const { return sample_rate_; }
    bool is_initialized() const { return initialized_; }
    double last_synthesis_ms() const { return last_synthesis_ms_; }
    int last_num_samples() const { return last_num_samples_; }

    // Change voice at runtime (e.g. when personality changes)
    void set_voice(const std::string& voice) { config_.voice = voice; }
    const std::string& voice() const { return config_.voice; }

private:
    void* handle_ = nullptr;
    MetalRTTtsConfig config_;
    int sample_rate_ = 24000;
    bool initialized_ = false;
    double last_synthesis_ms_ = 0;
    int last_num_samples_ = 0;
};

} // namespace rastack
