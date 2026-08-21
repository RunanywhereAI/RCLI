#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "audio/audio.h"
#include "cli/commands.h"
#include "cli/output.h"
#include "sdk/speech.h"

namespace rcli::cli {
namespace {

/// A 16-bit mono WAV, chunk-walked rather than assuming a 44-byte header:
/// afconvert and friends write extra chunks before `data`.
std::vector<float> ReadWav(const std::string& path, int* rate, std::string* error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        *error = "cannot read " + path;
        return {};
    }
    const std::string bytes((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
    if (bytes.size() < 12 || std::memcmp(bytes.data(), "RIFF", 4) != 0) {
        *error = path + " is not a WAV";
        return {};
    }
    std::size_t offset = 12;
    std::size_t data_at = 0;
    std::size_t data_bytes = 0;
    while (offset + 8 <= bytes.size()) {
        std::uint32_t size = 0;
        std::memcpy(&size, bytes.data() + offset + 4, 4);
        if (std::memcmp(bytes.data() + offset, "fmt ", 4) == 0 && size >= 16) {
            std::uint32_t hz = 0;
            std::memcpy(&hz, bytes.data() + offset + 12, 4);
            *rate = static_cast<int>(hz);
        } else if (std::memcmp(bytes.data() + offset, "data", 4) == 0) {
            data_at = offset + 8;
            data_bytes = std::min<std::size_t>(size, bytes.size() - data_at);
            break;
        }
        offset += 8 + size + (size & 1U);
    }
    if (data_bytes == 0 || *rate <= 0) {
        *error = path + " has no readable audio";
        return {};
    }
    std::vector<float> samples(data_bytes / 2);
    for (std::size_t i = 0; i < samples.size(); ++i) {
        std::int16_t value = 0;
        std::memcpy(&value, bytes.data() + data_at + i * 2, 2);
        samples[i] = static_cast<float>(value) / 32768.0F;
    }
    return samples;
}

bool WriteWav(const std::string& path, const std::vector<float>& samples, int rate) {
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    std::vector<std::int16_t> pcm(samples.size());
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const float clamped = std::max(-1.0F, std::min(1.0F, samples[i]));
        pcm[i] = static_cast<std::int16_t>(clamped * 32767.0F);
    }
    const auto data_bytes = static_cast<std::uint32_t>(pcm.size() * 2);
    const auto hz = static_cast<std::uint32_t>(rate);
    auto u32 = [&file](std::uint32_t v) { file.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&file](std::uint16_t v) { file.write(reinterpret_cast<const char*>(&v), 2); };
    file.write("RIFF", 4);
    u32(36 + data_bytes);
    file.write("WAVEfmt ", 8);
    u32(16);
    u16(1);
    u16(1);
    u32(hz);
    u32(hz * 2);
    u16(2);
    u16(16);
    file.write("data", 4);
    u32(data_bytes);
    file.write(reinterpret_cast<const char*>(pcm.data()), data_bytes);
    return static_cast<bool>(file);
}

int Transcribe(const std::string& path, const std::string& model) {
    if (!Start()) {
        return 1;
    }
    sdk::Speech speech;
    const std::string id = model.empty() ? sdk::Speech::DefaultRecogniser() : model;
    std::string error;
    if (id.empty() || !speech.LoadRecogniser(id, &error)) {
        out::Error(id.empty() ? "no speech model downloaded — rcli pull sherpa-onnx-whisper-tiny.en"
                              : error);
        return 1;
    }
    int rate = 0;
    const std::vector<float> samples = ReadWav(path, &rate, &error);
    if (samples.empty()) {
        out::Error(error);
        return 1;
    }
    const std::string text = speech.Transcribe(samples, rate, &error);
    if (text.empty()) {
        out::Error(error);
        return 1;
    }
    out::Line(text);
    return 0;
}

int Synthesize(const std::string& text, const std::string& path, const std::string& voice) {
    if (!Start()) {
        return 1;
    }
    sdk::Speech speech;
    const std::string id = voice.empty() ? sdk::Speech::DefaultVoice() : voice;
    std::string error;
    if (id.empty() || !speech.LoadVoice(id, &error)) {
        out::Error(id.empty() ? "no voice downloaded — rcli pull vits-piper-en_US-lessac-medium"
                              : error);
        return 1;
    }
    sdk::Speech::Clip clip = speech.Speak(text, &error);
    if (clip.samples.empty()) {
        out::Error(error);
        return 1;
    }
    // No -o means play it, which is what someone typing `rcli tts "hello"`
    // wants; a path means write it and stay silent.
    if (path.empty()) {
        if (!audio::Available()) {
            out::Error("this build has no audio output — pass -o to write a file");
            return 1;
        }
        audio::Player player;
        const auto seconds =
            static_cast<std::int64_t>(clip.samples.size()) / std::max(1, clip.sample_rate);
        if (!player.Play(std::move(clip.samples), clip.sample_rate, &error)) {
            out::Error(error);
            return 1;
        }
        out::Progress progress("speaking");
        while (player.playing()) {
            progress.Tick(out::HumanDuration(seconds));
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
        }
        progress.Finish("");
        return 0;
    }
    if (!WriteWav(path, clip.samples, clip.sample_rate)) {
        out::Error("could not write " + path);
        return 1;
    }
    out::Line(path);
    return 0;
}

}  // namespace

void RegisterSpeech(CLI::App& app, Options& options) {
    auto stt_file = std::make_shared<std::string>();
    auto stt_model = std::make_shared<std::string>();
    auto* stt = app.add_subcommand("stt", "transcribe a WAV file");
    stt->add_option("file", *stt_file, "16-bit mono WAV")->required();
    stt->add_option("-m,--model", *stt_model, "speech model; the downloaded one by default");
    stt->callback([&options, stt_file, stt_model] {
        options.status = Transcribe(*stt_file, *stt_model);
    });

    auto text = std::make_shared<std::string>();
    auto tts_out = std::make_shared<std::string>();
    auto tts_voice = std::make_shared<std::string>();
    auto* tts = app.add_subcommand("tts", "speak text, or write it to a WAV");
    tts->add_option("text", *text, "what to say")->required();
    tts->add_option("-o,--output", *tts_out, "write a WAV here instead of playing it");
    tts->add_option("-m,--voice", *tts_voice, "voice model; the downloaded one by default");
    tts->callback([&options, text, tts_out, tts_voice] {
        options.status = Synthesize(*text, *tts_out, *tts_voice);
    });
}

}  // namespace rcli::cli
