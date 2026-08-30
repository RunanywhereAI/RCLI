# RunAnywhere CLI

Run open models on your machine.

```bash
rcli pull qwen3
rcli run qwen3
```

Chat, vision, speech, and embeddings — all local. Nothing leaves the device.

## Signing in

Models you have pulled run on this machine and need no account. To use a hosted
model instead, sign in to a RunAnywhere console:

```bash
rcli login          # opens a browser; approve it there
rcli whoami         # who you are, and what you have used this month
rcli logout
```

The terminal never asks for a password. It shows a code, you approve it in the
browser, and it collects an API key with your credit behind it. That key appears
on the console's Cloud keys page and can be revoked there at any time.

Against a console running on your own machine:

```bash
export RCLI_CONSOLE_URL=http://localhost:8002
rcli login
```

Then hand a hosted model to a coding session:

```bash
rcli opencode -m gemma-4
```

If the model is on this machine, rcli serves it locally. If it is not, the
request goes to the console you are signed in to, is checked against your
balance before it runs, and is metered.

## Install

### macOS (Apple Silicon)

```bash
brew install runanywhereai/rcli/rcli
```

or

```bash
curl -fsSL https://raw.githubusercontent.com/RunanywhereAI/RCLI/main/install.sh | sh
```

### From source

Needs a built SDK kit, not SDK source:

```bash
cmake -B build -DRCLI_SDK_KIT=<sdks>/dist/cpp-desktop-macos-arm64
export RCLI_SDK_SWIFT_PATH=<sdks>          # for the MLX backend on Apple
cmake --build build -j8
```

`build/rcli` is the full binary. `build/rcli-cxx` is the same CLI without MLX,
and is what you get if `RCLI_SDK_SWIFT_PATH` is unset.

MLX loads its Metal shaders from `mlx-swift_Cmlx.bundle` next to the executable,
so install the pair together:

```bash
mkdir -p ~/.local/lib/rcli
cp -R build/mlx-swift_Cmlx.bundle build/rcli ~/.local/lib/rcli/
printf '#!/bin/sh\nexec "$HOME/.local/lib/rcli/rcli" "$@"\n' > ~/.local/bin/rcli
chmod +x ~/.local/bin/rcli
```

Copy the binary on its own and MLX will not register.

### Windows (x64)

```powershell
irm https://raw.githubusercontent.com/RunanywhereAI/RCLI/main/install.ps1 | iex
```

### Linux (x86_64)

```bash
curl -fsSL https://raw.githubusercontent.com/RunanywhereAI/RCLI/main/install.sh | sh
```

## Get started

```bash
rcli pull qwen3          # download
rcli run qwen3           # chat
rcli run qwen3 "Hello"   # one-shot
rcli serve qwen3         # OpenAI-compatible API on :8080 (macOS/Linux)
```

`rcli models list --all` is the full catalog. Short names work everywhere (`qwen3`, `llama3.2`, `whisper-tiny`, `piper`, …). Any Hugging Face GGUF works too:

```bash
rcli pull hf.co/Qwen/Qwen3-0.6B-GGUF/Qwen3-0.6B-Q8_0.gguf
```

## Backends

One `rcli` binary. **Catalog models already name their engine** (GGUF → llama.cpp, `mlx-*` → MLX, Core ML → NeuRT, QNN-context → QHexRT). You normally do not pick one.

Override only when you mean it:

```bash
rcli llm generate --engine mlx -m mlx-qwen3 "Hello"
rcli run --engine qhexrt /path/to/lfm2_5_230m_HNPU "Hello"
rcli image generate --engine neurt --prompt "a red cube" --out out.png
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

`rcli backends` is the source of truth for **this** binary. Public bottles never list `neurt` or `qhexrt`. Those engines are private overlays, never Homebrew / GitHub Release assets.

### Where each engine exists

| Backend | macOS Apple Silicon | Windows x64 | Windows ARM64 | Linux x64 |
|---|---|---|---|---|
| [llama.cpp](https://github.com/ggml-org/llama.cpp) | public bottle | public bottle | — | public bottle |
| [MLX](https://github.com/ml-explore/mlx) (Apple GPU) | public bottle (product `rcli`, not `rcli-cxx`) | — | — | — |
| [Sherpa-ONNX](https://github.com/k2-fsa/sherpa-onnx) | public bottle | public bottle | — | public bottle |
| [ONNX Runtime](https://onnxruntime.ai) | public bottle | public bottle | — | public bottle |
| NeuRT (Apple Neural Engine; Core ML is the format) | **overlay** rebuild | — | — | — |
| QHexRT (Qualcomm Hexagon NPU) | — | — | **overlay** rebuild | — |

Public Windows ARM64 kits are commons-only (no llama.cpp / ONNX / Sherpa on MSVC ARM64). Snapdragon NPU is overlay-only. x64 Windows has no Hexagon path.

### Modalities × engines

Yes = this engine implements the primitive. Try = a catalog id that `rcli pull` / a local path can run. Overlay engines still need the matching **on-disk bundle** (compiled `.mlmodelc` tree, or `*_HNPU` / `v81/` QNN-context dir) — a Hugging Face *repo page* is HTML, not a model.

| Modality | Command | llama.cpp | MLX | Sherpa | ONNX | NeuRT | QHexRT |
|---|---|---|---|---|---|---|---|
| LLM | `rcli run` / `llm generate` | yes · `smollm2`, `qwen3` | yes · `mlx-qwen3` | — | — | yes · `lfm2-230m-ane` local Core ML tree | yes · `lfm2-230m-npu` local `*_HNPU` |
| VLM | `rcli vlm generate --image` | yes · `smolvlm2` | yes · `mlx-qwen2-vl` | — | — | — | yes · `internvl-1b-npu` local HNPU |
| TTS | `rcli tts synthesize -o out.wav` | — | yes · `mlx-soprano` | yes · `piper` | — | — | yes · `kitten-micro-npu` local HNPU |
| STT | `rcli stt transcribe audio.wav` | — | yes · `mlx-qwen3-asr` | yes · `whisper-tiny` | — | yes · `parakeet-tdt-v2-ane` local Core ML | yes · `whisper-base-npu` local HNPU |
| VAD | `rcli vad detect audio.wav` | — | — | yes | yes · `silero` | — | — |
| Embeddings | `rcli embed` | yes · `nemotron-3-embed` | yes · `mlx-qwen3-embed` | — | yes · `minilm` | — | yes · `embeddinggemma-npu` local HNPU |
| Rerank | `rcli rerank -d …` | yes · `bge-reranker` | — | — | — | — | yes · `nv-rerank-npu` local HNPU |
| Segmentation | `rcli segment image.ppm` (binary P6 PPM) | — | — | — | yes · `segformer` | — | — |
| Diarization | `rcli diarize audio.wav` | — | — | — | yes · `sortformer` | — | — |
| Image gen | `rcli image generate --prompt … --out …` | — | — | — | — | yes · `sd15` (compiled Core ML zip, not the HF repo HTML) | yes · `cosmos3-diffusion-npu` local HNPU |

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
rcli vlm generate --model smolvlm2 --image photo.png "What is in this picture?"
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
rcli tts synthesize "Hello from the device." -o hello.wav
rcli stt transcribe hello.wav
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

**macOS Apple Silicon** (public bottle): llama.cpp + MLX + Sherpa + ONNX. Pull `qwen3` (GGUF) or `mlx-qwen3` (GPU). Image generation is NeuRT (`sd15`) and only works after the private overlay is linked into product `rcli`.

**Windows x64** (public zip): GGUF / ONNX / Sherpa. No MLX, no NeuRT, no QHexRT.

**Windows ARM64** (Snapdragon): public kit has no llama.cpp/ONNX/Sherpa. The QHexRT overlay runs Hexagon NPU models from a local `*_HNPU` tree. Do not expect `mlx-*`, GGUF, or `sd15` on that binary.

`rcli serve` is macOS and Linux.

Device round-trips are **by modality**, not by engine. `scripts/e2e.sh` always
runs `scripts/e2e-modalities.sh`; public CI leaves the knobs unset and skips.
On a machine that already has models:

```bash
export RUNANYWHERE_HOME=/path/to/home          # already-pulled OSS models
export RCLI_E2E_MODEL_ROOTS=/path/to/hnpu      # *_HNPU / *_ANE / *.mlmodelc trees
bash scripts/e2e-modalities.sh /path/to/rcli   # no --engine required
```

`RCLI_E2E_LLM`, `RCLI_E2E_STT`, `RCLI_E2E_IMAGE`, … pin one primitive. Catalog
ids (`mlx-qwen3`, `whisper-base-npu`) pin the framework; a Hugging Face repo
page is HTML, not a bundle.

## Commands

| | |
|---|---|
| `rcli run` / `rcli chat` | chat (REPL with no prompt) |
| `rcli pull` / `rcli models download` | download |
| `rcli list` / `rcli ls` | local models (`--all` = catalog) |
| `rcli show` | one model |
| `rcli rm` | delete |
| `rcli llm generate` / `stream` | completion |
| `rcli vlm generate --image` | vision |
| `rcli stt transcribe` | speech → text |
| `rcli tts synthesize` | text → WAV |
| `rcli vad detect` | voice activity |
| `rcli embed` | embeddings |
| `rcli rerank` | rerank documents |
| `rcli image generate` | text → image (NeuRT / Apple Silicon) |
| `rcli serve` | OpenAI-compatible HTTP (macOS/Linux) |
| `rcli backends` | registered engines |
| `rcli info` | versions and paths |
| `--engine` | force mlx / llamacpp / sherpa / onnx / neurt / qhexrt |
| `rcli login` / `logout` / `whoami` | sign in to the console that serves upstream models |
| `rcli claude-code` / `claude-desktop` | open Claude against a model |
| `rcli clion` / `rustrover` | point a JetBrains IDE at a model |
| `rcli opencode` | open a coding session against a model |

`rcli --help` and `rcli <command> --help` cover the rest.

## Editors and coding agents

One command points a tool at a model and starts it. There is nothing to
configure by hand:

```bash
rcli claude-code -m qwen3-0.6b
rcli clion -m models/gemma-4-31b-it
rcli claude-desktop -m models/gemma-4-31b-it
```

The model can be one on this machine or one the console serves. Without `-m` the
tool starts the way you already have it configured, and rcli wires nothing.

| Tool | How it is wired |
| --- | --- |
| `claude-code`, `opencode` | `ANTHROPIC_BASE_URL` and `ANTHROPIC_AUTH_TOKEN` in the process |
| `claude-desktop` | a gateway profile in Claude Desktop's third party mode, covering the chat and Cowork tabs |
| `clion`, `rustrover` | AI Assistant's OpenAI-compatible provider, which works without a JetBrains AI subscription |

Two flags go with `-m`. `--serve` holds the endpoint open and prints it instead
of launching anything, which is how a tool nobody has taught rcli about gets
wired up. `--restore` puts Claude Desktop or a JetBrains IDE back the way it was
and starts nothing; a normal run already undoes its own configuration when the
app quits, so this is for the run that was interrupted before it could.

The first `rcli clion` on a machine takes a while, because it installs the AI
Assistant plugin headlessly before starting the IDE. Later runs are quick. That
endpoint sits on a fixed port rather than whatever happened to be free, because
the IDE reads the address once at startup out of a file rcli writes beforehand,
and a port that moved would leave that file naming something dead.

Claude Code and Claude Desktop speak Anthropic's Messages API, while the models
rcli serves speak OpenAI's, so a translator sits between them. It carries tool
definitions out, tool calls back, and the results of those calls out again,
which is what lets an agent on the far side run the tools it was given rather
than describe them. The JetBrains IDEs need no translator, because AI Assistant
speaks OpenAI already.

## Signing in

A model you have not downloaded can still answer, if the console serves it:

```bash
rcli login
rcli whoami
rcli run models/gemma-4-31b-it "why is the sky blue"
```

`rcli login` opens the console in a browser and waits for you to approve the
machine. Credentials land in `~/.config/rcli/credentials.json`. `rcli logout`
deletes them. `RCLI_CONSOLE_URL` points at a console other than the default and
`RCLI_PROFILE_DIR` moves where the credentials are kept.

This is separate from `rcli auth login`, which signs the device in to the
control plane with an API key. The two are being unified; see the auth work in
flight.

## Build from source

Stage a C++ desktop kit from [runanywhere-sdks](https://github.com/RunanywhereAI/runanywhere-sdks). The pin is `cmake/sdk-pin.cmake` (`RCLI_PINNED_SDK_VERSION`).

**C++-only** (`rcli-cxx` on Apple; `rcli` elsewhere):

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/path/to/kit
cmake --build build
./build/rcli version   # ./build/rcli-cxx on Apple
./build/rcli backends
```

**Apple Silicon product binary** is the Swift MLX host (`build/rcli`). Independent clones need the SDK Swift tree (`RCLI_SDK_SWIFT_PATH`) and `RCLI_APPLE_MLX_HOST=ON` (the default):

```bash
export RCLI_SDK_SWIFT_PATH=/path/to/runanywhere-sdks
cmake -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/path/to/kit
cmake --build build
# or: scripts/build-mlx.sh build
./build/rcli version
./build/rcli backends
```

See [CONTRIBUTING.md](./CONTRIBUTING.md).

## Docs

- [docs.runanywhere.ai](https://docs.runanywhere.ai)
- [Discord](https://discord.gg/N359FBbDVd)
- [Hugging Face models](https://huggingface.co/runanywhere)

MIT. See [LICENSE](./LICENSE).
