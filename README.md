# rcli

`rcli` runs AI models on your own machine: text generation, speech to text, text to speech, and
image generation, all through the
[RunAnywhere SDK](https://github.com/RunanywhereAI/runanywhere-sdks).

It is a plain command-line tool with no full-screen interface. Results go to stdout, progress and
errors go to stderr, so a redirect keeps the answer and leaves the progress bar on your terminal:

```bash
rcli run qwen3 "why is the sky blue" > answer.txt
```

## Install

On macOS and Linux:

```bash
curl -fsSL https://raw.githubusercontent.com/RunanywhereAI/RCLI/main/install.sh | bash
```

That installs Homebrew if you do not have it, adds the tap, and installs rcli. To do the same by
hand, note that this repo is its own tap, so tapping it needs the explicit URL form:

```bash
brew tap RunanywhereAI/rcli https://github.com/RunanywhereAI/RCLI.git
brew install runanywhereai/rcli/rcli
```

The name is spelled out because `runanywhereai/tap` also provides a formula called `rcli`. If you
do not have that tap, plain `brew install rcli` works too.

That covers an Apple Silicon Mac on macOS 14.5 or later, and x86-64 Linux through Homebrew on
Linux. Nothing else to set up: models download when you first ask for one and are kept in
`~/.local/share/runanywhere`.

## Install on Windows

There is no Homebrew on Windows, so the installer downloads the release and unpacks it itself. In
PowerShell:

```powershell
irm https://raw.githubusercontent.com/RunanywhereAI/RCLI/main/install.ps1 | iex
```

That puts `rcli` in `%LOCALAPPDATA%\Programs\rcli` and adds the directory to your user PATH, so
open a new terminal before you run it. 64-bit x86 only, and models are kept in
`%LOCALAPPDATA%\RunAnywhere`.

## Quick start

```bash
rcli list --all                    # everything in the catalog
rcli pull qwen3-0.6b               # download one
rcli run qwen3 "explain mmap"      # ask once and exit
rcli run qwen3                     # interactive prompt, /? for commands
rcli                               # same prompt, no model loaded yet
```

Models are named by id (`qwen3-0.6b`) or by the shorter alias (`qwen3`). `rcli run` on a model you
have not downloaded pulls it first.

## Commands

| Command | What it does |
| --- | --- |
| `rcli run [model] [prompt]` | Talk to a model. With a prompt it answers and exits; without one you get the interactive prompt. `chat` is the same command. |
| `rcli list [-a]` | Models on this machine. `-a` / `--all` lists the whole catalog and marks what is downloaded. |
| `rcli search <query>` | Match a query against catalog ids, aliases, and names. |
| `rcli pull <model>` | Download a model and wait for it to land. |
| `rcli rm <model>` | Delete a downloaded model and report the space freed. |
| `rcli show <model>` | Catalog details: engine, kind, format, size, context length, whether it reasons. |
| `rcli stt <file>` | Transcribe a 16-bit mono WAV. `-m` picks the speech model. |
| `rcli tts <text>` | Speak text through the speakers, or write a WAV with `-o file`. `-m` picks the voice. |
| `rcli imagine <prompt>` | Generate an image, draw a preview in the terminal, and print the path. `-m` picks the model, `-q` skips the preview. `draw` is the same command. |
| `rcli bench [model]` | Measure tokens per second and time to first token, over one model or every downloaded model that generates text. |
| `rcli engines` | Which engines came up and which primitives each one serves. Engines that were compiled in but did not start are listed with the reason. |
| `rcli config [setting] [value]` | List settings, read one, or change one. |
| `rcli where` | The directory models and generated images live in. |

Global flags: `--version`, `-v` / `--verbose` to let the engines log to stderr, and
`--color auto|always|never`.

`rcli imagine` prints the image path alone on the last line of stdout, so `rcli imagine "a red
apple" | xargs open` works.

## The interactive prompt

`rcli run <model>` with no prompt opens a readline prompt with history and tab completion. Anything
that does not start with `/` goes to the model.

| | |
| --- | --- |
| `/load <model>` | Load a model, downloading it first if needed |
| `/models` | What is on this machine |
| `/pull <model>` | Download from the catalog |
| `/rm <model>` | Delete a downloaded model |
| `/show` | Current settings |
| `/set <key> <value>` | Change a setting |
| `/image <path>` | Ask about a picture. Needs a vision model loaded, and the turn is single-turn |
| `/doc <path>` | Put a text file in the context. Text only, truncated at 32000 characters |
| `/imagine <text>` | Generate an image |
| `/mic` | Record until you press enter, transcribe, and send |
| `/say [text]` | Speak the text, or the last answer |
| `/run <cmd>` | Run a shell command after you confirm it |
| `/think` | Show or hide the model's reasoning |
| `/history` | The conversation so far |
| `/clear` | Forget the conversation |
| `/bye` | Quit |

Reasoning tokens go to stderr and the answer goes to stdout, which is why redirecting a one-shot
`run` captures the answer and nothing else.

## Settings

`rcli config` with no arguments lists the settings and their current values:

| Setting | |
| --- | --- |
| `accelerator` | `auto`, `cpu`, `gpu`, or `npu`. Advisory; an engine may ignore it |
| `engine` | Pin one engine instead of letting priority decide. The allowed values are the engines that actually registered on this machine |
| `context-length` | Context window at load time. `0` leaves it to the engine |
| `reasoning` | `auto`, `on`, or `off`. `auto` follows the model |
| `temperature` | `0` is greedy, `2` is as random as the sampler goes |
| `max-tokens` | Longest answer the model may produce, 4096 by default. A model that reasons spends this budget thinking before it answers, so lowering it much can leave no room for the answer itself |

Settings live for one process. `rcli config temperature 0.2` changes it for that command and
nothing else, so to use a setting you either set it in the interactive prompt with `/set` or accept
the default. There is no config file.

Two environment variables: `RUNANYWHERE_HOME` moves the storage directory, and `RCLI_LOG` names a
file for the engine logs that `--verbose` would otherwise put on stderr.

## Engines

A macOS build links six engines: mlx, llamacpp, neurt, sherpa, cloud, and onnx. Linux and Windows
get llamacpp, sherpa, cloud, and onnx. MLX is Metal and NeuRT is the Apple Neural Engine, so
neither has anything to link against elsewhere.

Which of those are usable also depends on the machine, so `rcli engines` is the honest answer for
any given install: it lists what registered, and names anything that was compiled in but could not
start, with the reason. A model's catalog entry names the engine that runs it, so pulling a model
is also how you choose an engine.

## Building from source

You need CMake 3.24 or later, Apple Clang for the C++ build, and Xcode for the MLX one. Point the
build at a checkout of the SDK:

```bash
cmake -B build -DRCLI_SDK_DIR=/path/to/runanywhere-sdks
cmake --build build
```

Without `RCLI_SDK_DIR` the SDK is fetched at the tag pinned in `cmake/RunAnywhereSDK.cmake`, which
means a long first build. A sibling checkout reuses what is already built there.

That build produces `build/rcli-cxx`, with five engines and no MLX. For the binary that ships:

```bash
scripts/build-mlx.sh
```

which produces `build/rcli` with all six.

The two steps are not interchangeable. MLX inference is Swift, and SwiftPM on the command line
cannot compile Metal shaders; mlx-swift documents this in its own README. Only xcodebuild can, so
xcodebuild has to own the final link. A binary built without the shaders still links MLX, registers
nothing, and reports MLX as unavailable at runtime. `scripts/build-mlx.sh` harvests the link line
CMake wrote and hands it to xcodebuild, which is why `cmake --build` has to run first.

One more thing that build produces: `mlx-swift_Cmlx.bundle`, next to the executable. mlx-swift keeps
its Metal shaders in a resource bundle rather than inside the binary, so moving `rcli` somewhere
without that directory beside it costs you MLX while the other five engines carry on as if nothing
happened. The Homebrew formula installs both into `libexec` and symlinks the binary for this reason.

## Licence

This repo is MIT (see [LICENSE](LICENSE)), but it links the RunAnywhere SDK, which is not. The SDK
is source-available under the RunAnywhere License: free for individuals, for organizations with
both less than $1M in total funding and less than $1M in gross annual revenue, and for educational
institutions, non-profits, government bodies, and projects under an OSI-approved open source
licence. Anyone outside those categories needs a commercial licence, and the contact for that is
san@runanywhere.ai.

Read the [SDK's licence](https://github.com/RunanywhereAI/runanywhere-sdks/blob/main/LICENSE)
before you build `rcli` into a commercial product. The summary above is not the terms.

Built by [RunAnywhere, Inc.](https://runanywhere.ai)
