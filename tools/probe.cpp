// Isolation probe: load a local model and stream one reply on the MAIN thread,
// with no UI. If this works, the fault is in how the app threads or renders;
// if it segfaults, it is the SDK path itself.
#include <cstdio>
#include <string>

#include "rac/core/rac_core.h"
#include "rac/desktop/rac_desktop.h"
#include "rac/features/llm/rac_llm_component.h"
#include "rac/features/llm/rac_llm_types.h"
#include "rac/infrastructure/model_management/rac_model_paths.h"

extern "C" rac_result_t rac_backend_llamacpp_register(void);

static rac_bool_t on_token(const char* token, void*) {
    std::fputs(token ? token : "", stdout);
    std::fflush(stdout);
    return RAC_TRUE;
}

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: probe <model.gguf>\n"); return 2; }

    char base[1024] = {};
    rac_desktop_default_base_dir(base, sizeof(base));
    rac_model_paths_set_base_dir(base);

    rac_platform_adapter_t adapter{};
    rac_desktop_adapter_init(nullptr, &adapter);
    rac_config_t config{};
    config.platform_adapter = &adapter;
    config.log_level = RAC_LOG_ERROR;
    std::printf("rac_init: %d\n", rac_init(&config));
    rac_desktop_http_transport_register();
    std::printf("llamacpp register: %d\n", rac_backend_llamacpp_register());

    rac_handle_t llm = nullptr;
    std::printf("create: %d\n", rac_llm_component_create(&llm));
    std::printf("load: %d\n", rac_llm_component_load_model(llm, argv[1], "probe", nullptr));

    rac_llm_options_t options = RAC_LLM_OPTIONS_DEFAULT;
    std::printf("--- generating ---\n");
    const rac_result_t rc =
        rac_llm_component_generate_stream(llm, "say hi", &options, on_token, nullptr, nullptr, nullptr);
    std::printf("\nstream returned: %d\n", rc);
    return 0;
}
