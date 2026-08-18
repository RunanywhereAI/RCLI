#include "sdk/session.h"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <set>

#include "rac/core/rac_core.h"
#include "rac/infrastructure/model_management/rac_model_paths.h"
#include "rac/desktop/rac_desktop.h"
#include "rac/plugin/rac_plugin_entry.h"
#include "rac/plugin/rac_primitive.h"

#if defined(RCLI_HAS_LLAMACPP)
#include "rac/backends/rac_llm_llamacpp.h"
extern "C" rac_result_t rac_backend_llamacpp_register(void);
#endif
#if defined(RCLI_HAS_SHERPA)
extern "C" rac_result_t rac_backend_sherpa_register(void);
#endif
#if defined(RCLI_HAS_ONNX)
extern "C" rac_result_t rac_backend_onnx_register(void);
#endif
#if defined(RCLI_HAS_CLOUD)
extern "C" rac_result_t rac_backend_cloud_register(void);
#endif

namespace rcli::sdk {
namespace {

constexpr rac_primitive_t kPrimitives[] = {
    RAC_PRIMITIVE_GENERATE_TEXT, RAC_PRIMITIVE_TRANSCRIBE, RAC_PRIMITIVE_SYNTHESIZE,
    RAC_PRIMITIVE_DETECT_VOICE,  RAC_PRIMITIVE_EMBED,      RAC_PRIMITIVE_VLM,
    RAC_PRIMITIVE_DIARIZE,       RAC_PRIMITIVE_SEGMENT,
};

std::string ResolveHome() {
    if (const char* override_home = std::getenv("RUNANYWHERE_HOME")) {
        if (override_home[0] != '\0') {
            return override_home;
        }
    }
    char path[1024] = {};
    if (rac_desktop_default_base_dir(path, sizeof(path)) == RAC_SUCCESS) {
        return path;
    }
    return {};
}

}  // namespace

Session& Session::Instance() {
    static Session session;
    return session;
}

bool Session::Start() {
    if (started_) {
        return true;
    }

    home_ = ResolveHome();
    if (home_.empty()) {
        error_ = "could not resolve a storage directory";
        return false;
    }
    // Before rac_init: the registry reads the base dir while it discovers what
    // is already downloaded.
    rac_model_paths_set_base_dir(home_.c_str());

    // MUST outlive this function. rac_init stores the POINTER, and commons
    // calls back through it for every file read, log line and secure-store
    // access for the life of the process. As a local it went out of scope the
    // moment Start() returned, and the first callback after that jumped through
    // freed stack memory — an EXC_BAD_ACCESS at address 0x1.
    static rac_platform_adapter_t adapter{};
    if (rac_desktop_adapter_init(nullptr, &adapter) != RAC_SUCCESS) {
        error_ = "desktop platform adapter failed to initialise";
        return false;
    }

    static rac_config_t config{};
    config.platform_adapter = &adapter;
    config.log_level = RAC_LOG_ERROR;  // a TUI owns the screen; logs would corrupt it
    if (rac_init(&config) != RAC_SUCCESS) {
        error_ = "rac_init failed";
        return false;
    }
    rac_desktop_http_transport_register();

    // Each engine is optional: a build without one, or a machine that cannot
    // serve it, simply contributes nothing to the list.
#if defined(RCLI_HAS_LLAMACPP)
    rac_backend_llamacpp_register();
#endif
#if defined(RCLI_HAS_SHERPA)
    rac_backend_sherpa_register();
#endif
#if defined(RCLI_HAS_ONNX)
    rac_backend_onnx_register();
#endif
#if defined(RCLI_HAS_CLOUD)
    rac_backend_cloud_register();
#endif

    std::map<std::string, BackendInfo> found;
    for (const rac_primitive_t primitive : kPrimitives) {
        const rac_engine_vtable_t* plugins[16] = {};
        std::size_t count = 0;
        if (rac_plugin_list(primitive, plugins, 16, &count) != RAC_SUCCESS) {
            continue;
        }
        for (std::size_t i = 0; i < count; ++i) {
            const rac_engine_metadata_t& meta = plugins[i]->metadata;
            BackendInfo& info = found[meta.name ? meta.name : "?"];
            info.name = meta.name ? meta.name : "?";
            info.priority = meta.priority;
            const char* label = rac_primitive_name(primitive);
            if (label != nullptr) {
                info.primitives.emplace_back(label);
            }
        }
    }
    for (auto& [name, info] : found) {
        std::sort(info.primitives.begin(), info.primitives.end());
        backends_.push_back(info);
    }
    std::sort(backends_.begin(), backends_.end(),
              [](const BackendInfo& a, const BackendInfo& b) { return a.priority > b.priority; });

    started_ = true;
    return true;
}

}  // namespace rcli::sdk
