<div align="center">

<img src="docs/assets/wally.gif" alt="Wally, the RunAnywhere mascot" width="200">

# Wally

**Run open models on your own machine, or hosted when the job outgrows it.**

Chat, vision, speech and embeddings, all from the terminal. Local models never
leave the device; hosted ones go through a console you sign in to and are
metered against your own credit.

</div>

```bash
wally pull qwen3     # download a model
wally run qwen3      # chat with it, offline
```

That is the whole first run. No account, no key, nothing leaves the machine.

## Signing in

Models you have pulled run on this machine and need no account. To use a hosted
model instead, sign in to a RunAnywhere console:

```bash
wally login          # opens a browser; approve it there
wally whoami         # the account you are signed in as
wally usage          # credit, and what you have used this month
wally logout
```

The terminal never asks for a password. It shows a code, you approve it in the
browser, and it collects an API key with your credit behind it. That key appears
on the console's Cloud keys page and can be revoked there at any time.

Against a console running on your own machine:

```bash
export WALLY_CONSOLE_URL=http://localhost:8002
wally login
```

Then hand a hosted model to a coding session:

```bash
wally opencode -m gemma-4
```

For the Open Frontier hosted path, make the choice explicit and pass any
OpenCode arguments after `--`:

```bash
wally opencode --cloud --model <console-model-id> -- --agent build
```

`--cloud` never falls back to a local model. `wally opencode -m <model>` runs
whichever model you name, local or hosted.

If the model is on this machine, wally serves it locally. If it is not, the
request goes to the console you are signed in to, is checked against your
balance before it runs, and is metered.

## Install

### macOS (Apple Silicon)

```bash
brew install runanywhereai/wally/wally
```

or

```bash
curl -fsSL https://raw.githubusercontent.com/RunanywhereAI/RCLI/main/install.sh | sh
```

### From source

Needs a built SDK kit, not SDK source:

```bash
cmake -B build -DWALLY_SDK_KIT=<sdks>/dist/cpp-desktop-macos-arm64
export WALLY_SDK_SWIFT_PATH=<sdks>          # for the MLX backend on Apple
cmake --build build -j8
```

`build/wally` is the full binary. `build/wally-cxx` is the same CLI without MLX,
and is what you get if `WALLY_SDK_SWIFT_PATH` is unset.

MLX loads its Metal shaders from `mlx-swift_Cmlx.bundle` next to the executable,
so install the pair together:

```bash
mkdir -p ~/.local/lib/wally
cp -R build/mlx-swift_Cmlx.bundle build/wally ~/.local/lib/wally/
printf '#!/bin/sh\nexec "$HOME/.local/lib/wally/wally" "$@"\n' > ~/.local/bin/wally
chmod +x ~/.local/bin/wally
```

Copy the binary on its own and MLX will not register.

### Windows (x64)

```powershell
irm https://raw.githubusercontent.com/RunanywhereAI/RCLI/main/install.ps1 | iex
```

### Linux (x86_64)

No Linux release asset is currently published. Use the source build below;
`install.sh` intentionally fails instead of claiming that an unavailable bottle
was installed.

## Get started

```bash
wally pull qwen3          # download
wally run qwen3           # chat
wally run qwen3 "Hello"   # one-shot
wally serve qwen3         # OpenAI-compatible API on :8080 (macOS/Linux)
```

`wally models list --all` is the full catalog. Short names work everywhere (`qwen3`, `llama3.2`, `whisper-tiny`, `piper`, …). Any Hugging Face GGUF works too:

```bash
wally pull hf.co/Qwen/Qwen3-0.6B-GGUF/Qwen3-0.6B-Q8_0.gguf
```

## Backends

One `wally` binary. **Catalog models already name their engine** (GGUF → llama.cpp, `mlx-*` → MLX, Core ML → NeuRT, QNN-context → QHexRT). You normally do not pick one.

Override only when you mean it:

```bash
wally llm generate --engine mlx -m mlx-qwen3 "Hello"
wally run --engine qhexrt /path/to/lfm2_5_230m_HNPU "Hello"
wally image generate --engine neurt --prompt "a red cube" --out out.png
```

`--engine` accepts `mlx`, `llamacpp`, `sherpa`, `onnx`, `neurt` / `coreml` / `ane`, and `qhexrt` / `qnn` / `npu` / `hexagon`. If you omit it, commons picks the highest-priority **registered** backend that implements that primitive:

| Priority | Engine | Who wins unpinned work |
|---|---|---|
| 150 | QHexRT | Every primitive it implements, and only on a Windows ARM64 overlay binary (often the *only* engine in that binary) |
| 110 | MLX | Apple GPU: LLM / VLM / TTS / STT / embeddings when an `mlx-*` model is not already pinned |
| 100 | llama.cpp | GGUF LLM / VLM / embed / rerank |
| 100 | NeuRT | Core ML only. Stays at 100 **on purpose** so it never steals GGUF/MLX traffic. A Core ML bundle reaches NeuRT by framework pin, not by winning priority |
| 90 | Sherpa-ONNX | STT / TTS / VAD |
| 50 | ONNX Runtime | embeddings / VAD / diarization / segmentation |

`wally backends` is the source of truth for **this** binary. Public bottles never list `neurt` or `qhexrt`. Those engines are private overlays, never Homebrew / GitHub Release assets.

### Where each engine exists

| Backend | macOS Apple Silicon | Windows x64 | Windows ARM64 | Linux x64 |
|---|---|---|---|---|
| [llama.cpp](https://github.com/ggml-org/llama.cpp) | public bottle | public bottle | — | source build only |
| [MLX](https://github.com/ml-explore/mlx) (Apple GPU) | public bottle (product `wally`, not `wally-cxx`) | — | — | — |
| [Sherpa-ONNX](https://github.com/k2-fsa/sherpa-onnx) | public bottle | public bottle | — | source build only |
| [ONNX Runtime](https://onnxruntime.ai) | public bottle | public bottle | — | source build only |
| NeuRT (Apple Neural Engine; Core ML is the format) | **overlay** rebuild | — | — | — |
| QHexRT (Qualcomm Hexagon NPU) | — | — | **overlay** rebuild | — |

Public Windows ARM64 kits are commons-only (no llama.cpp / ONNX / Sherpa on MSVC ARM64). Snapdragon NPU is overlay-only. x64 Windows has no Hexagon path.

### Modalities × engines

Yes = this engine implements the primitive. Try = a catalog id that `wally pull` / a local path can run. Overlay engines still need the matching **on-disk bundle** (compiled `.mlmodelc` tree, or `*_HNPU` / `v81/` QNN-context dir) — a Hugging Face *repo page* is HTML, not a model.

| Modality | Command | llama.cpp | MLX | Sherpa | ONNX | NeuRT | QHexRT |
|---|---|---|---|---|---|---|---|
| LLM | `wally run` / `llm generate` | yes · `smollm2`, `qwen3` | yes · `mlx-qwen3` | — | — | yes · `lfm2-230m-ane` local Core ML tree | yes · `lfm2-230m-npu` local `*_HNPU` |
| VLM | `wally vlm generate --image` | yes · `smolvlm2` | yes · `mlx-qwen2-vl` | — | — | — | yes · `internvl-1b-npu` local HNPU |
| TTS | `wally tts synthesize -o out.wav` | — | yes · `mlx-soprano` | yes · `piper` | — | — | yes · `kitten-micro-npu` local HNPU |
| STT | `wally stt transcribe audio.wav` | — | yes · `mlx-qwen3-asr` | yes · `whisper-tiny` | — | yes · `parakeet-tdt-v2-ane` local Core ML | yes · `whisper-base-npu` local HNPU |
| VAD | `wally vad detect audio.wav` | — | — | yes | yes · `silero` | — | — |
| Embeddings | `wally embed` | yes · `nemotron-3-embed` | yes · `mlx-qwen3-embed` | — | yes · `minilm` | — | yes · `embeddinggemma-npu` local HNPU |
| Rerank | `wally rerank -d …` | yes · `bge-reranker` | — | — | — | — | yes · `nv-rerank-npu` local HNPU |
| Segmentation | `wally segment image.ppm` (binary P6 PPM) | — | — | — | yes · `segformer` | — | — |
| Diarization | `wally diarize audio.wav` | — | — | — | yes · `sortformer` | — | — |
| Image gen | `wally image generate --prompt … --out …` | — | — | — | — | yes · `sd15` (compiled Core ML zip, not the HF repo HTML) | yes · `cosmos3-diffusion-npu` local HNPU |

MLX registers with a one-line `-811` then Swift callbacks install it — that warning is expected. `image generate` is compiled only when NeuRT is linked; `--prompt` and `--out` are required (not a positional prompt). `--steps 4` is enough for a smoke PNG.

QHexRT on device also needs QAIRT matching the Hexagon skel (`QNN_SDK_ROOT` + `ADSP_LIBRARY_PATH=…\lib\hexagon-v81\unsigned` on v81). Overlay 2.47 DLLs vs a 2.41/2.48 device skel will fail to instantiate graphs. Pass the `*_HNPU` directory, not a GGUF. GGUF files cannot run on the ARM64 overlay binary (no llama.cpp).

## Models

Catalog models are grouped by the org that trains them. GGUF rows run on llama.cpp (macOS, Windows x64, Linux). `mlx-*` rows run on Apple Silicon only.

### Language

| Org | Families | Try |
|---|---|---|
| [Alibaba Qwen](https://huggingface.co/Qwen) | Qwen3, Qwen3.6, Qwen3.8 | `qwen3`, `mlx-qwen3` |
| [Meta](https://huggingface.co/meta-llama) | Llama 3.2 | `llama3.2`, `mlx-llama3.2` |
| [Google](https://huggingface.co/google) | Gemma 4 | `gemma4-e2b`, `mlx-gemma4-e2b` |
| [Hugging Face](https://huggingface.co/HuggingFaceTB) | SmolLM2 | `smollm2` |
| [Liquid AI](https://huggingface.co/LiquidAI) | LFM2 | `lfm2` |
| [IBM](https://huggingface.co/ibm-granite) | Granite 4.1 | `granite4.1-3b`, `mlx-granite4.1-3b` |
| [NVIDIA](https://huggingface.co/nvidia) | Nemotron | `mlx-nemotron-nano` |
| [PrismML](https://huggingface.co/prism-ml) | Bonsai, Ternary-Bonsai | `bonsai-1.7b`, `mlx-bonsai-1.7b` |
| [DeepGrove](https://huggingface.co/deepgrove) | Maple Preview | `maple-preview`, `mlx-maple-preview` |

### Vision

| Org | Families | Try |
|---|---|---|
| Hugging Face | SmolVLM2 | `smolvlm2` |
| Alibaba Qwen | Qwen2-VL | `qwen2-vl`, `mlx-qwen2-vl` |
| Liquid AI | LFM2-VL, LFM2.5-VL | `lfm2-vl`, `mlx-lfm2.5-vl` |
| Apple | FastVLM | `mlx-fastvlm` |
| Microsoft | Fara 1.5 (computer use) | `fara` |
| Meta | Muse Glimmer | `muse-glimmer` |
| NVIDIA | Nemotron Omni | `nemotron-omni` |

```bash
wally vlm generate --model smolvlm2 --image photo.png "What is in this picture?"
```

### Speech

| Org | Families | Role | Try |
|---|---|---|---|
| OpenAI | Whisper | STT | `whisper-tiny` |
| NVIDIA | Parakeet, Canary, Nemotron ASR | STT | `parakeet-tdt-v2` |
| Alibaba Qwen | Qwen3-ASR / Qwen3-TTS | STT / TTS (MLX) | `mlx-qwen3-asr` |
| [rhasspy](https://github.com/rhasspy/piper) | Piper | TTS | `piper` |
| [Supertone](https://huggingface.co/Supertone) | Supertonic | TTS | `supertonic` |
| [Zhipu](https://huggingface.co/THUDM) | GLM-ASR | STT (MLX) | `mlx-glm-asr` |
| [Silero](https://github.com/snakers4/silero-vad) | Silero | VAD | `silero` |

```bash
wally tts synthesize "Hello from the device." -o hello.wav
wally stt transcribe hello.wav
```

### Embeddings, rerank, other

| Org | Families | Role | Try |
|---|---|---|---|
| NVIDIA | Nemotron Embed, Llama-Nemotron Embed | embeddings | `nemotron-3-embed` |
| Alibaba Qwen | Qwen3 Embedding | embeddings (MLX) | `mlx-qwen3-embed` |
| [sentence-transformers](https://huggingface.co/sentence-transformers) | MiniLM | embeddings | `minilm` |
| [BAAI](https://huggingface.co/BAAI) | BGE Reranker | rerank | `bge-reranker` |
| NVIDIA | Sortformer | diarization | `sortformer` |
| [NVIDIA / Hugging Face](https://huggingface.co/nvidia) | SegFormer | segmentation | `segformer` |
| Stability AI / Apple | Stable Diffusion 1.5 | image gen (NeuRT) | `sd15` |

## macOS vs Windows

**macOS Apple Silicon** (public bottle): llama.cpp + MLX + Sherpa + ONNX. Pull `qwen3` (GGUF) or `mlx-qwen3` (GPU). Image generation is NeuRT (`sd15`) and only works after the private overlay is linked into product `wally`.

**Windows x64** (public zip): GGUF / ONNX / Sherpa. No MLX, no NeuRT, no QHexRT.

**Windows ARM64** (Snapdragon): public kit has no llama.cpp/ONNX/Sherpa. The QHexRT overlay runs Hexagon NPU models from a local `*_HNPU` tree. Do not expect `mlx-*`, GGUF, or `sd15` on that binary.

`wally serve` is macOS and Linux.

Device round-trips are **by modality**, not by engine. `scripts/test/e2e.sh` always
runs `scripts/test/e2e-modalities.sh`; public CI leaves the knobs unset and skips.
On a machine that already has models:

```bash
export RUNANYWHERE_HOME=/path/to/home          # already-pulled OSS models
export WALLY_E2E_MODEL_ROOTS=/path/to/hnpu      # *_HNPU / *_ANE / *.mlmodelc trees
bash scripts/test/e2e-modalities.sh /path/to/wally   # no --engine required
```

`WALLY_E2E_LLM`, `WALLY_E2E_STT`, `WALLY_E2E_IMAGE`, … pin one primitive. Catalog
ids (`mlx-qwen3`, `whisper-base-npu`) pin the framework; a Hugging Face repo
page is HTML, not a bundle.

## Commands

| | |
|---|---|
| `wally run` / `wally chat` | chat (REPL with no prompt) |
| `wally pull` / `wally models download` | download |
| `wally list` / `wally ls` | local models (`--all` = catalog) |
| `wally show` | one model |
| `wally rm` | delete |
| `wally llm generate` / `stream` | completion |
| `wally vlm generate --image` | vision |
| `wally stt transcribe` | speech → text |
| `wally tts synthesize` | text → WAV |
| `wally vad detect` | voice activity |
| `wally embed` | embeddings |
| `wally rerank` | rerank documents |
| `wally image generate` | text → image (NeuRT / Apple Silicon) |
| `wally serve` | OpenAI-compatible HTTP (macOS/Linux) |
| `wally backends` | registered engines |
| `wally info` | versions and paths |
| `--engine` | force mlx / llamacpp / sherpa / onnx / neurt / qhexrt |
| `wally login` / `logout` / `whoami` | sign in to the console that serves upstream models |
| `wally usage` | credit left, then tokens and spend over the last hour and day |
| `wally claude-code` / `claude-desktop` | open Claude against a model |
| `wally clion` / `rustrover` | point a JetBrains IDE at a model |
| `wally opencode` | open a coding session against a model |

`wally --help` and `wally <command> --help` cover the rest.

## Editors and coding agents

One command points a tool at a model and starts it. There is nothing to
configure by hand:

```bash
wally claude-code -m qwen3-0.6b
wally clion -m models/gemma-4-31b-it
wally claude-desktop -m models/gemma-4-31b-it
```

The model can be one on this machine or one the console serves. Without `-m` the
tool starts the way you already have it configured, and wally wires nothing.

| Tool | How it is wired |
| --- | --- |
| `claude-code`, `opencode` | `ANTHROPIC_BASE_URL` and `ANTHROPIC_AUTH_TOKEN` in the process |
| `claude-desktop` | a gateway profile in Claude Desktop's third party mode, covering the chat and Cowork tabs |
| `clion`, `rustrover` | AI Assistant's OpenAI-compatible provider, which works without a JetBrains AI subscription |

Two flags go with `-m`. `--serve` holds the endpoint open and prints it instead
of launching anything, which is how a tool nobody has taught wally about gets
wired up. `--restore` puts Claude Desktop or a JetBrains IDE back the way it was
and starts nothing; a normal run already undoes its own configuration when the
app quits, so this is for the run that was interrupted before it could.

The first `wally clion` on a machine takes a while, because it installs the AI
Assistant plugin headlessly before starting the IDE. Later runs are quick. That
endpoint sits on a fixed port rather than whatever happened to be free, because
the IDE reads the address once at startup out of a file wally writes beforehand,
and a port that moved would leave that file naming something dead.

Claude Code and Claude Desktop speak Anthropic's Messages API, while the models
wally serves speak OpenAI's, so a translator sits between them. It carries tool
definitions out, tool calls back, and the results of those calls out again,
which is what lets an agent on the far side run the tools it was given rather
than describe them. The JetBrains IDEs need no translator, because AI Assistant
speaks OpenAI already.

## Hosted models

A model you have not downloaded can still answer, if the console serves it:

```bash
wally login
wally whoami
wally run models/gemma-4-31b-it "why is the sky blue"
```

`wally login` opens the console in a browser and waits for you to approve the
machine. `wally logout` deletes the session.

Where the credential is kept depends on the platform, and `WALLY_PROFILE_DIR`
moves it anywhere:

| | Path |
|---|---|
| macOS, Linux | `$XDG_CONFIG_HOME/wally/credentials.json`, or `~/.config/wally` when unset |
| Windows | `%LOCALAPPDATA%\RunAnywhere\Wally\credentials.dat`, encrypted with DPAPI |

`WALLY_CONSOLE_URL` points the CLI at a console API other than the default, and
`WALLY_CONSOLE_WEB_URL` at the page that approves the sign-in. Those are two
different hosts; see AGENTS.md.

This is separate from `wally auth login`, which signs a device in with an API
key rather than a browser. Most people want `wally login`.

## Build from source

Stage a C++ desktop kit from [runanywhere-sdks](https://github.com/RunanywhereAI/runanywhere-sdks). The pin is `cmake/sdk-pin.cmake` (`WALLY_PINNED_SDK_VERSION`).

**C++-only** (`wally-cxx` on Apple; `wally` elsewhere):

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/path/to/kit
cmake --build build
./build/wally version   # ./build/wally-cxx on Apple
./build/wally backends
```

**Apple Silicon product binary** is the Swift MLX host (`build/wally`). Independent clones need the SDK Swift tree (`WALLY_SDK_SWIFT_PATH`) and `WALLY_APPLE_MLX_HOST=ON` (the default):

```bash
export WALLY_SDK_SWIFT_PATH=/path/to/runanywhere-sdks
cmake -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/path/to/kit
cmake --build build
# or: scripts/build/build-mlx.sh build
./build/wally version
./build/wally backends
```

See [CONTRIBUTING.md](./CONTRIBUTING.md).

## Docs

- [docs.runanywhere.ai](https://docs.runanywhere.ai)
- [Discord](https://discord.gg/N359FBbDVd)
- [Hugging Face models](https://huggingface.co/runanywhere)

MIT. See [LICENSE](./LICENSE).
