#ifndef WALLY_APP_H
#define WALLY_APP_H

#ifdef __cplusplus
extern "C" {
#endif

/// Lowers the SDK's log level before MLX.register() runs, which logs its own
/// INFO lines through the same logger. Call this first; wally_run_main()
/// lowers it again for entries that skip this call, so the order here only
/// matters for silencing MLX's own registration lines.
void wally_quiet_sdk_logging(void);

/// Same entry the C++ `wally-cxx` binary uses. The Swift MLX host registers
/// MLX callbacks, then calls this so there is one application, two ways in.
int wally_run_main(int argc, char** argv);

#ifdef __cplusplus
}
#endif

#endif  // WALLY_APP_H
