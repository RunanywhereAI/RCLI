#pragma once

#include "core/types.h"
#include "core/ring_buffer.h"
#include <string>
#include <atomic>
#include <thread>

// Forward declare sherpa-onnx types
struct SherpaOnnxOnlineRecognizer;
struct SherpaOnnxOnlineStream;
struct SherpaOnnxOfflineRecognizer;

namespace rastack {

struct SttConfig {
    std::string encoder_path;     // encoder ONNX model
    std::string decoder_path;     // decoder ONNX model
    std::string joiner_path;      // joiner ONNX model
    std::string tokens_path;      // tokens.txt
    int         sample_rate = 16000;
    int         num_threads = 2;
    std::string provider   = "cpu"; // "cpu" or "coreml"
};

class SttEngine {
public:
    SttEngine();
    ~SttEngine();

    // Initialize with model paths
    bool init(const SttConfig& config);

    // Feed audio samples (float32, mono, 16kHz)
    void feed_audio(const float* samples, int num_samples);

    // Feed from ring buffer
    void feed_from_ring_buffer(RingBuffer<float>& rb, int max_samples);

    // Get current recognition result
    TextSegment get_result();

    // Check if there's a new result since last call
    bool has_result() const;

    // Reset recognizer state (e.g., after barge-in)
    void reset();

    // Set callback for streaming results
    void set_callback(SttCallback cb) { callback_ = std::move(cb); }

    // Process a loop iteration (call from dedicated thread)
    void process_tick();

    // Stats
    int64_t total_audio_ms() const { return total_audio_samples_ * 1000 / sample_rate_; }
    int64_t last_result_latency_us() const { return last_latency_us_; }

    bool is_initialized() const { return initialized_; }

private:
    const SherpaOnnxOnlineRecognizer* recognizer_ = nullptr;
    const SherpaOnnxOnlineStream*     stream_     = nullptr;
    SttCallback                 callback_;

    int          sample_rate_ = 16000;
    bool         initialized_ = false;
    std::string  last_text_;
    int64_t      total_audio_samples_ = 0;
    int64_t      last_latency_us_ = 0;
    int64_t      last_feed_time_us_ = 0;
    std::atomic<bool> has_new_result_{false};

    // Buffered endpoint result: process_tick() stores final text here
    // before resetting the stream, so get_result() can retrieve it.
    std::string  pending_final_text_;
    bool         pending_final_ = false;
};

// --- Offline STT (Whisper or Parakeet) ---

enum class OfflineSttBackend {
    WHISPER,          // sherpa-onnx whisper (smaller, English-focused)
    NEMO_TRANSDUCER,  // NeMo transducer / Parakeet TDT (higher accuracy, multilingual)
};

struct OfflineSttConfig {
    OfflineSttBackend backend = OfflineSttBackend::WHISPER;

    // Common
    std::string tokens_path;      // tokens.txt
    int         sample_rate = 16000;
    int         num_threads = 2;
    std::string provider = "cpu";

    // Whisper-specific
    std::string encoder_path;     // whisper encoder ONNX model
    std::string decoder_path;     // whisper decoder ONNX model
    std::string language = "en";
    std::string task = "transcribe";
    int         tail_paddings = 500;

    // NeMo Transducer / Parakeet-specific
    std::string transducer_encoder_path;
    std::string transducer_decoder_path;
    std::string transducer_joiner_path;
};

class OfflineSttEngine {
public:
    OfflineSttEngine();
    ~OfflineSttEngine();

    bool init(const OfflineSttConfig& config);

    // Transcribe entire audio buffer at once (blocking)
    std::string transcribe(const float* samples, int num_samples);

    bool is_initialized() const { return initialized_; }
    int64_t last_latency_us() const { return last_latency_us_; }
    OfflineSttBackend backend() const { return backend_; }
    const char* backend_name() const {
        return backend_ == OfflineSttBackend::NEMO_TRANSDUCER ? "Parakeet TDT" : "Whisper";
    }

private:
    const SherpaOnnxOfflineRecognizer* recognizer_ = nullptr;
    OfflineSttBackend backend_ = OfflineSttBackend::WHISPER;
    int sample_rate_ = 16000;
    bool initialized_ = false;
    int64_t last_latency_us_ = 0;
};

} // namespace rastack
