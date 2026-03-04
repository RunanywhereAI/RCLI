# RCLI — On-Device Voice AI + RAG for macOS

A high-performance, low-latency voice AI system for Apple Silicon. Speak commands, execute macOS actions, and query your documents — 100% on-device, zero cloud dependency.

**Powered by RunAnywhere**

**Platform:** macOS (Apple Silicon M1+)
**Language:** C++17 engine, interactive TUI
**Hardware tested:** Apple M3 Max (36 GB), Apple M4

---

## Install (Homebrew)

```bash
brew tap RunanywhereAI/tap && brew install rcli
rcli setup        # downloads ~700MB of AI models (one-time)
```

## Quick Start

```bash
rcli                                    # interactive TUI (push-to-talk + text)
rcli listen                             # continuous voice control
rcli ask "open Safari"                  # one-shot command
rcli ask "create a note called Ideas"   # triggers macOS action
rcli actions                            # see all 43 available actions
rcli bench                              # benchmark all modalities
```

## Features

- **43 macOS Actions**: Notes, Reminders, iMessage, FaceTime, app control, files, clipboard, screenshots, music, Slack, email, and more
- **Voice-to-Action**: Speak naturally, RCLI classifies intent and executes via hybrid tool calling
- **RAG**: Index your documents for voice Q&A — drag-and-drop in TUI or `rcli rag ingest ~/Documents`
- **Interactive TUI**: Hardware monitoring, model management, benchmarks, actions — all in a terminal dashboard
- **Multiple Models**: Switch between LLMs (Qwen3, Qwen3.5, LFM2), TTS voices (Piper, KittenTTS, Kokoro), and STT engines (Zipformer, Whisper, Parakeet)
- **Real-Time Metrics**: TTFA, tok/s, RTF displayed live in the TUI

---

## Architecture

```
                          ┌──────────────────────────────────────────────────┐
                          │              RCLI Pipeline                       │
                          │                                                  │
  ┌─────────┐   ┌────────┤  ┌─────┐   ┌─────┐   ┌─────┐   ┌─────┐        │   ┌──────────┐
  │   Mic   │──▶│ VAD    │─▶│ STT │──▶│ RAG │──▶│ LLM │──▶│ TTS │────────│──▶│ Speaker  │
  └─────────┘   └────────┤  └─────┘   └─────┘   └─────┘   └─────┘        │   └──────────┘
                          │  Zipformer  Hybrid    Qwen3     Piper          │
                          │  (37ms)    Retrieval  0.6B     (150ms)         │
                          │             (4ms)    (160t/s)                   │
                          └──────────────────────────────────────────────────┘
                                                      ↓
                                              Tool Calling → 43 macOS Actions
```

### Components

| Component | Engine | Model | Purpose |
|-----------|--------|-------|---------|
| **VAD** | Silero VAD | silero_vad.onnx (629 KB) | Speech/silence detection |
| **STT** | Sherpa-ONNX | Zipformer (streaming) / Whisper (offline) | Speech-to-Text |
| **RAG** | Custom C++ | Snowflake Arctic Embed S (34 MB) | Knowledge retrieval |
| **LLM** | llama.cpp | Qwen3 0.6B Q4_K_M (456 MB) | Response generation |
| **TTS** | Sherpa-ONNX/Piper | en_US-amy-medium | Text-to-Speech |
| **Tools** | Hybrid engine | 43 macOS actions | Function calling |

---

## Project Structure

```
RCLI/
├── CMakeLists.txt              Build configuration
├── deps/                       llama.cpp + sherpa-onnx (cloned by setup.sh)
├── scripts/
│   ├── setup.sh                Clone dependencies
│   └── download_models.sh      Download AI models
├── src/
│   ├── engines/                ML engine wrappers (STT, LLM, TTS, VAD, embedding)
│   ├── pipeline/               Orchestrator, sentence detector, text sanitizer
│   ├── rag/                    Vector index, BM25, hybrid retriever, doc processor
│   ├── core/                   Types, ring buffer, memory pool, hardware profiler
│   ├── audio/                  CoreAudio mic/speaker I/O
│   ├── tools/                  Tool calling engine
│   ├── bench/                  Benchmark suite (STT, LLM, TTS, E2E, memory)
│   ├── actions/                43 macOS action implementations
│   ├── api/                    C API (rcli_api.h) — public engine interface
│   ├── cli/                    TUI dashboard (FTXUI), CLI commands
│   └── models/                 Model registries (LLM, TTS, STT)
├── swift/                      macOS SwiftUI app bridge
├── homebrew/                   Homebrew formula + cask
└── .github/workflows/          CI/CD
```

---

## Build from Source

```bash
# 1. Clone dependencies (one-time)
bash scripts/setup.sh

# 2. Download AI models (one-time, ~700MB)
bash scripts/download_models.sh

# 3. Build
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(sysctl -n hw.ncpu)

# 4. Run
./rcli                          # interactive TUI
./rcli --help                   # see all commands
```

## CLI Reference

```
rcli                          Interactive TUI (push-to-talk + text)
rcli listen [--no-speak]      Continuous voice mode
rcli ask <text>               One-shot text command
rcli actions [name]           List actions or show detail
rcli action <name> [json]     Execute action directly
rcli rag ingest <dir>         Index documents for RAG
rcli rag query <text>         Query indexed documents
rcli bench [suite]            Run benchmarks (stt, llm, tts, e2e, rag, all)
rcli models                   Manage all AI models (LLM, STT, TTS)
rcli upgrade-llm              Upgrade to larger LLM
rcli cleanup                  Remove unused models
rcli setup                    Download AI models (~700MB)
rcli info                     Show engine info

Options:
  --models <dir>      Models directory (default: ~/Library/RCLI/models)
  --gpu-layers <n>    GPU layers for LLM (default: 99 = all)
  --ctx-size <n>      LLM context size (default: 4096)
  --no-speak          Text output only, don't speak responses
  --verbose, -v       Show debug logs
```

## TUI Shortcuts

| Key | Action |
|-----|--------|
| **SPACE + ENTER** | Push-to-talk voice recording |
| **M** | Model management (switch, download, info) |
| **A** | Actions browser (browse and execute) |
| **B** | Benchmark runner |
| **R** | RAG management (ingest, clear, delete) |
| **D** | Model cleanup (delete unused models) |
| **P** | Stop all processing (STT, LLM, TTS) |
| **Q** | Quit |

---

## Benchmark Results

> All benchmarks run on **Apple M3 Max** (14-core CPU, 30-core GPU, 36 GB)

| Component | Metric | Value |
|-----------|--------|-------|
| **STT** | Avg Latency | 43.7 ms |
| **STT** | Real-Time Factor | 0.022x |
| **LLM** | First Token (TTFT) | 22.5 ms |
| **LLM** | Throughput | 159.6 tok/s |
| **LLM** | Prompt Eval | 25,699 tok/s |
| **TTS** | Avg Latency | 150.6 ms |
| **RAG** | Hybrid Retrieval | 3.82 ms |
| **RAG** | Cache Hit Rate | 99.9% |
| **E2E** | Total (short response) | **131 ms** |
| **E2E** | First Audio | 59.9 ms |

---

## Models

| Model | Size | Purpose |
|-------|------|---------|
| Qwen3 0.6B Q4_K_M | 456 MB | Default LLM |
| Qwen3.5 0.8B / 2B / 4B | 600MB–2.7GB | Upgrade LLMs |
| Liquid LFM2 1.2B | 731 MB | Tool-calling optimized LLM |
| Snowflake Arctic Embed S | 34 MB | Text embeddings (RAG) |
| Zipformer | ~50 MB | Streaming STT |
| Whisper Base EN | ~140 MB | Offline STT |
| Piper (Amy Medium) | ~60 MB | Default TTS |
| KittenTTS Nano | 90 MB | Upgrade TTS |
| Kokoro English v0.19 | 310 MB | Premium TTS |
| Silero VAD | 0.6 MB | Voice Activity Detection |

---

## Dependencies

- **llama.cpp** — LLM inference with Metal GPU acceleration
- **sherpa-onnx** — STT/TTS/VAD (ONNX Runtime)
- **USearch** — HNSW vector index for RAG
- **FTXUI** — Terminal UI library
- **CoreAudio/Metal/Accelerate/IOKit** — macOS system frameworks
- **CMake 3.15+** — Build system
- **C++17** compiler (Apple Clang)

---

## License

MIT License. See [LICENSE](LICENSE) for details.

Powered by [RunAnywhere AI](https://github.com/RunanywhereAI).
