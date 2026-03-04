# Contributing to RCLI

Thanks for your interest in contributing. This guide covers how to build, test, and extend RCLI.

## Prerequisites

- macOS 13+ on Apple Silicon (M1 or later)
- CMake 3.15+
- Apple Clang (ships with Xcode or Command Line Tools)

## Build

```bash
git clone https://github.com/RunanywhereAI/RCLI.git && cd RCLI
bash scripts/setup.sh              # clone llama.cpp + sherpa-onnx into deps/
bash scripts/download_models.sh    # download models (~1GB)
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(sysctl -n hw.ncpu)
./rcli
```

Debug build:

```bash
cmake .. -DCMAKE_BUILD_TYPE=Debug
cmake --build . -j$(sysctl -n hw.ncpu)
```

## Test

```bash
cd build
./rcli_test ~/Library/RCLI/models
./rcli_test ~/Library/RCLI/models --actions-only    # fast, no models needed
./rcli_test ~/Library/RCLI/models --llm-only
./rcli_test ~/Library/RCLI/models --stt-only
./rcli_test ~/Library/RCLI/models --tts-only
./rcli_test ~/Library/RCLI/models --api-only
```

The `--actions-only` suite runs without any model downloads and is a good smoke test.

## Architecture Overview

```
Mic --> VAD --> STT --> [RAG] --> LLM --> TTS --> Speaker
                                  |
                           Tool Calling --> macOS Actions
```

Three threads run concurrently in live mode:

1. **STT thread** — captures mic audio, runs VAD, detects speech endpoints
2. **LLM thread** — receives transcribed text, generates tokens, dispatches tool calls
3. **TTS thread** — queues sentences from LLM, double-buffered playback

Synchronization uses `std::condition_variable` + mutex on pending text. Audio passes between threads via lock-free ring buffers.

### Source Layout

```
src/
  engines/     ML engine wrappers (stt, llm, tts, vad, embedding)
  pipeline/    Orchestrator, sentence detector, text sanitizer
  rag/         Vector index, BM25, hybrid retriever, document processor
  core/        types.h, ring_buffer.h, memory_pool.h, hardware_profile.h
  audio/       CoreAudio mic/speaker I/O, WAV file I/O
  tools/       Tool calling engine with JSON schema definitions
  bench/       Benchmark harness (STT, LLM, TTS, E2E, RAG, memory)
  actions/     macOS action implementations (AppleScript + shell)
  api/         C API (rcli_api.h/.cpp) — the engine's public interface
  cli/         main.cpp, TUI dashboard (FTXUI), model pickers, help
  models/      Model registries (LLM, TTS, STT) with on-demand download
  test/        Pipeline test harness
```

### Key Files

| File | What it does |
|------|-------------|
| `src/api/rcli_api.h` | Public C API — all engine functionality exposed here |
| `src/pipeline/orchestrator.h` | Central class that owns all engines and coordinates data flow |
| `src/actions/action_registry.h` | Action registration and dispatch |
| `src/models/model_registry.h` | LLM model definitions (id, URL, size, speed, flags) |
| `src/models/tts_model_registry.h` | TTS voice definitions |
| `src/models/stt_model_registry.h` | STT model definitions |
| `src/tools/tool_engine.h` | Tool call parsing and execution |

## Extension Points

### Adding a New macOS Action

Actions live in `src/actions/`. Each action is a function registered with the `ActionRegistry`.

1. Open `src/actions/macos_actions.h` (or create a new `.h` file)
2. Write your action function:

```cpp
inline ActionResult action_my_feature(const std::string& params_json) {
    // Parse JSON params (use the lightweight JSON helpers in the file)
    // Execute via AppleScript, shell command, or native API
    // Return ActionResult{true, "output text"} or {false, "error message"}
}
```

3. Register it in `src/actions/action_registry.h` inside `register_defaults()`:

```cpp
register_action("my_feature", action_my_feature, ActionDef{
    "my_feature",
    "Description of what it does",
    "category",
    R"({"type":"object","properties":{"param1":{"type":"string"}},"required":["param1"]})"
});
```

4. The action is now available via `rcli actions`, `rcli action my_feature '{...}'`, and automatic LLM tool calling.

### Adding a New LLM Model

Edit `src/models/model_registry.h` and add an entry to the `all_models()` function:

```cpp
{
    /* id            */ "my-model-id",
    /* name          */ "My Model Name",
    /* filename      */ "my-model.gguf",
    /* url           */ "https://huggingface.co/.../resolve/main/my-model.gguf",
    /* family        */ "model-family",
    /* size_mb       */ 500,
    /* priority      */ 10,
    /* speed_est     */ "~200 t/s",
    /* tool_calling  */ "Good",
    /* description   */ "Brief description of the model.",
    /* is_default    */ false,
    /* is_recommended*/ false,
},
```

The model will appear in `rcli models`, `rcli upgrade-llm`, and the TUI models panel.

### Adding a New TTS Voice

Edit `src/models/tts_model_registry.h` and add an entry to `all_tts_models()`. The key fields are:

- `architecture` — the sherpa-onnx TTS backend (`vits`, `kokoro`, `matcha`, `kitten`)
- `dir_name` — subdirectory under `~/Library/RCLI/models/`
- `download_url` — URL to a `.tar.bz2` archive that extracts to `dir_name/`

### Adding a New STT Model

Edit `src/models/stt_model_registry.h`. STT models are either `streaming` (live mic) or `offline` (batch transcription).

## Code Style

- C++17, Apple Clang
- No external package manager — all dependencies are vendored or fetched via CMake
- Avoid emojis in output strings — use plain text markers (`[ok]`, `[PASS]`, `>`, `*`, `Tip:`)
- Header-only where practical for CLI modules (reduces build complexity)
- Use `fprintf(stderr, ...)` for user-facing output (stdout is reserved for machine-parseable output)

## Pull Requests

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/my-change`)
3. Make your changes and ensure the build succeeds
4. Run `./rcli_test` if your changes affect the engine
5. Open a pull request with a clear description of what changed and why

## Good First Issues

If you're looking for a place to start:

- Add a new macOS action (see "Adding a New macOS Action" above)
- Add a new LLM model to the registry
- Improve error messages or help text
- Add a benchmark for a new component
- Write documentation for an under-documented feature
