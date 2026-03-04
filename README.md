<p align="center">
  <pre align="center">
  ██████╗  ██████╗██╗     ██╗
  ██╔══██╗██╔════╝██║     ██║
  ██████╔╝██║     ██║     ██║
  ██╔══██╗██║     ██║     ██║
  ██║  ██║╚██████╗███████╗██║
  ╚═╝  ╚═╝ ╚═════╝╚══════╝╚═╝
  </pre>
  <strong>On-Device Voice AI + RAG for macOS</strong><br>
  <em>Speak commands, control your Mac, query your docs — 100% local, zero cloud.</em>
</p>

<p align="center">
  <a href="#install">Install</a> •
  <a href="#quick-start">Quick Start</a> •
  <a href="#features">Features</a> •
  <a href="#model-management">Models</a> •
  <a href="#rag">RAG</a> •
  <a href="#benchmarks">Benchmarks</a> •
  <a href="#build-from-source">Build</a>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/platform-macOS-blue" alt="macOS">
  <img src="https://img.shields.io/badge/chip-Apple_Silicon-black" alt="Apple Silicon">
  <img src="https://img.shields.io/badge/language-C++17-orange" alt="C++17">
  <img src="https://img.shields.io/badge/inference-100%25_local-green" alt="Local">
  <img src="https://img.shields.io/github/license/RunanywhereAI/RCLI" alt="MIT License">
</p>

---

RCLI is a high-performance voice AI system for Apple Silicon. It runs a complete **STT → LLM → TTS** pipeline entirely on-device with Metal GPU acceleration, executes **40 macOS actions** via natural language, and supports **RAG** over your local documents — all with sub-200ms end-to-end latency.

**No API keys. No internet required. No data leaves your machine.**

## Install

```bash
brew tap RunanywhereAI/rcli https://github.com/RunanywhereAI/RCLI.git
brew install rcli
rcli setup     # downloads default models (~1GB, one-time)
```

Requires macOS 13+ on Apple Silicon (M1 or later).

> **macOS 26 beta:** Homebrew's CLT version check does not yet recognize macOS 26. If `brew install` fails with a Command Line Tools error, install manually from the [latest release](https://github.com/RunanywhereAI/RCLI/releases/latest) tarball or [build from source](#build-from-source).

## Quick Start

```bash
# Interactive TUI — push-to-talk voice + text input
rcli

# Continuous voice mode — hands-free, always listening
rcli listen

# One-shot text commands
rcli ask "open Safari"
rcli ask "create a note called Meeting Notes"
rcli ask "play some jazz on Spotify"
rcli ask "set volume to 40"
rcli ask "toggle dark mode"
rcli ask "take a screenshot"

# Execute actions directly (bypass the LLM)
rcli action create_note '{"title": "Ideas", "body": "My brilliant idea"}'
rcli action open_app '{"app": "Safari"}'
```

## Features

### Voice Pipeline

Full STT → LLM → TTS pipeline running on Metal GPU with three concurrent threads:

- **VAD** — Silero voice activity detection, filters silence in real-time
- **STT** — Zipformer streaming (live mic) + Whisper/Parakeet offline (batch)
- **LLM** — Liquid LFM2 1.2B Tool with system prompt KV caching, Flash Attention
- **TTS** — Double-buffered sentence-level synthesis (next sentence synthesizes while current plays)
- **Tool Calling** — Hybrid: Tier 1 keyword match + Tier 2 LLM-based extraction

### 40 macOS Actions

Control your Mac entirely by voice or text. RCLI classifies your intent and executes actions locally via AppleScript and shell commands.

| Category | Actions |
|----------|---------|
| **Productivity** | `create_note`, `create_reminder`, `run_shortcut` |
| **Communication** | `send_message`, `facetime_call`, `facetime_audio` |
| **Media** | `play_on_spotify`, `play_apple_music`, `play_pause_music`, `next_track`, `previous_track`, `set_music_volume`, `get_now_playing` |
| **System** | `open_app`, `quit_app`, `switch_app`, `set_volume`, `toggle_dark_mode`, `lock_screen`, `screenshot`, `search_files`, `open_settings`, `open_url` |
| **Window** | `close_window`, `minimize_window`, `fullscreen_window`, `get_frontmost_app`, `list_apps` |
| **Clipboard** | `clipboard_read`, `clipboard_write` |
| **Info** | `get_battery`, `get_wifi`, `get_ip_address`, `get_uptime`, `get_disk_usage` |
| **Web** | `search_web`, `search_youtube`, `get_browser_url`, `get_browser_tabs` |
| **Navigation** | `open_maps` |

```bash
rcli actions                    # list all actions with descriptions
rcli actions create_note        # show parameters and examples for one action
```

### Interactive TUI

A full terminal dashboard built with FTXUI:

| Key | Action |
|-----|--------|
| **SPACE → ENTER** | Push-to-talk voice recording |
| **M** | Model management — switch, download, delete models |
| **A** | Actions browser — browse and execute all 40 actions |
| **B** | Benchmark runner — STT, LLM, TTS, E2E, RAG |
| **R** | RAG management — ingest docs, clear index |
| **D** | Cleanup — remove unused models, free disk space |
| **P** | Stop all processing |
| **Q** | Quit |

The TUI includes live hardware monitoring (CPU, GPU, memory, battery, network), real-time performance metrics (TTFT, tok/s, RTF), and a chat interface with voice + text input.

### RAG (Retrieval-Augmented Generation)

Index your local documents and query them with voice or text. RCLI uses a hybrid retrieval system combining **vector search** (USearch HNSW) and **BM25 full-text search**, fused via Reciprocal Rank Fusion.

```bash
# Index a folder of documents
rcli rag ingest ~/Documents/notes

# Query your documents
rcli rag query "What were the key decisions from last week's meeting?"

# Check index status
rcli rag status

# Use RAG in interactive mode
rcli --rag ~/Library/RCLI/index

# Use RAG with one-shot commands
rcli ask --rag ~/Library/RCLI/index "summarize the project plan"
```

| RAG Metric | Value |
|------------|-------|
| Hybrid retrieval latency | **3.82 ms** |
| Embedding cache hit rate | 99.9% |
| Index: USearch HNSW + BM25 | Over 5K+ chunks |
| Embedding model | Snowflake Arctic Embed S (34 MB) |

Documents are chunked, embedded locally, and stored on disk. The embedding model auto-downloads on first use.

## Model Management

RCLI ships with a default model set via `rcli setup`, and provides full CLI-based model management to download, switch, and delete models across all three modalities.

### Default Models (installed by `rcli setup`)

| Component | Model | Size |
|-----------|-------|------|
| **LLM** | Liquid LFM2 1.2B Tool (Q4_K_M) | 731 MB |
| **STT** | Zipformer streaming + Whisper base.en | ~190 MB |
| **TTS** | Piper Lessac (English) | ~60 MB |
| **VAD** | Silero VAD | 0.6 MB |
| **Embeddings** | Snowflake Arctic Embed S (Q8_0) | 34 MB |

### Available LLMs

| Model | Size | Speed | Tool Calling | Notes |
|-------|------|-------|-------------|-------|
| Liquid LFM2 1.2B Tool | 731 MB | ~180 t/s | Excellent | **Default** — purpose-built for tool calling |
| Qwen3 0.6B | 456 MB | ~250 t/s | Basic | Ultra-fast, smallest footprint |
| Qwen3.5 0.8B | 600 MB | ~220 t/s | Basic | Qwen3.5 generation |
| Liquid LFM2 350M | 219 MB | ~350 t/s | Basic | Fastest inference, 128K context |
| Liquid LFM2.5 1.2B Instruct | 731 MB | ~180 t/s | Good | 128K context |
| Liquid LFM2 2.6B | 1.5 GB | ~120 t/s | Good | Stronger conversational |
| Qwen3.5 2B | 1.2 GB | ~150 t/s | Good | Good all-rounder |
| Qwen3 4B | 2.5 GB | ~80 t/s | Good | Smart reasoning |
| Qwen3.5 4B | 2.7 GB | ~75 t/s | Excellent | Best small model, 262K context |

### Available TTS Voices

| Voice | Architecture | Speakers | Quality | Size |
|-------|-------------|----------|---------|------|
| Piper Lessac | VITS | 1 | Good | 60 MB |
| Piper Amy | VITS | 1 | Good | 60 MB |
| KittenTTS Nano | Kitten | 8 | Great | 90 MB |
| Matcha LJSpeech | Matcha | 1 | Great | 100 MB |
| Kokoro English v0.19 | Kokoro | 11 | Excellent | 310 MB |
| Kokoro Multi-lang v1.1 | Kokoro | 103 | Excellent | 500 MB |

### Available STT Models

| Model | Category | Accuracy | Size |
|-------|----------|----------|------|
| Zipformer | Streaming (live mic) | Good | 50 MB |
| Whisper base.en | Offline | ~5% WER | 140 MB |
| Parakeet TDT 0.6B v3 | Offline | ~1.9% WER | 640 MB |

### Model Commands

```bash
# Interactive model management (all modalities)
rcli models

# Jump to a specific modality
rcli models llm
rcli models tts
rcli models stt

# Guided upgrade flows
rcli upgrade-llm        # choose and download a larger LLM
rcli upgrade-stt        # upgrade to Parakeet TDT (~1.9% WER)

# Manage TTS voices
rcli voices             # browse, download, switch voices

# Remove unused models
rcli cleanup            # lists all models with sizes, delete non-active ones
rcli cleanup --all-unused   # remove everything except active models

# Show what's installed
rcli info
```

Models are stored in `~/Library/RCLI/models/`. Your active model selection persists in `~/Library/RCLI/config` and auto-loads on every launch.

## Benchmarks

> All benchmarks on **Apple M3 Max** (14-core CPU, 30-core GPU, 36 GB unified memory)

| Component | Metric | Value |
|-----------|--------|-------|
| **STT** | Avg Latency | 43.7 ms |
| **STT** | Real-Time Factor | 0.022x |
| **LLM** | First Token (TTFT) | 22.5 ms |
| **LLM** | Generation Throughput | 159.6 tok/s |
| **LLM** | Prompt Eval | 25,699 tok/s |
| **TTS** | Avg Latency | 150.6 ms |
| **RAG** | Hybrid Retrieval | 3.82 ms |
| **RAG** | Cache Hit Rate | 99.9% |
| **E2E** | Total (short response) | **131 ms** |
| **E2E** | First Audio Byte | 59.9 ms |

```bash
rcli bench                          # run all benchmarks
rcli bench --suite llm              # LLM only
rcli bench --suite stt,tts          # multiple suites
rcli bench --all-llm --suite llm    # compare all installed LLMs
rcli bench --output results.json    # export results
```

Benchmark suites: `stt`, `llm`, `tts`, `e2e`, `tools`, `rag`, `memory`, `all`.

## Architecture

```
                          ┌──────────────────────────────────────────────────┐
                          │              RCLI Pipeline                       │
                          │                                                  │
  ┌─────────┐   ┌────────┤  ┌─────┐   ┌─────┐   ┌─────┐   ┌─────┐        │   ┌──────────┐
  │   Mic   │──▶│  VAD   │─▶│ STT │──▶│ RAG │──▶│ LLM │──▶│ TTS │────────│──▶│ Speaker  │
  └─────────┘   └────────┤  └─────┘   └─────┘   └─────┘   └─────┘        │   └──────────┘
                          │  Zipformer  Hybrid    LFM2      Piper          │
                          │  (37ms)    Retrieval  1.2B     (150ms)         │
                          │             (4ms)    (180t/s)                   │
                          └──────────────────────────────────────────────────┘
                                                      │
                                              Tool Calling → 40 macOS Actions
```

### Threading Model

Three dedicated threads in live mode, synchronized via condition variables:

| Thread | Responsibility |
|--------|---------------|
| **STT thread** | Feeds mic audio, detects speech endpoints via VAD |
| **LLM thread** | Waits for STT output, generates tokens, dispatches tool calls |
| **TTS thread** | Queues sentences from LLM, double-buffered playback |

### Key Design Decisions

- **64 MB pre-allocated memory pool** — no runtime malloc during inference
- **Lock-free ring buffers** — zero-copy audio passing between threads
- **System prompt KV caching** — reuses llama.cpp KV cache state across queries
- **Sentence-level TTS scheduling** — TTS synthesizes the next sentence while the current one plays
- **Hybrid tool calling** — fast keyword matching (Tier 1) with LLM fallback (Tier 2)
- **Hardware profiling at startup** — detects P-cores, E-cores, Metal GPU, RAM for optimal config

## Project Structure

```
RCLI/
├── CMakeLists.txt
├── src/
│   ├── engines/          STT, LLM, TTS, VAD, embedding engine wrappers
│   ├── pipeline/         Orchestrator, sentence detector, text sanitizer
│   ├── rag/              Vector index, BM25, hybrid retriever, doc processor
│   ├── core/             Types, ring buffer, memory pool, hardware profiler
│   ├── audio/            CoreAudio mic/speaker I/O
│   ├── tools/            Tool calling engine with JSON schema definitions
│   ├── bench/            Benchmark harness (STT, LLM, TTS, E2E, RAG, memory)
│   ├── actions/          40 macOS action implementations
│   ├── api/              C API (rcli_api.h) — public engine interface
│   ├── cli/              TUI dashboard (FTXUI), CLI commands
│   └── models/           Model registries (LLM, TTS, STT)
├── Formula/              Homebrew formula (self-hosted tap)
├── scripts/
│   ├── setup.sh          Clone dependencies
│   ├── download_models.sh  Download AI models
│   └── package.sh        Package binary + dylibs for distribution
└── .github/workflows/    CI/CD release automation
```

## Build from Source

```bash
# 1. Clone
git clone https://github.com/RunanywhereAI/RCLI.git
cd RCLI

# 2. Clone dependencies (one-time)
bash scripts/setup.sh

# 3. Download AI models (one-time, ~1GB)
bash scripts/download_models.sh

# 4. Build
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(sysctl -n hw.ncpu)

# 5. Run
./rcli
```

### Dependencies

All vendored or CMake-fetched (no external package manager required):

| Dependency | Purpose |
|------------|---------|
| [llama.cpp](https://github.com/ggml-org/llama.cpp) | LLM + embedding inference with Metal GPU |
| [sherpa-onnx](https://github.com/k2-fsa/sherpa-onnx) | STT / TTS / VAD via ONNX Runtime |
| [USearch](https://github.com/unum-cloud/usearch) | HNSW vector index for RAG |
| [FTXUI](https://github.com/ArthurSonzogni/FTXUI) | Terminal UI library |
| CoreAudio, Metal, Accelerate, IOKit | macOS system frameworks |

Requires CMake 3.15+ and Apple Clang (C++17).

## CLI Reference

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

## License

MIT License. See [LICENSE](LICENSE) for details.

Built by [RunAnywhere AI](https://github.com/RunanywhereAI).
