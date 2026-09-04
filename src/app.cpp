#include "app.h"

#include <exception>
#include <string>
#include <utility>
#include <vector>

#include <CLI11.hpp>

#include "bootstrap.h"
#include "commands/commands.h"
#include "io/output.h"

#include "rac/core/rac_logger.h"

#ifndef WALLY_VERSION
#define WALLY_VERSION "0.0.0-dev"
#endif

namespace wally {

void configure_app(CLI::App& app, GlobalOptions& options) {
    app.set_version_flag("--version,-V", std::string("wally ") + WALLY_VERSION);
    app.require_subcommand(0, 1);
    app.fallthrough(true);

    app.add_flag("--json", options.json, "Machine-readable JSON output on stdout");
    app.add_flag("-v,--verbose", options.verbose, "Debug logging on stderr");
    app.add_flag("-q,--quiet", options.quiet, "Errors only on stderr");
    app.add_flag("--no-progress", options.no_progress, "Disable progress rendering");
    app.add_option("--home", options.home_override,
                   "RunAnywhere home directory (default: $RUNANYWHERE_HOME or "
                   "~/.local/share/runanywhere; models live under <home>/Models)");

    // Control-plane connection. validation happens in resolve_connection().
    app.add_option("--environment", options.environment,
                   "SDK environment: development (default, keyless OSS → baked staging "
                   "backend) or production (API key + https URL).")
        ->envname("RUNANYWHERE_ENVIRONMENT")
        ->check(CLI::IsMember({"dev", "development", "prod", "production"}));
    app.add_option("--base-url", options.base_url,
                   "Backend base URL. Optional in development (baked staging URL). "
                   "Required https for production.")
        ->envname("RUNANYWHERE_BASE_URL");
    app.add_option("--api-key", options.api_key,
                   "Control-plane API key (required for production; omit for "
                   "keyless development)")
        ->envname("RUNANYWHERE_API_KEY");

    // Namespaces first (the spec grammar), then the terminal aliases, then the
    // infrastructure commands — that is the order `--help` lists them in.
    commands::register_llm(app, options);
    commands::register_vlm(app, options);
    commands::register_tool(app, options);  // must follow register_llm (extends the `llm` group)
    commands::register_stt(app, options);
    commands::register_tts(app, options);
    commands::register_vad(app, options);
    commands::register_embed(app, options);
    commands::register_rerank(app, options);
    commands::register_image(app, options);
    commands::register_diarize(app, options);
    commands::register_segment(app, options);
    commands::register_voice(app, options);
    commands::register_rag(app, options);
    commands::register_models(app, options);
    commands::register_lora(app, options);

    commands::register_llm_aliases(app, options);
    commands::register_models_aliases(app, options);

    commands::register_serve(app, options);
    commands::register_bench(app, options);
    commands::register_backends(app, options);
    commands::register_info(app, options);
    commands::register_version(app, options);
    commands::register_auth(app, options);
    commands::register_account(app, options);
    commands::register_usage(app, options);
    commands::register_editors(app, options);
    commands::register_harness(app, options);
    commands::register_telemetry(app, options);

    // `--help` groups: CLI11 prints one heading per distinct group string, in
    // the order each group is first seen (Formatter::make_subcommands), so
    // this order is the print order. Centralized here rather than one
    // ->group() call per register_* file: 36 top-level commands with no
    // grouping at all used to land in a single default SUBCOMMANDS: bucket.
    const std::vector<std::pair<const char*, const char*>> help_groups = {
        {"llm", "Generate"},      {"vlm", "Generate"},      {"stt", "Generate"},
        {"tts", "Generate"},      {"vad", "Generate"},      {"embed", "Generate"},
        {"rerank", "Generate"},   {"image", "Generate"},    {"diarize", "Generate"},
        {"segment", "Generate"},  {"voice", "Generate"},    {"rag", "Generate"},
        {"run", "Shortcuts"},     {"chat", "Shortcuts"},     {"ls", "Shortcuts"},
        {"show", "Shortcuts"},    {"pull", "Shortcuts"},     {"rm", "Shortcuts"},
        {"models", "Models"},     {"lora", "Models"},
        {"serve", "Serve & measure"}, {"bench", "Serve & measure"},
        {"backends", "Serve & measure"}, {"info", "Serve & measure"},
        {"version", "Serve & measure"},
        {"auth", "Account"},      {"login", "Account"},      {"logout", "Account"},
        {"whoami", "Account"},    {"usage", "Account"},
        {"opencode", "Editors & agents"},    {"claude-code", "Editors & agents"},
        {"claude-desktop", "Editors & agents"}, {"clion", "Editors & agents"},
        {"rustrover", "Editors & agents"},
    };
    // configure_app() runs ahead of run()'s own try/catch (and tests call it
    // directly with none at all), so a typo here must never propagate as an
    // uncaught exception -- that crashed the Windows CI binaries outright
    // (0xC0000409, no diagnostic) the one time a name here didn't match.
    // Report it and keep going with the default flat listing rather than
    // taking the whole CLI down over a --help cosmetic.
    for (const auto& [name, group] : help_groups) {
        try {
            app.get_subcommand(name)->group(group);
        } catch (const CLI::OptionNotFound&) {
            out::error_line(std::string("internal: --help grouping named an unknown "
                                        "subcommand '") +
                            name + "', skipping it");
        }
    }
    // Internal debug tool, not a command a user reaches for. An empty group
    // string drops a subcommand out of the default listing entirely
    // (Formatter::make_subcommands) while it stays fully callable —
    // `wally telemetry --help` still works.
    try {
        app.get_subcommand("telemetry")->group("");
    } catch (const CLI::OptionNotFound&) {
        // Nothing to hide if it isn't there.
    }
}

int run(int argc, char** argv) {
    GlobalOptions options;

    CLI::App app{"RunAnywhere on-device AI CLI — llm, vlm, stt, tts, vad, embed, rerank, "
                 "image, rag, voice and the models that back them"};
    configure_app(app, options);

    int exit_code = 0;
    try {
        app.parse(argc, argv);
        if (app.get_subcommands().empty()) {
            // Bare `wally` prints help like `ollama` does.
            out::status_line(app.help());
        }
    } catch (const CLI::CallForHelp& e) {
        exit_code = app.exit(e);
    } catch (const CLI::CallForVersion& e) {
        exit_code = app.exit(e);
    } catch (const CLI::RuntimeError& e) {
        exit_code = (e.get_exit_code() != 0) ? e.get_exit_code() : 1;
    } catch (const CLI::ParseError& e) {
        app.exit(e);  // prints the usage message to stderr
        exit_code = 2;
    } catch (const std::exception& e) {
        out::error_line(e.what());
        exit_code = 1;
    }

    shutdown();
    return exit_code;
}

}  // namespace wally

// Called from Swift, before MLX.register() — measured, not inferred: the
// Swift host logs 3 more INFO lines during that call (Swift callbacks
// registered, MLX backend registered, RunAnywhereMLX backend registered
// successfully), all before wally_run_main ever runs, so muting only inside
// wally_run_main left 5 RAC lines on `wally --version` instead of the 2 the
// old comment here assumed. Splitting the mute into its own entry point,
// called from WallyMLX.swift ahead of MLX.register(), is what actually gets
// there.
extern "C" void wally_quiet_sdk_logging() {
    rac_logger_set_min_level(RAC_LOG_ERROR);
}

extern "C" int wally_run_main(int argc, char** argv) {
    // Covers `wally-cxx` and any other entry that skips the Swift host, where
    // wally_quiet_sdk_logging() above is never called. Idempotent with it.
    //
    // The 2 RAC lines still on stderr on every entry point are backend
    // registration WARNs emitted during static initialisation, which
    // completes before any entry point runs. No call from inside the process
    // can catch them; silencing them needs a pre-registration hook in the
    // kit, and the kit owns backend registration.
    //
    // `--verbose` raises the level again in bootstrap().
    rac_logger_set_min_level(RAC_LOG_ERROR);
    return wally::run(argc, argv);
}
