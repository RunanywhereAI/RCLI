#include "engines/vad_engine.h"
#include "core/log.h"
#include "sherpa-onnx/c-api/c-api.h"
#include <cstdio>
#include <cstring>

namespace rastack {

VadEngine::VadEngine() = default;

VadEngine::~VadEngine() {
    if (vad_) {
        SherpaOnnxDestroyVoiceActivityDetector(vad_);
        vad_ = nullptr;
    }
}

bool VadEngine::init(const VadConfig& config) {
    config_ = config;
    window_size_ = config.window_size;

    SherpaOnnxVadModelConfig vad_config;
    memset(&vad_config, 0, sizeof(vad_config));

    vad_config.silero_vad.model = config.model_path.c_str();
    vad_config.silero_vad.threshold = config.threshold;
    vad_config.silero_vad.min_silence_duration = config.min_silence_duration;
    vad_config.silero_vad.min_speech_duration = config.min_speech_duration;
    vad_config.silero_vad.max_speech_duration = config.max_speech_duration;
    vad_config.silero_vad.window_size = config.window_size;

    vad_config.sample_rate = config.sample_rate;
    vad_config.num_threads = config.num_threads;
    vad_config.provider = "cpu";
    vad_config.debug = 0;

    vad_ = SherpaOnnxCreateVoiceActivityDetector(&vad_config, 30.0f);
    if (!vad_) {
        LOG_ERROR("VAD", "Failed to create voice activity detector");
        return false;
    }

    initialized_ = true;
    LOG_DEBUG("VAD", "Initialized (threshold=%.2f, window=%d)",
            config.threshold, config.window_size);
    return true;
}

void VadEngine::feed_audio(const float* samples, int num_samples) {
    if (!initialized_) return;

    // Feed in window_size chunks as required by Silero VAD
    for (int i = 0; i + window_size_ <= num_samples; i += window_size_) {
        SherpaOnnxVoiceActivityDetectorAcceptWaveform(vad_, samples + i, window_size_);
    }
}

bool VadEngine::is_speech() const {
    if (!initialized_) return false;
    return SherpaOnnxVoiceActivityDetectorDetected(vad_) == 1;
}

bool VadEngine::has_segment() const {
    if (!initialized_) return false;
    return SherpaOnnxVoiceActivityDetectorEmpty(vad_) == 0;
}

SpeechSegment VadEngine::pop_segment() {
    SpeechSegment seg;
    if (!initialized_ || !has_segment()) return seg;

    const SherpaOnnxSpeechSegment* raw = SherpaOnnxVoiceActivityDetectorFront(vad_);
    if (raw) {
        seg.start_sample = raw->start;
        seg.samples.assign(raw->samples, raw->samples + raw->n);
        SherpaOnnxDestroySpeechSegment(raw);
    }
    SherpaOnnxVoiceActivityDetectorPop(vad_);
    return seg;
}

void VadEngine::reset() {
    if (!initialized_) return;
    SherpaOnnxVoiceActivityDetectorReset(vad_);
}

void VadEngine::flush() {
    if (!initialized_) return;
    SherpaOnnxVoiceActivityDetectorFlush(vad_);
}

std::vector<SpeechSegment> VadEngine::extract_speech(const float* samples, int num_samples) {
    std::vector<SpeechSegment> segments;
    if (!initialized_) return segments;

    reset();
    feed_audio(samples, num_samples);
    flush();

    while (has_segment()) {
        segments.push_back(pop_segment());
    }

    int total_speech_samples = 0;
    for (auto& s : segments) {
        total_speech_samples += (int)s.samples.size();
    }

    float total_ms = num_samples * 1000.0f / config_.sample_rate;
    float speech_ms = total_speech_samples * 1000.0f / config_.sample_rate;
    LOG_DEBUG("VAD", "Extracted %zu segment(s): %.0fms speech / %.0fms total (%.0f%% speech)",
            segments.size(), speech_ms, total_ms,
            total_ms > 0 ? (speech_ms / total_ms * 100) : 0);

    return segments;
}

} // namespace rastack
