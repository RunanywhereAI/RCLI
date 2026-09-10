/**
 * @file app.h
 * @brief Shared wally app wiring for the binary and in-process tests.
 */

#ifndef WALLY_APP_H
#define WALLY_APP_H

#include <string>
#include <vector>

#include <CLI11.hpp>

#include "bootstrap.h"

namespace wally {

void configure_app(CLI::App& app, GlobalOptions& options);
int run(int argc, char** argv);

/// Rewrites a command line so a passthrough subcommand's tool arguments survive
/// CLI11's parse: a `--` is inserted before the first token that belongs to the
/// wrapped tool. Exposed for tests; `argv` includes the program name at 0.
std::vector<std::string> SplitPassthroughArgv(const std::vector<std::string>& argv);

}  // namespace wally

/// The one entry point both binaries use: `main()` here, and the Swift MLX host
/// that ships as `wally` on Apple. Anything that must happen before a command
/// runs belongs behind this, not in `main()`, which the product binary skips.
extern "C" int wally_run_main(int argc, char** argv);

#endif  // WALLY_APP_H
