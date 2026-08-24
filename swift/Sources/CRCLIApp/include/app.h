#ifndef RCLI_APP_H
#define RCLI_APP_H

#ifdef __cplusplus
extern "C" {
#endif

/// Same entry the C++ `rcli-cxx` binary uses. The Swift MLX host registers
/// MLX callbacks, then calls this so there is one application, two ways in.
int rcli_run_main(int argc, char** argv);

#ifdef __cplusplus
}
#endif

#endif  // RCLI_APP_H
