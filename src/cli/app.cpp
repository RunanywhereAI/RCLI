#include "cli/app.h"

#include <CLI11.hpp>

#include <cstdio>
#include <cstdlib>
#include <string>

#include "cli/commands.h"
#include "cli/output.h"
#include "sdk/session.h"

namespace rcli::cli {

/// Everything the engines write to stderr at INFO — llama.cpp's own logging,
/// commons' generation lifecycle — is noise in a terminal that is also
/// streaming an answer. It goes to the file RCLI_LOG names, or nowhere, unless
/// --verbose says the user wants it.
void QuietEngines(bool verbose) {
    if (verbose) {
        return;
    }
    // The copy has to be taken before the redirect, or every notice below
    // writes to whatever stderr was pointed at.
    out::KeepNotices();
    const char* path = std::getenv("RCLI_LOG");
    std::freopen(path != nullptr ? path : "/dev/null", "a", stderr);
}

bool Start() {
    static const bool started = [] {
        auto& session = sdk::Session::Instance();
        if (!session.Start()) {
            out::Error(std::string("could not start: ") + std::string(session.error()));
            return false;
        }
        return true;
    }();
    return started;
}

}  // namespace rcli::cli

extern "C" void rcli_begin(int argc, char** argv) {
    static bool once = false;
    if (once) {
        return;
    }
    once = true;
    rcli::out::DetectColor();
    // argv is scanned by hand rather than parsed: this runs before CLI11 does,
    // because by the time CLI11 has an answer the engines have already logged.
    bool verbose = false;
    for (int i = 1; i < argc; ++i) {
        if (argv[i] != nullptr &&
            (std::string(argv[i]) == "-v" || std::string(argv[i]) == "--verbose")) {
            verbose = true;
        }
    }
    rcli::cli::QuietEngines(verbose);
}

extern "C" int rcli_run(int argc, char** argv) {
    using namespace rcli;

    rcli_begin(argc, argv);

    CLI::App app{"rcli — run language, speech and image models on this machine"};
    app.set_version_flag("--version", std::string(RCLI_VERSION));
    app.require_subcommand(0, 1);
    app.get_formatter()->column_width(26);

    cli::Options options;
    app.add_flag("-v,--verbose", options.verbose, "let the engines log to stderr");
    app.add_option("--color", options.color, "auto, always or never")
        ->check(CLI::IsMember({"auto", "always", "never"}));

    cli::RegisterChat(app, options);
    cli::RegisterModels(app, options);
    cli::RegisterSpeech(app, options);
    cli::RegisterImage(app, options);
    cli::RegisterBench(app, options);
    cli::RegisterEngines(app, options);
    cli::RegisterHarness(app, options);
    cli::RegisterAccount(app, options);
    cli::RegisterEditors(app, options);

    // Only --color is left to decide here; logging was settled in rcli_begin,
    // which had to run before the engines got a chance to write anything.
    app.parse_complete_callback([&options] {
        if (options.color == "always") {
            out::ForceColor(true);
        } else if (options.color == "never") {
            out::ForceColor(false);
        }
    });

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& e) {
        return app.exit(e);
    }

    // Bare `rcli` opens the prompt, which is what someone who just wants to
    // talk to a model expects.
    if (app.get_subcommands().empty()) {
        return cli::Chat("", "");
    }
    return options.status;
}
