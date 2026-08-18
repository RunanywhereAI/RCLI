#include "catalog/catalog.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <iterator>

namespace rcli::catalog {
namespace {

// Extracted from the SDK's built-in catalog (runanywhere-sdks
// rcli/src/catalog/catalog.cpp) rather than retyped, so ids, aliases and byte
// counts are the ones the SDK already ships. It is a SNAPSHOT: once this repo
// links the SDK, All() should read the live registry and this table goes away.
// Do not hand-edit entries here — regenerate.
constexpr Model kModels[] = {
    {"qwen3-0.6b", "qwen3", "Qwen3 0.6B Q8_0", Backend::LlamaCpp, Category::Language, 670040064LL, "https://huggingface.co/Qwen/Qwen3-0.6B-GGUF/resolve/main/Qwen3-0.6B-Q8_0.gguf", Format::Gguf, 4096},
    {"qwen3-1.7b-q4_k_m", "qwen3-1.7b", "Qwen3 1.7B Q4_K_M", Backend::LlamaCpp, Category::Language, 1289748480LL, "https://huggingface.co/unsloth/Qwen3-1.7B-GGUF/resolve/main/Qwen3-1.7B-Q4_K_M.gguf", Format::Gguf, 4096},
    {"qwen3-4b-q4_k_m", "qwen3-4b", "Qwen3 4B Q4_K_M", Backend::LlamaCpp, Category::Language, 2684354560LL, "https://huggingface.co/unsloth/Qwen3-4B-GGUF/resolve/main/Qwen3-4B-Q4_K_M.gguf", Format::Gguf, 4096},
    {"bonsai-1.7b-q1_0", "bonsai-1.7b", "Bonsai-1.7B 1-bit Q1_0 (CPU)", Backend::LlamaCpp, Category::Language, 248302272LL, "https://huggingface.co/prism-ml/Bonsai-1.7B-gguf/resolve/main/Bonsai-1.7B-Q1_0.gguf", Format::Gguf, 4096},
    {"bonsai-4b-q1_0", "bonsai-4b", "Bonsai-4B 1-bit Q1_0 (CPU)", Backend::LlamaCpp, Category::Language, 572270624LL, "https://huggingface.co/prism-ml/Bonsai-4B-gguf/resolve/main/Bonsai-4B-Q1_0.gguf", Format::Gguf, 4096},
    {"bonsai-8b-q1_0", "bonsai-8b", "Bonsai-8B 1-bit Q1_0 (CPU)", Backend::LlamaCpp, Category::Language, 1158654496LL, "https://huggingface.co/prism-ml/Bonsai-8B-gguf/resolve/main/Bonsai-8B-Q1_0.gguf", Format::Gguf, 4096},
    {"bonsai-27b-q1_0", "bonsai-27b", "Bonsai-27B 1-bit Q1_0 (CPU)", Backend::LlamaCpp, Category::Language, 3803452480LL, "https://huggingface.co/prism-ml/Bonsai-27B-gguf/resolve/main/Bonsai-27B-Q1_0.gguf", Format::Gguf, 4096},
    {"ternary-bonsai-1.7b-q2_0-g64", "ternary-bonsai-1.7b", "Ternary-Bonsai-1.7B Q2_0 g64", Backend::LlamaCpp, Category::Language, 490163968LL, "https://huggingface.co/prism-ml/Ternary-Bonsai-1.7B-gguf/resolve/983b5dec2ff16aab79990711ba0f828a499a7e6a/Ternary-Bonsai-1.7B-Q2_0_g64.gguf", Format::Gguf, 4096},
    {"ternary-bonsai-4b-q2_0-g64", "ternary-bonsai-4b", "Ternary-Bonsai-4B Q2_0 g64", Backend::LlamaCpp, Category::Language, 1137806656LL, "https://huggingface.co/prism-ml/Ternary-Bonsai-4B-gguf/resolve/a3eb42bafe873f9686bc97486c43b72ef7d75ec8/Ternary-Bonsai-4B-Q2_0_g64.gguf", Format::Gguf, 4096},
    {"ternary-bonsai-8b-q2_0-g64", "ternary-bonsai-8b", "Ternary-Bonsai-8B Q2_0 g64", Backend::LlamaCpp, Category::Language, 2310125920LL, "https://huggingface.co/prism-ml/Ternary-Bonsai-8B-gguf/resolve/c2aefbeb4b24469cd11579c3384b990404c17a30/Ternary-Bonsai-8B-Q2_0_g64.gguf", Format::Gguf, 4096},
    {"maple-preview-tq1_0-q4_k", "maple-preview", "DeepGrove Maple Preview TQ1_0 + Q4_K head (CPU)", Backend::LlamaCpp, Category::Language, 4984016416LL, "https://huggingface.co/deepgrove/maple-preview-GGUF/resolve/f5466f918e0c50cdb9d4d47a6f35813509a42a30/maple-preview-TQ1_0-head-Q4_K.gguf", Format::Gguf, 4096},
    {"llama-3.2-3b", "llama3.2", "Llama 3.2 3B Instruct Q4_K_M", Backend::LlamaCpp, Category::Language, 2118123520LL, "https://huggingface.co/bartowski/Llama-3.2-3B-Instruct-GGUF/resolve/main/Llama-3.2-3B-Instruct-Q4_K_M.gguf", Format::Gguf, 0},
    {"lfm2-350m-q8_0", "lfm2", "LiquidAI LFM2 350M Q8_0", Backend::LlamaCpp, Category::Language, 419430400LL, "https://huggingface.co/LiquidAI/LFM2-350M-GGUF/resolve/main/LFM2-350M-Q8_0.gguf", Format::Gguf, 2048},
    {"smollm2-360m-q8_0", "smollm2", "SmolLM2 360M Q8_0", Backend::LlamaCpp, Category::Language, 404750336LL, "https://huggingface.co/prithivMLmods/SmolLM2-360M-GGUF/resolve/main/SmolLM2-360M.Q8_0.gguf", Format::Gguf, 2048},
    {"gemma-4-e2b-it-q4_k_m", "gemma4-e2b", "Gemma 4 E2B IT Q4_K_M", Backend::LlamaCpp, Category::Language, 3106738272LL, "https://huggingface.co/unsloth/gemma-4-E2B-it-GGUF/resolve/0314792d7f1f7e229411f620751375812bb9faf2/gemma-4-E2B-it-Q4_K_M.gguf", Format::Gguf, 4096},
    {"gemma-4-e4b-it-q4_k_m", "gemma4-e4b", "Gemma 4 E4B IT Q4_K_M", Backend::LlamaCpp, Category::Language, 4977171584LL, "https://huggingface.co/unsloth/gemma-4-E4B-it-GGUF/resolve/bfc15c382204943c3a8fff0c750b94ae2364d7a3/gemma-4-E4B-it-Q4_K_M.gguf", Format::Gguf, 4096},
    {"gemma-4-12b-it-q4_k_m", "gemma4-12b", "Gemma 4 12B IT Q4_K_M", Backend::LlamaCpp, Category::Language, 7121861440LL, "https://huggingface.co/unsloth/gemma-4-12b-it-GGUF/resolve/fc034cfff751157913579611efad8462ac1be606/gemma-4-12b-it-Q4_K_M.gguf", Format::Gguf, 4096},
    {"gemma-4-26b-a4b-it-q4_k_xl", "gemma4-26b-a4b", "Gemma 4 26B-A4B IT UD-Q4_K_XL (MoE)", Backend::LlamaCpp, Category::Language, 17010980576LL, "https://huggingface.co/unsloth/gemma-4-26B-A4B-it-GGUF/resolve/c099eb48e663fd284577b04978a94ffccb261841/gemma-4-26B-A4B-it-UD-Q4_K_XL.gguf", Format::Gguf, 4096},
    {"gemma-4-31b-it-q4_k_m", "gemma4-31b", "Gemma 4 31B IT Q4_K_M", Backend::LlamaCpp, Category::Language, 18323733440LL, "https://huggingface.co/unsloth/gemma-4-31B-it-GGUF/resolve/c1ac76e99d5513b141e8adde7288b85c3f9c32ec/gemma-4-31B-it-Q4_K_M.gguf", Format::Gguf, 4096},
    {"gemma-4-31b-it-ud-q2_k_xl", "gemma4-31b-q2", "Gemma 4 31B IT UD-Q2_K_XL", Backend::LlamaCpp, Category::Language, 11774991296LL, "https://huggingface.co/unsloth/gemma-4-31B-it-GGUF/resolve/c1ac76e99d5513b141e8adde7288b85c3f9c32ec/gemma-4-31B-it-UD-Q2_K_XL.gguf", Format::Gguf, 4096},
    {"qwen3.6-35b-a3b-q4_k_m", "qwen3.6-35b", "Qwen3.6 35B-A3B UD-Q4_K_M (MoE)", Backend::LlamaCpp, Category::Language, 22134528992LL, "https://huggingface.co/unsloth/Qwen3.6-35B-A3B-GGUF/resolve/a483e9e6cbd595906af30beda3187c2663a1118c/Qwen3.6-35B-A3B-UD-Q4_K_M.gguf", Format::Gguf, 4096},
    {"qwen3.8-27b-q4_k_m", "qwen3.8-27b", "Qwen3.8 27B Q4_K_M", Backend::LlamaCpp, Category::Language, 17106775008LL, "https://huggingface.co/unsloth/Qwen3.8-27B-GGUF/resolve/f1bfb127c64f7072bdd2cad55f258b9c8b2910fe/Qwen3.8-27B-Q4_K_M.gguf", Format::Gguf, 4096},
    {"granite-4.1-3b-q4_k_m", "granite4.1-3b", "IBM Granite 4.1 3B Q4_K_M", Backend::LlamaCpp, Category::Language, 2099502400LL, "https://huggingface.co/unsloth/granite-4.1-3b-GGUF/resolve/5b88826e4b80789548180f8faab39c5cf68772c9/granite-4.1-3b-Q4_K_M.gguf", Format::Gguf, 4096},
    {"granite-4.1-8b-q4_k_m", "granite4.1-8b", "IBM Granite 4.1 8B Q4_K_M", Backend::LlamaCpp, Category::Language, 5347915136LL, "https://huggingface.co/unsloth/granite-4.1-8b-GGUF/resolve/6f9671f73eb03273bc09319194b8a4e810e03a8f/granite-4.1-8b-Q4_K_M.gguf", Format::Gguf, 4096},
    {"granite-4.1-30b-q4_k_m", "granite4.1-30b", "IBM Granite 4.1 30B Q4_K_M", Backend::LlamaCpp, Category::Language, 17490241472LL, "https://huggingface.co/unsloth/granite-4.1-30b-GGUF/resolve/6cb34f31b11ca4c1433de1af7391dac46de4e666/granite-4.1-30b-Q4_K_M.gguf", Format::Gguf, 4096},
    {"smolvlm2-256m-video-instruct-q8_0", "smolvlm2", "SmolVLM2 256M Video Instruct Q8_0", Backend::LlamaCpp, Category::Multimodal, 440401920LL, "", Format::Gguf, 2048},
    {"lfm2-vl-450m-q8_0", "lfm2-vl", "LFM2-VL 450M Q8_0", Backend::LlamaCpp, Category::Multimodal, 629145600LL, "", Format::Gguf, 0},
    {"lfm2.5-vl-3b-q4_k_m", "lfm2.5-vl", "LFM2.5-VL 3B Q4_K_M", Backend::LlamaCpp, Category::Multimodal, 2257563360LL, "", Format::Gguf, 4096},
    {"qwen2-vl-2b-instruct-q4_k_m", "qwen2-vl", "Qwen2-VL 2B Instruct Q4_K_M", Backend::LlamaCpp, Category::Multimodal, 1887436800LL, "", Format::Gguf, 2048},
    {"fara1.5-4b-q4_k_m", "fara", "Fara1.5 4B Computer-Use Agent Q4_K_M", Backend::LlamaCpp, Category::Multimodal, 3460300800LL, "", Format::Gguf, 4096},
    {"muse-glimmer-30b-q4_k_xl", "muse-glimmer", "Muse Glimmer 30B UD-Q4_K_XL", Backend::LlamaCpp, Category::Multimodal, 17929907456LL, "", Format::Gguf, 4096},
    {"nemotron-3-nano-omni-30b-a3b-reasoning-q4_k_m", "nemotron-omni", "NVIDIA Nemotron-3-Nano-Omni 30B-A3B Reasoning UD-Q4_K_M (vision, MoE)", Backend::LlamaCpp, Category::Multimodal, 25474563776LL, "", Format::Gguf, 4096},
    {"sherpa-onnx-whisper-tiny.en", "whisper-tiny", "Whisper Tiny English (Sherpa-ONNX)", Backend::Sherpa, Category::SpeechRecognition, 78643200LL, "https://github.com/RunanywhereAI/sherpa-onnx/releases/download/runanywhere-models-v1/sherpa-onnx-whisper-tiny.en.tar.gz", Format::Onnx, 0},
    {"sherpa-nemo-parakeet-tdt-0.6b-v2-int8", "parakeet-tdt-v2", "NVIDIA Parakeet TDT 0.6B v2 INT8 (Sherpa-ONNX)", Backend::Sherpa, Category::SpeechRecognition, 661190513LL, "", Format::Onnx, 0},
    {"sherpa-nemo-parakeet-tdt-0.6b-v3-int8", "parakeet-tdt-v3", "NVIDIA Parakeet TDT 0.6B v3 INT8 (Sherpa-ONNX)", Backend::Sherpa, Category::SpeechRecognition, 670478772LL, "", Format::Onnx, 0},
    {"sherpa-nemo-parakeet-ctc-1.1b-int8", "parakeet-ctc", "NVIDIA Parakeet CTC 1.1B INT8 (Sherpa-ONNX)", Backend::Sherpa, Category::SpeechRecognition, 1110024519LL, "", Format::Onnx, 0},
    {"sherpa-nemo-canary-180m-flash-int8", "canary-180m", "NVIDIA Canary 180M Flash INT8 (Sherpa-ONNX)", Backend::Sherpa, Category::SpeechRecognition, 207170046LL, "", Format::Onnx, 0},
    {"sherpa-nemotron-3.5-asr-streaming-0.6b-320ms-int8", "nemotron-asr-streaming", "NVIDIA Nemotron 3.5 Streaming ASR 0.6B 320ms INT8 (Sherpa-ONNX)", Backend::Sherpa, Category::SpeechRecognition, 682215471LL, "", Format::Onnx, 0},
    {"vits-piper-en_US-lessac-medium", "piper", "Piper TTS US English (Lessac Medium)", Backend::Sherpa, Category::SpeechSynthesis, 68157440LL, "https://github.com/RunanywhereAI/sherpa-onnx/releases/download/runanywhere-models-v1/vits-piper-en_US-lessac-medium.tar.gz", Format::Onnx, 0},
    {"sherpa-supertonic-3-tts-int8", "supertonic", "Supertone Supertonic v3 TTS INT8 (Sherpa-ONNX)", Backend::Sherpa, Category::SpeechSynthesis, 145295768LL, "", Format::Onnx, 0},
    {"silero-vad", "silero", "Silero VAD", Backend::Onnx, Category::VoiceActivityDetection, 2327524LL, "https://github.com/snakers4/silero-vad/raw/master/src/silero_vad/data/silero_vad.onnx", Format::Onnx, 0},
    {"diar-streaming-sortformer-4spk-v2.1", "sortformer", "NVIDIA Streaming Sortformer 4-Speaker v2.1", Backend::Onnx, Category::SpeakerDiarization, 492242946LL, "https://huggingface.co/cgus/diar_streaming_sortformer_4spk-v2.1-onnx/resolve/main/diar_streaming_sortformer_4spk-v2.1.onnx", Format::Onnx, 0},
    {"segformer-b0-ade-512", "segformer", "SegFormer B0 ADE20K 512 (Semantic Segmentation)", Backend::Onnx, Category::SemanticSegmentation, 15335446LL, "https://huggingface.co/Xenova/segformer-b0-finetuned-ade-512-512/resolve/main/onnx/model.onnx", Format::Onnx, 0},
    {"nemotron-3-embed-1b-q4_k_m", "nemotron-3-embed", "NVIDIA Nemotron 3 Embed 1B Q4_K_M", Backend::LlamaCpp, Category::Embedding, 749352096LL, "https://huggingface.co/zenmagnets/Nemotron-3-Embed-1B-Q4_K_M-GGUF/resolve/06df1fde6f7009c91f6cc3cd520081921929a678/nemotron-3-embed-1b-q4_k_m.gguf", Format::Gguf, 0},
    {"llama-nemotron-embed-1b-v2-q4_k_m", "llama-nemotron-embed", "NVIDIA Llama Nemotron Embed 1B v2 Q4_K_M", Backend::LlamaCpp, Category::Embedding, 807690624LL, "https://huggingface.co/mykor/llama-nemotron-embed-1b-v2-GGUF/resolve/bf7c9832b1d76f86777379e58b7b74805ee58006/llama-nemotron-embed-1B-v2-Q4_K_M.gguf", Format::Gguf, 0},
    {"llama-embed-nemotron-8b-q4_k_m", "llama-embed-nemotron", "NVIDIA Llama Embed Nemotron 8B Q4_K_M", Backend::LlamaCpp, Category::Embedding, 4625233184LL, "https://huggingface.co/mradermacher/llama-embed-nemotron-8b-GGUF/resolve/e7ae3cbae4f7693bbd75ec959bf293f39e1f2e25/llama-embed-nemotron-8b.Q4_K_M.gguf", Format::Gguf, 0},
    {"all-minilm-l6-v2", "minilm", "All-MiniLM-L6-v2 (Embeddings)", Backend::Onnx, Category::Embedding, 94371840LL, "", Format::Onnx, 0},
    {"bge-reranker-v2-m3-q4_k_m", "bge-reranker", "BGE Reranker v2-m3 Q4_K_M (Reranking)", Backend::LlamaCpp, Category::Embedding, 438376864LL, "https://huggingface.co/gpustack/bge-reranker-v2-m3-GGUF/resolve/main/bge-reranker-v2-m3-Q4_K_M.gguf", Format::Gguf, 0},
    {"stable-diffusion-v1-5-coreml", "sd15", "Stable Diffusion 1.5 (CoreML)", Backend::NeuRT, Category::ImageGeneration, 1258291200LL, "https://huggingface.co/apple/coreml-stable-diffusion-v1-5-palettized", Format::Mlpackage, 0},
    {"mlx-qwen3-0.6b-4bit", "mlx-qwen3", "Qwen3 0.6B 4-bit (MLX)", Backend::Mlx, Category::Language, 351383618LL, "", Format::Safetensors, 4096},
    {"mlx-maple-preview-2bit", "mlx-maple-preview", "DeepGrove Maple Preview 2-bit (MLX)", Backend::Mlx, Category::Language, 5330252282LL, "", Format::Safetensors, 128000},
    {"mlx-llama-3.1-nemotron-nano-8b-v1-4bit", "mlx-nemotron-nano", "NVIDIA Llama 3.1 Nemotron Nano 8B 4-bit (MLX)", Backend::Mlx, Category::Language, 4534806075LL, "", Format::Safetensors, 131072},
    {"mlx-nemotron-mini-4b-instruct-4bit", "mlx-nemotron-mini", "NVIDIA Nemotron Mini 4B Instruct 4-bit (MLX)", Backend::Mlx, Category::Language, 2392679103LL, "", Format::Safetensors, 4096},
    {"mlx-bonsai-1.7b-1bit", "mlx-bonsai-1.7b", "MLX Bonsai-1.7B 1-bit", Backend::Mlx, Category::Language, 269060904LL, "", Format::Safetensors, 4096},
    {"mlx-bonsai-4b-1bit", "mlx-bonsai-4b", "MLX Bonsai-4B 1-bit", Backend::Mlx, Category::Language, 628865840LL, "", Format::Safetensors, 4096},
    {"mlx-bonsai-8b-1bit", "mlx-bonsai-8b", "MLX Bonsai-8B 1-bit", Backend::Mlx, Category::Language, 1280131424LL, "", Format::Safetensors, 4096},
    {"mlx-bonsai-27b-1bit", "mlx-bonsai", "MLX Bonsai-27B 1-bit", Backend::Mlx, Category::Language, 5129115752LL, "", Format::Safetensors, 4096},
    {"mlx-ternary-bonsai-1.7b-2bit", "mlx-ternary-bonsai-1.7b", "MLX Ternary-Bonsai-1.7B 2-bit", Backend::Mlx, Category::Language, 484049216LL, "", Format::Safetensors, 4096},
    {"mlx-ternary-bonsai-4b-2bit", "mlx-ternary-bonsai-4b", "MLX Ternary-Bonsai-4B 2-bit", Backend::Mlx, Category::Language, 1131565944LL, "", Format::Safetensors, 4096},
    {"mlx-ternary-bonsai-8b-2bit", "mlx-ternary-bonsai-8b", "MLX Ternary-Bonsai-8B 2-bit", Backend::Mlx, Category::Language, 2303661704LL, "", Format::Safetensors, 4096},
    {"mlx-ternary-bonsai-27b-2bit", "mlx-ternary-bonsai-27b", "MLX Ternary-Bonsai-27B 2-bit", Backend::Mlx, Category::Language, 8490785104LL, "", Format::Safetensors, 4096},
    {"mlx-llama-3.2-1b-instruct-4bit", "mlx-llama3.2", "Llama 3.2 1B Instruct 4-bit (MLX)", Backend::Mlx, Category::Language, 712575975LL, "", Format::Safetensors, 0},
    {"mlx-qwen2-vl-2b-instruct-4bit", "mlx-qwen2-vl", "Qwen2-VL 2B Instruct 4-bit (MLX)", Backend::Mlx, Category::Multimodal, 1261853827LL, "", Format::Safetensors, 2048},
    {"mlx-fastvlm-0.5b-bf16", "mlx-fastvlm", "FastVLM 0.5B bf16 (MLX)", Backend::Mlx, Category::Multimodal, 1256926974LL, "", Format::Safetensors, 2048},
    {"mlx-lfm2.5-vl-3b-4bit", "mlx-lfm2.5-vl", "LFM2.5-VL 3B 4-bit (MLX)", Backend::Mlx, Category::Multimodal, 2388258432LL, "", Format::Safetensors, 4096},
    {"mlx-qwen3-embedding-0.6b-4bit-dwq", "mlx-qwen3-embed", "Qwen3 Embedding 0.6B 4-bit DWQ (MLX)", Backend::Mlx, Category::Embedding, 351230811LL, "", Format::Safetensors, 0},
    {"mlx-qwen3-asr-0.6b-8bit", "mlx-qwen3-asr", "Qwen3-ASR 0.6B 8-bit (MLX)", Backend::Mlx, Category::SpeechRecognition, 1010773761LL, "", Format::Safetensors, 0},
    {"mlx-glm-asr-nano-2512-4bit", "mlx-glm-asr", "GLM-ASR Nano 2512 4-bit (MLX)", Backend::Mlx, Category::SpeechRecognition, 1288437789LL, "", Format::Safetensors, 0},
    {"mlx-parakeet-ctc-1.1b", "mlx-parakeet-ctc", "NVIDIA Parakeet CTC 1.1B (MLX)", Backend::Mlx, Category::SpeechRecognition, 4250718357LL, "", Format::Safetensors, 0},
    {"mlx-parakeet-tdt-0.6b-v2", "mlx-parakeet-tdt-v2", "NVIDIA Parakeet TDT 0.6B v2 (MLX)", Backend::Mlx, Category::SpeechRecognition, 2471596080LL, "", Format::Safetensors, 0},
    {"mlx-parakeet-tdt-0.6b-v3", "mlx-parakeet-tdt-v3", "NVIDIA Parakeet TDT 0.6B v3 (MLX)", Backend::Mlx, Category::SpeechRecognition, 2508532829LL, "", Format::Safetensors, 0},
    {"mlx-parakeet-rnnt-1.1b", "mlx-parakeet-rnnt", "NVIDIA Parakeet RNNT 1.1B (MLX)", Backend::Mlx, Category::SpeechRecognition, 4282283914LL, "", Format::Safetensors, 0},
    {"mlx-nemotron-3.5-asr-streaming-0.6b-8bit", "mlx-nemotron-asr", "NVIDIA Nemotron 3.5 Streaming ASR 0.6B 8-bit (MLX)", Backend::Mlx, Category::SpeechRecognition, 755758528LL, "", Format::Safetensors, 0},
    {"mlx-qwen3-tts-12hz-0.6b-base-8bit", "mlx-qwen3-tts", "Qwen3-TTS 12Hz 0.6B Base 8-bit (MLX)", Backend::Mlx, Category::SpeechSynthesis, 1991299138LL, "", Format::Safetensors, 0},
    {"mlx-soprano-1.1-80m-5bit", "mlx-soprano", "Soprano 1.1 80M 5-bit (MLX)", Backend::Mlx, Category::SpeechSynthesis, 82220814LL, "", Format::Safetensors, 0},
    {"mlx-gemma-4-e2b-it-4bit", "mlx-gemma4-e2b", "Gemma 4 E2B IT 4-bit (MLX)", Backend::Mlx, Category::Language, 3550670554LL, "", Format::Safetensors, 4096},
    {"mlx-gemma-4-e4b-it-qat-4bit", "mlx-gemma4-e4b", "Gemma 4 E4B IT QAT 4-bit (MLX)", Backend::Mlx, Category::Language, 6798307742LL, "", Format::Safetensors, 4096},
    {"mlx-gemma-4-12b-it-qat-4bit", "mlx-gemma4-12b", "Gemma 4 12B IT QAT 4-bit (MLX)", Backend::Mlx, Category::Language, 10987772430LL, "", Format::Safetensors, 4096},
    {"mlx-gemma-4-26b-a4b-it-4bit", "mlx-gemma4-26b-a4b", "Gemma 4 26B-A4B IT 4-bit (MLX, MoE)", Backend::Mlx, Category::Language, 15341205776LL, "", Format::Safetensors, 4096},
    {"mlx-gemma-4-31b-it-4bit", "mlx-gemma4-31b", "Gemma 4 31B IT 4-bit (MLX)", Backend::Mlx, Category::Language, 18412016676LL, "", Format::Safetensors, 4096},
    {"mlx-qwen3.6-35b-a3b-4bit", "mlx-qwen3.6-35b", "Qwen3.6 35B-A3B 4-bit (MLX, MoE)", Backend::Mlx, Category::Language, 20402204271LL, "", Format::Safetensors, 4096},
    {"mlx-qwen3.8-27b-4bit", "mlx-qwen3.8-27b", "Qwen3.8 27B 4-bit (MLX)", Backend::Mlx, Category::Language, 16054541349LL, "", Format::Safetensors, 4096},
    {"mlx-granite-4.1-3b-4bit", "mlx-granite4.1-3b", "IBM Granite 4.1 3B 4-bit (MLX)", Backend::Mlx, Category::Language, 2127162429LL, "", Format::Safetensors, 4096},
    {"mlx-granite-4.1-8b-4bit", "mlx-granite4.1-8b", "IBM Granite 4.1 8B 4-bit (MLX)", Backend::Mlx, Category::Language, 5238406779LL, "", Format::Safetensors, 4096},
    {"mlx-granite-4.1-30b-4bit", "mlx-granite4.1-30b", "IBM Granite 4.1 30B 4-bit (MLX)", Backend::Mlx, Category::Language, 18041976573LL, "", Format::Safetensors, 4096},
};

}  // namespace

std::span<const Model> All() {
    return kModels;
}

std::string_view Label(Backend backend) {
    switch (backend) {
        case Backend::LlamaCpp: return "llama.cpp";
        case Backend::Mlx: return "MLX";
        case Backend::NeuRT: return "NeuRT";
        case Backend::Onnx: return "ONNX";
        case Backend::Sherpa: return "Sherpa";
    }
    return "unknown";
}

bool Installable(const Model& model) {
    return !model.url.empty();
}

std::string_view Label(Format format) {
    switch (format) {
        case Format::Gguf: return "GGUF";
        case Format::Onnx: return "ONNX";
        case Format::Safetensors: return "safetensors";
        case Format::Mlpackage: return "mlpackage";
        case Format::Unspecified: return "";
    }
    return "";
}

std::string_view Label(Category category) {
    switch (category) {
        case Category::Language: return "language";
        case Category::Multimodal: return "vision";
        case Category::SpeechRecognition: return "speech";
        case Category::SpeechSynthesis: return "voice";
        case Category::VoiceActivityDetection: return "vad";
        case Category::SpeakerDiarization: return "diarize";
        case Category::SemanticSegmentation: return "segment";
        case Category::Embedding: return "embedding";
        case Category::ImageGeneration: return "image";
    }
    return "other";
}

std::string HumanSize(std::int64_t bytes) {
    constexpr std::int64_t kMib = 1024 * 1024;
    constexpr std::int64_t kGib = kMib * 1024;
    char buffer[32];
    if (bytes >= kGib) {
        std::snprintf(buffer, sizeof(buffer), "%.1f GB",
                      static_cast<double>(bytes) / static_cast<double>(kGib));
    } else {
        std::snprintf(buffer, sizeof(buffer), "%lld MB",
                      static_cast<long long>((bytes + kMib / 2) / kMib));
    }
    return buffer;
}

std::vector<const Model*> Search(std::string_view query) {
    const auto fold = [](std::string_view text) {
        std::string lowered(text);
        std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return lowered;
    };
    const std::string needle = fold(query);

    std::vector<const Model*> hits;
    hits.reserve(std::size(kModels));
    for (const Model& model : kModels) {
        if (needle.empty() || fold(model.id).find(needle) != std::string::npos ||
            fold(model.alias).find(needle) != std::string::npos ||
            fold(model.name).find(needle) != std::string::npos) {
            hits.push_back(&model);
        }
    }
    return hits;
}

}  // namespace rcli::catalog
