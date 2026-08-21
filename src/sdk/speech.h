#ifndef RCLI_SDK_SPEECH_H
#define RCLI_SDK_SPEECH_H

#include <cstdint>
#include <string>
#include <vector>

namespace rcli::sdk {

/// Speech models are loaded and held separately from the language model: a
/// conversation uses all three at once, and unloading one to use another would
/// make every spoken turn a reload.
///
/// `model_id` may be a catalog id or a directory already on disk; both go
/// through the same lifecycle load as an LLM.
class Speech {
   public:
    bool LoadRecogniser(const std::string& model_id, std::string* error);
    bool LoadVoice(const std::string& model_id, std::string* error);

    bool has_recogniser() const { return !recogniser_.empty(); }
    bool has_voice() const { return !voice_.empty(); }
    const std::string& recogniser() const { return recogniser_; }
    const std::string& voice() const { return voice_; }

    /// Mono float samples at `sample_rate`. Empty on failure, with `error` set.
    std::string Transcribe(const std::vector<float>& samples, int sample_rate,
                           std::string* error);

    struct Clip {
        std::vector<float> samples;
        int sample_rate = 0;
    };
    /// Empty samples on failure, with `error` set.
    Clip Speak(const std::string& text, std::string* error);

    /// What the engine actually returned, before any interpretation. Only the
    /// probe uses it, and only to answer "what is in these bytes".
    struct Raw {
        std::string bytes;
        int format = 0;
        int sample_rate = 0;
        std::int64_t duration_ms = 0;
    };
    Raw SpeakRaw(const std::string& text, std::string* error);

    /// The first downloaded model of each kind, so a first run has something to
    /// use without the user naming one.
    static std::string DefaultRecogniser();
    static std::string DefaultVoice();

   private:
    std::string recogniser_;
    std::string voice_;
};

}  // namespace rcli::sdk

#endif  // RCLI_SDK_SPEECH_H
