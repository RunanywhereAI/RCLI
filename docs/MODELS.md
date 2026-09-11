# Models

`wally models list --all` is the live list.

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

