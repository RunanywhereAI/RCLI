# RunAnywhere CLI

Run open models on your machine.

```bash
rcli pull qwen3
rcli run qwen3
```

Chat, vision, speech, and embeddings — all local. Nothing leaves the device.

## Install

### macOS (Apple Silicon)

```bash
brew install runanywhereai/rcli/rcli
```

or

```bash
curl -fsSL https://raw.githubusercontent.com/RunanywhereAI/RCLI/main/install.sh | sh
```

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

One `rcli` binary. The kit picks the engine; you do not.

| Backend | macOS (Apple Silicon) | Windows x64 | Windows ARM64 | Linux x64 |
|---|---|---|---|---|
| [llama.cpp](https://github.com/ggml-org/llama.cpp) | yes | yes | — | yes |
| [MLX](https://github.com/ml-explore/mlx) | yes | — | — | — |
| [Sherpa-ONNX](https://github.com/k2-fsa/sherpa-onnx) | yes | yes | — | yes |
| [ONNX Runtime](https://onnxruntime.ai) | yes | yes | — | yes |
| NeuRT (Apple Neural Engine + Core ML) | overlay | — | — | — |
| QHexRT (Qualcomm Hexagon NPU) | — | — | overlay | — |

`rcli backends` prints what this binary actually registered.

MLX is Apple GPU. NeuRT is Apple Neural Engine — Core ML is the stack NeuRT uses, not a separate backend. Image generation (`sd15`) runs on NeuRT. NeuRT and QHexRT are private overlays, not in the public bottle. QHexRT is Snapdragon NPU on Windows ARM64 only (x64 Windows has no Hexagon path). llama.cpp / Sherpa / ONNX do not configure on MSVC ARM64 today, so the public Windows ARM64 kit is commons + the desktop adapter.

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

**macOS Apple Silicon** is the full product: llama.cpp + MLX + Sherpa + ONNX in one binary. Pull `qwen3` (CPU/Metal GGUF) or `mlx-qwen3` (Apple GPU). Image generation (`sd15`) is NeuRT (Apple Neural Engine / Core ML). NeuRT links when the private overlay is present.

**Windows x64** runs the GGUF / ONNX / Sherpa catalog: Qwen, Llama, Gemma, Granite, Whisper, Piper, MiniLM, and the rest of the non-`mlx-*` rows. No MLX, no NeuRT, no QHexRT.

**Windows ARM64** (Snapdragon): public kit has no llama.cpp/ONNX/Sherpa yet. With the QHexRT overlay, Hexagon NPU models run on device. Do not expect `mlx-*` or `sd15` here.

`rcli serve` is macOS and Linux.

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

`rcli --help` and `rcli <command> --help` cover the rest.

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
