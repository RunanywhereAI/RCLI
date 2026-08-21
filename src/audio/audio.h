#ifndef RCLI_AUDIO_AUDIO_H
#define RCLI_AUDIO_AUDIO_H

#include <atomic>
#include <cstddef>
#include <mutex>
#include <string>
#include <vector>

namespace rcli::audio {

/// True when this build has a capture and playback implementation. Everything
/// below still links without one and reports the reason through `error`, so
/// callers never have to guard their call sites with a platform macro.
bool Available();

/// The rate every STT model in the catalog expects.
inline constexpr int kCaptureRate = 16000;

/// Microphone capture into a growing mono float buffer.
///
/// Recording is a mode the user is in, not a call they make: it starts, the
/// level moves while they talk, and it ends when they say so. That shape is
/// why this owns its buffer rather than taking a callback.
class Recorder {
   public:
    ~Recorder();

    bool Start(std::string* error);
    /// Returns everything captured, and leaves the recorder empty and stopped.
    std::vector<float> Stop();

    bool recording() const { return recording_.load(); }
    /// Short-term RMS, 0..1, for a level meter.
    float level() const { return level_.load(); }

    /// Called from the audio thread.
    void Push(const float* samples, std::size_t count);

   private:
    void* queue_ = nullptr;
    std::atomic<bool> recording_{false};
    std::atomic<float> level_{0.0F};
    mutable std::mutex mutex_;
    std::vector<float> samples_;
};

/// Plays one mono float clip at a time.
class Player {
   public:
    ~Player();

    bool Play(std::vector<float> samples, int sample_rate, std::string* error);
    void Stop();
    bool playing() const { return playing_.load(); }

    /// Called from the audio thread; returns how many samples were written.
    std::size_t Pull(float* out, std::size_t count);
    /// Called from the audio thread once the clip has run out.
    void MarkIdle();

   private:
    void* queue_ = nullptr;
    std::atomic<bool> playing_{false};
    mutable std::mutex mutex_;
    std::vector<float> samples_;
    std::size_t cursor_ = 0;
};

/// 16-bit PCM as commons hands it back, widened to float for playback.
std::vector<float> FromPcm16(const std::string& bytes);
/// Float samples narrowed to signed 16-bit.
std::string ToPcm16(const std::vector<float>& samples);

}  // namespace rcli::audio

#endif  // RCLI_AUDIO_AUDIO_H
