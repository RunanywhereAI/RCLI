#include <cstdlib>
#include <string>
#include <vector>

#include "catalog/catalog.h"
#include "cli/commands.h"
#include "cli/output.h"
#include "harness/harness.h"
#include "sdk/llm.h"

namespace rcli::cli {

void RegisterHarness(CLI::App& app, Options& options) {
    // `rcli opencode <model>` rather than a flag on `run`: it hands the terminal
    // to another program, which is a different thing to do than talk to a model.
    auto model = std::make_shared<std::string>();
    auto rest = std::make_shared<std::vector<std::string>>();
    auto* opencode =
        app.add_subcommand("opencode", "open a coding session in opencode, wired to a model");
    // A named option rather than a positional: with two positionals there is no
    // way to tell `rcli opencode run` asking for passthrough from someone
    // naming a model called run, and the first reading wins silently.
    opencode->add_option("-m,--model", *model, "a model on this machine, or one served upstream");
    opencode->add_option("args", *rest, "passed through to opencode")->allow_extra_args();
    opencode->prefix_command();
    opencode->callback([&options, model, rest] {
        options.status = harness::Launch("opencode", *model, *rest);
    });
}

}  // namespace rcli::cli
