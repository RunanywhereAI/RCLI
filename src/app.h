/**
 * @file app.h
 * @brief Shared rcli app wiring for the binary and in-process tests.
 */

#ifndef RCLI_APP_H
#define RCLI_APP_H

#include <CLI11.hpp>

#include "bootstrap.h"

namespace rcli {

void configure_app(CLI::App& app, GlobalOptions& options);
int run(int argc, char** argv);

}  // namespace rcli

/// The one entry point both binaries use: `main()` here, and the Swift MLX host
/// that ships as `rcli` on Apple. Anything that must happen before a command
/// runs belongs behind this, not in `main()`, which the product binary skips.
extern "C" int rcli_run_main(int argc, char** argv);

#endif  // RCLI_APP_H
