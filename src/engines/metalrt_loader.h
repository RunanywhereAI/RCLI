#pragma once
// =============================================================================
// MetalRT Dynamic Loader — dlopen/dlsym bridge to libmetalrt.dylib
// =============================================================================
//
// Discovers, downloads, loads, and manages the closed-source MetalRT inference
// engine binary. RCLI never links against MetalRT at compile time — all access
// goes through resolved C function pointers.
//
// =============================================================================

#include <string>
#include <cstdint>
#include <mutex>
#include <dlfcn.h>

namespace rastack {

struct MetalRTResult {
    const char* text;
    const char* thinking;
    const char* response;
    int prompt_tokens;
    int generated_tokens;
    double prefill_ms;
    double decode_ms;
    double tps;
};

struct MetalRTOptions {
    int max_tokens;
    int top_k;
    float temperature;
    bool think;
    bool reset_cache;
    bool ignore_eos;
};

using MetalRTStreamCb = bool (*)(const char* piece, void* user_data);

struct MetalRTAudio {
    float* samples;
    int    num_samples;
    int    sample_rate;
    double synthesis_ms;
};

class MetalRTLoader {
public:
    static MetalRTLoader& instance() {
        static MetalRTLoader s;
        return s;
    }

    bool is_available() const;
    bool is_loaded() const { return handle_ != nullptr; }
    bool load();
    void unload();

    uint32_t abi_version() const {
        return fn_abi_version_ ? fn_abi_version_() : 0;
    }

    // --- C API function pointers (null if not loaded) ---

    using AbiVersionFn        = uint32_t (*)();
    using CreateFn            = void* (*)();
    using DestroyFn           = void (*)(void*);
    using LoadFn              = bool (*)(void*, const char*);
    using SetSystemPromptFn   = void (*)(void*, const char*);
    using GenerateFn          = MetalRTResult (*)(void*, const char*, const MetalRTOptions*);
    using GenerateStreamFn    = MetalRTResult (*)(void*, const char*, MetalRTStreamCb, void*, const MetalRTOptions*);
    using CachePromptFn       = void (*)(void*, const char*);
    using CountTokensFn       = int (*)(void*, const char*);
    using ContextSizeFn       = int (*)(void*);
    using ClearKvFn           = void (*)(void*);
    using ResetFn             = void (*)(void*);
    using ModelNameFn         = const char* (*)(void*);
    using DeviceNameFn        = const char* (*)(void*);
    using SupportsThinkingFn  = bool (*)(void*);
    using FreeResultFn        = void (*)(MetalRTResult);

    // LLM
    CreateFn           create            = nullptr;
    DestroyFn          destroy           = nullptr;
    LoadFn             load_model        = nullptr;
    SetSystemPromptFn  set_system_prompt = nullptr;
    GenerateFn         generate          = nullptr;
    GenerateStreamFn   generate_stream   = nullptr;
    GenerateFn         generate_raw      = nullptr;  // optional: raw pre-formatted prompt
    GenerateStreamFn   generate_raw_stream = nullptr;
    CachePromptFn      cache_prompt      = nullptr;  // optional: KV cache prefill
    GenerateFn         generate_raw_continue = nullptr;  // optional: generate from cached KV
    GenerateStreamFn   generate_raw_continue_stream = nullptr;
    CountTokensFn      count_tokens      = nullptr;  // optional: token counting
    ContextSizeFn      context_size      = nullptr;  // optional: max context window
    ClearKvFn          clear_kv          = nullptr;  // optional: lightweight KV wipe
    ResetFn            reset             = nullptr;
    ModelNameFn        model_name        = nullptr;
    DeviceNameFn       device_name       = nullptr;
    SupportsThinkingFn supports_thinking = nullptr;
    FreeResultFn       free_result       = nullptr;

    // --- Whisper STT function pointers ---

    using WhisperTranscribeFn = const char* (*)(void*, const float*, int, int);
    using WhisperFreeTextFn   = void (*)(const char*);
    using WhisperTimingFn     = double (*)(void*);

    CreateFn           whisper_create       = nullptr;
    DestroyFn          whisper_destroy      = nullptr;
    LoadFn             whisper_load         = nullptr;
    WhisperTranscribeFn whisper_transcribe  = nullptr;
    WhisperFreeTextFn  whisper_free_text    = nullptr;
    WhisperTimingFn    whisper_last_encode_ms = nullptr;
    WhisperTimingFn    whisper_last_decode_ms = nullptr;

    // --- Kokoro TTS function pointers ---

    using TtsSynthesizeFn = MetalRTAudio (*)(void*, const char*, const char*, float);
    using TtsFreeAudioFn  = void (*)(MetalRTAudio);
    using TtsSampleRateFn = int (*)(void*);

    CreateFn           tts_create       = nullptr;
    DestroyFn          tts_destroy      = nullptr;
    LoadFn             tts_load         = nullptr;
    TtsSynthesizeFn    tts_synthesize   = nullptr;
    TtsFreeAudioFn     tts_free_audio   = nullptr;
    TtsSampleRateFn    tts_sample_rate  = nullptr;

    // --- Install / remove / version management ---

    static bool install(const std::string& version = "latest");
    static bool remove();
    static std::string installed_version();
    static std::string dylib_path();
    static std::string engines_dir();
    static std::string local_repo_path();
    static bool is_local_mode();

    static constexpr uint32_t REQUIRED_ABI_VERSION = 2;

    // Serializes all dylib calls — Metal GPU cannot handle concurrent dispatch
    // from multiple threads. Lock this around generate/synthesize/transcribe.
    std::mutex& gpu_mutex() { return gpu_mutex_; }

private:
    MetalRTLoader() = default;
    ~MetalRTLoader() { unload(); }

    MetalRTLoader(const MetalRTLoader&) = delete;
    MetalRTLoader& operator=(const MetalRTLoader&) = delete;

    void* handle_ = nullptr;
    AbiVersionFn fn_abi_version_ = nullptr;
    std::mutex gpu_mutex_;

    template <typename T>
    T resolve(const char* name) {
        return reinterpret_cast<T>(dlsym(handle_, name));
    }
};

} // namespace rastack
