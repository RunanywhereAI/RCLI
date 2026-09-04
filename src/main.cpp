/**
 * @file main.cpp
 * @brief rcli — RunAnywhere desktop CLI entry point.
 *
 * Thin dispatch layer: global flags + CLI11 subcommands. All real work
 * happens in commons behind the rac_* C ABI (see AGENTS.md layering rule).
 *
 * Exit codes: 0 success, 1 runtime/SDK error, 2 usage error.
 */

#include "app.h"

#include "rac/core/rac_logger.h"

int main(int argc, char** argv) {
    // Silence the SDK before anything can log.
    //
    // Backend plugins register during static initialisation and at the first
    // rac_* call, both of which happen before bootstrap() reads --verbose. That
    // left five lines of MLX registration noise on top of every command,
    // including a WARN for a backend that then registers successfully a line
    // later. `--verbose` raises this again in bootstrap.
    rac_logger_set_min_level(RAC_LOG_ERROR);
    return rcli::run(argc, argv);
}
