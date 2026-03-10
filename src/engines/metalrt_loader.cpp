#include "engines/metalrt_loader.h"
#include "core/log.h"
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <mach-o/dyld.h>

// =============================================================================
// LOCAL-FIRST CONFIGURATION
//
// Set METALRT_LOCAL_BUILD = true to skip GitHub downloads and instead copy
// the dylib + metallib from a local metalrt-binaries repo checkout.
//
// Set METALRT_LOCAL_REPO_PATH to the absolute path of your local
// metalrt-binaries repo. The installer will look for:
//   <repo>/build/libmetalrt.dylib
//   <repo>/build/default.metallib
//
// When false, the installer fetches release tarballs from GitHub as usual.
// =============================================================================
static constexpr bool METALRT_LOCAL_BUILD = false;
static const char*    METALRT_LOCAL_REPO_PATH = nullptr;  // e.g. "/Users/you/metalrt-binaries"

namespace rastack {

static std::string get_executable_dir() {
    char buf[4096];
    uint32_t sz = sizeof(buf);
    if (_NSGetExecutablePath(buf, &sz) != 0) return "";
    char resolved[PATH_MAX];
    if (!realpath(buf, resolved)) return "";
    std::string exe(resolved);
    auto slash = exe.rfind('/');
    return (slash != std::string::npos) ? exe.substr(0, slash) : "";
}

static std::string resolve_local_repo() {
    // 1. Explicit compile-time path
    if (METALRT_LOCAL_REPO_PATH && METALRT_LOCAL_REPO_PATH[0] != '\0')
        return METALRT_LOCAL_REPO_PATH;

    // 2. Environment variable override (always checked, even when flag is false)
    const char* env = getenv("METALRT_REPO");
    if (env && env[0] != '\0')
        return env;

    // 3. Relative to the running binary — walk up to find a sibling
    //    "metalrt-binaries" folder (works from build/ or install locations)
    struct stat st;
    std::string exe_dir = get_executable_dir();
    if (!exe_dir.empty()) {
        std::string dir = exe_dir;
        for (int depth = 0; depth < 4; depth++) {
            auto slash = dir.rfind('/');
            if (slash == std::string::npos) break;
            dir = dir.substr(0, slash);
            std::string candidate = dir + "/metalrt-binaries";
            if (stat(candidate.c_str(), &st) == 0 && S_ISDIR(st.st_mode))
                return candidate;
        }
    }

    return "";
}

std::string MetalRTLoader::engines_dir() {
    const char* home = getenv("HOME");
    if (!home) return "";
    return std::string(home) + "/Library/RCLI/engines";
}

std::string MetalRTLoader::dylib_path() {
    return engines_dir() + "/libmetalrt.dylib";
}

std::string MetalRTLoader::local_repo_path() {
    return resolve_local_repo();
}

bool MetalRTLoader::is_local_mode() {
    const char* env = getenv("METALRT_REPO");
    return METALRT_LOCAL_BUILD || (env && env[0] != '\0') || !resolve_local_repo().empty();
}

bool MetalRTLoader::is_available() const {
    struct stat st;
    return stat(dylib_path().c_str(), &st) == 0;
}

bool MetalRTLoader::load() {
    if (handle_) return true;

    std::string path = dylib_path();
    if (!is_available()) {
        LOG_ERROR("MetalRT", "dylib not found at %s", path.c_str());
        return false;
    }

    handle_ = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle_) {
        LOG_ERROR("MetalRT", "dlopen failed: %s", dlerror());
        return false;
    }

    LOG_DEBUG("MetalRT", "dlopen succeeded for %s, resolving symbols...", path.c_str());

    // LLM symbols
    fn_abi_version_    = resolve<AbiVersionFn>("metalrt_abi_version");
    create             = resolve<CreateFn>("metalrt_create");
    destroy            = resolve<DestroyFn>("metalrt_destroy");
    load_model         = resolve<LoadFn>("metalrt_load");
    set_system_prompt  = resolve<SetSystemPromptFn>("metalrt_set_system_prompt");
    generate           = resolve<GenerateFn>("metalrt_generate");
    generate_stream    = resolve<GenerateStreamFn>("metalrt_generate_stream");
    generate_raw       = resolve<GenerateFn>("metalrt_generate_raw");
    generate_raw_stream = resolve<GenerateStreamFn>("metalrt_generate_raw_stream");
    cache_prompt       = resolve<CachePromptFn>("metalrt_cache_prompt");
    generate_raw_continue = resolve<GenerateFn>("metalrt_generate_raw_continue");
    generate_raw_continue_stream = resolve<GenerateStreamFn>("metalrt_generate_raw_continue_stream");
    count_tokens       = resolve<CountTokensFn>("metalrt_count_tokens");
    context_size       = resolve<ContextSizeFn>("metalrt_context_size");
    clear_kv           = resolve<ClearKvFn>("metalrt_clear_kv");
    reset              = resolve<ResetFn>("metalrt_reset");
    model_name         = resolve<ModelNameFn>("metalrt_model_name");
    device_name        = resolve<DeviceNameFn>("metalrt_device_name");
    supports_thinking  = resolve<SupportsThinkingFn>("metalrt_supports_thinking");
    free_result        = resolve<FreeResultFn>("metalrt_free_result");

    LOG_DEBUG("MetalRT", "LLM symbols: create=%p generate=%p stream=%p raw=%p cache=%p continue=%p count_tok=%p ctx_size=%p clear_kv=%p",
              (void*)create, (void*)generate, (void*)generate_stream,
              (void*)generate_raw, (void*)cache_prompt, (void*)generate_raw_continue,
              (void*)count_tokens, (void*)context_size, (void*)clear_kv);

    // Whisper STT symbols (optional — not all builds include these)
    whisper_create       = resolve<CreateFn>("metalrt_whisper_create");
    whisper_destroy      = resolve<DestroyFn>("metalrt_whisper_destroy");
    whisper_load         = resolve<LoadFn>("metalrt_whisper_load");
    whisper_transcribe   = resolve<WhisperTranscribeFn>("metalrt_whisper_transcribe");
    whisper_free_text    = resolve<WhisperFreeTextFn>("metalrt_whisper_free_text");
    whisper_last_encode_ms = resolve<WhisperTimingFn>("metalrt_whisper_last_encode_ms");
    whisper_last_decode_ms = resolve<WhisperTimingFn>("metalrt_whisper_last_decode_ms");

    LOG_DEBUG("MetalRT", "STT symbols: whisper_create=%p whisper_transcribe=%p encode_ms=%p decode_ms=%p",
              (void*)whisper_create, (void*)whisper_transcribe,
              (void*)whisper_last_encode_ms, (void*)whisper_last_decode_ms);

    // Kokoro TTS symbols (optional)
    tts_create       = resolve<CreateFn>("metalrt_tts_create");
    tts_destroy      = resolve<DestroyFn>("metalrt_tts_destroy");
    tts_load         = resolve<LoadFn>("metalrt_tts_load");
    tts_synthesize   = resolve<TtsSynthesizeFn>("metalrt_tts_synthesize");
    tts_free_audio   = resolve<TtsFreeAudioFn>("metalrt_tts_free_audio");
    tts_sample_rate  = resolve<TtsSampleRateFn>("metalrt_tts_sample_rate");

    LOG_DEBUG("MetalRT", "TTS symbols: tts_create=%p tts_synthesize=%p tts_sample_rate=%p",
              (void*)tts_create, (void*)tts_synthesize, (void*)tts_sample_rate);

    if (!fn_abi_version_ || !create || !destroy || !load_model || !generate) {
        LOG_ERROR("MetalRT", "dylib missing required LLM symbols: abi=%p create=%p destroy=%p load=%p gen=%p",
                  (void*)fn_abi_version_, (void*)create, (void*)destroy, (void*)load_model, (void*)generate);
        unload();
        return false;
    }

    uint32_t ver = fn_abi_version_();
    if (ver != REQUIRED_ABI_VERSION) {
        LOG_ERROR("MetalRT", "ABI mismatch: got %u, need %u", ver, REQUIRED_ABI_VERSION);
        unload();
        return false;
    }

    LOG_DEBUG("MetalRT", "loaded successfully from %s (ABI v%u) — all GPU dispatch via libmetalrt.dylib", path.c_str(), ver);
    return true;
}

void MetalRTLoader::unload() {
    if (handle_) {
        dlclose(handle_);
        handle_ = nullptr;
    }
    fn_abi_version_    = nullptr;
    create             = nullptr;
    destroy            = nullptr;
    load_model         = nullptr;
    set_system_prompt  = nullptr;
    generate           = nullptr;
    generate_stream    = nullptr;
    generate_raw       = nullptr;
    generate_raw_stream = nullptr;
    cache_prompt       = nullptr;
    generate_raw_continue = nullptr;
    generate_raw_continue_stream = nullptr;
    count_tokens       = nullptr;
    context_size       = nullptr;
    clear_kv           = nullptr;
    reset              = nullptr;
    model_name         = nullptr;
    device_name        = nullptr;
    supports_thinking  = nullptr;
    free_result        = nullptr;

    whisper_create       = nullptr;
    whisper_destroy      = nullptr;
    whisper_load         = nullptr;
    whisper_transcribe   = nullptr;
    whisper_free_text    = nullptr;
    whisper_last_encode_ms = nullptr;
    whisper_last_decode_ms = nullptr;

    tts_create       = nullptr;
    tts_destroy      = nullptr;
    tts_load         = nullptr;
    tts_synthesize   = nullptr;
    tts_free_audio   = nullptr;
    tts_sample_rate  = nullptr;
}

std::string MetalRTLoader::installed_version() {
    std::string ver_path = engines_dir() + "/VERSION";
    FILE* f = fopen(ver_path.c_str(), "r");
    if (!f) return "";
    char buf[64] = {};
    if (fgets(buf, sizeof(buf), f)) {
        std::string v(buf);
        while (!v.empty() && (v.back() == '\n' || v.back() == '\r'))
            v.pop_back();
        fclose(f);
        return v;
    }
    fclose(f);
    return "";
}

static bool install_from_local(const std::string& edir) {
    std::string repo = resolve_local_repo();
    if (repo.empty()) {
        fprintf(stderr, "  Local install requested but no repo found.\n");
        fprintf(stderr, "  Set METALRT_REPO env var, METALRT_LOCAL_REPO_PATH in metalrt_loader.cpp,\n");
        fprintf(stderr, "  or clone metalrt-binaries next to RCLI-MetalRT.\n");
        return false;
    }

    // Search order: build/ dir first, then release/ dir, then repo root
    std::string search_dirs[] = {
        repo + "/build",
        repo + "/release",
        repo,
    };

    std::string dylib_src;
    for (auto& dir : search_dirs) {
        std::string candidate = dir + "/libmetalrt.dylib";
        struct stat st;
        if (stat(candidate.c_str(), &st) == 0) {
            dylib_src = candidate;
            break;
        }
    }

    if (dylib_src.empty()) {
        fprintf(stderr, "  libmetalrt.dylib not found in local repo: %s\n", repo.c_str());
        fprintf(stderr, "  Searched: build/, release/, and repo root.\n");
        return false;
    }

    fprintf(stderr, "  Source (local): %s\n", dylib_src.c_str());
    fprintf(stderr, "  Target:         %s/\n", edir.c_str());

    std::string cp_cmd = "cp '" + dylib_src + "' '" + edir + "/libmetalrt.dylib'";
    if (system(cp_cmd.c_str()) != 0) {
        fprintf(stderr, "  Failed to copy libmetalrt.dylib\n");
        return false;
    }

    // Copy default.metallib if present alongside the dylib
    std::string metallib_src = dylib_src.substr(0, dylib_src.rfind('/')) + "/default.metallib";
    struct stat mst;
    if (stat(metallib_src.c_str(), &mst) == 0) {
        std::string cp2 = "cp '" + metallib_src + "' '" + edir + "/default.metallib'";
        system(cp2.c_str());
        fprintf(stderr, "  Copied default.metallib\n");
    }

    // Read version from VERSION file in repo, or mark as "local"
    std::string ver = "local";
    std::string ver_file = repo + "/LATEST_VERSION";
    FILE* vf = fopen(ver_file.c_str(), "r");
    if (vf) {
        char buf[64] = {};
        if (fgets(buf, sizeof(buf), vf)) {
            ver = buf;
            while (!ver.empty() && (ver.back() == '\n' || ver.back() == '\r'))
                ver.pop_back();
        }
        fclose(vf);
    }

    std::string ver_cmd = "echo '" + ver + "-local' > '" + edir + "/VERSION'";
    system(ver_cmd.c_str());

    fprintf(stderr, "  Version: %s-local\n", ver.c_str());

    // Re-sign since we copied the binary
    std::string sign_cmd = "codesign --force --sign - '" + edir + "/libmetalrt.dylib' 2>/dev/null";
    if (system(sign_cmd.c_str()) != 0) {
        fprintf(stderr, "  Warning: ad-hoc codesign failed\n");
    }

    fprintf(stderr, "  Installed from local repo!\n");
    return true;
}

static bool install_from_remote(const std::string& edir, const std::string& version) {
    std::string ver = version;
    if (ver == "latest") {
        std::string fetch_cmd =
            "curl -fsSL https://raw.githubusercontent.com/RunanywhereAI/metalrt-binaries/main/LATEST_VERSION 2>/dev/null";
        FILE* p = popen(fetch_cmd.c_str(), "r");
        if (!p) return false;
        char buf[64] = {};
        if (fgets(buf, sizeof(buf), p)) {
            ver = buf;
            while (!ver.empty() && (ver.back() == '\n' || ver.back() == '\r'))
                ver.pop_back();
        }
        pclose(p);
        if (ver.empty() || ver == "v0.0.0") return false;
    }

    std::string tarball = "metalrt-" + ver + "-macos-arm64.tar.gz";
    std::string url =
        "https://github.com/RunanywhereAI/metalrt-binaries/releases/download/" +
        ver + "/" + tarball;

    fprintf(stderr, "  Source (remote): %s\n", url.c_str());

    std::string extractdir = "metalrt-" + ver + "-macos-arm64";
    std::string cmd =
        "set -e; cd /tmp; "
        "curl -fL -# -o '" + tarball + "' '" + url + "'; "
        "tar xzf '" + tarball + "'; "
        "cp " + extractdir + "/libmetalrt.dylib '" + edir + "/'; "
        "cp " + extractdir + "/default.metallib '" + edir + "/' 2>/dev/null || true; "
        "cp " + extractdir + "/metalrt_c_api.h '" + edir + "/' 2>/dev/null || true; "
        "xattr -rd com.apple.quarantine '" + edir + "/libmetalrt.dylib' 2>/dev/null || true; "
        "rm -rf " + extractdir + " '" + tarball + "'; "
        "echo '" + ver + "' > '" + edir + "/VERSION'";

    fprintf(stderr, "  Downloading MetalRT %s ...\n", ver.c_str());
    int rc = system(("bash -c '" + cmd + "'").c_str());
    if (rc != 0) return false;

    std::string verify_cmd = "codesign --verify --deep --strict '" +
                             edir + "/libmetalrt.dylib" + "' 2>/dev/null";
    if (system(verify_cmd.c_str()) != 0) {
        fprintf(stderr, "  Warning: MetalRT binary code signature verification failed\n");
    }

    return true;
}

bool MetalRTLoader::install(const std::string& version) {
    std::string edir = engines_dir();
    std::string mkdir_cmd = "mkdir -p '" + edir + "'";
    if (system(mkdir_cmd.c_str()) != 0) return false;

    // Check environment override first (always honored regardless of compile flag)
    const char* env_repo = getenv("METALRT_REPO");
    bool use_local = METALRT_LOCAL_BUILD || (env_repo && env_repo[0] != '\0');

    if (use_local) {
        fprintf(stderr, "  Mode: LOCAL (copying from local repo)\n");
        return install_from_local(edir);
    } else {
        fprintf(stderr, "  Mode: REMOTE (downloading from GitHub)\n");
        return install_from_remote(edir, version);
    }
}

bool MetalRTLoader::remove() {
    std::string edir = engines_dir();
    std::string cmd = "rm -f '" + edir + "/libmetalrt.dylib' '" + edir + "/VERSION'";
    return system(cmd.c_str()) == 0;
}

} // namespace rastack
