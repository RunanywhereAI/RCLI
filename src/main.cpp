#include "cli/app.h"

// The plain C++ entry point. The Apple build has a Swift one that registers the
// MLX callbacks first; both end up in rcli_run.
int main(int argc, char** argv) {
    return rcli_run(argc, argv);
}
