#include "engines/tts_engine.h"
#include "core/log.h"
#include "sherpa-onnx/c-api/c-api.h"
#include <cstdio>
#include <cstring>

namespace rastack {

TtsEngine::TtsEngine() = default;

TtsEngine::~TtsEngine() {
    if (tts_) {
        SherpaOnnxDestroyOfflineTts(tts_);
        tts_ = nullptr;
    }
}

bool TtsEngine::init(const TtsConfig& config) {
    config_ = config;

    SherpaOnnxOfflineTtsConfig c;
    std::memset(&c, 0, sizeof(c));

    const std::string& arch = config.architecture;

    if (arch == "kokoro") {
        c.model.kokoro.model        = config.model_path.c_str();
        c.model.kokoro.voices       = config.voices_path.c_str();
        c.model.kokoro.tokens       = config.tokens_path.c_str();
        c.model.kokoro.data_dir     = config.data_dir.c_str();
        c.model.kokoro.length_scale = 1.0f / config.speed;
        if (!config.lexicon_path.empty())
            c.model.kokoro.lexicon  = config.lexicon_path.c_str();
        if (!config.lang.empty())
            c.model.kokoro.lang     = config.lang.c_str();
        LOG_DEBUG("TTS", "Using Kokoro architecture (lang=%s)", config.lang.c_str());
    } else if (arch == "kitten") {
        c.model.kitten.model        = config.model_path.c_str();
        c.model.kitten.voices       = config.voices_path.c_str();
        c.model.kitten.tokens       = config.tokens_path.c_str();
        c.model.kitten.data_dir     = config.data_dir.c_str();
        c.model.kitten.length_scale = 1.0f / config.speed;
        LOG_DEBUG("TTS", "Using Kitten architecture");
    } else if (arch == "matcha") {
        c.model.matcha.acoustic_model = config.model_path.c_str();
        c.model.matcha.vocoder        = config.vocoder_path.c_str();
        c.model.matcha.tokens         = config.tokens_path.c_str();
        c.model.matcha.data_dir       = config.data_dir.c_str();
        c.model.matcha.length_scale   = 1.0f / config.speed;
        LOG_DEBUG("TTS", "Using Matcha architecture");
    } else {
        c.model.vits.model         = config.model_path.c_str();
        c.model.vits.tokens        = config.tokens_path.c_str();
        c.model.vits.data_dir      = config.data_dir.c_str();
        c.model.vits.noise_scale   = 0.667f;
        c.model.vits.noise_scale_w = 0.8f;
        c.model.vits.length_scale  = 1.0f / config.speed;
        LOG_DEBUG("TTS", "Using VITS/Piper architecture");
    }

    c.model.num_threads  = config.num_threads;
    c.model.provider     = "cpu";
    c.model.debug        = 0;
    c.max_num_sentences   = 1;

    tts_ = SherpaOnnxCreateOfflineTts(&c);
    if (!tts_) {
        LOG_ERROR("TTS", "Failed to create TTS engine (arch=%s)", arch.c_str());
        return false;
    }

    sample_rate_ = SherpaOnnxOfflineTtsSampleRate(tts_);
    initialized_ = true;

    LOG_DEBUG("TTS", "Initialized (arch=%s, sample_rate=%d, threads=%d)",
            arch.c_str(), sample_rate_, config.num_threads);
    return true;
}

std::vector<float> TtsEngine::synthesize(const std::string& text) {
    if (!initialized_ || !tts_) return {};

    stats_ = TtsStats{};
    int64_t t_start = now_us();

    const SherpaOnnxGeneratedAudio* audio =
        SherpaOnnxOfflineTtsGenerate(tts_, text.c_str(), config_.speaker_id, config_.speed);

    if (!audio || audio->n <= 0) {
        LOG_ERROR("TTS", "Failed to synthesize: '%s'", text.c_str());
        if (audio) SherpaOnnxDestroyOfflineTtsGeneratedAudio(audio);
        return {};
    }

    stats_.synthesis_us = now_us() - t_start;
    stats_.num_samples = audio->n;

    // Copy to vector
    std::vector<float> samples(audio->samples, audio->samples + audio->n);

    LOG_DEBUG("TTS", "Synthesized %d samples (%.1fms audio, %.1fms compute, RTF=%.2f)",
            audio->n,
            (double)(audio->n * 1000.0 / sample_rate_),
            stats_.synthesis_us / 1000.0,
            stats_.real_time_factor());

    SherpaOnnxDestroyOfflineTtsGeneratedAudio(audio);
    return samples;
}

bool TtsEngine::synthesize_to_ring_buffer(const std::string& text, RingBuffer<float>& rb) {
    auto samples = synthesize(text);
    if (samples.empty()) return false;

    size_t written = rb.write(samples.data(), samples.size());
    if (written < samples.size()) {
        LOG_WARN("TTS", "Ring buffer full, dropped %zu samples",
                samples.size() - written);
    }
    return written > 0;
}

} // namespace rastack
