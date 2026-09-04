#include <cstdlib>
#include <string>
#include <vector>

#include "catalog/catalog.h"
#include "commands/commands.h"
#include "io/output.h"
#include "harness/harness.h"
#include "harness/opencode.h"

namespace rcli::commands {
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
    // `rcli opencode <model>` rather than a flag on `run`: it hands the terminal
    // to another program, which is a different thing to do than talk to a model.
    auto model = std::make_shared<std::string>();
    auto rest = std::make_shared<std::vector<std::string>>();
    auto cloud = std::make_shared<bool>(false);
    auto* opencode =
        app.add_subcommand("opencode", "open a coding session in opencode, wired to a model");
    // A named option rather than a positional: with two positionals there is no
    // way to tell `rcli opencode run` asking for passthrough from someone
    // naming a model called run, and the first reading wins silently.
    opencode->add_option("-m,--model", *model, "a model on this machine, or one served upstream");
    opencode->add_flag("--cloud", *cloud,
                       "use the signed-in hosted endpoint (never routes local models)");
    opencode->add_option("args", *rest, "passed through to opencode")->allow_extra_args();
    opencode->prefix_command();
    opencode->callback([model, rest, cloud] {
        if (*cloud) {
            if (model->empty()) {
                out::error_line("--cloud requires --model <console-model-id>");
                fail(2);
            }
            fail(harness::LaunchOpenCodeCloud(*model, *rest));
            return;
        }
        fail(harness::Launch("opencode", *model, *rest));
    });
}

}  // namespace rcli::commands
