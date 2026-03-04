<p align="center">
  <img src="assets/terminal.png" alt="RCLI" width="700" />
</p>

<h3 align="center">Talk to your Mac, query your docs, no cloud required.</h3>

<p align="center">
  <a href="https://github.com/RunanywhereAI/RCLI"><img src="https://img.shields.io/badge/platform-macOS-blue" alt="macOS"></a>
  <a href="https://github.com/RunanywhereAI/RCLI"><img src="https://img.shields.io/badge/chip-Apple_Silicon-black" alt="Apple Silicon"></a>
  <a href="https://github.com/RunanywhereAI/RCLI"><img src="https://img.shields.io/badge/language-C++17-orange" alt="C++17"></a>
  <a href="https://github.com/RunanywhereAI/RCLI"><img src="https://img.shields.io/badge/inference-100%25_local-green" alt="Local"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-MIT-blue" alt="MIT"></a>
</p>

<p align="center">
  <a href="https://liquid.ai"><img src="https://img.shields.io/badge/Liquid_AI-LFM2-8A2BE2" alt="Liquid AI"></a>
  <a href="https://github.com/QwenLM"><img src="https://img.shields.io/badge/Qwen-3.5-blue" alt="Qwen"></a>
  <a href="https://github.com/openai/whisper"><img src="https://img.shields.io/badge/OpenAI-Whisper-412991" alt="Whisper"></a>
  <a href="https://huggingface.co/nvidia/parakeet-tdt-0.6b-v3"><img src="https://img.shields.io/badge/NVIDIA-Parakeet-76B900" alt="Parakeet"></a>
  <a href="https://github.com/rhasspy/piper"><img src="https://img.shields.io/badge/Piper-TTS-green" alt="Piper"></a>
  <a href="https://github.com/KittenML/KittenTTS"><img src="https://img.shields.io/badge/KittenML-TTS-FF69B4" alt="KittenTTS"></a>
  <a href="https://huggingface.co/hexgrad/Kokoro-82M"><img src="https://img.shields.io/badge/Kokoro-TTS-orange" alt="Kokoro"></a>
</p>

A complete STT + LLM + TTS pipeline running on Apple Silicon with Metal GPU. 40 macOS actions via voice or text. Local RAG over your documents. Sub-200ms end-to-end latency. No cloud, no API keys.

## Table of Contents

- [Demo](#demo)
- [Install](#install)
- [Quick Start](#quick-start)
- [Features](#features)
- [Supported Models](#supported-models)
- [Benchmarks](#benchmarks)
- [Architecture](#architecture)
- [Build from Source](#build-from-source)
- [Contributing](#contributing)
- [License](#license)

## Demo

<!-- TODO: Add a short (~60s) demo video showing interactive TUI, voice commands, and RAG in action. -->
<!-- Recommended: Record with asciinema or screen capture, upload to YouTube/GitHub, embed here. -->

> **Coming soon** — a walkthrough video showing voice commands, RAG queries, and the interactive TUI.

## Install

```bash
brew tap RunanywhereAI/rcli https://github.com/RunanywhereAI/RCLI.git
brew install rcli
rcli setup     # downloads default models (~1GB, one-time)
```

Requires macOS 13+ on Apple Silicon (M1 or later).

> [!NOTE]
> **macOS 26 beta:** Homebrew does not yet recognize macOS 26 CLT versions. If `brew install` fails, download the [latest release](https://github.com/RunanywhereAI/RCLI/releases/latest) tarball and install manually:
>
> ```bash
> brew tap RunanywhereAI/rcli https://github.com/RunanywhereAI/RCLI.git
> cd /tmp && curl -fsSL -o rcli.tar.gz "https://github.com/RunanywhereAI/RCLI/releases/latest/download/rcli-0.1.2-Darwin-arm64.tar.gz"
> tar xzf rcli.tar.gz
> CELLAR="/opt/homebrew/Cellar/rcli/0.1.2"
> rm -rf "$CELLAR" && mkdir -p "$CELLAR/bin" "$CELLAR/lib"
> cp rcli-*/bin/rcli "$CELLAR/bin/" && cp rcli-*/lib/*.dylib "$CELLAR/lib/"
> brew link --overwrite rcli
> rcli setup
> ```

## Quick Start

```bash
rcli                             # interactive TUI (push-to-talk + text)
rcli listen                      # continuous voice mode, always listening
rcli ask "open Safari"           # one-shot text command
rcli ask "create a note called Meeting Notes"
rcli ask "play some jazz on Spotify"
```

Run `rcli actions` to see all 40 available macOS actions, or `rcli --help` for the full CLI reference.

## Features

### Voice Pipeline

A complete STT, LLM, TTS pipeline running on Metal GPU with three concurrent threads:

- **VAD** — Silero voice activity detection, filters silence in real-time
- **STT** — Zipformer streaming (live mic) + Whisper/Parakeet offline (batch)
- **LLM** — Liquid LFM2 1.2B Tool with system prompt KV caching and Flash Attention
- **TTS** — Double-buffered sentence-level synthesis (next sentence synthesizes while current plays)
- **Tool Calling** — Hybrid approach: Tier 1 keyword match + Tier 2 LLM-based extraction

### macOS Actions

Control your Mac by voice or text. RCLI classifies intent and executes 40 actions locally via AppleScript and shell commands.

| Category | Actions |
|----------|---------|
| **Productivity** | `create_note`, `create_reminder`, `run_shortcut` |
| **Communication** | `send_message`, `facetime_call`, `facetime_audio` |
| **Media** | `play_on_spotify`, `play_apple_music`, `play_pause_music`, `next_track`, `previous_track`, `set_music_volume`, `get_now_playing` |
| **System** | `open_app`, `quit_app`, `switch_app`, `set_volume`, `toggle_dark_mode`, `lock_screen`, `screenshot`, `search_files`, `open_settings`, `open_url` |
| **Window** | `close_window`, `minimize_window`, `fullscreen_window`, `get_frontmost_app`, `list_apps` |
| **Info** | `get_battery`, `get_wifi`, `get_ip_address`, `get_uptime`, `get_disk_usage` |
| **Web / Nav** | `search_web`, `search_youtube`, `get_browser_url`, `get_browser_tabs`, `open_maps`, `clipboard_read`, `clipboard_write` |

### RAG (Retrieval-Augmented Generation)

Index local documents and query them by voice or text. Hybrid retrieval combining vector search (USearch HNSW) and BM25 full-text search, fused via Reciprocal Rank Fusion. Retrieval latency is ~4ms over 5K+ chunks.

```bash
rcli rag ingest ~/Documents/notes
rcli rag query "What were the key decisions from last week?"
rcli ask --rag ~/Library/RCLI/index "summarize the project plan"
```

### Interactive TUI

A terminal dashboard built with [FTXUI](https://github.com/ArthurSonzogni/FTXUI) featuring push-to-talk voice input, live hardware monitoring (CPU, GPU, memory), real-time performance metrics, model management, and an actions browser.

## Supported Models

RCLI ships with a default model set (~1GB via `rcli setup`) and supports 20 models across 5 modalities. All models run locally on Apple Silicon with Metal GPU. Use `rcli models` to download, switch, or remove any model.

### LLM

| Model | Provider | Size | Speed | License | Features |
|-------|----------|------|-------|---------|----------|
| [LFM2 1.2B Tool](https://huggingface.co/LiquidAI/LFM2-1.2B-Tool-GGUF) | [Liquid AI](https://liquid.ai) | 731 MB | ~180 t/s | [LFM Open](https://liquid.ai/lfm-license) | tool calling, **default** |
| [LFM2 350M](https://huggingface.co/LiquidAI/LFM2-350M-GGUF) | [Liquid AI](https://liquid.ai) | 219 MB | ~350 t/s | [LFM Open](https://liquid.ai/lfm-license) | fastest inference, 128K context |
| [LFM2.5 1.2B Instruct](https://huggingface.co/LiquidAI/LFM2.5-1.2B-Instruct-GGUF) | [Liquid AI](https://liquid.ai) | 731 MB | ~180 t/s | [LFM Open](https://liquid.ai/lfm-license) | 128K context |
| [LFM2 2.6B](https://huggingface.co/LiquidAI/LFM2-2.6B-GGUF) | [Liquid AI](https://liquid.ai) | 1.5 GB | ~120 t/s | [LFM Open](https://liquid.ai/lfm-license) | stronger conversational, 128K context |
| [Qwen3 0.6B](https://huggingface.co/Qwen/Qwen3-0.6B) | [Alibaba Qwen](https://github.com/QwenLM) | 456 MB | ~250 t/s | [Apache 2.0](https://github.com/QwenLM/Qwen3.5/blob/main/LICENSE) | ultra-fast, smallest footprint |
| [Qwen3.5 0.8B](https://huggingface.co/Qwen/Qwen3.5-0.8B) | [Alibaba Qwen](https://github.com/QwenLM) | 600 MB | ~220 t/s | [Apache 2.0](https://github.com/QwenLM/Qwen3.5/blob/main/LICENSE) | Qwen3.5 generation |
| [Qwen3.5 2B](https://huggingface.co/Qwen/Qwen3.5-2B) | [Alibaba Qwen](https://github.com/QwenLM) | 1.2 GB | ~150 t/s | [Apache 2.0](https://github.com/QwenLM/Qwen3.5/blob/main/LICENSE) | good all-rounder |
| [Qwen3 4B](https://huggingface.co/Qwen/Qwen3-4B) | [Alibaba Qwen](https://github.com/QwenLM) | 2.5 GB | ~80 t/s | [Apache 2.0](https://github.com/QwenLM/Qwen3.5/blob/main/LICENSE) | smart reasoning |
| [Qwen3.5 4B](https://huggingface.co/Qwen/Qwen3.5-4B) | [Alibaba Qwen](https://github.com/QwenLM) | 2.7 GB | ~75 t/s | [Apache 2.0](https://github.com/QwenLM/Qwen3.5/blob/main/LICENSE) | best small model, 262K context |

### TTS

| Voice | Provider | Size | Speakers | License | Features |
|-------|----------|------|----------|---------|----------|
| [Piper Lessac](https://github.com/rhasspy/piper) | [Rhasspy](https://github.com/rhasspy/piper) | 60 MB | 1 | [MIT](https://github.com/rhasspy/piper/blob/master/LICENSE.md) | fast, clear English, **default** |
| [Piper Amy](https://github.com/rhasspy/piper) | [Rhasspy](https://github.com/rhasspy/piper) | 60 MB | 1 | [MIT](https://github.com/rhasspy/piper/blob/master/LICENSE.md) | warm female voice |
| [KittenTTS Nano](https://huggingface.co/KittenML/kitten-tts-nano-0.8) | [KittenML](https://github.com/KittenML/KittenTTS) | 90 MB | 8 | [Apache 2.0](https://github.com/KittenML/KittenTTS/blob/main/LICENSE) | 8 voices (4M/4F), lightweight |
| [Matcha LJSpeech](https://github.com/shivammehta25/Matcha-TTS) | [Matcha-TTS](https://github.com/shivammehta25/Matcha-TTS) | 100 MB | 1 | [MIT](https://github.com/shivammehta25/Matcha-TTS/blob/main/LICENSE) | HiFi-GAN vocoder |
| [Kokoro English v0.19](https://huggingface.co/hexgrad/Kokoro-82M) | [Hexgrad](https://huggingface.co/hexgrad) | 310 MB | 11 | [Apache 2.0](https://huggingface.co/hexgrad/Kokoro-82M) | best English quality |
| [Kokoro Multi-lang v1.1](https://huggingface.co/hexgrad/Kokoro-82M) | [Hexgrad](https://huggingface.co/hexgrad) | 500 MB | 103 | [Apache 2.0](https://huggingface.co/hexgrad/Kokoro-82M) | 103 speakers, Chinese + English |

### STT

| Model | Provider | Size | Accuracy | License | Features |
|-------|----------|------|----------|---------|----------|
| [Zipformer](https://github.com/k2-fsa/sherpa-onnx) | [k2-fsa](https://github.com/k2-fsa/sherpa-onnx) | 50 MB | Good | [Apache 2.0](https://github.com/k2-fsa/sherpa-onnx/blob/master/LICENSE) | streaming (live mic), **default** |
| [Whisper base.en](https://github.com/openai/whisper) | [OpenAI](https://github.com/openai/whisper) | 140 MB | ~5% WER | [MIT](https://github.com/openai/whisper/blob/main/LICENSE) | offline, English, **default** |
| [Parakeet TDT 0.6B v3](https://huggingface.co/nvidia/parakeet-tdt-0.6b-v3) | [NVIDIA](https://huggingface.co/nvidia) | 640 MB | ~1.9% WER | [CC-BY-4.0](https://huggingface.co/nvidia/parakeet-tdt-0.6b-v3) | 25 languages, auto-punctuation |

### VAD and Embeddings

| Model | Provider | Modality | Size | License |
|-------|----------|----------|------|---------|
| [Silero VAD](https://github.com/snakers4/silero-vad) | [Silero](https://github.com/snakers4/silero-vad) | Voice Activity Detection | 0.6 MB | [MIT](https://github.com/snakers4/silero-vad/blob/master/LICENSE) |
| [Snowflake Arctic Embed S](https://github.com/Snowflake-Labs/arctic-embed) | [Snowflake](https://github.com/Snowflake-Labs/arctic-embed) | Text Embeddings (RAG) | 34 MB | [Apache 2.0](https://github.com/Snowflake-Labs/arctic-embed/blob/main/LICENSE) |

### Defaults (installed by `rcli setup`)

`rcli setup` downloads ~1GB: LFM2 1.2B Tool (LLM), Zipformer + Whisper base.en (STT), Piper Lessac (TTS), Silero (VAD), and Snowflake Arctic Embed S (embeddings).

### Model Commands

```bash
rcli models                  # interactive model management (all modalities)
rcli models llm              # jump to LLM management
rcli upgrade-llm             # guided LLM upgrade
rcli upgrade-stt             # upgrade to Parakeet TDT (~1.9% WER)
rcli voices                  # browse, download, switch TTS voices
rcli cleanup                 # remove unused models to free disk space
rcli info                    # show engine info and installed models
```

Models are stored in `~/Library/RCLI/models/`. Active model selection persists across launches in `~/Library/RCLI/config`.

## Benchmarks

All measurements on Apple M3 Max (14-core CPU, 30-core GPU, 36 GB unified memory).

| Component | Metric | Value |
|-----------|--------|-------|
| **STT** | Avg latency | 43.7 ms |
| **STT** | Real-time factor | 0.022x |
| **LLM** | Time to first token | 22.5 ms |
| **LLM** | Generation throughput | 159.6 tok/s |
| **TTS** | Avg latency | 150.6 ms |
| **RAG** | Hybrid retrieval | 3.82 ms |
| **E2E** | Voice-in to audio-out | **131 ms** |

```bash
rcli bench                          # run all benchmarks
rcli bench --suite llm              # LLM only
rcli bench --all-llm --suite llm    # compare all installed LLMs
rcli bench --output results.json    # export to JSON
```

Suites: `stt`, `llm`, `tts`, `e2e`, `tools`, `rag`, `memory`, `all`.

## Architecture

```
Mic → VAD → STT → [RAG] → LLM → TTS → Speaker
                            |
                     Tool Calling → 40 macOS Actions
```

Three dedicated threads in live mode, synchronized via condition variables:

| Thread | Role |
|--------|------|
| STT | Captures mic audio, runs VAD, detects speech endpoints |
| LLM | Receives transcribed text, generates tokens, dispatches tool calls |
| TTS | Queues sentences from LLM, double-buffered playback |

**Design decisions:**

- 64 MB pre-allocated memory pool — no runtime malloc during inference
- Lock-free ring buffers — zero-copy audio transfer between threads
- System prompt KV caching — reuses llama.cpp KV cache across queries
- Sentence-level TTS scheduling — next sentence synthesizes while current plays
- Hardware profiling at startup — detects P/E cores, Metal GPU, RAM for optimal config

### Project Structure

```
src/
  engines/     STT, LLM, TTS, VAD, embedding engine wrappers
  pipeline/    Orchestrator, sentence detector, text sanitizer
  rag/         Vector index, BM25, hybrid retriever, document processor
  core/        Types, ring buffer, memory pool, hardware profiler
  audio/       CoreAudio mic/speaker I/O
  tools/       Tool calling engine with JSON schema definitions
  bench/       Benchmark harness
  actions/     40 macOS action implementations
  api/         C API (rcli_api.h) — public engine interface
  cli/         TUI dashboard (FTXUI), CLI commands
  models/      Model registries (LLM, TTS, STT)
scripts/       setup.sh, download_models.sh, package.sh
Formula/       Homebrew formula (self-hosted tap)
```

## Build from Source

```bash
git clone https://github.com/RunanywhereAI/RCLI.git && cd RCLI
bash scripts/setup.sh              # clone llama.cpp + sherpa-onnx
bash scripts/download_models.sh    # download models (~1GB)
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(sysctl -n hw.ncpu)
./rcli
```

### Dependencies

All vendored or CMake-fetched. No external package manager required.

| Dependency | Purpose |
|------------|---------|
| [llama.cpp](https://github.com/ggml-org/llama.cpp) | LLM + embedding inference with Metal GPU |
| [sherpa-onnx](https://github.com/k2-fsa/sherpa-onnx) | STT / TTS / VAD via ONNX Runtime |
| [USearch](https://github.com/unum-cloud/usearch) | HNSW vector index for RAG |
| [FTXUI](https://github.com/ArthurSonzogni/FTXUI) | Terminal UI library |
| CoreAudio, Metal, Accelerate, IOKit | macOS system frameworks |

Requires CMake 3.15+ and Apple Clang (C++17).

<details>
<summary><strong>CLI Reference</strong></summary>

```
rcli                          Interactive TUI (push-to-talk + text)
rcli listen                   Continuous voice mode (always listening)
rcli ask <text>               One-shot text command
rcli actions [name]           List actions or show detail for one
rcli action <name> [json]     Execute action directly
rcli rag ingest <dir>         Index documents for RAG
rcli rag query <text>         Query indexed documents
rcli rag status               Show index info
rcli models [llm|stt|tts]    Manage AI models
rcli voices                   Manage TTS voices
rcli upgrade-llm              Download a larger LLM
rcli upgrade-stt              Download Parakeet TDT
rcli bench [--suite ...]      Run benchmarks
rcli cleanup                  Remove unused models
rcli setup                    Download default models (~1GB)
rcli info                     Show engine info and installed models

Options:
  --models <dir>      Models directory (default: ~/Library/RCLI/models)
  --rag <index>       Load RAG index for document-grounded answers
  --gpu-layers <n>    GPU layers for LLM (default: 99 = all)
  --ctx-size <n>      LLM context size (default: 4096)
  --no-speak          Text output only (no TTS playback)
  --verbose, -v       Show debug logs
```

</details>

## Contributing

Contributions are welcome. See [CONTRIBUTING.md](CONTRIBUTING.md) for build instructions, architecture overview, and how to add new actions, models, or voices.

## License

MIT License. See [LICENSE](LICENSE) for details.

<p align="center">
  Powered by <a href="https://github.com/RunanywhereAI">RunAnywhere, Inc.</a>
</p>
