#ifndef RCLI_APP_H
#define RCLI_APP_H

/// Implemented in C++ (src/cli/app.cpp) and linked in through librcli_bundle.a.
void rcli_begin(int argc, char** argv);
int rcli_run(int argc, char** argv);

#endif  // RCLI_APP_H
