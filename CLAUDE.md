# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

RCLI is a high-performance, low-latency on-device voice AI system built in C++17 for Apple Silicon. It combines STT (Zipformer/Whisper), LLM (Qwen3 via llama.cpp), TTS (Piper/Kokoro via sherpa-onnx), VAD (Silero), tool calling (43 macOS actions), and RAG into a single unified pipeline with an interactive TUI.

**Platform:** macOS (Apple Silicon with Metal GPU).
**Language:** C++17, Apple Clang.

## Build Commands

```bash
# First-time setup (clones llama.cpp + sherpa-onnx into deps/)
bash scripts/setup.sh

# Download AI models (~700 MB)
bash scripts/download_models.sh

# Build
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(sysctl -n hw.ncpu)
```

## Running

```bash
./build/rcli                                    # Interactive TUI mode
./build/rcli listen                             # Continuous voice mode
./build/rcli ask "open Safari"                  # One-shot text command
./build/rcli ask "create a note called Ideas"   # Triggers macOS action
./build/rcli actions                            # List all actions
./build/rcli bench                              # Run benchmarks (STT, LLM, TTS, E2E)
./build/rcli models                             # Manage LLM/STT/TTS models
./build/rcli rag ingest ~/Documents             # Index docs for RAG
./build/rcli info                               # Show engine info
./build/rcli cleanup                            # Remove unused models
```

## Tests

```bash
cd build && ./rcli_test
```

Testing is primarily done via CLI modes, the interactive TUI, and the built-in benchmark suite.

## Architecture

```
Mic → VAD → STT (Zipformer) → [RAG Retrieval] → LLM (Qwen3) → TTS (Piper) → Speaker
                                                      ↓
                                              Tool Calling → macOS Actions
```

### Source Layout

```
src/
├── engines/        ML engine wrappers (stt, llm, tts, vad, embedding, model_profile)
├── pipeline/       Orchestrator, sentence detector, text sanitizer
├── rag/            Vector index (USearch HNSW), BM25, hybrid retriever, doc processor
├── core/           types.h, ring_buffer.h, memory_pool.h, hardware_profile.h, log.h
├── audio/          CoreAudio mic/speaker I/O, WAV file I/O
├── tools/          Tool calling engine with JSON schema definitions
├── bench/          Benchmark harness (STT, LLM, TTS, E2E, memory)
├── actions/        43 macOS action implementations (AppleScript + shell)
├── api/            C API (rcli_api.h/.cpp) — the engine's public interface
├── cli/            main.cpp, TUI dashboard (FTXUI), model pickers, help, setup commands
├── models/         Model registries (LLM, TTS, STT) with on-demand download
└── test/           Pipeline test harness
```

### Key Design Patterns

- **Orchestrator pattern**: Central class owns all engines and coordinates data flow.
- **Lock-free ring buffers**: Zero-copy audio passing between STT/LLM/TTS threads.
- **Pre-allocated memory pool**: 64 MB arena allocated at init; no runtime malloc.
- **Hardware profiling at startup**: `hardware_profile.h` detects P-cores, E-cores, Metal GPU, RAM.
- **Atomic pipeline state**: `std::atomic<PipelineState>` (IDLE → LISTENING → PROCESSING → SPEAKING → INTERRUPTED).
- **Sentence-level TTS scheduling**: `SentenceDetector` accumulates LLM tokens and flushes complete sentences.
- **Double-buffered TTS**: TTS synthesizes the next sentence while current one plays.
- **System prompt KV caching**: Reuses llama.cpp KV cache state across queries.
- **Hybrid tool calling**: Tier 1 keyword match + Tier 2 LLM-based extraction.

### Threading Model (Live Mode)

- STT thread: feeds mic audio, detects speech endpoints
- LLM thread: waits for STT output, generates tokens
- TTS thread: queues sentences from LLM, double-buffered playback
- Synchronization via `std::condition_variable` + mutex on pending text

### RAG System

Hybrid retrieval combining vector search (USearch HNSW) + BM25 full-text search, fused via Reciprocal Rank Fusion (RRF). Embedding cache (LRU) achieves 99.9% hit rate. Total retrieval latency is ~4 ms over 5K+ chunks.

## Dependencies

All vendored or CMake-fetched (no external package manager):

- **llama.cpp** — LLM + embedding inference with Metal GPU (`deps/llama.cpp`, `add_subdirectory`)
- **sherpa-onnx** — STT/TTS/VAD via ONNX Runtime (`deps/sherpa-onnx`, `add_subdirectory`)
- **USearch v2.16.5** — HNSW vector index (header-only, `FetchContent`)
- **FTXUI v5.0.0** — Terminal UI library (`FetchContent`)
- **CoreAudio/Metal/Accelerate/IOKit** — macOS system frameworks

## Models

| Model | Size | Purpose |
|-------|------|---------|
| Qwen3 0.6B Q4_K_M | 456 MB | Default LLM |
| Qwen3.5 0.8B / 2B / 4B | 600MB–2.7GB | Upgrade LLMs |
| Liquid LFM2 1.2B | 731 MB | Tool-calling LLM |
| Snowflake Arctic Embed S (Q8_0) | 34 MB | Text embeddings (RAG) |
| Zipformer (streaming) | ~50 MB | Live STT |
| Whisper base.en | ~140 MB | Offline STT |
| Piper en_US-amy-medium | ~60 MB | Default TTS |
| KittenTTS / Kokoro | 90MB–500MB | Upgrade TTS voices |
| Silero VAD | 0.6 MB | Voice Activity Detection |

Models are gitignored. Download via `scripts/download_models.sh` or `rcli setup`.
