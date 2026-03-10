#include "engines/stt_engine.h"
#include "core/log.h"
#include "sherpa-onnx/c-api/c-api.h"
#include <cstdio>
#include <cstring>

namespace rastack {

SttEngine::SttEngine() = default;

SttEngine::~SttEngine() {
    if (stream_) {
        SherpaOnnxDestroyOnlineStream(stream_);
        stream_ = nullptr;
    }
    if (recognizer_) {
        SherpaOnnxDestroyOnlineRecognizer(recognizer_);
        recognizer_ = nullptr;
    }
}

bool SttEngine::init(const SttConfig& config) {
    sample_rate_ = config.sample_rate;

    SherpaOnnxOnlineRecognizerConfig c;
    std::memset(&c, 0, sizeof(c));

    // Transducer model config (Zipformer)
    c.model_config.transducer.encoder = config.encoder_path.c_str();
    c.model_config.transducer.decoder = config.decoder_path.c_str();
    c.model_config.transducer.joiner  = config.joiner_path.c_str();
    c.model_config.tokens             = config.tokens_path.c_str();
    c.model_config.num_threads        = config.num_threads;
    c.model_config.provider           = config.provider.c_str();
    c.model_config.debug              = 0;

    c.feat_config.sample_rate         = config.sample_rate;
    c.feat_config.feature_dim         = 80;

    c.decoding_method                 = "greedy_search";
    c.max_active_paths                = 4;
    c.enable_endpoint                 = 1;
    c.rule1_min_trailing_silence      = 2.4f;
    c.rule2_min_trailing_silence      = 1.2f;
    c.rule3_min_utterance_length      = 20.0f;

    recognizer_ = SherpaOnnxCreateOnlineRecognizer(&c);
    if (!recognizer_) {
        LOG_ERROR("STT", "Failed to create recognizer");
        return false;
    }

    stream_ = SherpaOnnxCreateOnlineStream(recognizer_);
    if (!stream_) {
        LOG_ERROR("STT", "Failed to create stream");
        SherpaOnnxDestroyOnlineRecognizer(recognizer_);
        recognizer_ = nullptr;
        return false;
    }

    initialized_ = true;
    LOG_DEBUG("STT", "Initialized (sample_rate=%d, threads=%d)",
            config.sample_rate, config.num_threads);
    return true;
}

void SttEngine::feed_audio(const float* samples, int num_samples) {
    if (!initialized_ || !stream_) return;

    last_feed_time_us_ = now_us();
    SherpaOnnxOnlineStreamAcceptWaveform(stream_, sample_rate_, samples, num_samples);
    total_audio_samples_ += num_samples;
}

void SttEngine::feed_from_ring_buffer(RingBuffer<float>& rb, int max_samples) {
    // Read from ring buffer into a local buffer (stack-allocated for small chunks)
    float local_buf[4800]; // 300ms at 16kHz
    int to_read = std::min(max_samples, (int)sizeof(local_buf) / (int)sizeof(float));
    size_t got = rb.read(local_buf, to_read);
    if (got > 0) {
        feed_audio(local_buf, static_cast<int>(got));
    }
}

TextSegment SttEngine::get_result() {
    has_new_result_.store(false, std::memory_order_relaxed);

    TextSegment seg;
    seg.timestamp_us = now_us();
    seg.is_final = false;
    seg.confidence = 0.0f;

    if (!initialized_ || !stream_) {
        return seg;
    }

    // Return buffered endpoint result if process_tick() detected one.
    // The stream is already reset at this point, so we can't read from it.
    if (pending_final_) {
        seg.text = pending_final_text_;
        seg.is_final = true;
        seg.confidence = 1.0f;
        pending_final_ = false;
        pending_final_text_.clear();
        if (last_feed_time_us_ > 0) {
            last_latency_us_ = now_us() - last_feed_time_us_;
        }
        return seg;
    }

    const SherpaOnnxOnlineRecognizerResult* r =
        SherpaOnnxGetOnlineStreamResult(recognizer_, stream_);
    if (r && r->text && r->text[0] != '\0') {
        seg.text = r->text;
        seg.is_final = false;  // Non-endpoint partials only; endpoints go through pending_final_
        seg.confidence = 1.0f;
    }
    if (r) {
        SherpaOnnxDestroyOnlineRecognizerResult(r);
    }

    return seg;
}

bool SttEngine::has_result() const {
    return has_new_result_.load(std::memory_order_relaxed);
}

void SttEngine::reset() {
    if (!initialized_) return;

    if (stream_) {
        SherpaOnnxDestroyOnlineStream(stream_);
    }
    stream_ = SherpaOnnxCreateOnlineStream(recognizer_);
    last_text_.clear();
    pending_final_ = false;
    pending_final_text_.clear();
    has_new_result_.store(false, std::memory_order_relaxed);
}

void SttEngine::process_tick() {
    if (!initialized_ || !stream_) return;

    // Decode available frames
    int decoded = 0;
    while (SherpaOnnxIsOnlineStreamReady(recognizer_, stream_)) {
        SherpaOnnxDecodeOnlineStream(recognizer_, stream_);
        decoded++;
    }

    // Check for new text
    const SherpaOnnxOnlineRecognizerResult* r =
        SherpaOnnxGetOnlineStreamResult(recognizer_, stream_);
    if (r && r->text) {
        std::string text(r->text);
        if (text != last_text_ && !text.empty()) {
            last_text_ = text;
            has_new_result_.store(true, std::memory_order_release);

            bool is_endpoint = SherpaOnnxOnlineStreamIsEndpoint(recognizer_, stream_);

            if (callback_) {
                TextSegment seg;
                seg.text = text;
                seg.is_final = is_endpoint;
                seg.confidence = 1.0f;
                seg.timestamp_us = now_us();
                callback_(seg);
            }

            // Buffer endpoint result BEFORE resetting, so get_result()
            // can still retrieve the final text after the stream is reset.
            if (is_endpoint) {
                pending_final_text_ = text;
                pending_final_ = true;
                SherpaOnnxOnlineStreamReset(recognizer_, stream_);
                last_text_.clear();
            }
        }
        SherpaOnnxDestroyOnlineRecognizerResult(r);
    }
}

// --- Offline STT (Whisper or Parakeet/NeMo Transducer) ---

OfflineSttEngine::OfflineSttEngine() = default;

OfflineSttEngine::~OfflineSttEngine() {
    if (recognizer_) {
        SherpaOnnxDestroyOfflineRecognizer(recognizer_);
        recognizer_ = nullptr;
    }
}

bool OfflineSttEngine::init(const OfflineSttConfig& config) {
    sample_rate_ = config.sample_rate;
    backend_ = config.backend;

    SherpaOnnxOfflineRecognizerConfig c;
    std::memset(&c, 0, sizeof(c));

    if (config.backend == OfflineSttBackend::NEMO_TRANSDUCER) {
        c.model_config.transducer.encoder = config.transducer_encoder_path.c_str();
        c.model_config.transducer.decoder = config.transducer_decoder_path.c_str();
        c.model_config.transducer.joiner  = config.transducer_joiner_path.c_str();
        c.model_config.model_type = "nemo_transducer";
    } else {
        c.model_config.whisper.encoder      = config.encoder_path.c_str();
        c.model_config.whisper.decoder      = config.decoder_path.c_str();
        c.model_config.whisper.language     = config.language.c_str();
        c.model_config.whisper.task         = config.task.c_str();
        c.model_config.whisper.tail_paddings = config.tail_paddings;
    }

    c.model_config.tokens      = config.tokens_path.c_str();
    c.model_config.num_threads = config.num_threads;
    c.model_config.provider    = config.provider.c_str();
    c.model_config.debug       = 0;

    c.feat_config.sample_rate  = config.sample_rate;
    c.feat_config.feature_dim  = 80;

    c.decoding_method = "greedy_search";

    recognizer_ = SherpaOnnxCreateOfflineRecognizer(&c);
    if (!recognizer_) {
        LOG_ERROR("OfflineSTT", "Failed to create %s recognizer", backend_name());
        return false;
    }

    initialized_ = true;
    LOG_DEBUG("OfflineSTT", "Initialized %s (threads=%d)",
            backend_name(), config.num_threads);
    return true;
}

std::string OfflineSttEngine::transcribe(const float* samples, int num_samples) {
    if (!initialized_ || !recognizer_) return "";

    int64_t t_start = now_us();

    const SherpaOnnxOfflineStream* stream = SherpaOnnxCreateOfflineStream(recognizer_);
    if (!stream) return "";

    SherpaOnnxAcceptWaveformOffline(stream, sample_rate_, samples, num_samples);
    SherpaOnnxDecodeOfflineStream(recognizer_, stream);

    std::string result;
    const SherpaOnnxOfflineRecognizerResult* r = SherpaOnnxGetOfflineStreamResult(stream);
    if (r && r->text) {
        result = r->text;
    }
    if (r) {
        SherpaOnnxDestroyOfflineRecognizerResult(r);
    }

    SherpaOnnxDestroyOfflineStream(stream);

    last_latency_us_ = now_us() - t_start;
    return result;
}

} // namespace rastack
