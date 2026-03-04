#pragma once

#include "core/types.h"
#include <string>
#include <vector>

// Forward declare sherpa-onnx types
struct SherpaOnnxVoiceActivityDetector;

namespace rastack {

struct VadConfig {
    std::string model_path;              // silero_vad.onnx
    float       threshold            = 0.5f;
    float       min_silence_duration = 0.5f;   // seconds
    float       min_speech_duration  = 0.25f;  // seconds
    float       max_speech_duration  = 30.0f;  // seconds
    int         window_size          = 512;     // 32ms at 16kHz
    int         sample_rate          = 16000;
    int         num_threads          = 1;
};

struct SpeechSegment {
    std::vector<float> samples;
    int start_sample = 0;  // offset in original audio
};

class VadEngine {
public:
    VadEngine();
    ~VadEngine();

    bool init(const VadConfig& config);

    // Feed audio samples (float32, mono, 16kHz)
    void feed_audio(const float* samples, int num_samples);

    // Is speech currently being detected?
    bool is_speech() const;

    // Are there completed speech segments ready?
    bool has_segment() const;

    // Pop the next completed speech segment
    SpeechSegment pop_segment();

    // Reset VAD state
    void reset();

    // Flush remaining audio (call at end of stream)
    void flush();

    // Extract all speech segments from a complete audio buffer
    // Convenience method for file pipeline
    std::vector<SpeechSegment> extract_speech(const float* samples, int num_samples);

    bool is_initialized() const { return initialized_; }

private:
    const SherpaOnnxVoiceActivityDetector* vad_ = nullptr;
    VadConfig config_;
    bool initialized_ = false;
    int window_size_ = 512;
};

} // namespace rastack
