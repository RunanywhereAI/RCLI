#include <cstdlib>
#include <string>
#include <vector>

#include "catalog/catalog.h"
#include "commands/commands.h"
#include "io/output.h"
#include "harness/codex.h"
#include "harness/harness.h"
#include "harness/opencode.h"

namespace wally::commands {
namespace {

/// CLI11 callbacks return void, so a non-zero status leaves as the runtime
/// error the app turns back into an exit code.
void fail(int status) {
    if (status != 0) {
        throw CLI::RuntimeError(status);
    }
}
}  // namespace

void register_harness(CLI::App& app, GlobalOptions& options) {
    static_cast<void>(options);
    // `wally opencode <model>` rather than a flag on `run`: it hands the terminal
    // to another program, which is a different thing to do than talk to a model.
    auto model = std::make_shared<std::string>();
    auto rest = std::make_shared<std::vector<std::string>>();
    auto cloud = std::make_shared<bool>(false);
    auto* opencode =
        app.add_subcommand("opencode", "open a coding session in opencode, wired to a model");
    // A named option rather than a positional: with two positionals there is no
    // way to tell `wally opencode run` asking for passthrough from someone
    // naming a model called run, and the first reading wins silently.
    opencode->add_option("-m,--model", *model, "a model on this machine, or one served upstream");
    opencode->add_flag("--cloud", *cloud,
                       "use the signed-in hosted endpoint (never routes local models)");
    opencode->add_option("args", *rest, "passed through to opencode")->allow_extra_args();
    opencode->prefix_command();
    opencode->callback([model, rest, cloud] {
        const std::string effective = ResolveDefaultModel(*model);
        if (*cloud) {
            if (effective.empty()) {
                out::error_line(
                    "--cloud requires --model <console-model-id>, and no default is set "
                    "(wally default-models <id>)");
                fail(2);
            }
            fail(harness::LaunchOpenCodeCloud(effective, *rest));
            return;
        }
        fail(harness::Launch("opencode", effective, *rest));
    });

    // Codex speaks the Responses API, which the hosted gateway serves and a
    // local `wally serve` does not, so `wally codex` is hosted-only for now.
    // Same prefix-command passthrough as opencode.
    auto codex_model = std::make_shared<std::string>();
    auto codex_rest = std::make_shared<std::vector<std::string>>();
    auto* codex =
        app.add_subcommand("codex", "open a coding session in Codex against a hosted model");
    codex->add_option("-m,--model", *codex_model, "a console model id (e.g. glm-5.3-flash)");
    codex->add_option("args", *codex_rest, "passed through to codex")->allow_extra_args();
    codex->prefix_command();
    codex->callback([codex_model, codex_rest] {
        const std::string effective = ResolveDefaultModel(*codex_model);
        if (effective.empty()) {
            out::error_line("wally codex needs a hosted model: wally codex -m <console-model-id> "
                            "(or set one with wally default-models <id>)");
            fail(2);
        }
        fail(harness::LaunchCodexCloud(effective, *codex_rest));
    });
}

}  // namespace wally::commands
