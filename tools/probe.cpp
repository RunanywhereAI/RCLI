// Isolation probe, not shipped. Exercises one SDK path with no UI so a failure
// can be attributed to the SDK or to the app.
//
//   probe generate <model.gguf>   load a local model and stream a reply
//   probe install  <catalog-id>   register and download from the catalog
#include <cstdio>
#include <cstring>
#include <string>

#include "catalog/catalog.h"
#include "sdk/install.h"
#include "sdk/session.h"

#include "rac/features/llm/rac_llm_component.h"
#include "rac/features/llm/rac_llm_types.h"

static rac_bool_t on_token(const char* token, void*) {
    std::fputs(token ? token : "", stdout);
    std::fflush(stdout);
    return RAC_TRUE;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: probe generate <model.gguf> | probe install <id>\n");
        return 2;
    }
    auto& session = rcli::sdk::Session::Instance();
    if (!session.Start()) {
        std::fprintf(stderr, "session failed: %s\n", std::string(session.error()).c_str());
        return 1;
    }
    std::printf("engines: %zu   home: %s\n", session.backends().size(),
                std::string(session.home()).c_str());

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
