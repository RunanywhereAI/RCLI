// Isolation probe, not shipped. Exercises one SDK path with no UI so a failure
// can be attributed to the SDK or to the app.
//
//   probe generate <model.gguf>   load a local model and stream a reply
//   probe install  <catalog-id>   register and download from the catalog
//   probe pull     <catalog-id>   download through the orchestrator, with progress
//   probe say      <text>         synthesize and write /tmp/rcli-say.wav
//   probe hear     <file.wav>     transcribe a 16-bit mono WAV
//   probe play     <file.wav>     play it through the app's own audio path
//   probe listen   <seconds>      record from the microphone and transcribe
#include <cstdio>
#include <cstring>
#include <chrono>
#include <string>
#include <thread>

#include "catalog/catalog.h"
#include "audio/audio.h"
#include "sdk/download.h"
#include "sdk/install.h"
#include "sdk/session.h"
#include "sdk/speech.h"
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <fstream>
#include <vector>

#include "rac/features/llm/rac_llm_component.h"
#include "rac/features/llm/rac_llm_types.h"

static rac_bool_t on_token(const char* token, void*) {
    std::fputs(token ? token : "", stdout);
    std::fflush(stdout);
    return RAC_TRUE;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: probe generate <model.gguf> | probe install|pull <id>\n");
        return 2;
    }
    auto& session = rcli::sdk::Session::Instance();
    if (!session.Start()) {
        std::fprintf(stderr, "session failed: %s\n", std::string(session.error()).c_str());
        return 1;
    }
    std::printf("engines: %zu   home: %s\n", session.backends().size(),
                std::string(session.home()).c_str());

    if (std::strcmp(argv[1], "listen") == 0) {
        rcli::audio::Recorder recorder;
        std::string error;
        if (!recorder.Start(&error)) {
            std::fprintf(stderr, "%s\n", error.c_str());
            return 1;
        }
        const int seconds = std::atoi(argv[2]);
        for (int i = 0; i < seconds * 5; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            std::printf("\r  level %.4f  ", static_cast<double>(recorder.level()));
            std::fflush(stdout);
        }
        const std::vector<float> samples = recorder.Stop();
        float peak = 0.0F;
        for (const float sample : samples) {
            const float magnitude = sample < 0 ? -sample : sample;
            peak = magnitude > peak ? magnitude : peak;
        }
        std::printf("\ncaptured %zu samples (%.2fs) peak %.4f\n", samples.size(),
                    static_cast<double>(samples.size()) / rcli::audio::kCaptureRate, peak);
        if (samples.empty()) {
            return 1;
        }
        rcli::sdk::Speech speech;
        const std::string model = rcli::sdk::Speech::DefaultRecogniser();
        if (model.empty() || !speech.LoadRecogniser(model, &error)) {
            std::fprintf(stderr, "no recogniser: %s\n", error.c_str());
            return 1;
        }
        const std::string text = speech.Transcribe(samples, rcli::audio::kCaptureRate, &error);
        std::printf("heard: %s\n", text.empty() ? ("(" + error + ")").c_str() : text.c_str());
        return 0;
    }

    if (std::strcmp(argv[1], "play") == 0 || std::strcmp(argv[1], "hear") == 0) {
        const bool play_only = std::strcmp(argv[1], "play") == 0;
        std::ifstream wav(argv[2], std::ios::binary);
        if (!wav) {
            std::fprintf(stderr, "cannot read %s\n", argv[2]);
            return 1;
        }
        std::string all((std::istreambuf_iterator<char>(wav)), std::istreambuf_iterator<char>());
        if (all.size() < 44) {
            std::fprintf(stderr, "not a WAV\n");
            return 1;
        }
        std::uint32_t rate = 0;
        // Walk the chunks: a 44-byte header is only the simplest case, and
        // afconvert writes extra ones before `data`.
        std::size_t offset = 12;
        std::size_t data_bytes = 0;
        while (offset + 8 <= all.size()) {
            std::uint32_t size = 0;
            std::memcpy(&size, all.data() + offset + 4, 4);
            if (std::memcmp(all.data() + offset, "fmt ", 4) == 0 && size >= 16) {
                std::memcpy(&rate, all.data() + offset + 8 + 4, 4);
            }
            if (std::memcmp(all.data() + offset, "data", 4) == 0) {
                offset += 8;
                data_bytes = std::min<std::size_t>(size, all.size() - offset);
                break;
            }
            offset += 8 + size + (size & 1U);
        }
        if (data_bytes == 0) {
            std::fprintf(stderr, "no data chunk\n");
            return 1;
        }
        const std::size_t count = data_bytes / 2;
        std::vector<float> samples(count);
        for (std::size_t i = 0; i < count; ++i) {
            std::int16_t value = 0;
            std::memcpy(&value, all.data() + offset + i * 2, 2);
            samples[i] = static_cast<float>(value) / 32768.0F;
        }
        if (play_only) {
            rcli::audio::Player player;
            std::string play_error;
            const auto begin = std::chrono::steady_clock::now();
            if (!player.Play(samples, static_cast<int>(rate), &play_error)) {
                std::fprintf(stderr, "play failed: %s\n", play_error.c_str());
                return 1;
            }
            std::printf("playing %.2fs at %u Hz...\n",
                        static_cast<double>(count) / rate, rate);
            while (player.playing()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            const double elapsed = std::chrono::duration<double>(
                                       std::chrono::steady_clock::now() - begin).count();
            std::printf("finished after %.2fs (expected %.2fs)\n", elapsed,
                        static_cast<double>(count) / rate);
            return 0;
        }

        rcli::sdk::Speech speech;
        const std::string model = rcli::sdk::Speech::DefaultRecogniser();
        std::string error;
        if (model.empty() || !speech.LoadRecogniser(model, &error)) {
            std::fprintf(stderr, "no recogniser: %s\n", error.c_str());
            return 1;
        }
        std::printf("recogniser: %s   %zu samples at %u Hz\n", model.c_str(), count, rate);
        const std::string text = speech.Transcribe(samples, static_cast<int>(rate), &error);
        if (text.empty()) {
            std::fprintf(stderr, "no transcript: %s\n", error.c_str());
            return 1;
        }
        std::printf("heard: %s\n", text.c_str());
        return 0;
    }

    if (std::strcmp(argv[1], "say") == 0) {
        rcli::sdk::Speech speech;
        const std::string voice = rcli::sdk::Speech::DefaultVoice();
        std::string error;
        if (voice.empty() || !speech.LoadVoice(voice, &error)) {
            std::fprintf(stderr, "no voice: %s\n", error.c_str());
            return 1;
        }
        std::printf("voice: %s\n", voice.c_str());
        rcli::sdk::Speech::Raw raw = speech.SpeakRaw(argv[2], &error);
        std::printf("format: %d  rate: %d  duration_ms: %lld  bytes: %zu\n", raw.format,
                    raw.sample_rate, static_cast<long long>(raw.duration_ms), raw.bytes.size());
        std::printf("head:");
        for (std::size_t i = 0; i < 24 && i < raw.bytes.size(); ++i) {
            std::printf(" %02x", static_cast<unsigned char>(raw.bytes[i]));
        }
        std::printf("\n");
        const rcli::sdk::Speech::Clip clip = speech.Speak(argv[2], &error);
        if (clip.samples.empty()) {
            std::fprintf(stderr, "synthesis failed: %s\n", error.c_str());
            return 1;
        }
        float peak = 0.0F;
        for (const float sample : clip.samples) {
            peak = sample > peak ? sample : (-sample > peak ? -sample : peak);
        }
        std::printf("samples: %zu  rate: %d  seconds: %.2f  peak: %.3f\n", clip.samples.size(),
                    clip.sample_rate,
                    static_cast<double>(clip.samples.size()) / clip.sample_rate, peak);

        std::vector<std::int16_t> pcm(clip.samples.size());
        for (std::size_t i = 0; i < pcm.size(); ++i) {
            pcm[i] = static_cast<std::int16_t>(clip.samples[i] * 32767.0F);
        }
        const std::uint32_t data_bytes = static_cast<std::uint32_t>(pcm.size() * 2);
        const std::uint32_t rate = static_cast<std::uint32_t>(clip.sample_rate);
        std::ofstream wav("/tmp/rcli-say.wav", std::ios::binary);
        auto u32 = [&wav](std::uint32_t v) { wav.write(reinterpret_cast<const char*>(&v), 4); };
        auto u16 = [&wav](std::uint16_t v) { wav.write(reinterpret_cast<const char*>(&v), 2); };
        wav.write("RIFF", 4); u32(36 + data_bytes); wav.write("WAVEfmt ", 8);
        u32(16); u16(1); u16(1); u32(rate); u32(rate * 2); u16(2); u16(16);
        wav.write("data", 4); u32(data_bytes);
        wav.write(reinterpret_cast<const char*>(pcm.data()), data_bytes);
        std::printf("wrote /tmp/rcli-say.wav\n");
        return 0;
    }

    if (std::strcmp(argv[1], "pull") == 0) {
        for (const rcli::catalog::Model& model : rcli::catalog::All()) {
            if (model.id != argv[2]) {
                continue;
            }
            auto& downloads = rcli::sdk::Downloads::Instance();
            std::string error;
            if (!downloads.Start(model, &error)) {
                std::fprintf(stderr, "start failed: %s\n", error.c_str());
                return 1;
            }
            int last = -1;
            while (downloads.Busy()) {
                const rcli::sdk::Download state = downloads.Get(argv[2]);
                const int percent = static_cast<int>(state.fraction * 100.0F);
                if (percent != last) {
                    std::printf("\r  %3d%%", percent);
                    std::fflush(stdout);
                    last = percent;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
            const rcli::sdk::Download state = downloads.Get(argv[2]);
            const bool ok = state.phase == rcli::sdk::Phase::Done;
            std::printf("\npull %s %s\n", ok ? "OK" : "FAILED", state.detail.c_str());
            return ok ? 0 : 1;
        }
        std::fprintf(stderr, "no catalog entry called %s\n", argv[2]);
        return 1;
    }

    if (std::strcmp(argv[1], "install") == 0) {
        for (const rcli::catalog::Model& model : rcli::catalog::All()) {
            if (model.id == argv[2]) {
                std::string error;
                const bool ok = rcli::sdk::Install(
                    model,
                    [](rcli::sdk::Progress p) {
                        std::printf("  [%s %d%%]\n", p.stage.c_str(), p.percent);
                        std::fflush(stdout);
                    },
                    &error);
                std::printf("install %s: %s\n", ok ? "OK" : "FAILED", error.c_str());
                return ok ? 0 : 1;
            }
        }
        std::fprintf(stderr, "no catalog entry called %s\n", argv[2]);
        return 1;
    }

    rac_handle_t llm = nullptr;
    rac_llm_component_create(&llm);
    std::printf("load: %d\n", rac_llm_component_load_model(llm, argv[2], "probe", nullptr));
    rac_llm_options_t options = RAC_LLM_OPTIONS_DEFAULT;
    std::printf("--- generating ---\n");
    rac_llm_component_generate_stream(llm, "say hi", &options, on_token, nullptr, nullptr, nullptr);
    std::printf("\n");
    return 0;
}
