#pragma once

#include "engines/metalrt_loader.h"
#include "core/types.h"
#include <string>
#include <functional>
#include <atomic>

namespace rastack {

struct MetalRTVlmConfig {
    std::string model_dir;
    int max_tokens = 512;
    int top_k = 40;
    float temperature = 0.7f;
};

struct MetalRTVlmStats {
    double vision_encode_ms = 0;
    double prefill_ms = 0;
    double decode_ms = 0;
    double tps = 0;
    int prompt_tokens = 0;
    int generated_tokens = 0;
};

class MetalRTVlmEngine {
public:
    MetalRTVlmEngine() = default;
    ~MetalRTVlmEngine() { shutdown(); }

    MetalRTVlmEngine(const MetalRTVlmEngine&) = delete;
    MetalRTVlmEngine& operator=(const MetalRTVlmEngine&) = delete;

    bool init(const MetalRTVlmConfig& config);
    void shutdown();
    void reset();

    // Analyze an image with a text prompt (blocking)
    std::string analyze_image(const std::string& image_path,
                              const std::string& prompt);

    // Analyze with streaming token callback
    std::string analyze_image_stream(const std::string& image_path,
                                     const std::string& prompt,
                                     TokenCallback on_token);

    // Text-only generation (follow-up without new image)
    std::string generate(const std::string& prompt);

    std::string model_name() const;
    std::string device_name() const;

    bool is_initialized() const { return initialized_; }
    const MetalRTVlmStats& last_stats() const { return stats_; }

private:
    void* handle_ = nullptr;
    MetalRTVlmConfig config_;
    MetalRTVlmStats stats_;
    bool initialized_ = false;
};

} // namespace rastack