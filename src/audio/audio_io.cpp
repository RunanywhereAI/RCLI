#include "audio/audio_io.h"
#include "core/log.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <cmath>
#include <algorithm>

#if defined(__APPLE__) && !RASTACK_FILE_AUDIO_ONLY
#include <CoreAudio/CoreAudio.h>
#endif

namespace rastack {

AudioIO::AudioIO() = default;

AudioIO::~AudioIO() {
    shutdown();
}

bool AudioIO::init(const AudioConfig& config, RingBuffer<float>* capture_rb, RingBuffer<float>* playback_rb) {
    config_      = config;
    capture_rb_  = capture_rb;
    playback_rb_ = playback_rb;

    if (config.mode == AudioMode::LIVE_MODE) {
#if defined(__APPLE__) && !RASTACK_FILE_AUDIO_ONLY
        return init_core_audio();
#else
        LOG_ERROR("Audio", "Live mode only supported on macOS");
        return false;
#endif
    }
    return true; // File mode needs no init
}

void AudioIO::shutdown() {
    stop();
#if defined(__APPLE__) && !RASTACK_FILE_AUDIO_ONLY
    shutdown_core_audio();
#endif
}

bool AudioIO::start() {
    if (config_.mode == AudioMode::LIVE_MODE) {
#if defined(__APPLE__) && !RASTACK_FILE_AUDIO_ONLY
        if (capture_unit_) {
            OSStatus status = AudioOutputUnitStart(capture_unit_);
            if (status != noErr) {
                LOG_ERROR("Audio", "Failed to start capture: %d", (int)status);
                return false;
            }
        }
        if (playback_unit_) {
            OSStatus status = AudioOutputUnitStart(playback_unit_);
            if (status != noErr) {
                LOG_ERROR("Audio", "Failed to start playback: %d", (int)status);
                return false;
            }
        }
#endif
    }
    running_.store(true, std::memory_order_release);
    return true;
}

void AudioIO::stop() {
    running_.store(false, std::memory_order_release);
#if defined(__APPLE__) && !RASTACK_FILE_AUDIO_ONLY
    if (capture_unit_)  AudioOutputUnitStop(capture_unit_);
    if (playback_unit_) AudioOutputUnitStop(playback_unit_);
#endif
}

// --- WAV file I/O ---

// Minimal WAV header parser
struct WavHeader {
    char     riff[4];
    uint32_t file_size;
    char     wave[4];
    char     fmt_chunk[4];
    uint32_t fmt_size;
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
};

std::vector<float> AudioIO::load_wav_to_vec(const std::string& path, int target_sample_rate) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        LOG_ERROR("Audio", "Cannot open WAV: %s", path.c_str());
        return {};
    }

    // Read RIFF header
    WavHeader header;
    file.read(reinterpret_cast<char*>(&header), sizeof(WavHeader));

    if (std::strncmp(header.riff, "RIFF", 4) != 0 ||
        std::strncmp(header.wave, "WAVE", 4) != 0) {
        LOG_ERROR("Audio", "Invalid WAV file: %s", path.c_str());
        return {};
    }

    // Skip to data chunk
    char chunk_id[4];
    uint32_t chunk_size;
    while (file.read(chunk_id, 4)) {
        file.read(reinterpret_cast<char*>(&chunk_size), 4);
        if (std::strncmp(chunk_id, "data", 4) == 0) break;
        file.seekg(chunk_size, std::ios::cur);
    }

    // Read raw audio data
    int bytes_per_sample = header.bits_per_sample / 8;
    int num_samples = chunk_size / bytes_per_sample / header.num_channels;

    std::vector<float> samples(num_samples);

    if (header.bits_per_sample == 16) {
        std::vector<int16_t> raw(num_samples * header.num_channels);
        file.read(reinterpret_cast<char*>(raw.data()), chunk_size);
        // Convert to float mono
        for (int i = 0; i < num_samples; i++) {
            float sum = 0;
            for (int ch = 0; ch < header.num_channels; ch++) {
                sum += raw[i * header.num_channels + ch] / 32768.0f;
            }
            samples[i] = sum / header.num_channels;
        }
    } else if (header.bits_per_sample == 32 && header.audio_format == 3) {
        // Float32
        file.read(reinterpret_cast<char*>(samples.data()), num_samples * sizeof(float));
    } else {
        LOG_ERROR("Audio", "Unsupported WAV format: %d-bit, format=%d",
                header.bits_per_sample, header.audio_format);
        return {};
    }

    LOG_DEBUG("Audio", "Loaded WAV: %s (%d samples, %dHz, %dch)",
            path.c_str(), num_samples, header.sample_rate, header.num_channels);

    // Resample if needed (simple linear interpolation)
    if ((int)header.sample_rate != target_sample_rate && target_sample_rate > 0) {
        double ratio = (double)target_sample_rate / header.sample_rate;
        int new_count = (int)(num_samples * ratio);
        std::vector<float> resampled(new_count);
        for (int i = 0; i < new_count; i++) {
            double src_idx = i / ratio;
            int idx0 = (int)src_idx;
            int idx1 = std::min(idx0 + 1, num_samples - 1);
            double frac = src_idx - idx0;
            resampled[i] = (float)(samples[idx0] * (1.0 - frac) + samples[idx1] * frac);
        }
        LOG_DEBUG("Audio", "Resampled %dHz -> %dHz (%d -> %d samples)",
                header.sample_rate, target_sample_rate, num_samples, new_count);
        return resampled;
    }

    return samples;
}

bool AudioIO::load_wav(const std::string& path, RingBuffer<float>& rb) {
    auto samples = load_wav_to_vec(path, config_.capture_rate);
    if (samples.empty()) return false;

    size_t written = rb.write(samples.data(), samples.size());
    return written == samples.size();
}

bool AudioIO::save_wav(const std::string& path, const float* samples,
                       int num_samples, int sample_rate) {
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        LOG_ERROR("Audio", "Cannot create WAV: %s", path.c_str());
        return false;
    }

    // Convert to 16-bit PCM
    std::vector<int16_t> pcm(num_samples);
    for (int i = 0; i < num_samples; i++) {
        float s = std::max(-1.0f, std::min(1.0f, samples[i]));
        pcm[i] = static_cast<int16_t>(s * 32767.0f);
    }

    uint32_t data_size = num_samples * sizeof(int16_t);

    // Write WAV header
    WavHeader header;
    std::memcpy(header.riff, "RIFF", 4);
    header.file_size = 36 + data_size;
    std::memcpy(header.wave, "WAVE", 4);
    std::memcpy(header.fmt_chunk, "fmt ", 4);
    header.fmt_size = 16;
    header.audio_format = 1; // PCM
    header.num_channels = 1;
    header.sample_rate = sample_rate;
    header.byte_rate = sample_rate * 2;
    header.block_align = 2;
    header.bits_per_sample = 16;

    file.write(reinterpret_cast<char*>(&header), sizeof(header));

    // Data chunk
    file.write("data", 4);
    file.write(reinterpret_cast<char*>(&data_size), 4);
    file.write(reinterpret_cast<char*>(pcm.data()), data_size);

    return true;
}

// --- CoreAudio ---
#if defined(__APPLE__) && !RASTACK_FILE_AUDIO_ONLY

OSStatus AudioIO::capture_callback(void* inRefCon,
                                   AudioUnitRenderActionFlags* ioActionFlags,
                                   const AudioTimeStamp* inTimeStamp,
                                   UInt32 inBusNumber,
                                   UInt32 inNumberFrames,
                                   AudioBufferList* ioData) {
    auto* self = static_cast<AudioIO*>(inRefCon);
    if (!self->running_.load(std::memory_order_relaxed) || !self->capture_rb_) {
        return noErr;
    }

    AudioBufferList bufferList;
    bufferList.mNumberBuffers = 1;
    bufferList.mBuffers[0].mDataByteSize = inNumberFrames * sizeof(float);
    bufferList.mBuffers[0].mNumberChannels = 1;

    float local_buf[8192];
    bufferList.mBuffers[0].mData = local_buf;

    OSStatus status = AudioUnitRender(self->capture_unit_, ioActionFlags,
                                       inTimeStamp, inBusNumber, inNumberFrames,
                                       &bufferList);
    if (status != noErr) return status;

    float sum_sq = 0.0f;
    for (UInt32 i = 0; i < inNumberFrames; ++i)
        sum_sq += local_buf[i] * local_buf[i];
    self->current_rms_.store(std::sqrtf(sum_sq / inNumberFrames), std::memory_order_relaxed);

    int dev_rate = self->device_capture_rate_;
    int target   = self->config_.capture_rate;

    if (dev_rate > 0 && dev_rate != target) {
        double ratio = (double)target / dev_rate;
        int out_count = (int)(inNumberFrames * ratio);
        if (out_count > 0) {
            float rs_buf[4096];
            for (int i = 0; i < out_count && i < 4096; ++i) {
                double src = i / ratio;
                int s0 = (int)src;
                int s1 = std::min(s0 + 1, (int)inNumberFrames - 1);
                double f = src - s0;
                rs_buf[i] = (float)(local_buf[s0] * (1.0 - f) + local_buf[s1] * f);
            }
            self->capture_rb_->write(rs_buf, std::min(out_count, 4096));
        }
    } else {
        self->capture_rb_->write(local_buf, inNumberFrames);
    }
    return noErr;
}

OSStatus AudioIO::playback_callback(void* inRefCon,
                                    AudioUnitRenderActionFlags* ioActionFlags,
                                    const AudioTimeStamp* inTimeStamp,
                                    UInt32 inBusNumber,
                                    UInt32 inNumberFrames,
                                    AudioBufferList* ioData) {
    auto* self = static_cast<AudioIO*>(inRefCon);
    float* out = static_cast<float*>(ioData->mBuffers[0].mData);

    if (!self->running_.load(std::memory_order_relaxed) || !self->playback_rb_) {
        std::memset(out, 0, inNumberFrames * sizeof(float));
        return noErr;
    }

    size_t read = self->playback_rb_->read(out, inNumberFrames);
    // Zero-fill if not enough data
    if (read < inNumberFrames) {
        std::memset(out + read, 0, (inNumberFrames - read) * sizeof(float));
    }

    // Track playback activity and RMS for barge-in detection
    if (read > 0) {
        float sum_sq = 0.0f;
        for (size_t i = 0; i < read; ++i) sum_sq += out[i] * out[i];
        float rms = std::sqrtf(sum_sq / (float)read);
        self->playback_rms_.store(rms, std::memory_order_relaxed);
        self->playback_active_.store(rms > 0.001f, std::memory_order_relaxed);
    } else {
        self->playback_rms_.store(0.0f, std::memory_order_relaxed);
        self->playback_active_.store(false, std::memory_order_relaxed);
    }

    return noErr;
}

bool AudioIO::init_core_audio() {
    OSStatus status;

    // --- Capture (input) audio unit ---
    AudioComponentDescription capture_desc = {};
    capture_desc.componentType = kAudioUnitType_Output;
    capture_desc.componentSubType = kAudioUnitSubType_HALOutput;
    capture_desc.componentManufacturer = kAudioUnitManufacturer_Apple;

    AudioComponent capture_comp = AudioComponentFindNext(nullptr, &capture_desc);
    if (!capture_comp) {
        LOG_ERROR("Audio", "No capture audio component found");
        return false;
    }

    status = AudioComponentInstanceNew(capture_comp, &capture_unit_);
    if (status != noErr) {
        LOG_ERROR("Audio", "Failed to create capture unit: %d", (int)status);
        return false;
    }

    UInt32 enableIO = 1;
    status = AudioUnitSetProperty(capture_unit_, kAudioOutputUnitProperty_EnableIO,
                         kAudioUnitScope_Input, 1, &enableIO, sizeof(enableIO));
    if (status != noErr) {
        LOG_ERROR("Audio", "Failed to enable input IO: %d (mic permission denied?)", (int)status);
        return false;
    }

    UInt32 disableIO = 0;
    AudioUnitSetProperty(capture_unit_, kAudioOutputUnitProperty_EnableIO,
                         kAudioUnitScope_Output, 0, &disableIO, sizeof(disableIO));

    // Look up the default input device and explicitly bind it to the capture unit.
    // HALOutput does not automatically route from the default input device.
    AudioObjectPropertyAddress devProp;
    devProp.mSelector = kAudioHardwarePropertyDefaultInputDevice;
    devProp.mScope    = kAudioObjectPropertyScopeGlobal;
    devProp.mElement  = kAudioObjectPropertyElementMain;
    AudioDeviceID inputDevId = 0;
    UInt32 devSz = sizeof(inputDevId);
    AudioObjectGetPropertyData(kAudioObjectSystemObject, &devProp, 0, nullptr, &devSz, &inputDevId);

    status = AudioUnitSetProperty(capture_unit_, kAudioOutputUnitProperty_CurrentDevice,
                         kAudioUnitScope_Global, 0, &inputDevId, sizeof(inputDevId));
    if (status != noErr) {
        LOG_ERROR("Audio", "Failed to set input device %u: %d", inputDevId, (int)status);
    }

    Float64 deviceRate = 48000.0;
    devProp.mSelector = kAudioDevicePropertyNominalSampleRate;
    devProp.mScope    = kAudioObjectPropertyScopeInput;
    devSz = sizeof(deviceRate);
    AudioObjectGetPropertyData(inputDevId, &devProp, 0, nullptr, &devSz, &deviceRate);

    device_capture_rate_ = (int)deviceRate;
    int hw_rate = device_capture_rate_;
    LOG_DEBUG("Audio", "Input device: %u, native rate: %dHz, target: %dHz",
            inputDevId, hw_rate, config_.capture_rate);

    AudioStreamBasicDescription fmt = {};
    fmt.mSampleRate       = hw_rate;
    fmt.mFormatID         = kAudioFormatLinearPCM;
    fmt.mFormatFlags      = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    fmt.mBitsPerChannel   = 32;
    fmt.mChannelsPerFrame = 1;
    fmt.mFramesPerPacket  = 1;
    fmt.mBytesPerFrame    = 4;
    fmt.mBytesPerPacket   = 4;

    status = AudioUnitSetProperty(capture_unit_, kAudioUnitProperty_StreamFormat,
                         kAudioUnitScope_Output, 1, &fmt, sizeof(fmt));
    if (status != noErr) {
        LOG_ERROR("Audio", "Failed to set capture format: %d", (int)status);
    }

    AURenderCallbackStruct cb = {};
    cb.inputProc = capture_callback;
    cb.inputProcRefCon = this;
    AudioUnitSetProperty(capture_unit_, kAudioOutputUnitProperty_SetInputCallback,
                         kAudioUnitScope_Global, 0, &cb, sizeof(cb));

    status = AudioUnitInitialize(capture_unit_);
    if (status != noErr) {
        LOG_ERROR("Audio", "Failed to initialize capture unit: %d", (int)status);
        return false;
    }

    // --- Playback (output) audio unit ---
    AudioComponentDescription play_desc = {};
    play_desc.componentType = kAudioUnitType_Output;
    play_desc.componentSubType = kAudioUnitSubType_DefaultOutput;
    play_desc.componentManufacturer = kAudioUnitManufacturer_Apple;

    AudioComponent play_comp = AudioComponentFindNext(nullptr, &play_desc);
    AudioComponentInstanceNew(play_comp, &playback_unit_);

    fmt.mSampleRate = config_.playback_rate;
    AudioUnitSetProperty(playback_unit_, kAudioUnitProperty_StreamFormat,
                         kAudioUnitScope_Input, 0, &fmt, sizeof(fmt));

    AURenderCallbackStruct render_cb = {};
    render_cb.inputProc = playback_callback;
    render_cb.inputProcRefCon = this;
    AudioUnitSetProperty(playback_unit_, kAudioUnitProperty_SetRenderCallback,
                         kAudioUnitScope_Input, 0, &render_cb, sizeof(render_cb));

    AudioUnitInitialize(playback_unit_);

    LOG_DEBUG("Audio", "CoreAudio initialized (capture=%dHz->%dHz, playback=%dHz)",
            device_capture_rate_, config_.capture_rate, config_.playback_rate);
    return true;
}

void AudioIO::shutdown_core_audio() {
    if (capture_unit_) {
        AudioComponentInstanceDispose(capture_unit_);
        capture_unit_ = nullptr;
    }
    if (playback_unit_) {
        AudioComponentInstanceDispose(playback_unit_);
        playback_unit_ = nullptr;
    }
}

#endif // __APPLE__ && !RASTACK_FILE_AUDIO_ONLY

} // namespace rastack
