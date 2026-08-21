#include "audio/audio.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <utility>

#if defined(__APPLE__)
#include <AudioToolbox/AudioToolbox.h>
#endif

namespace rcli::audio {

std::vector<float> FromPcm16(const std::string& bytes) {
    std::vector<float> samples(bytes.size() / 2);
    for (std::size_t i = 0; i < samples.size(); ++i) {
        std::int16_t value = 0;
        std::memcpy(&value, bytes.data() + i * 2, 2);
        samples[i] = static_cast<float>(value) / 32768.0F;
    }
    return samples;
}

std::string ToPcm16(const std::vector<float>& samples) {
    std::string bytes(samples.size() * 2, '\0');
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const float clamped = std::max(-1.0F, std::min(1.0F, samples[i]));
        const auto value = static_cast<std::int16_t>(clamped * 32767.0F);
        std::memcpy(bytes.data() + i * 2, &value, 2);
    }
    return bytes;
}

void Recorder::Push(const float* samples, std::size_t count) {
    double energy = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        energy += static_cast<double>(samples[i]) * samples[i];
    }
    level_.store(count == 0 ? 0.0F : static_cast<float>(std::sqrt(energy / static_cast<double>(count))));
    const std::lock_guard<std::mutex> lock(mutex_);
    samples_.insert(samples_.end(), samples, samples + count);
}

void Player::MarkIdle() {
    playing_.store(false);
}

std::size_t Player::Pull(float* out, std::size_t count) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const std::size_t left = samples_.size() - std::min(cursor_, samples_.size());
    const std::size_t taken = std::min(count, left);
    std::copy_n(samples_.begin() + static_cast<std::ptrdiff_t>(cursor_), taken, out);
    cursor_ += taken;
    return taken;
}

#if defined(__APPLE__)

namespace {

/// Three buffers is the usual floor for a glitch-free queue: one being filled
/// by the device, one in flight, one being consumed.
constexpr int kBuffers = 3;
constexpr std::size_t kFramesPerBuffer = 2048;

AudioStreamBasicDescription MonoFloat(int rate) {
    AudioStreamBasicDescription format{};
    format.mSampleRate = rate;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    format.mFramesPerPacket = 1;
    format.mChannelsPerFrame = 1;
    format.mBitsPerChannel = 32;
    format.mBytesPerFrame = 4;
    format.mBytesPerPacket = 4;
    return format;
}

void OnCaptured(void* user_data, AudioQueueRef queue, AudioQueueBufferRef buffer,
                const AudioTimeStamp*, UInt32, const AudioStreamPacketDescription*) {
    auto* recorder = static_cast<Recorder*>(user_data);
    recorder->Push(static_cast<const float*>(buffer->mAudioData),
                   buffer->mAudioDataByteSize / sizeof(float));
    if (recorder->recording()) {
        AudioQueueEnqueueBuffer(queue, buffer, 0, nullptr);
    }
}

void OnPlayed(void* user_data, AudioQueueRef queue, AudioQueueBufferRef buffer) {
    auto* player = static_cast<Player*>(user_data);
    const std::size_t written =
        player->Pull(static_cast<float*>(buffer->mAudioData), kFramesPerBuffer);
    if (written == 0) {
        // Flush what is already queued, then report idle. Without this the
        // caller has no way to know a clip ended.
        AudioQueueStop(queue, /*immediate=*/false);
        player->MarkIdle();
        return;
    }
    buffer->mAudioDataByteSize = static_cast<UInt32>(written * sizeof(float));
    AudioQueueEnqueueBuffer(queue, buffer, 0, nullptr);
}

bool Fail(std::string* error, const char* what, OSStatus status) {
    if (error != nullptr) {
        *error = std::string(what) + " (" + std::to_string(status) + ")";
    }
    return false;
}

}  // namespace

bool Available() {
    return true;
}

Recorder::~Recorder() {
    Stop();
}

bool Recorder::Start(std::string* error) {
    if (recording_.load()) {
        return true;
    }
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        samples_.clear();
    }
    AudioStreamBasicDescription format = MonoFloat(kCaptureRate);
    AudioQueueRef queue = nullptr;
    OSStatus status =
        AudioQueueNewInput(&format, OnCaptured, this, nullptr, nullptr, 0, &queue);
    if (status != noErr) {
        // The usual cause is the terminal not holding microphone permission;
        // macOS reports it as a queue-creation failure, not a separate error.
        return Fail(error, "the microphone is unavailable — check System Settings › Privacy "
                           "› Microphone for your terminal",
                    status);
    }
    recording_.store(true);
    for (int i = 0; i < kBuffers; ++i) {
        AudioQueueBufferRef buffer = nullptr;
        AudioQueueAllocateBuffer(queue, kFramesPerBuffer * sizeof(float), &buffer);
        AudioQueueEnqueueBuffer(queue, buffer, 0, nullptr);
    }
    status = AudioQueueStart(queue, nullptr);
    if (status != noErr) {
        recording_.store(false);
        AudioQueueDispose(queue, true);
        return Fail(error, "could not start recording", status);
    }
    queue_ = queue;
    return true;
}

std::vector<float> Recorder::Stop() {
    if (queue_ != nullptr) {
        recording_.store(false);
        auto queue = static_cast<AudioQueueRef>(queue_);
        AudioQueueStop(queue, /*immediate=*/true);
        AudioQueueDispose(queue, /*immediate=*/true);
        queue_ = nullptr;
    }
    level_.store(0.0F);
    const std::lock_guard<std::mutex> lock(mutex_);
    return std::exchange(samples_, {});
}

Player::~Player() {
    Stop();
}

bool Player::Play(std::vector<float> samples, int sample_rate, std::string* error) {
    Stop();
    if (samples.empty()) {
        if (error != nullptr) {
            *error = "nothing to play";
        }
        return false;
    }
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        samples_ = std::move(samples);
        cursor_ = 0;
    }
    AudioStreamBasicDescription format = MonoFloat(sample_rate);
    AudioQueueRef queue = nullptr;
    OSStatus status =
        AudioQueueNewOutput(&format, OnPlayed, this, nullptr, nullptr, 0, &queue);
    if (status != noErr) {
        return Fail(error, "no audio output", status);
    }
    playing_.store(true);
    for (int i = 0; i < kBuffers; ++i) {
        AudioQueueBufferRef buffer = nullptr;
        AudioQueueAllocateBuffer(queue, kFramesPerBuffer * sizeof(float), &buffer);
        const std::size_t written = Pull(static_cast<float*>(buffer->mAudioData), kFramesPerBuffer);
        if (written == 0) {
            break;
        }
        buffer->mAudioDataByteSize = static_cast<UInt32>(written * sizeof(float));
        AudioQueueEnqueueBuffer(queue, buffer, 0, nullptr);
    }
    status = AudioQueueStart(queue, nullptr);
    if (status != noErr) {
        playing_.store(false);
        AudioQueueDispose(queue, true);
        return Fail(error, "could not start playback", status);
    }
    queue_ = queue;
    return true;
}

void Player::Stop() {
    if (queue_ != nullptr) {
        auto queue = static_cast<AudioQueueRef>(queue_);
        AudioQueueStop(queue, /*immediate=*/true);
        AudioQueueDispose(queue, /*immediate=*/true);
        queue_ = nullptr;
    }
    playing_.store(false);
    const std::lock_guard<std::mutex> lock(mutex_);
    samples_.clear();
    cursor_ = 0;
}

#else

bool Available() {
    return false;
}

Recorder::~Recorder() = default;

bool Recorder::Start(std::string* error) {
    if (error != nullptr) {
        *error = "this build has no audio capture";
    }
    return false;
}

std::vector<float> Recorder::Stop() {
    return {};
}

Player::~Player() = default;

bool Player::Play(std::vector<float>, int, std::string* error) {
    if (error != nullptr) {
        *error = "this build has no audio output";
    }
    return false;
}

void Player::Stop() {}

#endif

}  // namespace rcli::audio
