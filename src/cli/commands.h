#ifndef RCLI_CLI_COMMANDS_H
#define RCLI_CLI_COMMANDS_H

#include <CLI11.hpp>

#include <string>

namespace rcli::cli {

/// Options that apply to every command, plus the exit code a callback wants.
///
/// CLI11 callbacks return void, so the status travels here rather than through
/// exceptions; a wrong model name is an ordinary failure, not an exceptional
/// one, and should not unwind through the SDK.
struct Options {
    bool verbose = false;
    std::string color = "auto";
    int status = 0;
};

/// Brings the SDK up once, on the first command that needs it. Commands that
/// only print (help, version) never pay for it.
bool Start();

/// Sends engine logging to RCLI_LOG or nowhere, unless --verbose.
void QuietEngines(bool verbose);

void RegisterChat(CLI::App& app, Options& options);
void RegisterModels(CLI::App& app, Options& options);
void RegisterSpeech(CLI::App& app, Options& options);
void RegisterImage(CLI::App& app, Options& options);
void RegisterBench(CLI::App& app, Options& options);
void RegisterEngines(CLI::App& app, Options& options);
void RegisterHarness(CLI::App& app, Options& options);
void RegisterAccount(CLI::App& app, Options& options);
void RegisterEditors(CLI::App& app, Options& options);

/// The interactive prompt, and the one-shot form when `prompt` is not empty.
int Chat(const std::string& model, const std::string& prompt);

}  // namespace rcli::cli

#endif  // RCLI_CLI_COMMANDS_H
