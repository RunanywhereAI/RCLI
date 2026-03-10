#include "engines/metalrt_stt_engine.h"
#include "core/log.h"
#include "core/types.h"
#include <cmath>
#include <mutex>
#include <future>
#include <chrono>

namespace rastack {

bool MetalRTSttEngine::init(const MetalRTSttConfig& config) {
    auto& loader = MetalRTLoader::instance();
    if (!loader.is_loaded() && !loader.load()) {
        LOG_ERROR("MetalRT-STT", "dylib not loaded");
        return false;
    }

    if (!loader.whisper_create || !loader.whisper_load || !loader.whisper_transcribe) {
        LOG_WARN("MetalRT-STT", "Whisper symbols not available in dylib — "
                 "create=%p load=%p transcribe=%p",
                 (void*)loader.whisper_create, (void*)loader.whisper_load, (void*)loader.whisper_transcribe);
        return false;
    }

    LOG_DEBUG("MetalRT-STT", "Creating Whisper instance via Metal GPU...");
    handle_ = loader.whisper_create();
    if (!handle_) {
        LOG_ERROR("MetalRT-STT", "Failed to create Whisper instance");
        return false;
    }

    LOG_DEBUG("MetalRT-STT", "Loading Whisper model from %s ...", config.model_dir.c_str());
    if (!loader.whisper_load(handle_, config.model_dir.c_str())) {
        LOG_ERROR("MetalRT-STT", "Failed to load model from %s", config.model_dir.c_str());
        loader.whisper_destroy(handle_);
        handle_ = nullptr;
        return false;
    }

    initialized_ = true;
    LOG_DEBUG("MetalRT-STT", "=== MetalRT STT GPU VERIFICATION ===");
    LOG_DEBUG("MetalRT-STT", "  Engine:    Whisper via libmetalrt.dylib (Metal GPU)");
    LOG_DEBUG("MetalRT-STT", "  Model dir: %s", config.model_dir.c_str());
    return true;
}

void MetalRTSttEngine::shutdown() {
    if (handle_) {
        auto& loader = MetalRTLoader::instance();
        if (loader.whisper_destroy) {
            loader.whisper_destroy(handle_);
        }
        handle_ = nullptr;
    }
    initialized_ = false;
}

std::string MetalRTSttEngine::transcribe(const float* samples, int num_samples, int sample_rate) {
    if (!initialized_ || !handle_) return "";
    if (!samples || num_samples <= 0) return "";

    constexpr int MIN_SAMPLES_16K = 8000;
    int min_samples = (sample_rate > 0) ? (sample_rate / 2) : MIN_SAMPLES_16K;

    if (num_samples < min_samples) {
        fprintf(stderr, "[MetalRT-STT] Audio too short (%d samples, need >= %d) — skipping\n",
                num_samples, min_samples);
        return "";
    }

    double sum_sq = 0;
    for (int i = 0; i < num_samples; i++) sum_sq += (double)samples[i] * samples[i];
    double rms = std::sqrt(sum_sq / num_samples);
    if (rms < 0.001) {
        fprintf(stderr, "[MetalRT-STT] Audio RMS too low (%.6f) — skipping silent recording\n", rms);
        return "";
    }

    double audio_duration_ms = (sample_rate > 0) ? (num_samples * 1000.0 / sample_rate) : 0;
    fprintf(stderr, "[MetalRT-STT] transcribe() → Metal GPU | %d samples (%.1fms, rms=%.4f)\n",
            num_samples, audio_duration_ms, rms);

    auto& loader = MetalRTLoader::instance();
    int64_t t_start = now_us();

    constexpr int TIMEOUT_SEC = 15;
    auto fut = std::async(std::launch::async, [&]() -> const char* {
        std::lock_guard<std::mutex> gpu_lock(loader.gpu_mutex());
        return loader.whisper_transcribe(handle_, samples, num_samples, sample_rate);
    });

    auto status = fut.wait_for(std::chrono::seconds(TIMEOUT_SEC));
    if (status != std::future_status::ready) {
        fprintf(stderr, "[MetalRT-STT] Transcription timed out after %ds — skipping\n", TIMEOUT_SEC);
        return "";
    }

    const char* text = fut.get();
    last_latency_us_ = now_us() - t_start;

    if (loader.whisper_last_encode_ms)
        last_encode_ms_ = loader.whisper_last_encode_ms(handle_);
    if (loader.whisper_last_decode_ms)
        last_decode_ms_ = loader.whisper_last_decode_ms(handle_);

    if (!text) return "";

    std::string result(text);
    if (loader.whisper_free_text)
        loader.whisper_free_text(text);

    double wall_ms = last_latency_us_ / 1000.0;
    double rtf = (audio_duration_ms > 0) ? (wall_ms / audio_duration_ms) : 0;
    fprintf(stderr, "[MetalRT-STT] Result: \"%s\" (%.1fms, RTF=%.3fx)\n",
            result.c_str(), wall_ms, rtf);
    return result;
}

} // namespace rastack
