#ifndef RCLI_CLI_APP_H
#define RCLI_CLI_APP_H

#ifdef __cplusplus
extern "C" {
#endif

/// Decides where our own notices and the engines' logging go, and must be
/// called before anything that might log.
///
/// The Apple entry point registers MLX before the CLI starts, and that logs
/// three lines. Redirecting from Swift instead would take our own progress bars
/// and errors with it, which is exactly what happened: the rule lives here so
/// there is one of it.
void rcli_begin(int argc, char** argv);

/// Runs the whole CLI and returns its exit code. Calls rcli_begin if the caller
/// has not.
///
/// Exposed as C rather than being `main` because MLX's inference is Swift: the
/// engine in commons is a callback shim only Swift can fill in. The Apple build
/// therefore has a Swift entry point that registers those callbacks and then
/// calls this — one application, two ways in.
int rcli_run(int argc, char** argv);

#ifdef __cplusplus
}
#endif

#endif  // RCLI_CLI_APP_H
