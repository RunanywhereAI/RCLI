#pragma once

#include "core/types.h"
#include "core/ring_buffer.h"
#include <string>
#include <atomic>
#include <vector>

// Forward declare sherpa-onnx types
struct SherpaOnnxOfflineTts;

namespace rastack {

struct TtsConfig {
    std::string architecture;     // "vits" (default), "kokoro", "kitten", "matcha"
    std::string model_path;       // ONNX model file
    std::string model_config_path;// JSON config (VITS/Piper only)
    std::string tokens_path;      // tokens.txt (phoneme ID map)
    std::string data_dir;         // espeak-ng data directory
    std::string voices_path;      // voices.bin (Kokoro/Kitten only)
    std::string vocoder_path;     // vocoder ONNX (Matcha only)
    std::string lexicon_path;     // lexicon file (Kokoro multi-lang only)
    std::string lang;             // language code (Kokoro multi-lang only, e.g. "en-us")
    int         sample_rate = 22050;
    int         num_threads = 2;
    float       speed       = 1.0f;
    int         speaker_id  = 0;
};

struct TtsStats {
    int64_t synthesis_us  = 0;
    int64_t num_samples   = 0;
    double  real_time_factor() const {
        if (synthesis_us == 0 || num_samples == 0) return 0;
        double audio_duration_us = num_samples * 1e6 / 22050.0;
        return synthesis_us / audio_duration_us;
    }
};

class TtsEngine {
public:
    TtsEngine();
    ~TtsEngine();

    // Initialize with model
    bool init(const TtsConfig& config);

    // Synthesize text to audio samples (blocking)
    // Returns audio samples (float32, mono)
    std::vector<float> synthesize(const std::string& text);

    // Synthesize and write directly to ring buffer
    bool synthesize_to_ring_buffer(const std::string& text, RingBuffer<float>& rb);

    // Get output sample rate
    int sample_rate() const { return sample_rate_; }

    // Stats from last synthesis
    const TtsStats& last_stats() const { return stats_; }

    bool is_initialized() const { return initialized_; }

    // Change speaker at runtime (Kokoro multi-voice)
    void set_speaker_id(int id) { config_.speaker_id = id; }

private:
    const SherpaOnnxOfflineTts* tts_ = nullptr;
    TtsConfig config_;
    TtsStats  stats_;
    int       sample_rate_ = 22050;
    bool      initialized_ = false;
};

} // namespace rastack
