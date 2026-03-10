#include "engines/metalrt_tts_engine.h"
#include "core/log.h"
#include "core/types.h"
#include <chrono>

namespace rastack {

bool MetalRTTtsEngine::init(const MetalRTTtsConfig& config) {
    auto& loader = MetalRTLoader::instance();
    if (!loader.is_loaded() && !loader.load()) {
        LOG_ERROR("MetalRT-TTS", "dylib not loaded");
        return false;
    }

    if (!loader.tts_create || !loader.tts_load || !loader.tts_synthesize) {
        LOG_WARN("MetalRT-TTS", "Kokoro TTS symbols not available in dylib — "
                 "create=%p load=%p synthesize=%p",
                 (void*)loader.tts_create, (void*)loader.tts_load, (void*)loader.tts_synthesize);
        return false;
    }

    LOG_DEBUG("MetalRT-TTS", "Creating Kokoro TTS instance via Metal GPU...");
    auto t_init_start = std::chrono::high_resolution_clock::now();

    handle_ = loader.tts_create();
    if (!handle_) {
        LOG_ERROR("MetalRT-TTS", "Failed to create TTS instance");
        return false;
    }

    LOG_DEBUG("MetalRT-TTS", "Loading model from %s ...", config.model_dir.c_str());
    if (!loader.tts_load(handle_, config.model_dir.c_str())) {
        LOG_ERROR("MetalRT-TTS", "Failed to load model from %s", config.model_dir.c_str());
        loader.tts_destroy(handle_);
        handle_ = nullptr;
        return false;
    }

    config_ = config;
    if (loader.tts_sample_rate)
        sample_rate_ = loader.tts_sample_rate(handle_);

    auto t_init_end = std::chrono::high_resolution_clock::now();
    double init_ms = std::chrono::duration<double, std::milli>(t_init_end - t_init_start).count();

    initialized_ = true;
    LOG_DEBUG("MetalRT-TTS", "=== MetalRT TTS GPU VERIFICATION ===");
    LOG_DEBUG("MetalRT-TTS", "  Engine:      Kokoro via libmetalrt.dylib (Metal GPU)");
    LOG_DEBUG("MetalRT-TTS", "  Model dir:   %s", config.model_dir.c_str());
    LOG_DEBUG("MetalRT-TTS", "  Voice:       %s", config.voice.empty() ? "(default)" : config.voice.c_str());
    LOG_DEBUG("MetalRT-TTS", "  Speed:       %.2f", config.speed);
    LOG_DEBUG("MetalRT-TTS", "  Sample rate: %d Hz", sample_rate_);
    LOG_DEBUG("MetalRT-TTS", "  Init time:   %.1f ms", init_ms);
    return true;
}

void MetalRTTtsEngine::shutdown() {
    if (handle_) {
        auto& loader = MetalRTLoader::instance();
        if (loader.tts_destroy) {
            loader.tts_destroy(handle_);
        }
        handle_ = nullptr;
    }
    initialized_ = false;
}

std::vector<float> MetalRTTtsEngine::synthesize(const std::string& text) {
    if (!initialized_ || !handle_) return {};

    auto& loader = MetalRTLoader::instance();
    const char* voice = config_.voice.empty() ? nullptr : config_.voice.c_str();

    LOG_DEBUG("MetalRT-TTS", "synthesize() → Metal GPU | text=%zu chars, voice=%s, speed=%.2f",
              text.size(), voice ? voice : "(default)", config_.speed);

    auto wall_start = std::chrono::high_resolution_clock::now();
    MetalRTAudio audio;
    {
        std::lock_guard<std::mutex> gpu_lock(loader.gpu_mutex());
        audio = loader.tts_synthesize(handle_, text.c_str(), voice, config_.speed);
    }
    auto wall_end = std::chrono::high_resolution_clock::now();

    double wall_ms = std::chrono::duration<double, std::milli>(wall_end - wall_start).count();
    last_synthesis_ms_ = wall_ms;
    last_num_samples_ = audio.num_samples;

    if (!audio.samples || audio.num_samples <= 0) {
        LOG_ERROR("MetalRT-TTS", "synthesize() FAILED after %.1fms: '%s'", wall_ms, text.c_str());
        return {};
    }

    double audio_duration_ms = (double)audio.num_samples * 1000.0 / sample_rate_;
    double rtf_wall = (audio_duration_ms > 0) ? (wall_ms / audio_duration_ms) : 0;
    double rtf_compute = (audio_duration_ms > 0) ? (audio.synthesis_ms / audio_duration_ms) : 0;

    std::vector<float> samples(audio.samples, audio.samples + audio.num_samples);

    LOG_DEBUG("MetalRT-TTS", "=== TTS SYNTHESIS TIMING [Metal GPU] ===");
    LOG_DEBUG("MetalRT-TTS", "  Text:         \"%.*s%s\"",
              (int)(text.size() > 60 ? 60 : text.size()), text.c_str(),
              text.size() > 60 ? "..." : "");
    LOG_DEBUG("MetalRT-TTS", "  Samples:      %d (%.1fms audio @ %dHz)",
              audio.num_samples, audio_duration_ms, sample_rate_);
    LOG_DEBUG("MetalRT-TTS", "  Compute time: %.1fms (reported by dylib)", audio.synthesis_ms);
    LOG_DEBUG("MetalRT-TTS", "  Wall time:    %.1fms (measured end-to-end)", wall_ms);
    LOG_DEBUG("MetalRT-TTS", "  RTF (compute):%.3fx realtime", rtf_compute);
    LOG_DEBUG("MetalRT-TTS", "  RTF (wall):   %.3fx realtime", rtf_wall);
    if (rtf_wall > 1.0) {
        LOG_WARN("MetalRT-TTS", "TTS is SLOWER than realtime (RTF=%.2fx) — "
                 "possible CPU fallback or GPU contention!", rtf_wall);
    }
    if (wall_ms > audio.synthesis_ms * 1.5 && wall_ms > 50) {
        LOG_WARN("MetalRT-TTS", "Wall time (%.1fms) >> compute time (%.1fms) — "
                 "overhead in memory copy or dylib dispatch?", wall_ms, audio.synthesis_ms);
    }

    if (loader.tts_free_audio)
        loader.tts_free_audio(audio);

    return samples;
}

bool MetalRTTtsEngine::synthesize_to_ring_buffer(const std::string& text, RingBuffer<float>& rb) {
    auto samples = synthesize(text);
    if (samples.empty()) return false;

    size_t written = rb.write(samples.data(), samples.size());
    if (written < samples.size()) {
        LOG_WARN("MetalRT-TTS", "Ring buffer full, dropped %zu samples",
                 samples.size() - written);
    }
    return written > 0;
}

} // namespace rastack
