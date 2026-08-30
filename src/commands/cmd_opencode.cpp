#include <memory>
#include <string>
#include <vector>

#include "commands/commands.h"
#include "harness/opencode.h"

namespace rcli::commands {
namespace {

void Fail(int status) {
    if (status != 0) {
        throw CLI::RuntimeError(status);
    }
}

}  // namespace

void register_opencode(CLI::App& app, GlobalOptions& options) {
    static_cast<void>(options);
    auto cloud = std::make_shared<bool>(false);
    auto model = std::make_shared<std::string>();
    auto arguments = std::make_shared<std::vector<std::string>>();

    CLI::App* command = app.add_subcommand(
        "opencode", "start OpenCode with an ephemeral RunAnywhere cloud provider");
    command->add_flag("--cloud", *cloud, "use the signed-in hosted inference endpoint")->required();
    command->add_option("-m,--model", *model, "hosted model id")->required();
    command
        ->add_option("arguments", *arguments,
                     "arguments after `--` are passed directly to OpenCode")
        ->allow_extra_args();
    command->positionals_at_end(true);
    command->callback(
        [cloud, model, arguments] { Fail(harness::LaunchOpenCodeCloud(*model, *arguments)); });
}

}  // namespace rcli::commands
