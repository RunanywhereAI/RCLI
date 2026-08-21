# Contributing to rcli

`rcli` is the terminal half of the product. Every model, engine and inference call comes from the
[RunAnywhere SDK](https://github.com/RunanywhereAI/runanywhere-sdks), so a change to how a model
runs usually belongs there, and a change to how it is asked for belongs here.

## Prerequisites

- An Apple Silicon Mac on macOS 14.5 or later
- CMake 3.24 or later
- Apple Clang, from Xcode or the Command Line Tools
- Xcode itself, for the binary that ships. `xcodebuild` owns the final link, and the reason is
  below.
- Optionally a checkout of `runanywhere-sdks`. Without one, CMake fetches the tag pinned in
  `cmake/RunAnywhereSDK.cmake`, which means a long first build.

If you do use a local checkout, keep it named `runanywhere-sdks` and put it beside this repo.
`swift/Package.swift` depends on the SDK by the relative path `../../runanywhere-sdks`, so the name
and the position both matter. CI mirrors that layout rather than special-casing it.

A fresh SDK clone carries no generated protos, because `core/src/generated/proto/` is gitignored.
Configure will stop on a missing `model_types.pb.cc`. Generate them once, from the SDK checkout:

```bash
bash idl/codegen/generate_cpp.sh
```

## Build

Two steps, and they are not interchangeable.

```bash
git clone https://github.com/RunanywhereAI/RCLI.git && cd RCLI
cmake -B build -DRCLI_SDK_DIR=/path/to/runanywhere-sdks
cmake --build build
```

That produces `build/rcli-cxx`, with five engines and no MLX. For the binary that ships:

```bash
scripts/build-mlx.sh
```

which produces `build/rcli` with all six.

MLX inference is written in Swift, and SwiftPM on the command line cannot compile Metal shaders.
mlx-swift says so in its own README. Only `xcodebuild` can, so `xcodebuild` has to own the final
link. A binary built without the shaders still links MLX, registers nothing, and reports MLX as
unavailable at runtime. `scripts/build-mlx.sh` harvests the link line CMake wrote and hands it to
`xcodebuild`, which is why `cmake --build` has to run first.

The names differ on purpose. A later `cmake --build` would otherwise replace the six-engine binary
with the five-engine one and nothing would look wrong until someone ran an MLX model.

That second step also produces `mlx-swift_Cmlx.bundle` next to the executable. mlx-swift keeps its
Metal shaders in a resource bundle rather than inside the binary, so copying `rcli` somewhere
without that directory beside it costs you MLX while the other five engines carry on as if nothing
happened. `scripts/package.sh` and `Formula/rcli.rb` both exist in the shape they do for this
reason: `libexec/` holds the binary and the bundle together, and `bin/rcli` is a symlink.

The build type defaults to Release. For a debug build, configure a second directory so the release
one survives:

```bash
cmake -B build-debug -DCMAKE_BUILD_TYPE=Debug -DRCLI_SDK_DIR=/path/to/runanywhere-sdks
cmake --build build-debug
```

## Tests

There is no test suite in this repo yet. What stands in for one is `.github/workflows/ci.yml`,
which builds both binaries, asserts that every engine registered, runs the commands that need no
model, packages the tarball, and checks that MLX still works when the binary is reached through the
Homebrew symlink. You can run the same pass locally:

```bash
./build/rcli-cxx --version
./build/rcli engines
./build/rcli list
./build/rcli list --all
./build/rcli search qwen
./build/rcli show qwen3-0.6b
./build/rcli where
./build/rcli config
echo /bye | ./build/rcli run
```

None of that downloads a model. The smallest is a few hundred megabytes, so generation itself is
checked by hand before a release.

`tools/probe.cpp` builds a `probe` binary that reproduces an SDK call path with no terminal output
around it. It is not shipped. Use it when a crash could be either side of the seam and you need to
know which.

## Source layout

```
src/
  main.cpp       the C++ entry point; calls rcli_run()
  cli/           command registration, output, terminal image preview
  repl/          the interactive prompt and its slash commands
  chat/          transcript and completion
  catalog/       the model catalog snapshot
  sdk/           the seam with the SDK: session, download, install, llm, speech, imagine
  settings/      the settings registry
  audio/         CoreAudio capture and playback
  media/         PNG encode and decode
  tools/         shell execution for the /run command
swift/           the Apple entry point: registers the MLX callbacks, then calls rcli_run()
third_party/     CLI11 and linenoise, vendored
tools/probe.cpp  isolation probe, not shipped
```

The application exists once. `src/main.cpp` and `swift/Sources/RCLIMLX` are two entry points into
the same `rcli_run()`, which is what stops the Apple build and the plain one from drifting.

## Adding a command

Commands live one per file in `src/cli/`. To add one:

1. Write `src/cli/cmd_yours.cpp` with a `RegisterYours(CLI::App&, Options&)` function.
2. Declare it in `src/cli/commands.h`.
3. Call it from `rcli_run()` in `src/cli/app.cpp`.
4. Add the file to the `rcli_core` sources in `CMakeLists.txt`.

Two conventions worth knowing. CLI11 callbacks return void, so an exit code travels back through
`Options::status` rather than an exception: a bad model name is an ordinary failure and should not
unwind through the SDK. And call `Start()` before touching the SDK. It brings the session up once,
on the first command that needs it, so `--help` and `--version` never pay for it.

## Adding a setting

Add one `Setting` to `src/settings/settings.cpp`. It carries its own name, summary, allowed values,
getter and setter, which is enough for `rcli config`, the `/set` command and its tab completion to
pick it up without any of the three being touched.

Settings live for one process. There is no config file, and adding one that `rcli run` silently
obeys is a bigger decision than a patch should make on its own.

## Adding a model

Not here. `src/catalog/catalog.cpp` is a snapshot extracted from the SDK's own catalog so that ids,
aliases and byte counts match what the SDK ships. Hand-editing it puts the two out of step. Add the
model to the SDK, then regenerate the snapshot. Once this repo can read the live registry through
the SDK, the table goes away.

## Code style

- C++20, Apple Clang, built with `-Wall -Wextra`
- stdout carries what the user asked for. Progress, notices and errors go to stderr, which is what
  makes `rcli imagine "..." | xargs open` work. Use `out::Line()` for the first and `out::Status()`
  or `out::Error()` for the second, from `src/cli/output.h`.
- No emoji in output.
- Colour goes through `out::Paint()`, never a raw escape sequence. Colour is decided once and is
  off when stdout is not a terminal.
- Comment where a reader would otherwise be misled: an invariant that is not visible locally, a
  workaround and the reason it exists, a constraint imposed from outside. Do not restate the line
  below.

## Pull requests

1. Branch off `main`.
2. Make both build steps pass, not just the first. A change that breaks only the MLX link is
  invisible to `cmake --build`.
3. Run the smoke pass above.
4. Describe what changed and why.
