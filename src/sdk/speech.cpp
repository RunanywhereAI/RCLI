#include "sdk/speech.h"

#include <cstring>

#include "audio/audio.h"

#include "model_types.pb.h"
#include "stt_options.pb.h"
#include "tts_options.pb.h"

#include "rac/core/rac_core.h"
#include "rac/core/rac_model_lifecycle.h"
#include "rac/features/stt/rac_stt_service.h"
#include "rac/features/tts/rac_tts_service.h"
#include "rac/foundation/rac_proto_buffer.h"

#include "catalog/catalog.h"
#include "sdk/install.h"
#include "sdk/llm.h"

namespace rcli::sdk {
namespace {

namespace v1 = runanywhere::v1;

/// Loading a speech model is the same lifecycle call as an LLM, minus the
/// placement settings — those describe where text generation runs and mean
/// nothing to a Sherpa graph.
bool LoadSpeech(const std::string& id, v1::ModelCategory category, std::string* error) {
    for (const catalog::Model& model : catalog::All()) {
        if (model.id == id && catalog::Installable(model)) {
            Register(model, nullptr);
            break;
        }
    }
    v1::ModelLoadRequest request;
    request.set_model_id(id);
    request.set_category(category);
    request.set_validate_availability(true);

    std::string bytes;
    if (!request.SerializeToString(&bytes)) {
        if (error != nullptr) {
            *error = "could not serialize the load request";
        }
        return false;
    }
    rac_proto_buffer_t out;
    rac_proto_buffer_init(&out);
    const rac_result_t rc = rac_model_lifecycle_load_proto(
        rac_get_model_registry(), reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(),
        &out);
    bool ok = rc == RAC_SUCCESS;
    std::string message = ok ? "" : "load failed (" + std::to_string(rc) + ")";
    if (ok) {
        v1::ModelLoadResult result;
        if (result.ParseFromArray(out.data, static_cast<int>(out.size)) && result.has_error()) {
            ok = false;
            message = result.error().message();
        }
    }
    rac_proto_buffer_free(&out);
    if (error != nullptr) {
        *error = message;
    }
    return ok;
}

/// The first local model whose directory sits under the framework that serves
/// this modality. Better than nothing and better than a hardcoded id, which
/// would name a model the user may never have downloaded.
std::string FirstLocal(std::string_view marker) {
    for (const LocalModel& model : LocalModels()) {
        if (model.id.find(marker) != std::string::npos) {
            return model.id;
        }
    }
    return {};
}

/// `AUDIO_FORMAT_PCM` says raw samples and not how wide they are, and the
/// engines disagree: Sherpa emits float32 under that name while the Android
/// path emits signed 16-bit. Reading it wrong halves or doubles the duration
/// and turns speech into noise, so the width is confirmed against the reported
/// duration rather than assumed.
std::vector<float> Decode(const v1::TTSOutput& output) {
    const std::string& bytes = output.audio_data();
    if (output.audio_format() == v1::AUDIO_FORMAT_PCM_S16LE) {
        return audio::FromPcm16(bytes);
    }
    if (output.duration_ms() > 0 && output.sample_rate() > 0) {
        const std::size_t expected = static_cast<std::size_t>(output.duration_ms()) *
                                     static_cast<std::size_t>(output.sample_rate()) / 1000;
        if (expected > 0 && bytes.size() / expected < 3) {
            return audio::FromPcm16(bytes);
        }
    }
    std::vector<float> samples(bytes.size() / sizeof(float));
    std::memcpy(samples.data(), bytes.data(), samples.size() * sizeof(float));
    return samples;
}

}  // namespace

std::string Speech::DefaultRecogniser() {
    const std::string whisper = FirstLocal("whisper");
    return whisper.empty() ? FirstLocal("asr") : whisper;
}

std::string Speech::DefaultVoice() {
    const std::string piper = FirstLocal("piper");
    return piper.empty() ? FirstLocal("tts") : piper;
}

bool Speech::LoadRecogniser(const std::string& model_id, std::string* error) {
    if (!LoadSpeech(model_id, v1::MODEL_CATEGORY_SPEECH_RECOGNITION, error)) {
        return false;
    }
    recogniser_ = model_id;
    return true;
}

bool Speech::LoadVoice(const std::string& model_id, std::string* error) {
    if (!LoadSpeech(model_id, v1::MODEL_CATEGORY_SPEECH_SYNTHESIS, error)) {
        return false;
    }
    voice_ = model_id;
    return true;
}

std::string Speech::Transcribe(const std::vector<float>& samples, int sample_rate,
                               std::string* error) {
    if (recogniser_.empty()) {
        if (error != nullptr) {
            *error = "no speech model loaded";
        }
        return {};
    }
    v1::STTTranscriptionRequest request;
    // Signed 16-bit, not float, and not because the encoding field says so.
    // Commons passes these bytes straight to the engine and only reads
    // `encoding` to estimate a duration; sherpa reads them as 16-bit whatever
    // the field claims, so float32 arrives as twice as many samples at the
    // wrong pitch and transcribes to nothing.
    v1::STTAudioSource* audio = request.mutable_audio();
    audio->set_audio_data(audio::ToPcm16(samples));
    audio->set_encoding(v1::AUDIO_ENCODING_PCM_S16_LE);
    audio->set_audio_format(v1::AUDIO_FORMAT_PCM_S16LE);
    audio->set_sample_rate(sample_rate);
    audio->set_channels(1);
    audio->set_duration_ms(sample_rate == 0
                               ? 0
                               : static_cast<std::int64_t>(samples.size()) * 1000 / sample_rate);

    std::string bytes;
    if (!request.SerializeToString(&bytes)) {
        if (error != nullptr) {
            *error = "could not serialize the audio";
        }
        return {};
    }
    rac_proto_buffer_t out;
    rac_proto_buffer_init(&out);
    const rac_result_t rc = rac_stt_transcribe_lifecycle_proto(
        reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(), &out);
    std::string text;
    if (rc == RAC_SUCCESS) {
        v1::STTOutput result;
        if (result.ParseFromArray(out.data, static_cast<int>(out.size))) {
            text = result.text();
        }
    }
    rac_proto_buffer_free(&out);
    if (text.empty() && error != nullptr) {
        *error = rc == RAC_SUCCESS ? "nothing was recognised"
                                   : "transcription failed (" + std::to_string(rc) + ")";
    }
    return text;
}

Speech::Raw Speech::SpeakRaw(const std::string& text, std::string* error) {
    Raw raw;
    v1::TTSSynthesisRequest request;
    request.set_text(text);
    std::string bytes;
    if (voice_.empty() || !request.SerializeToString(&bytes)) {
        if (error != nullptr) {
            *error = "no voice loaded";
        }
        return raw;
    }
    rac_proto_buffer_t out;
    rac_proto_buffer_init(&out);
    const rac_result_t rc = rac_tts_synthesize_lifecycle_proto(
        reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(), &out);
    if (rc == RAC_SUCCESS) {
        v1::TTSOutput result;
        if (result.ParseFromArray(out.data, static_cast<int>(out.size))) {
            raw.bytes = result.audio_data();
            raw.format = static_cast<int>(result.audio_format());
            raw.sample_rate = result.sample_rate();
            raw.duration_ms = result.duration_ms();
        }
    }
    rac_proto_buffer_free(&out);
    return raw;
}

Speech::Clip Speech::Speak(const std::string& text, std::string* error) {
    if (voice_.empty()) {
        if (error != nullptr) {
            *error = "no voice loaded";
        }
        return {};
    }
    v1::TTSSynthesisRequest request;
    request.set_text(text);

    std::string bytes;
    if (!request.SerializeToString(&bytes)) {
        if (error != nullptr) {
            *error = "could not serialize the text";
        }
        return {};
    }
    rac_proto_buffer_t out;
    rac_proto_buffer_init(&out);
    const rac_result_t rc = rac_tts_synthesize_lifecycle_proto(
        reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(), &out);
    Clip clip;
    std::string message;
    if (rc == RAC_SUCCESS) {
        v1::TTSOutput result;
        if (result.ParseFromArray(out.data, static_cast<int>(out.size))) {
            clip.sample_rate = result.sample_rate();
            clip.samples = Decode(result);
        } else {
            message = "the voice returned something unreadable";
        }
    } else {
        message = "synthesis failed (" + std::to_string(rc) + ")";
    }
    rac_proto_buffer_free(&out);
    if (clip.samples.empty() && message.empty()) {
        message = "the voice produced no audio";
    }
    if (error != nullptr) {
        *error = message;
    }
    return clip;
}

}  // namespace rcli::sdk
