#pragma once

#include "core/types.h"
#include "core/ring_buffer.h"
#include <string>
#include <vector>
#include <atomic>
#include <functional>

#if defined(__APPLE__) && !RASTACK_FILE_AUDIO_ONLY
#include <AudioToolbox/AudioToolbox.h>
#endif

namespace rastack {

enum class AudioMode {
    FILE_MODE,  // Read/write WAV files
    LIVE_MODE   // CoreAudio mic/speaker
};

struct AudioConfig {
    AudioMode mode             = AudioMode::FILE_MODE;
    int       capture_rate     = 16000;  // STT expects 16kHz
    int       playback_rate    = 22050;  // Piper outputs 22050Hz
    int       buffer_size_ms   = 30;     // audio callback buffer size
    std::string input_file;              // WAV file for FILE_MODE
    std::string output_file;             // WAV file for FILE_MODE output
};

class AudioIO {
public:
    AudioIO();
    ~AudioIO();

    bool init(const AudioConfig& config, RingBuffer<float>* capture_rb, RingBuffer<float>* playback_rb);
    void shutdown();

    // Start/stop audio processing
    bool start();
    void stop();

    // --- File mode operations ---
    // Load WAV file into ring buffer
    bool load_wav(const std::string& path, RingBuffer<float>& rb);

    // Save audio samples to WAV file
    static bool save_wav(const std::string& path, const float* samples,
                         int num_samples, int sample_rate);

    // Load WAV to vector (for direct processing)
    static std::vector<float> load_wav_to_vec(const std::string& path, int target_sample_rate);

    bool is_running() const { return running_.load(std::memory_order_relaxed); }

    float get_rms() const { return current_rms_.load(std::memory_order_relaxed); }

    // Barge-in support: is speaker currently outputting non-silence?
    bool is_playing() const { return playback_active_.load(std::memory_order_relaxed); }
    float playback_rms() const { return playback_rms_.load(std::memory_order_relaxed); }

private:
#if defined(__APPLE__) && !RASTACK_FILE_AUDIO_ONLY
    // CoreAudio (macOS only — iOS uses AVAudioEngine in Swift layer)
    AudioComponentInstance capture_unit_  = nullptr;
    AudioComponentInstance playback_unit_ = nullptr;

    static OSStatus capture_callback(void* inRefCon,
                                     AudioUnitRenderActionFlags* ioActionFlags,
                                     const AudioTimeStamp* inTimeStamp,
                                     UInt32 inBusNumber,
                                     UInt32 inNumberFrames,
                                     AudioBufferList* ioData);

    static OSStatus playback_callback(void* inRefCon,
                                      AudioUnitRenderActionFlags* ioActionFlags,
                                      const AudioTimeStamp* inTimeStamp,
                                      UInt32 inBusNumber,
                                      UInt32 inNumberFrames,
                                      AudioBufferList* ioData);
    bool init_core_audio();
    void shutdown_core_audio();
#endif

    AudioConfig         config_;
    RingBuffer<float>*  capture_rb_  = nullptr;
    RingBuffer<float>*  playback_rb_ = nullptr;
    std::atomic<bool>   running_{false};
    std::atomic<float>  current_rms_{0.0f};
    std::atomic<bool>   playback_active_{false};   // true when speaker outputting audio
    std::atomic<float>  playback_rms_{0.0f};       // RMS of current playback output
    int                 device_capture_rate_ = 0;  // actual hardware sample rate
    std::vector<float>  resample_buf_;             // scratch for downsampling
};

} // namespace rastack
