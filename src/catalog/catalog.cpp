#include "catalog/catalog.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <iterator>

namespace rcli::catalog {
namespace {

constexpr File kFara15GgufFiles[] = {
    {"https://huggingface.co/runanywhere/Fara1.5-4B-GGUF/resolve/main/Fara1.5-4B-Q4_K_M.gguf", "Fara1.5-4B-Q4_K_M.gguf", true, 0},
    {"https://huggingface.co/runanywhere/Fara1.5-4B-GGUF/resolve/main/mmproj-Fara1.5-4B-f16.gguf", "mmproj-Fara1.5-4B-f16.gguf", true, 0},
};

constexpr File kLfm2VlFiles[] = {
    {"https://huggingface.co/runanywhere/LFM2-VL-450M-GGUF/resolve/main/LFM2-VL-450M-Q8_0.gguf", "LFM2-VL-450M-Q8_0.gguf", true, 0},
    {"https://huggingface.co/runanywhere/LFM2-VL-450M-GGUF/resolve/main/mmproj-LFM2-VL-450M-Q8_0.gguf", "mmproj-LFM2-VL-450M-Q8_0.gguf", true, 0},
};

constexpr File kLfm2_5Vl3BFiles[] = {
    {"https://huggingface.co/LiquidAI/LFM2.5-VL-3B-GGUF/resolve/main/LFM2.5-VL-3B-Q4_K_M.gguf", "LFM2.5-VL-3B-Q4_K_M.gguf", true, 1674454240LL},
    {"https://huggingface.co/LiquidAI/LFM2.5-VL-3B-GGUF/resolve/main/mmproj-LFM2.5-VL-3B-Q8_0.gguf", "mmproj-LFM2.5-VL-3B-Q8_0.gguf", true, 583109120LL},
};

constexpr File kMiniLmFiles[] = {
    {"https://huggingface.co/Xenova/all-MiniLM-L6-v2/resolve/main/onnx/model.onnx", "model.onnx", true, 0},
    {"https://huggingface.co/Xenova/all-MiniLM-L6-v2/resolve/main/vocab.txt", "vocab.txt", true, 0},
};

constexpr File kMlxBonsai1_7B1BitFiles[] = {
    {"https://huggingface.co/prism-ml/Bonsai-1.7B-mlx-1bit/resolve/main/chat_template.jinja", "chat_template.jinja", true, 0},
    {"https://huggingface.co/prism-ml/Bonsai-1.7B-mlx-1bit/resolve/main/config.json", "config.json", true, 0},
    {"https://huggingface.co/prism-ml/Bonsai-1.7B-mlx-1bit/resolve/main/merges.txt", "merges.txt", true, 0},
    {"https://huggingface.co/prism-ml/Bonsai-1.7B-mlx-1bit/resolve/main/model.safetensors", "model.safetensors", true, 0},
    {"https://huggingface.co/prism-ml/Bonsai-1.7B-mlx-1bit/resolve/main/model.safetensors.index.json", "model.safetensors.index.json", true, 0},
    {"https://huggingface.co/prism-ml/Bonsai-1.7B-mlx-1bit/resolve/main/tokenizer.json", "tokenizer.json", true, 0},
    {"https://huggingface.co/prism-ml/Bonsai-1.7B-mlx-1bit/resolve/main/tokenizer_config.json", "tokenizer_config.json", true, 0},
    {"https://huggingface.co/prism-ml/Bonsai-1.7B-mlx-1bit/resolve/main/vocab.json", "vocab.json", true, 0},
};

constexpr File kMlxBonsai27B1BitFiles[] = {
    {"https://huggingface.co/prism-ml/Bonsai-27B-mlx-1bit/resolve/main/chat_template.jinja", "chat_template.jinja", true, 0},
    {"https://huggingface.co/prism-ml/Bonsai-27B-mlx-1bit/resolve/main/config.json", "config.json", true, 0},
    {"https://huggingface.co/prism-ml/Bonsai-27B-mlx-1bit/resolve/main/merges.txt", "merges.txt", true, 0},
    {"https://huggingface.co/prism-ml/Bonsai-27B-mlx-1bit/resolve/main/model.safetensors", "model.safetensors", true, 0},
    {"https://huggingface.co/prism-ml/Bonsai-27B-mlx-1bit/resolve/main/model.safetensors.index.json", "model.safetensors.index.json", true, 0},
    {"https://huggingface.co/prism-ml/Bonsai-27B-mlx-1bit/resolve/main/tokenizer.json", "tokenizer.json", true, 0},
    {"https://huggingface.co/prism-ml/Bonsai-27B-mlx-1bit/resolve/main/tokenizer_config.json", "tokenizer_config.json", true, 0},
    {"https://huggingface.co/prism-ml/Bonsai-27B-mlx-1bit/resolve/main/vocab.json", "vocab.json", true, 0},
};

constexpr File kMlxBonsai4B1BitFiles[] = {
    {"https://huggingface.co/prism-ml/Bonsai-4B-mlx-1bit/resolve/main/chat_template.jinja", "chat_template.jinja", true, 0},
    {"https://huggingface.co/prism-ml/Bonsai-4B-mlx-1bit/resolve/main/config.json", "config.json", true, 0},
    {"https://huggingface.co/prism-ml/Bonsai-4B-mlx-1bit/resolve/main/merges.txt", "merges.txt", true, 0},
    {"https://huggingface.co/prism-ml/Bonsai-4B-mlx-1bit/resolve/main/model.safetensors", "model.safetensors", true, 0},
    {"https://huggingface.co/prism-ml/Bonsai-4B-mlx-1bit/resolve/main/model.safetensors.index.json", "model.safetensors.index.json", true, 0},
    {"https://huggingface.co/prism-ml/Bonsai-4B-mlx-1bit/resolve/main/tokenizer.json", "tokenizer.json", true, 0},
    {"https://huggingface.co/prism-ml/Bonsai-4B-mlx-1bit/resolve/main/tokenizer_config.json", "tokenizer_config.json", true, 0},
    {"https://huggingface.co/prism-ml/Bonsai-4B-mlx-1bit/resolve/main/vocab.json", "vocab.json", true, 0},
};

constexpr File kMlxBonsai8B1BitFiles[] = {
    {"https://huggingface.co/prism-ml/Bonsai-8B-mlx-1bit/resolve/main/chat_template.jinja", "chat_template.jinja", true, 0},
    {"https://huggingface.co/prism-ml/Bonsai-8B-mlx-1bit/resolve/main/config.json", "config.json", true, 0},
    {"https://huggingface.co/prism-ml/Bonsai-8B-mlx-1bit/resolve/main/merges.txt", "merges.txt", true, 0},
    {"https://huggingface.co/prism-ml/Bonsai-8B-mlx-1bit/resolve/main/model.safetensors", "model.safetensors", true, 0},
    {"https://huggingface.co/prism-ml/Bonsai-8B-mlx-1bit/resolve/main/model.safetensors.index.json", "model.safetensors.index.json", true, 0},
    {"https://huggingface.co/prism-ml/Bonsai-8B-mlx-1bit/resolve/main/tokenizer.json", "tokenizer.json", true, 0},
    {"https://huggingface.co/prism-ml/Bonsai-8B-mlx-1bit/resolve/main/tokenizer_config.json", "tokenizer_config.json", true, 0},
    {"https://huggingface.co/prism-ml/Bonsai-8B-mlx-1bit/resolve/main/vocab.json", "vocab.json", true, 0},
};

constexpr File kMlxFastVlm05BFiles[] = {
    {"https://huggingface.co/mlx-community/FastVLM-0.5B-bf16/resolve/main/added_tokens.json", "added_tokens.json", true, 0},
    {"https://huggingface.co/mlx-community/FastVLM-0.5B-bf16/resolve/main/chat_template.jinja", "chat_template.jinja", true, 0},
    {"https://huggingface.co/mlx-community/FastVLM-0.5B-bf16/resolve/main/config.json", "config.json", true, 0},
    {"https://huggingface.co/mlx-community/FastVLM-0.5B-bf16/resolve/main/llava_qwen.py", "llava_qwen.py", false, 0},
    {"https://huggingface.co/mlx-community/FastVLM-0.5B-bf16/resolve/main/merges.txt", "merges.txt", true, 0},
    {"https://huggingface.co/mlx-community/FastVLM-0.5B-bf16/resolve/main/model.safetensors", "model.safetensors", true, 0},
    {"https://huggingface.co/mlx-community/FastVLM-0.5B-bf16/resolve/main/model.safetensors.index.json", "model.safetensors.index.json", true, 0},
    {"https://huggingface.co/mlx-community/FastVLM-0.5B-bf16/resolve/main/preprocessor_config.json", "preprocessor_config.json", true, 0},
    {"https://huggingface.co/mlx-community/FastVLM-0.5B-bf16/resolve/main/processing_fastvlm.py", "processing_fastvlm.py", false, 0},
    {"https://huggingface.co/mlx-community/FastVLM-0.5B-bf16/resolve/main/processor_config.json", "processor_config.json", true, 0},
    {"https://huggingface.co/mlx-community/FastVLM-0.5B-bf16/resolve/main/special_tokens_map.json", "special_tokens_map.json", true, 0},
    {"https://huggingface.co/mlx-community/FastVLM-0.5B-bf16/resolve/main/tokenizer.json", "tokenizer.json", true, 0},
    {"https://huggingface.co/mlx-community/FastVLM-0.5B-bf16/resolve/main/tokenizer_config.json", "tokenizer_config.json", true, 0},
    {"https://huggingface.co/mlx-community/FastVLM-0.5B-bf16/resolve/main/vocab.json", "vocab.json", true, 0},
};

constexpr File kMlxGemma4E2BFiles[] = {
    {"https://huggingface.co/mlx-community/gemma-4-e2b-it-4bit/resolve/238767527555cb75a05732a84dff5d6ba0dd6809/chat_template.jinja", "chat_template.jinja", true, 0},
    {"https://huggingface.co/mlx-community/gemma-4-e2b-it-4bit/resolve/238767527555cb75a05732a84dff5d6ba0dd6809/config.json", "config.json", true, 0},
    {"https://huggingface.co/mlx-community/gemma-4-e2b-it-4bit/resolve/238767527555cb75a05732a84dff5d6ba0dd6809/generation_config.json", "generation_config.json", true, 0},
    {"https://huggingface.co/mlx-community/gemma-4-e2b-it-4bit/resolve/238767527555cb75a05732a84dff5d6ba0dd6809/model.safetensors", "model.safetensors", true, 3550670554LL},
    {"https://huggingface.co/mlx-community/gemma-4-e2b-it-4bit/resolve/238767527555cb75a05732a84dff5d6ba0dd6809/model.safetensors.index.json", "model.safetensors.index.json", true, 0},
    {"https://huggingface.co/mlx-community/gemma-4-e2b-it-4bit/resolve/238767527555cb75a05732a84dff5d6ba0dd6809/processor_config.json", "processor_config.json", true, 0},
    {"https://huggingface.co/mlx-community/gemma-4-e2b-it-4bit/resolve/238767527555cb75a05732a84dff5d6ba0dd6809/tokenizer.json", "tokenizer.json", true, 0},
    {"https://huggingface.co/mlx-community/gemma-4-e2b-it-4bit/resolve/238767527555cb75a05732a84dff5d6ba0dd6809/tokenizer_config.json", "tokenizer_config.json", true, 0},
};

constexpr File kMlxGemma4E4BFiles[] = {
    {"https://huggingface.co/mlx-community/gemma-4-E4B-it-qat-4bit/resolve/0f35c6f6d386f7f74e628bd7c6526ce531212300/chat_template.jinja", "chat_template.jinja", true, 0},
    {"https://huggingface.co/mlx-community/gemma-4-E4B-it-qat-4bit/resolve/0f35c6f6d386f7f74e628bd7c6526ce531212300/config.json", "config.json", true, 0},
    {"https://huggingface.co/mlx-community/gemma-4-E4B-it-qat-4bit/resolve/0f35c6f6d386f7f74e628bd7c6526ce531212300/generation_config.json", "generation_config.json", true, 0},
    {"https://huggingface.co/mlx-community/gemma-4-E4B-it-qat-4bit/resolve/0f35c6f6d386f7f74e628bd7c6526ce531212300/model-00001-of-00002.safetensors", "model-00001-of-00002.safetensors", true, 4249502053LL},
    {"https://huggingface.co/mlx-community/gemma-4-E4B-it-qat-4bit/resolve/0f35c6f6d386f7f74e628bd7c6526ce531212300/model-00002-of-00002.safetensors", "model-00002-of-00002.safetensors", true, 2548805689LL},
    {"https://huggingface.co/mlx-community/gemma-4-E4B-it-qat-4bit/resolve/0f35c6f6d386f7f74e628bd7c6526ce531212300/model.safetensors.index.json", "model.safetensors.index.json", true, 0},
    {"https://huggingface.co/mlx-community/gemma-4-E4B-it-qat-4bit/resolve/0f35c6f6d386f7f74e628bd7c6526ce531212300/processor_config.json", "processor_config.json", true, 0},
    {"https://huggingface.co/mlx-community/gemma-4-E4B-it-qat-4bit/resolve/0f35c6f6d386f7f74e628bd7c6526ce531212300/tokenizer.json", "tokenizer.json", true, 0},
    {"https://huggingface.co/mlx-community/gemma-4-E4B-it-qat-4bit/resolve/0f35c6f6d386f7f74e628bd7c6526ce531212300/tokenizer_config.json", "tokenizer_config.json", true, 0},
};

constexpr File kMlxGemma4_12BFiles[] = {
    {"https://huggingface.co/mlx-community/gemma-4-12B-it-qat-4bit/resolve/e70c6b3ba0979b3357dcd2f223ad8bde7787a6b6/chat_template.jinja", "chat_template.jinja", true, 0},
    {"https://huggingface.co/mlx-community/gemma-4-12B-it-qat-4bit/resolve/e70c6b3ba0979b3357dcd2f223ad8bde7787a6b6/config.json", "config.json", true, 0},
    {"https://huggingface.co/mlx-community/gemma-4-12B-it-qat-4bit/resolve/e70c6b3ba0979b3357dcd2f223ad8bde7787a6b6/generation_config.json", "generation_config.json", true, 0},
    {"https://huggingface.co/mlx-community/gemma-4-12B-it-qat-4bit/resolve/e70c6b3ba0979b3357dcd2f223ad8bde7787a6b6/model-00001-of-00003.safetensors", "model-00001-of-00003.safetensors", true, 5343482357LL},
    {"https://huggingface.co/mlx-community/gemma-4-12B-it-qat-4bit/resolve/e70c6b3ba0979b3357dcd2f223ad8bde7787a6b6/model-00002-of-00003.safetensors", "model-00002-of-00003.safetensors", true, 5315166254LL},
    {"https://huggingface.co/mlx-community/gemma-4-12B-it-qat-4bit/resolve/e70c6b3ba0979b3357dcd2f223ad8bde7787a6b6/model-00003-of-00003.safetensors", "model-00003-of-00003.safetensors", true, 329123819LL},
    {"https://huggingface.co/mlx-community/gemma-4-12B-it-qat-4bit/resolve/e70c6b3ba0979b3357dcd2f223ad8bde7787a6b6/model.safetensors.index.json", "model.safetensors.index.json", true, 0},
    {"https://huggingface.co/mlx-community/gemma-4-12B-it-qat-4bit/resolve/e70c6b3ba0979b3357dcd2f223ad8bde7787a6b6/processor_config.json", "processor_config.json", true, 0},
    {"https://huggingface.co/mlx-community/gemma-4-12B-it-qat-4bit/resolve/e70c6b3ba0979b3357dcd2f223ad8bde7787a6b6/tokenizer.json", "tokenizer.json", true, 0},
    {"https://huggingface.co/mlx-community/gemma-4-12B-it-qat-4bit/resolve/e70c6b3ba0979b3357dcd2f223ad8bde7787a6b6/tokenizer_config.json", "tokenizer_config.json", true, 0},
};

constexpr File kMlxGemma4_26BA4BFiles[] = {
    {"https://huggingface.co/mlx-community/gemma-4-26b-a4b-it-4bit/resolve/0d77464eeb233a2da68ebf9d7dc4edaac7db956d/chat_template.jinja", "chat_template.jinja", true, 0},
    {"https://huggingface.co/mlx-community/gemma-4-26b-a4b-it-4bit/resolve/0d77464eeb233a2da68ebf9d7dc4edaac7db956d/config.json", "config.json", true, 0},
    {"https://huggingface.co/mlx-community/gemma-4-26b-a4b-it-4bit/resolve/0d77464eeb233a2da68ebf9d7dc4edaac7db956d/generation_config.json", "generation_config.json", true, 0},
    {"https://huggingface.co/mlx-community/gemma-4-26b-a4b-it-4bit/resolve/0d77464eeb233a2da68ebf9d7dc4edaac7db956d/model-00001-of-00003.safetensors", "model-00001-of-00003.safetensors", true, 5320218487LL},
    {"https://huggingface.co/mlx-community/gemma-4-26b-a4b-it-4bit/resolve/0d77464eeb233a2da68ebf9d7dc4edaac7db956d/model-00002-of-00003.safetensors", "model-00002-of-00003.safetensors", true, 5363328422LL},
    {"https://huggingface.co/mlx-community/gemma-4-26b-a4b-it-4bit/resolve/0d77464eeb233a2da68ebf9d7dc4edaac7db956d/model-00003-of-00003.safetensors", "model-00003-of-00003.safetensors", true, 4657658867LL},
    {"https://huggingface.co/mlx-community/gemma-4-26b-a4b-it-4bit/resolve/0d77464eeb233a2da68ebf9d7dc4edaac7db956d/model.safetensors.index.json", "model.safetensors.index.json", true, 0},
    {"https://huggingface.co/mlx-community/gemma-4-26b-a4b-it-4bit/resolve/0d77464eeb233a2da68ebf9d7dc4edaac7db956d/processor_config.json", "processor_config.json", true, 0},
    {"https://huggingface.co/mlx-community/gemma-4-26b-a4b-it-4bit/resolve/0d77464eeb233a2da68ebf9d7dc4edaac7db956d/tokenizer.json", "tokenizer.json", true, 0},
    {"https://huggingface.co/mlx-community/gemma-4-26b-a4b-it-4bit/resolve/0d77464eeb233a2da68ebf9d7dc4edaac7db956d/tokenizer_config.json", "tokenizer_config.json", true, 0},
};

constexpr File kMlxGemma4_31BFiles[] = {
    {"https://huggingface.co/mlx-community/gemma-4-31b-it-4bit/resolve/696d436c404745a59f30e4939a658162b0a9e57f/chat_template.jinja", "chat_template.jinja", true, 0},
    {"https://huggingface.co/mlx-community/gemma-4-31b-it-4bit/resolve/696d436c404745a59f30e4939a658162b0a9e57f/config.json", "config.json", true, 0},
    {"https://huggingface.co/mlx-community/gemma-4-31b-it-4bit/resolve/696d436c404745a59f30e4939a658162b0a9e57f/generation_config.json", "generation_config.json", true, 0},
    {"https://huggingface.co/mlx-community/gemma-4-31b-it-4bit/resolve/696d436c404745a59f30e4939a658162b0a9e57f/model-00001-of-00004.safetensors", "model-00001-of-00004.safetensors", true, 5366617512LL},
    {"https://huggingface.co/mlx-community/gemma-4-31b-it-4bit/resolve/696d436c404745a59f30e4939a658162b0a9e57f/model-00002-of-00004.safetensors", "model-00002-of-00004.safetensors", true, 5361642573LL},
    {"https://huggingface.co/mlx-community/gemma-4-31b-it-4bit/resolve/696d436c404745a59f30e4939a658162b0a9e57f/model-00003-of-00004.safetensors", "model-00003-of-00004.safetensors", true, 5367276094LL},
    {"https://huggingface.co/mlx-community/gemma-4-31b-it-4bit/resolve/696d436c404745a59f30e4939a658162b0a9e57f/model-00004-of-00004.safetensors", "model-00004-of-00004.safetensors", true, 2316480497LL},
    {"https://huggingface.co/mlx-community/gemma-4-31b-it-4bit/resolve/696d436c404745a59f30e4939a658162b0a9e57f/model.safetensors.index.json", "model.safetensors.index.json", true, 0},
    {"https://huggingface.co/mlx-community/gemma-4-31b-it-4bit/resolve/696d436c404745a59f30e4939a658162b0a9e57f/processor_config.json", "processor_config.json", true, 0},
    {"https://huggingface.co/mlx-community/gemma-4-31b-it-4bit/resolve/696d436c404745a59f30e4939a658162b0a9e57f/tokenizer.json", "tokenizer.json", true, 0},
    {"https://huggingface.co/mlx-community/gemma-4-31b-it-4bit/resolve/696d436c404745a59f30e4939a658162b0a9e57f/tokenizer_config.json", "tokenizer_config.json", true, 0},
};

constexpr File kMlxGlmAsrNano2512Files[] = {
    {"https://huggingface.co/mlx-community/GLM-ASR-Nano-2512-4bit/resolve/main/config.json", "config.json", true, 0},
    {"https://huggingface.co/mlx-community/GLM-ASR-Nano-2512-4bit/resolve/main/configuration_glmasr.py", "configuration_glmasr.py", false, 0},
    {"https://huggingface.co/mlx-community/GLM-ASR-Nano-2512-4bit/resolve/main/inference.py", "inference.py", false, 0},
    {"https://huggingface.co/mlx-community/GLM-ASR-Nano-2512-4bit/resolve/main/model.safetensors", "model.safetensors", true, 0},
    {"https://huggingface.co/mlx-community/GLM-ASR-Nano-2512-4bit/resolve/main/model.safetensors.index.json", "model.safetensors.index.json", true, 0},
    {"https://huggingface.co/mlx-community/GLM-ASR-Nano-2512-4bit/resolve/main/modeling_audio.py", "modeling_audio.py", false, 0},
    {"https://huggingface.co/mlx-community/GLM-ASR-Nano-2512-4bit/resolve/main/modeling_glmasr.py", "modeling_glmasr.py", false, 0},
    {"https://huggingface.co/mlx-community/GLM-ASR-Nano-2512-4bit/resolve/main/tokenizer.json", "tokenizer.json", true, 0},
    {"https://huggingface.co/mlx-community/GLM-ASR-Nano-2512-4bit/resolve/main/tokenizer_config.json", "tokenizer_config.json", true, 0},
};

constexpr File kMlxGranite4_1_30BFiles[] = {
    {"https://huggingface.co/mlx-community/granite-4.1-30b-4bit/resolve/03e8065d3219e525aa27fc4aaa9b375fe2cd6cb8/chat_template.jinja", "chat_template.jinja", true, 0},
    {"https://huggingface.co/mlx-community/granite-4.1-30b-4bit/resolve/03e8065d3219e525aa27fc4aaa9b375fe2cd6cb8/config.json", "config.json", true, 0},
    {"https://huggingface.co/mlx-community/granite-4.1-30b-4bit/resolve/03e8065d3219e525aa27fc4aaa9b375fe2cd6cb8/generation_config.json", "generation_config.json", true, 0},
    {"https://huggingface.co/mlx-community/granite-4.1-30b-4bit/resolve/03e8065d3219e525aa27fc4aaa9b375fe2cd6cb8/model-00001-of-00004.safetensors", "model-00001-of-00004.safetensors", true, 5360664833LL},
    {"https://huggingface.co/mlx-community/granite-4.1-30b-4bit/resolve/03e8065d3219e525aa27fc4aaa9b375fe2cd6cb8/model-00002-of-00004.safetensors", "model-00002-of-00004.safetensors", true, 5363828231LL},
    {"https://huggingface.co/mlx-community/granite-4.1-30b-4bit/resolve/03e8065d3219e525aa27fc4aaa9b375fe2cd6cb8/model-00003-of-00004.safetensors", "model-00003-of-00004.safetensors", true, 5363828281LL},
    {"https://huggingface.co/mlx-community/granite-4.1-30b-4bit/resolve/03e8065d3219e525aa27fc4aaa9b375fe2cd6cb8/model-00004-of-00004.safetensors", "model-00004-of-00004.safetensors", true, 1953655228LL},
    {"https://huggingface.co/mlx-community/granite-4.1-30b-4bit/resolve/03e8065d3219e525aa27fc4aaa9b375fe2cd6cb8/model.safetensors.index.json", "model.safetensors.index.json", true, 0},
    {"https://huggingface.co/mlx-community/granite-4.1-30b-4bit/resolve/03e8065d3219e525aa27fc4aaa9b375fe2cd6cb8/tokenizer.json", "tokenizer.json", true, 0},
    {"https://huggingface.co/mlx-community/granite-4.1-30b-4bit/resolve/03e8065d3219e525aa27fc4aaa9b375fe2cd6cb8/tokenizer_config.json", "tokenizer_config.json", true, 0},
};

constexpr File kMlxGranite4_1_3BFiles[] = {
    {"https://huggingface.co/mlx-community/granite-4.1-3b-4bit/resolve/b1b476b5a17c46b7d6cd663b4a8ed44b66720aef/chat_template.jinja", "chat_template.jinja", true, 0},
    {"https://huggingface.co/mlx-community/granite-4.1-3b-4bit/resolve/b1b476b5a17c46b7d6cd663b4a8ed44b66720aef/config.json", "config.json", true, 0},
    {"https://huggingface.co/mlx-community/granite-4.1-3b-4bit/resolve/b1b476b5a17c46b7d6cd663b4a8ed44b66720aef/generation_config.json", "generation_config.json", true, 0},
    {"https://huggingface.co/mlx-community/granite-4.1-3b-4bit/resolve/b1b476b5a17c46b7d6cd663b4a8ed44b66720aef/model.safetensors", "model.safetensors", true, 2127162429LL},
    {"https://huggingface.co/mlx-community/granite-4.1-3b-4bit/resolve/b1b476b5a17c46b7d6cd663b4a8ed44b66720aef/model.safetensors.index.json", "model.safetensors.index.json", true, 0},
    {"https://huggingface.co/mlx-community/granite-4.1-3b-4bit/resolve/b1b476b5a17c46b7d6cd663b4a8ed44b66720aef/tokenizer.json", "tokenizer.json", true, 0},
    {"https://huggingface.co/mlx-community/granite-4.1-3b-4bit/resolve/b1b476b5a17c46b7d6cd663b4a8ed44b66720aef/tokenizer_config.json", "tokenizer_config.json", true, 0},
};

constexpr File kMlxGranite4_1_8BFiles[] = {
    {"https://huggingface.co/mlx-community/granite-4.1-8b-4bit/resolve/08fb1e272f7bd49fa83ce279bbdc496c980380ac/chat_template.jinja", "chat_template.jinja", true, 0},
    {"https://huggingface.co/mlx-community/granite-4.1-8b-4bit/resolve/08fb1e272f7bd49fa83ce279bbdc496c980380ac/config.json", "config.json", true, 0},
    {"https://huggingface.co/mlx-community/granite-4.1-8b-4bit/resolve/08fb1e272f7bd49fa83ce279bbdc496c980380ac/generation_config.json", "generation_config.json", true, 0},
    {"https://huggingface.co/mlx-community/granite-4.1-8b-4bit/resolve/08fb1e272f7bd49fa83ce279bbdc496c980380ac/model.safetensors", "model.safetensors", true, 5238406779LL},
    {"https://huggingface.co/mlx-community/granite-4.1-8b-4bit/resolve/08fb1e272f7bd49fa83ce279bbdc496c980380ac/model.safetensors.index.json", "model.safetensors.index.json", true, 0},
    {"https://huggingface.co/mlx-community/granite-4.1-8b-4bit/resolve/08fb1e272f7bd49fa83ce279bbdc496c980380ac/tokenizer.json", "tokenizer.json", true, 0},
    {"https://huggingface.co/mlx-community/granite-4.1-8b-4bit/resolve/08fb1e272f7bd49fa83ce279bbdc496c980380ac/tokenizer_config.json", "tokenizer_config.json", true, 0},
};

constexpr File kMlxLfm2_5Vl3BFiles[] = {
    {"https://huggingface.co/LiquidAI/LFM2.5-VL-3B-MLX-4bit/resolve/main/chat_template.jinja", "chat_template.jinja", true, 0},
    {"https://huggingface.co/LiquidAI/LFM2.5-VL-3B-MLX-4bit/resolve/main/config.json", "config.json", true, 0},
    {"https://huggingface.co/LiquidAI/LFM2.5-VL-3B-MLX-4bit/resolve/main/generation_config.json", "generation_config.json", true, 0},
    {"https://huggingface.co/LiquidAI/LFM2.5-VL-3B-MLX-4bit/resolve/main/model.safetensors", "model.safetensors", true, 0},
    {"https://huggingface.co/LiquidAI/LFM2.5-VL-3B-MLX-4bit/resolve/main/model.safetensors.index.json", "model.safetensors.index.json", true, 0},
    {"https://huggingface.co/LiquidAI/LFM2.5-VL-3B-MLX-4bit/resolve/main/processor_config.json", "processor_config.json", true, 0},
    {"https://huggingface.co/LiquidAI/LFM2.5-VL-3B-MLX-4bit/resolve/main/special_tokens_map.json", "special_tokens_map.json", true, 0},
    {"https://huggingface.co/LiquidAI/LFM2.5-VL-3B-MLX-4bit/resolve/main/tokenizer.json", "tokenizer.json", true, 0},
    {"https://huggingface.co/LiquidAI/LFM2.5-VL-3B-MLX-4bit/resolve/main/tokenizer_config.json", "tokenizer_config.json", true, 0},
};

constexpr File kMlxLlama32_1BFiles[] = {
    {"https://huggingface.co/mlx-community/Llama-3.2-1B-Instruct-4bit/resolve/main/config.json", "config.json", true, 0},
    {"https://huggingface.co/mlx-community/Llama-3.2-1B-Instruct-4bit/resolve/main/model.safetensors", "model.safetensors", true, 0},
    {"https://huggingface.co/mlx-community/Llama-3.2-1B-Instruct-4bit/resolve/main/model.safetensors.index.json", "model.safetensors.index.json", true, 0},
    {"https://huggingface.co/mlx-community/Llama-3.2-1B-Instruct-4bit/resolve/main/special_tokens_map.json", "special_tokens_map.json", true, 0},
    {"https://huggingface.co/mlx-community/Llama-3.2-1B-Instruct-4bit/resolve/main/tokenizer.json", "tokenizer.json", true, 0},
    {"https://huggingface.co/mlx-community/Llama-3.2-1B-Instruct-4bit/resolve/main/tokenizer_config.json", "tokenizer_config.json", true, 0},
};

constexpr File kMlxMaplePreviewFiles[] = {
    {"https://huggingface.co/deepgrove/maple-preview-2bit-mlx/resolve/d0a7314d6bf14c880201b599d7a701cfbc8717e6/added_tokens.json", "added_tokens.json", true, 707LL},
    {"https://huggingface.co/deepgrove/maple-preview-2bit-mlx/resolve/d0a7314d6bf14c880201b599d7a701cfbc8717e6/chat_template.jinja", "chat_template.jinja", true, 3292LL},
    {"https://huggingface.co/deepgrove/maple-preview-2bit-mlx/resolve/d0a7314d6bf14c880201b599d7a701cfbc8717e6/config.json", "config.json", true, 2710LL},
    {"https://huggingface.co/deepgrove/maple-preview-2bit-mlx/resolve/d0a7314d6bf14c880201b599d7a701cfbc8717e6/merges.txt", "merges.txt", true, 1671853LL},
    {"https://huggingface.co/deepgrove/maple-preview-2bit-mlx/resolve/d0a7314d6bf14c880201b599d7a701cfbc8717e6/model-00001-of-00003.safetensors", "model-00001-of-00003.safetensors", true, 2162084350LL},
    {"https://huggingface.co/deepgrove/maple-preview-2bit-mlx/resolve/d0a7314d6bf14c880201b599d7a701cfbc8717e6/model-00002-of-00003.safetensors", "model-00002-of-00003.safetensors", true, 2187444586LL},
    {"https://huggingface.co/deepgrove/maple-preview-2bit-mlx/resolve/d0a7314d6bf14c880201b599d7a701cfbc8717e6/model-00003-of-00003.safetensors", "model-00003-of-00003.safetensors", true, 958711742LL},
    {"https://huggingface.co/deepgrove/maple-preview-2bit-mlx/resolve/d0a7314d6bf14c880201b599d7a701cfbc8717e6/model-flashhead.safetensors", "model-flashhead.safetensors", true, 6087456LL},
    {"https://huggingface.co/deepgrove/maple-preview-2bit-mlx/resolve/d0a7314d6bf14c880201b599d7a701cfbc8717e6/model.safetensors.index.json", "model.safetensors.index.json", true, 40054LL},
    {"https://huggingface.co/deepgrove/maple-preview-2bit-mlx/resolve/d0a7314d6bf14c880201b599d7a701cfbc8717e6/special_tokens_map.json", "special_tokens_map.json", true, 613LL},
    {"https://huggingface.co/deepgrove/maple-preview-2bit-mlx/resolve/d0a7314d6bf14c880201b599d7a701cfbc8717e6/tokenizer.json", "tokenizer.json", true, 11422654LL},
    {"https://huggingface.co/deepgrove/maple-preview-2bit-mlx/resolve/d0a7314d6bf14c880201b599d7a701cfbc8717e6/tokenizer_config.json", "tokenizer_config.json", true, 5432LL},
    {"https://huggingface.co/deepgrove/maple-preview-2bit-mlx/resolve/d0a7314d6bf14c880201b599d7a701cfbc8717e6/vocab.json", "vocab.json", true, 2776833LL},
};

constexpr File kMlxNemotronMini4BFiles[] = {
    {"https://huggingface.co/mlx-community/Nemotron-Mini-4B-Instruct-4bit-mlx/resolve/b5784198153d2d71afcc97d4cc38c049abced8cd/chat_template.jinja", "chat_template.jinja", true, 0},
    {"https://huggingface.co/mlx-community/Nemotron-Mini-4B-Instruct-4bit-mlx/resolve/b5784198153d2d71afcc97d4cc38c049abced8cd/config.json", "config.json", true, 0},
    {"https://huggingface.co/mlx-community/Nemotron-Mini-4B-Instruct-4bit-mlx/resolve/b5784198153d2d71afcc97d4cc38c049abced8cd/model.safetensors", "model.safetensors", true, 0},
    {"https://huggingface.co/mlx-community/Nemotron-Mini-4B-Instruct-4bit-mlx/resolve/b5784198153d2d71afcc97d4cc38c049abced8cd/model.safetensors.index.json", "model.safetensors.index.json", true, 0},
    {"https://huggingface.co/mlx-community/Nemotron-Mini-4B-Instruct-4bit-mlx/resolve/b5784198153d2d71afcc97d4cc38c049abced8cd/tokenizer.json", "tokenizer.json", true, 0},
    {"https://huggingface.co/mlx-community/Nemotron-Mini-4B-Instruct-4bit-mlx/resolve/b5784198153d2d71afcc97d4cc38c049abced8cd/tokenizer_config.json", "tokenizer_config.json", true, 0},
};

constexpr File kMlxNemotronNano8BFiles[] = {
    {"https://huggingface.co/bourn23/nvidia-llama-3.1-nemotron-nano-8b-v1-mlx-4bit/resolve/00378e66048eadf358aad0f66c09e5c3750f8243/chat_template.jinja", "chat_template.jinja", true, 0},
    {"https://huggingface.co/bourn23/nvidia-llama-3.1-nemotron-nano-8b-v1-mlx-4bit/resolve/00378e66048eadf358aad0f66c09e5c3750f8243/config.json", "config.json", true, 0},
    {"https://huggingface.co/bourn23/nvidia-llama-3.1-nemotron-nano-8b-v1-mlx-4bit/resolve/00378e66048eadf358aad0f66c09e5c3750f8243/generation_config.json", "generation_config.json", true, 0},
    {"https://huggingface.co/bourn23/nvidia-llama-3.1-nemotron-nano-8b-v1-mlx-4bit/resolve/00378e66048eadf358aad0f66c09e5c3750f8243/model.safetensors", "model.safetensors", true, 0},
    {"https://huggingface.co/bourn23/nvidia-llama-3.1-nemotron-nano-8b-v1-mlx-4bit/resolve/00378e66048eadf358aad0f66c09e5c3750f8243/model.safetensors.index.json", "model.safetensors.index.json", true, 0},
    {"https://huggingface.co/bourn23/nvidia-llama-3.1-nemotron-nano-8b-v1-mlx-4bit/resolve/00378e66048eadf358aad0f66c09e5c3750f8243/special_tokens_map.json", "special_tokens_map.json", true, 0},
    {"https://huggingface.co/bourn23/nvidia-llama-3.1-nemotron-nano-8b-v1-mlx-4bit/resolve/00378e66048eadf358aad0f66c09e5c3750f8243/tokenizer.json", "tokenizer.json", true, 0},
    {"https://huggingface.co/bourn23/nvidia-llama-3.1-nemotron-nano-8b-v1-mlx-4bit/resolve/00378e66048eadf358aad0f66c09e5c3750f8243/tokenizer_config.json", "tokenizer_config.json", true, 0},
};

constexpr File kMlxNemotronStreamingAsrFiles[] = {
    {"https://huggingface.co/mlx-community/nemotron-3.5-asr-streaming-0.6b-8bit/resolve/7279359e4481b5e9e185a318bd618e429c6d86cd/config.json", "config.json", true, 0},
    {"https://huggingface.co/mlx-community/nemotron-3.5-asr-streaming-0.6b-8bit/resolve/7279359e4481b5e9e185a318bd618e429c6d86cd/model.safetensors", "model.safetensors", true, 0},
};

constexpr File kMlxParakeetCtc11BFiles[] = {
    {"https://huggingface.co/mlx-community/parakeet-ctc-1.1b/resolve/295d0c0557aef0c445db79b3d09c9a94a69ffeaf/config.json", "config.json", true, 0},
    {"https://huggingface.co/mlx-community/parakeet-ctc-1.1b/resolve/295d0c0557aef0c445db79b3d09c9a94a69ffeaf/model.safetensors", "model.safetensors", true, 0},
};

constexpr File kMlxParakeetRnnt11BFiles[] = {
    {"https://huggingface.co/mlx-community/parakeet-rnnt-1.1b/resolve/7f399a0d3442123deae9194e71f5c984b2879efa/config.json", "config.json", true, 0},
    {"https://huggingface.co/mlx-community/parakeet-rnnt-1.1b/resolve/7f399a0d3442123deae9194e71f5c984b2879efa/model.safetensors", "model.safetensors", true, 0},
};

constexpr File kMlxParakeetTdtV2Files[] = {
    {"https://huggingface.co/mlx-community/parakeet-tdt-0.6b-v2/resolve/8ae155301e23d820d82aa60d24817c900e69e487/config.json", "config.json", true, 0},
    {"https://huggingface.co/mlx-community/parakeet-tdt-0.6b-v2/resolve/8ae155301e23d820d82aa60d24817c900e69e487/model.safetensors", "model.safetensors", true, 0},
};

constexpr File kMlxParakeetTdtV3Files[] = {
    {"https://huggingface.co/mlx-community/parakeet-tdt-0.6b-v3/resolve/ed2b7e8c15f9aaa0b5772e2efb986255eaef7e15/config.json", "config.json", true, 0},
    {"https://huggingface.co/mlx-community/parakeet-tdt-0.6b-v3/resolve/ed2b7e8c15f9aaa0b5772e2efb986255eaef7e15/model.safetensors", "model.safetensors", true, 0},
};

constexpr File kMlxQwen2Vl2BFiles[] = {
    {"https://huggingface.co/mlx-community/Qwen2-VL-2B-Instruct-4bit/resolve/main/added_tokens.json", "added_tokens.json", true, 0},
    {"https://huggingface.co/mlx-community/Qwen2-VL-2B-Instruct-4bit/resolve/main/chat_template.json", "chat_template.json", true, 0},
    {"https://huggingface.co/mlx-community/Qwen2-VL-2B-Instruct-4bit/resolve/main/config.json", "config.json", true, 0},
    {"https://huggingface.co/mlx-community/Qwen2-VL-2B-Instruct-4bit/resolve/main/merges.txt", "merges.txt", true, 0},
    {"https://huggingface.co/mlx-community/Qwen2-VL-2B-Instruct-4bit/resolve/main/model.safetensors", "model.safetensors", true, 0},
    {"https://huggingface.co/mlx-community/Qwen2-VL-2B-Instruct-4bit/resolve/main/model.safetensors.index.json", "model.safetensors.index.json", true, 0},
    {"https://huggingface.co/mlx-community/Qwen2-VL-2B-Instruct-4bit/resolve/main/preprocessor_config.json", "preprocessor_config.json", true, 0},
    {"https://huggingface.co/mlx-community/Qwen2-VL-2B-Instruct-4bit/resolve/main/special_tokens_map.json", "special_tokens_map.json", true, 0},
    {"https://huggingface.co/mlx-community/Qwen2-VL-2B-Instruct-4bit/resolve/main/tokenizer.json", "tokenizer.json", true, 0},
    {"https://huggingface.co/mlx-community/Qwen2-VL-2B-Instruct-4bit/resolve/main/tokenizer_config.json", "tokenizer_config.json", true, 0},
    {"https://huggingface.co/mlx-community/Qwen2-VL-2B-Instruct-4bit/resolve/main/vocab.json", "vocab.json", true, 0},
};

constexpr File kMlxQwen3Asr06BFiles[] = {
    {"https://huggingface.co/mlx-community/Qwen3-ASR-0.6B-8bit/resolve/main/chat_template.json", "chat_template.json", true, 0},
    {"https://huggingface.co/mlx-community/Qwen3-ASR-0.6B-8bit/resolve/main/config.json", "config.json", true, 0},
    {"https://huggingface.co/mlx-community/Qwen3-ASR-0.6B-8bit/resolve/main/generation_config.json", "generation_config.json", true, 0},
    {"https://huggingface.co/mlx-community/Qwen3-ASR-0.6B-8bit/resolve/main/merges.txt", "merges.txt", true, 0},
    {"https://huggingface.co/mlx-community/Qwen3-ASR-0.6B-8bit/resolve/main/model.safetensors", "model.safetensors", true, 0},
    {"https://huggingface.co/mlx-community/Qwen3-ASR-0.6B-8bit/resolve/main/model.safetensors.index.json", "model.safetensors.index.json", true, 0},
    {"https://huggingface.co/mlx-community/Qwen3-ASR-0.6B-8bit/resolve/main/preprocessor_config.json", "preprocessor_config.json", true, 0},
    {"https://huggingface.co/mlx-community/Qwen3-ASR-0.6B-8bit/resolve/main/tokenizer_config.json", "tokenizer_config.json", true, 0},
    {"https://huggingface.co/mlx-community/Qwen3-ASR-0.6B-8bit/resolve/main/vocab.json", "vocab.json", true, 0},
};

constexpr File kMlxQwen3Embedding06BFiles[] = {
    {"https://huggingface.co/mlx-community/Qwen3-Embedding-0.6B-4bit-DWQ/resolve/main/added_tokens.json", "added_tokens.json", true, 0},
    {"https://huggingface.co/mlx-community/Qwen3-Embedding-0.6B-4bit-DWQ/resolve/main/chat_template.jinja", "chat_template.jinja", true, 0},
    {"https://huggingface.co/mlx-community/Qwen3-Embedding-0.6B-4bit-DWQ/resolve/main/config.json", "config.json", true, 0},
    {"https://huggingface.co/mlx-community/Qwen3-Embedding-0.6B-4bit-DWQ/resolve/main/generation_config.json", "generation_config.json", true, 0},
    {"https://huggingface.co/mlx-community/Qwen3-Embedding-0.6B-4bit-DWQ/resolve/main/merges.txt", "merges.txt", true, 0},
    {"https://huggingface.co/mlx-community/Qwen3-Embedding-0.6B-4bit-DWQ/resolve/main/model.safetensors", "model.safetensors", true, 0},
    {"https://huggingface.co/mlx-community/Qwen3-Embedding-0.6B-4bit-DWQ/resolve/main/model.safetensors.index.json", "model.safetensors.index.json", true, 0},
    {"https://huggingface.co/mlx-community/Qwen3-Embedding-0.6B-4bit-DWQ/resolve/main/special_tokens_map.json", "special_tokens_map.json", true, 0},
    {"https://huggingface.co/mlx-community/Qwen3-Embedding-0.6B-4bit-DWQ/resolve/main/tokenizer.json", "tokenizer.json", true, 0},
    {"https://huggingface.co/mlx-community/Qwen3-Embedding-0.6B-4bit-DWQ/resolve/main/tokenizer_config.json", "tokenizer_config.json", true, 0},
    {"https://huggingface.co/mlx-community/Qwen3-Embedding-0.6B-4bit-DWQ/resolve/main/vocab.json", "vocab.json", true, 0},
};

constexpr File kMlxQwen3Tts06BBaseFiles[] = {
    {"https://huggingface.co/mlx-community/Qwen3-TTS-12Hz-0.6B-Base-8bit/resolve/main/config.json", "config.json", true, 0},
    {"https://huggingface.co/mlx-community/Qwen3-TTS-12Hz-0.6B-Base-8bit/resolve/main/generation_config.json", "generation_config.json", true, 0},
    {"https://huggingface.co/mlx-community/Qwen3-TTS-12Hz-0.6B-Base-8bit/resolve/main/merges.txt", "merges.txt", true, 0},
    {"https://huggingface.co/mlx-community/Qwen3-TTS-12Hz-0.6B-Base-8bit/resolve/main/model.safetensors", "model.safetensors", true, 0},
    {"https://huggingface.co/mlx-community/Qwen3-TTS-12Hz-0.6B-Base-8bit/resolve/main/model.safetensors.index.json", "model.safetensors.index.json", true, 0},
    {"https://huggingface.co/mlx-community/Qwen3-TTS-12Hz-0.6B-Base-8bit/resolve/main/preprocessor_config.json", "preprocessor_config.json", true, 0},
    {"https://huggingface.co/mlx-community/Qwen3-TTS-12Hz-0.6B-Base-8bit/resolve/main/speech_tokenizer/config.json", "speech_tokenizer/config.json", true, 0},
    {"https://huggingface.co/mlx-community/Qwen3-TTS-12Hz-0.6B-Base-8bit/resolve/main/speech_tokenizer/configuration.json", "speech_tokenizer/configuration.json", true, 0},
    {"https://huggingface.co/mlx-community/Qwen3-TTS-12Hz-0.6B-Base-8bit/resolve/main/speech_tokenizer/model.safetensors", "speech_tokenizer/model.safetensors", true, 0},
    {"https://huggingface.co/mlx-community/Qwen3-TTS-12Hz-0.6B-Base-8bit/resolve/main/speech_tokenizer/preprocessor_config.json", "speech_tokenizer/preprocessor_config.json", true, 0},
    {"https://huggingface.co/mlx-community/Qwen3-TTS-12Hz-0.6B-Base-8bit/resolve/main/tokenizer_config.json", "tokenizer_config.json", true, 0},
    {"https://huggingface.co/mlx-community/Qwen3-TTS-12Hz-0.6B-Base-8bit/resolve/main/vocab.json", "vocab.json", true, 0},
};

constexpr File kMlxQwen3_06BFiles[] = {
    {"https://huggingface.co/mlx-community/Qwen3-0.6B-4bit/resolve/main/added_tokens.json", "added_tokens.json", true, 0},
    {"https://huggingface.co/mlx-community/Qwen3-0.6B-4bit/resolve/main/config.json", "config.json", true, 0},
    {"https://huggingface.co/mlx-community/Qwen3-0.6B-4bit/resolve/main/merges.txt", "merges.txt", true, 0},
    {"https://huggingface.co/mlx-community/Qwen3-0.6B-4bit/resolve/main/model.safetensors", "model.safetensors", true, 0},
    {"https://huggingface.co/mlx-community/Qwen3-0.6B-4bit/resolve/main/model.safetensors.index.json", "model.safetensors.index.json", true, 0},
    {"https://huggingface.co/mlx-community/Qwen3-0.6B-4bit/resolve/main/special_tokens_map.json", "special_tokens_map.json", true, 0},
    {"https://huggingface.co/mlx-community/Qwen3-0.6B-4bit/resolve/main/tokenizer.json", "tokenizer.json", true, 0},
    {"https://huggingface.co/mlx-community/Qwen3-0.6B-4bit/resolve/main/tokenizer_config.json", "tokenizer_config.json", true, 0},
    {"https://huggingface.co/mlx-community/Qwen3-0.6B-4bit/resolve/main/vocab.json", "vocab.json", true, 0},
};

constexpr File kMlxQwen3_6_35BA3BFiles[] = {
    {"https://huggingface.co/mlx-community/Qwen3.6-35B-A3B-4bit/resolve/38740b847e4cb78f352aba30aa41c76e08e6eb46/chat_template.jinja", "chat_template.jinja", true, 0},
    {"https://huggingface.co/mlx-community/Qwen3.6-35B-A3B-4bit/resolve/38740b847e4cb78f352aba30aa41c76e08e6eb46/config.json", "config.json", true, 0},
    {"https://huggingface.co/mlx-community/Qwen3.6-35B-A3B-4bit/resolve/38740b847e4cb78f352aba30aa41c76e08e6eb46/configuration.json", "configuration.json", true, 0},
    {"https://huggingface.co/mlx-community/Qwen3.6-35B-A3B-4bit/resolve/38740b847e4cb78f352aba30aa41c76e08e6eb46/generation_config.json", "generation_config.json", true, 0},
    {"https://huggingface.co/mlx-community/Qwen3.6-35B-A3B-4bit/resolve/38740b847e4cb78f352aba30aa41c76e08e6eb46/model-00001-of-00004.safetensors", "model-00001-of-00004.safetensors", true, 5288196018LL},
    {"https://huggingface.co/mlx-community/Qwen3.6-35B-A3B-4bit/resolve/38740b847e4cb78f352aba30aa41c76e08e6eb46/model-00002-of-00004.safetensors", "model-00002-of-00004.safetensors", true, 5368472749LL},
    {"https://huggingface.co/mlx-community/Qwen3.6-35B-A3B-4bit/resolve/38740b847e4cb78f352aba30aa41c76e08e6eb46/model-00003-of-00004.safetensors", "model-00003-of-00004.safetensors", true, 5368324139LL},
    {"https://huggingface.co/mlx-community/Qwen3.6-35B-A3B-4bit/resolve/38740b847e4cb78f352aba30aa41c76e08e6eb46/model-00004-of-00004.safetensors", "model-00004-of-00004.safetensors", true, 4377211365LL},
    {"https://huggingface.co/mlx-community/Qwen3.6-35B-A3B-4bit/resolve/38740b847e4cb78f352aba30aa41c76e08e6eb46/model.safetensors.index.json", "model.safetensors.index.json", true, 0},
    {"https://huggingface.co/mlx-community/Qwen3.6-35B-A3B-4bit/resolve/38740b847e4cb78f352aba30aa41c76e08e6eb46/preprocessor_config.json", "preprocessor_config.json", true, 0},
    {"https://huggingface.co/mlx-community/Qwen3.6-35B-A3B-4bit/resolve/38740b847e4cb78f352aba30aa41c76e08e6eb46/processor_config.json", "processor_config.json", true, 0},
    {"https://huggingface.co/mlx-community/Qwen3.6-35B-A3B-4bit/resolve/38740b847e4cb78f352aba30aa41c76e08e6eb46/tokenizer.json", "tokenizer.json", true, 0},
    {"https://huggingface.co/mlx-community/Qwen3.6-35B-A3B-4bit/resolve/38740b847e4cb78f352aba30aa41c76e08e6eb46/tokenizer_config.json", "tokenizer_config.json", true, 0},
    {"https://huggingface.co/mlx-community/Qwen3.6-35B-A3B-4bit/resolve/38740b847e4cb78f352aba30aa41c76e08e6eb46/video_preprocessor_config.json", "video_preprocessor_config.json", true, 0},
    {"https://huggingface.co/mlx-community/Qwen3.6-35B-A3B-4bit/resolve/38740b847e4cb78f352aba30aa41c76e08e6eb46/vocab.json", "vocab.json", true, 0},
};

constexpr File kMlxQwen3_8_27BFiles[] = {
    {"https://huggingface.co/mlx-community/Qwen3.8-27B-4bit/resolve/3e6447f082e89cc7f0bc6e5441afd38dfce760ff/chat_template.jinja", "chat_template.jinja", true, 0},
    {"https://huggingface.co/mlx-community/Qwen3.8-27B-4bit/resolve/3e6447f082e89cc7f0bc6e5441afd38dfce760ff/config.json", "config.json", true, 0},
    {"https://huggingface.co/mlx-community/Qwen3.8-27B-4bit/resolve/3e6447f082e89cc7f0bc6e5441afd38dfce760ff/generation_config.json", "generation_config.json", true, 0},
    {"https://huggingface.co/mlx-community/Qwen3.8-27B-4bit/resolve/3e6447f082e89cc7f0bc6e5441afd38dfce760ff/model-00001-of-00003.safetensors", "model-00001-of-00003.safetensors", true, 5343268662LL},
    {"https://huggingface.co/mlx-community/Qwen3.8-27B-4bit/resolve/3e6447f082e89cc7f0bc6e5441afd38dfce760ff/model-00002-of-00003.safetensors", "model-00002-of-00003.safetensors", true, 5354185130LL},
    {"https://huggingface.co/mlx-community/Qwen3.8-27B-4bit/resolve/3e6447f082e89cc7f0bc6e5441afd38dfce760ff/model-00003-of-00003.safetensors", "model-00003-of-00003.safetensors", true, 5357087557LL},
    {"https://huggingface.co/mlx-community/Qwen3.8-27B-4bit/resolve/3e6447f082e89cc7f0bc6e5441afd38dfce760ff/model.safetensors.index.json", "model.safetensors.index.json", true, 0},
    {"https://huggingface.co/mlx-community/Qwen3.8-27B-4bit/resolve/3e6447f082e89cc7f0bc6e5441afd38dfce760ff/preprocessor_config.json", "preprocessor_config.json", true, 0},
    {"https://huggingface.co/mlx-community/Qwen3.8-27B-4bit/resolve/3e6447f082e89cc7f0bc6e5441afd38dfce760ff/processor_config.json", "processor_config.json", true, 0},
    {"https://huggingface.co/mlx-community/Qwen3.8-27B-4bit/resolve/3e6447f082e89cc7f0bc6e5441afd38dfce760ff/tokenizer.json", "tokenizer.json", true, 0},
    {"https://huggingface.co/mlx-community/Qwen3.8-27B-4bit/resolve/3e6447f082e89cc7f0bc6e5441afd38dfce760ff/tokenizer_config.json", "tokenizer_config.json", true, 0},
    {"https://huggingface.co/mlx-community/Qwen3.8-27B-4bit/resolve/3e6447f082e89cc7f0bc6e5441afd38dfce760ff/video_preprocessor_config.json", "video_preprocessor_config.json", true, 0},
    {"https://huggingface.co/mlx-community/Qwen3.8-27B-4bit/resolve/3e6447f082e89cc7f0bc6e5441afd38dfce760ff/vocab.json", "vocab.json", true, 0},
};

constexpr File kMlxSoprano1180M5BitFiles[] = {
    {"https://huggingface.co/mlx-community/Soprano-1.1-80M-5bit/resolve/main/config.json", "config.json", true, 0},
    {"https://huggingface.co/mlx-community/Soprano-1.1-80M-5bit/resolve/main/generation_config.json", "generation_config.json", true, 0},
    {"https://huggingface.co/mlx-community/Soprano-1.1-80M-5bit/resolve/main/model.safetensors", "model.safetensors", true, 0},
    {"https://huggingface.co/mlx-community/Soprano-1.1-80M-5bit/resolve/main/model.safetensors.index.json", "model.safetensors.index.json", true, 0},
    {"https://huggingface.co/mlx-community/Soprano-1.1-80M-5bit/resolve/main/special_tokens_map.json", "special_tokens_map.json", true, 0},
    {"https://huggingface.co/mlx-community/Soprano-1.1-80M-5bit/resolve/main/tokenizer.json", "tokenizer.json", true, 0},
    {"https://huggingface.co/mlx-community/Soprano-1.1-80M-5bit/resolve/main/tokenizer_config.json", "tokenizer_config.json", true, 0},
};

constexpr File kMlxTernaryBonsai1_7B2BitFiles[] = {
    {"https://huggingface.co/prism-ml/Ternary-Bonsai-1.7B-mlx-2bit/resolve/main/chat_template.jinja", "chat_template.jinja", true, 0},
    {"https://huggingface.co/prism-ml/Ternary-Bonsai-1.7B-mlx-2bit/resolve/main/config.json", "config.json", true, 0},
    {"https://huggingface.co/prism-ml/Ternary-Bonsai-1.7B-mlx-2bit/resolve/main/model.safetensors", "model.safetensors", true, 0},
    {"https://huggingface.co/prism-ml/Ternary-Bonsai-1.7B-mlx-2bit/resolve/main/model.safetensors.index.json", "model.safetensors.index.json", true, 0},
    {"https://huggingface.co/prism-ml/Ternary-Bonsai-1.7B-mlx-2bit/resolve/main/tokenizer.json", "tokenizer.json", true, 0},
    {"https://huggingface.co/prism-ml/Ternary-Bonsai-1.7B-mlx-2bit/resolve/main/tokenizer_config.json", "tokenizer_config.json", true, 0},
};

constexpr File kMlxTernaryBonsai27B2BitFiles[] = {
    {"https://huggingface.co/prism-ml/Ternary-Bonsai-27B-mlx-2bit/resolve/main/chat_template.jinja", "chat_template.jinja", true, 0},
    {"https://huggingface.co/prism-ml/Ternary-Bonsai-27B-mlx-2bit/resolve/main/config.json", "config.json", true, 0},
    {"https://huggingface.co/prism-ml/Ternary-Bonsai-27B-mlx-2bit/resolve/main/merges.txt", "merges.txt", true, 0},
    {"https://huggingface.co/prism-ml/Ternary-Bonsai-27B-mlx-2bit/resolve/main/model.safetensors", "model.safetensors", true, 0},
    {"https://huggingface.co/prism-ml/Ternary-Bonsai-27B-mlx-2bit/resolve/main/model.safetensors.index.json", "model.safetensors.index.json", true, 0},
    {"https://huggingface.co/prism-ml/Ternary-Bonsai-27B-mlx-2bit/resolve/main/tokenizer.json", "tokenizer.json", true, 0},
    {"https://huggingface.co/prism-ml/Ternary-Bonsai-27B-mlx-2bit/resolve/main/tokenizer_config.json", "tokenizer_config.json", true, 0},
    {"https://huggingface.co/prism-ml/Ternary-Bonsai-27B-mlx-2bit/resolve/main/vocab.json", "vocab.json", true, 0},
};

constexpr File kMlxTernaryBonsai4B2BitFiles[] = {
    {"https://huggingface.co/prism-ml/Ternary-Bonsai-4B-mlx-2bit/resolve/main/chat_template.jinja", "chat_template.jinja", true, 0},
    {"https://huggingface.co/prism-ml/Ternary-Bonsai-4B-mlx-2bit/resolve/main/config.json", "config.json", true, 0},
    {"https://huggingface.co/prism-ml/Ternary-Bonsai-4B-mlx-2bit/resolve/main/model.safetensors", "model.safetensors", true, 0},
    {"https://huggingface.co/prism-ml/Ternary-Bonsai-4B-mlx-2bit/resolve/main/model.safetensors.index.json", "model.safetensors.index.json", true, 0},
    {"https://huggingface.co/prism-ml/Ternary-Bonsai-4B-mlx-2bit/resolve/main/tokenizer.json", "tokenizer.json", true, 0},
    {"https://huggingface.co/prism-ml/Ternary-Bonsai-4B-mlx-2bit/resolve/main/tokenizer_config.json", "tokenizer_config.json", true, 0},
};

constexpr File kMlxTernaryBonsai8B2BitFiles[] = {
    {"https://huggingface.co/prism-ml/Ternary-Bonsai-8B-mlx-2bit/resolve/main/chat_template.jinja", "chat_template.jinja", true, 0},
    {"https://huggingface.co/prism-ml/Ternary-Bonsai-8B-mlx-2bit/resolve/main/config.json", "config.json", true, 0},
    {"https://huggingface.co/prism-ml/Ternary-Bonsai-8B-mlx-2bit/resolve/main/model.safetensors", "model.safetensors", true, 0},
    {"https://huggingface.co/prism-ml/Ternary-Bonsai-8B-mlx-2bit/resolve/main/model.safetensors.index.json", "model.safetensors.index.json", true, 0},
    {"https://huggingface.co/prism-ml/Ternary-Bonsai-8B-mlx-2bit/resolve/main/tokenizer.json", "tokenizer.json", true, 0},
    {"https://huggingface.co/prism-ml/Ternary-Bonsai-8B-mlx-2bit/resolve/main/tokenizer_config.json", "tokenizer_config.json", true, 0},
};

constexpr File kMuseGlimmer30BFiles[] = {
    {"https://huggingface.co/unsloth/Muse-Glimmer-30B-GGUF/resolve/faa5b025c584459c13febfa5c59883516710ae39/Muse-Glimmer-30B-UD-Q4_K_XL.gguf", "Muse-Glimmer-30B-UD-Q4_K_XL.gguf", true, 15878222368LL},
    {"https://huggingface.co/unsloth/Muse-Glimmer-30B-GGUF/resolve/faa5b025c584459c13febfa5c59883516710ae39/mmproj-Muse-Glimmer-30B-Q8_0.gguf", "mmproj-Muse-Glimmer-30B-Q8_0.gguf", true, 2051685088LL},
};

constexpr File kNemotronOmniReasoningFiles[] = {
    {"https://huggingface.co/unsloth/NVIDIA-Nemotron-3-Nano-Omni-30B-A3B-Reasoning-GGUF/resolve/571758804835f56154718683f5c0e388b7d0fef9/NVIDIA-Nemotron-3-Nano-Omni-30B-A3B-Reasoning-UD-Q4_K_M.gguf", "NVIDIA-Nemotron-3-Nano-Omni-30B-A3B-Reasoning-UD-Q4_K_M.gguf", true, 23887023552LL},
    {"https://huggingface.co/unsloth/NVIDIA-Nemotron-3-Nano-Omni-30B-A3B-Reasoning-GGUF/resolve/571758804835f56154718683f5c0e388b7d0fef9/mmproj-F16.gguf", "mmproj-F16.gguf", true, 1587540224LL},
};

constexpr File kQwen2VlFiles[] = {
    {"https://huggingface.co/ggml-org/Qwen2-VL-2B-Instruct-GGUF/resolve/main/Qwen2-VL-2B-Instruct-Q4_K_M.gguf", "Qwen2-VL-2B-Instruct-Q4_K_M.gguf", true, 0},
    {"https://huggingface.co/ggml-org/Qwen2-VL-2B-Instruct-GGUF/resolve/main/mmproj-Qwen2-VL-2B-Instruct-Q8_0.gguf", "mmproj-Qwen2-VL-2B-Instruct-Q8_0.gguf", true, 0},
};

constexpr File kSherpaCanary180MFiles[] = {
    {"https://huggingface.co/csukuangfj/sherpa-onnx-nemo-canary-180m-flash-en-es-de-fr-int8/resolve/9077164e0d3dd1d5353743e89ceaa1d3a770838c/encoder.int8.onnx", "encoder.int8.onnx", true, 132678643LL},
    {"https://huggingface.co/csukuangfj/sherpa-onnx-nemo-canary-180m-flash-en-es-de-fr-int8/resolve/9077164e0d3dd1d5353743e89ceaa1d3a770838c/decoder.int8.onnx", "decoder.int8.onnx", true, 74437848LL},
    {"https://huggingface.co/csukuangfj/sherpa-onnx-nemo-canary-180m-flash-en-es-de-fr-int8/resolve/9077164e0d3dd1d5353743e89ceaa1d3a770838c/tokens.txt", "tokens.txt", true, 53555LL},
};

constexpr File kSherpaNemotronStreamingAsrFiles[] = {
    {"https://huggingface.co/csukuangfj2/sherpa-onnx-nemotron-3.5-asr-streaming-0.6b-320ms-int8-2026-06-11/resolve/424ce58898995b713f84341f2e1492f9207a26aa/encoder.int8.onnx", "encoder.int8.onnx", true, 657601518LL},
    {"https://huggingface.co/csukuangfj2/sherpa-onnx-nemotron-3.5-asr-streaming-0.6b-320ms-int8-2026-06-11/resolve/424ce58898995b713f84341f2e1492f9207a26aa/decoder.int8.onnx", "decoder.int8.onnx", true, 14978075LL},
    {"https://huggingface.co/csukuangfj2/sherpa-onnx-nemotron-3.5-asr-streaming-0.6b-320ms-int8-2026-06-11/resolve/424ce58898995b713f84341f2e1492f9207a26aa/joiner.int8.onnx", "joiner.int8.onnx", true, 9504438LL},
    {"https://huggingface.co/csukuangfj2/sherpa-onnx-nemotron-3.5-asr-streaming-0.6b-320ms-int8-2026-06-11/resolve/424ce58898995b713f84341f2e1492f9207a26aa/tokens.txt", "tokens.txt", true, 131440LL},
};

constexpr File kSherpaParakeetCtcFiles[] = {
    {"https://huggingface.co/runanywhere/sherpa-onnx-nemo-parakeet-ctc-1.1b-int8/resolve/48a549f552774db3cd09dd1548f3d1a2b37bc7c5/model.int8.onnx", "model.int8.onnx", true, 1110014145LL},
    {"https://huggingface.co/runanywhere/sherpa-onnx-nemo-parakeet-ctc-1.1b-int8/resolve/48a549f552774db3cd09dd1548f3d1a2b37bc7c5/tokens.txt", "tokens.txt", true, 10374LL},
};

constexpr File kSherpaParakeetTdtV2Files[] = {
    {"https://huggingface.co/csukuangfj/sherpa-onnx-nemo-parakeet-tdt-0.6b-v2-int8/resolve/1ab9323565ddb038682214b292f588070a538ce2/encoder.int8.onnx", "encoder.int8.onnx", true, 652184296LL},
    {"https://huggingface.co/csukuangfj/sherpa-onnx-nemo-parakeet-tdt-0.6b-v2-int8/resolve/1ab9323565ddb038682214b292f588070a538ce2/decoder.int8.onnx", "decoder.int8.onnx", true, 7257753LL},
    {"https://huggingface.co/csukuangfj/sherpa-onnx-nemo-parakeet-tdt-0.6b-v2-int8/resolve/1ab9323565ddb038682214b292f588070a538ce2/joiner.int8.onnx", "joiner.int8.onnx", true, 1739080LL},
    {"https://huggingface.co/csukuangfj/sherpa-onnx-nemo-parakeet-tdt-0.6b-v2-int8/resolve/1ab9323565ddb038682214b292f588070a538ce2/tokens.txt", "tokens.txt", true, 9384LL},
};

constexpr File kSherpaParakeetTdtV3Files[] = {
    {"https://huggingface.co/csukuangfj/sherpa-onnx-nemo-parakeet-tdt-0.6b-v3-int8/resolve/2bda32ec70b097a55adaa07d9a7173915b43cc78/encoder.int8.onnx", "encoder.int8.onnx", true, 652184281LL},
    {"https://huggingface.co/csukuangfj/sherpa-onnx-nemo-parakeet-tdt-0.6b-v3-int8/resolve/2bda32ec70b097a55adaa07d9a7173915b43cc78/decoder.int8.onnx", "decoder.int8.onnx", true, 11845275LL},
    {"https://huggingface.co/csukuangfj/sherpa-onnx-nemo-parakeet-tdt-0.6b-v3-int8/resolve/2bda32ec70b097a55adaa07d9a7173915b43cc78/joiner.int8.onnx", "joiner.int8.onnx", true, 6355277LL},
    {"https://huggingface.co/csukuangfj/sherpa-onnx-nemo-parakeet-tdt-0.6b-v3-int8/resolve/2bda32ec70b097a55adaa07d9a7173915b43cc78/tokens.txt", "tokens.txt", true, 93939LL},
};

constexpr File kSherpaSupertonicV3Files[] = {
    {"https://huggingface.co/csukuangfj2/sherpa-onnx-supertonic-3-tts-int8-2026-05-11/resolve/cca5a0e6c96e1d2c720986bf7e75fcc81dee3ae4/duration_predictor.int8.onnx", "duration_predictor.int8.onnx", true, 3700147LL},
    {"https://huggingface.co/csukuangfj2/sherpa-onnx-supertonic-3-tts-int8-2026-05-11/resolve/cca5a0e6c96e1d2c720986bf7e75fcc81dee3ae4/text_encoder.int8.onnx", "text_encoder.int8.onnx", true, 36416150LL},
    {"https://huggingface.co/csukuangfj2/sherpa-onnx-supertonic-3-tts-int8-2026-05-11/resolve/cca5a0e6c96e1d2c720986bf7e75fcc81dee3ae4/tts.json", "tts.json", true, 8253LL},
    {"https://huggingface.co/csukuangfj2/sherpa-onnx-supertonic-3-tts-int8-2026-05-11/resolve/cca5a0e6c96e1d2c720986bf7e75fcc81dee3ae4/unicode_indexer.bin", "unicode_indexer.bin", true, 262144LL},
    {"https://huggingface.co/csukuangfj2/sherpa-onnx-supertonic-3-tts-int8-2026-05-11/resolve/cca5a0e6c96e1d2c720986bf7e75fcc81dee3ae4/vector_estimator.int8.onnx", "vector_estimator.int8.onnx", true, 78400833LL},
    {"https://huggingface.co/csukuangfj2/sherpa-onnx-supertonic-3-tts-int8-2026-05-11/resolve/cca5a0e6c96e1d2c720986bf7e75fcc81dee3ae4/vocoder.int8.onnx", "vocoder.int8.onnx", true, 25991073LL},
    {"https://huggingface.co/csukuangfj2/sherpa-onnx-supertonic-3-tts-int8-2026-05-11/resolve/cca5a0e6c96e1d2c720986bf7e75fcc81dee3ae4/voice.bin", "voice.bin", true, 517168LL},
};

constexpr File kSmolVlm2Files[] = {
    {"https://huggingface.co/ggml-org/SmolVLM2-256M-Video-Instruct-GGUF/resolve/main/SmolVLM2-256M-Video-Instruct-Q8_0.gguf", "SmolVLM2-256M-Video-Instruct-Q8_0.gguf", true, 0},
    {"https://huggingface.co/ggml-org/SmolVLM2-256M-Video-Instruct-GGUF/resolve/main/mmproj-SmolVLM2-256M-Video-Instruct-Q8_0.gguf", "mmproj-SmolVLM2-256M-Video-Instruct-Q8_0.gguf", true, 0},
};

// Extracted from the SDK's built-in catalog (runanywhere-sdks
// rcli/src/catalog/catalog.cpp) rather than retyped, so ids, aliases and byte
// counts are the ones the SDK already ships. It is a SNAPSHOT: once this repo
// links the SDK, All() should read the live registry and this table goes away.
// Do not hand-edit entries here — regenerate.
constexpr Model kModels[] = {
    {"qwen3-0.6b", "qwen3", "Qwen3 0.6B Q8_0", Backend::LlamaCpp, Category::Language, 670040064LL, "https://huggingface.co/Qwen/Qwen3-0.6B-GGUF/resolve/main/Qwen3-0.6B-Q8_0.gguf", Format::Gguf, 4096, true, {}},
    {"qwen3-1.7b-q4_k_m", "qwen3-1.7b", "Qwen3 1.7B Q4_K_M", Backend::LlamaCpp, Category::Language, 1289748480LL, "https://huggingface.co/unsloth/Qwen3-1.7B-GGUF/resolve/main/Qwen3-1.7B-Q4_K_M.gguf", Format::Gguf, 4096, true, {}},
    {"qwen3-4b-q4_k_m", "qwen3-4b", "Qwen3 4B Q4_K_M", Backend::LlamaCpp, Category::Language, 2684354560LL, "https://huggingface.co/unsloth/Qwen3-4B-GGUF/resolve/main/Qwen3-4B-Q4_K_M.gguf", Format::Gguf, 4096, true, {}},
    {"bonsai-1.7b-q1_0", "bonsai-1.7b", "Bonsai-1.7B 1-bit Q1_0 (CPU)", Backend::LlamaCpp, Category::Language, 248302272LL, "https://huggingface.co/prism-ml/Bonsai-1.7B-gguf/resolve/main/Bonsai-1.7B-Q1_0.gguf", Format::Gguf, 4096, true, {}},
    {"bonsai-4b-q1_0", "bonsai-4b", "Bonsai-4B 1-bit Q1_0 (CPU)", Backend::LlamaCpp, Category::Language, 572270624LL, "https://huggingface.co/prism-ml/Bonsai-4B-gguf/resolve/main/Bonsai-4B-Q1_0.gguf", Format::Gguf, 4096, true, {}},
    {"bonsai-8b-q1_0", "bonsai-8b", "Bonsai-8B 1-bit Q1_0 (CPU)", Backend::LlamaCpp, Category::Language, 1158654496LL, "https://huggingface.co/prism-ml/Bonsai-8B-gguf/resolve/main/Bonsai-8B-Q1_0.gguf", Format::Gguf, 4096, true, {}},
    {"bonsai-27b-q1_0", "bonsai-27b", "Bonsai-27B 1-bit Q1_0 (CPU)", Backend::LlamaCpp, Category::Language, 3803452480LL, "https://huggingface.co/prism-ml/Bonsai-27B-gguf/resolve/main/Bonsai-27B-Q1_0.gguf", Format::Gguf, 4096, true, {}},
    {"ternary-bonsai-1.7b-q2_0-g64", "ternary-bonsai-1.7b", "Ternary-Bonsai-1.7B Q2_0 g64", Backend::LlamaCpp, Category::Language, 490163968LL, "https://huggingface.co/prism-ml/Ternary-Bonsai-1.7B-gguf/resolve/983b5dec2ff16aab79990711ba0f828a499a7e6a/Ternary-Bonsai-1.7B-Q2_0_g64.gguf", Format::Gguf, 4096, true, {}},
    {"ternary-bonsai-4b-q2_0-g64", "ternary-bonsai-4b", "Ternary-Bonsai-4B Q2_0 g64", Backend::LlamaCpp, Category::Language, 1137806656LL, "https://huggingface.co/prism-ml/Ternary-Bonsai-4B-gguf/resolve/a3eb42bafe873f9686bc97486c43b72ef7d75ec8/Ternary-Bonsai-4B-Q2_0_g64.gguf", Format::Gguf, 4096, true, {}},
    {"ternary-bonsai-8b-q2_0-g64", "ternary-bonsai-8b", "Ternary-Bonsai-8B Q2_0 g64", Backend::LlamaCpp, Category::Language, 2310125920LL, "https://huggingface.co/prism-ml/Ternary-Bonsai-8B-gguf/resolve/c2aefbeb4b24469cd11579c3384b990404c17a30/Ternary-Bonsai-8B-Q2_0_g64.gguf", Format::Gguf, 4096, true, {}},
    {"maple-preview-tq1_0-q4_k", "maple-preview", "DeepGrove Maple Preview TQ1_0 + Q4_K head (CPU)", Backend::LlamaCpp, Category::Language, 4984016416LL, "https://huggingface.co/deepgrove/maple-preview-GGUF/resolve/f5466f918e0c50cdb9d4d47a6f35813509a42a30/maple-preview-TQ1_0-head-Q4_K.gguf", Format::Gguf, 4096, true, {}},
    {"llama-3.2-3b", "llama3.2", "Llama 3.2 3B Instruct Q4_K_M", Backend::LlamaCpp, Category::Language, 2118123520LL, "https://huggingface.co/bartowski/Llama-3.2-3B-Instruct-GGUF/resolve/main/Llama-3.2-3B-Instruct-Q4_K_M.gguf", Format::Gguf, 0, false, {}},
    {"lfm2-350m-q8_0", "lfm2", "LiquidAI LFM2 350M Q8_0", Backend::LlamaCpp, Category::Language, 419430400LL, "https://huggingface.co/LiquidAI/LFM2-350M-GGUF/resolve/main/LFM2-350M-Q8_0.gguf", Format::Gguf, 2048, false, {}},
    {"smollm2-360m-q8_0", "smollm2", "SmolLM2 360M Q8_0", Backend::LlamaCpp, Category::Language, 404750336LL, "https://huggingface.co/prithivMLmods/SmolLM2-360M-GGUF/resolve/main/SmolLM2-360M.Q8_0.gguf", Format::Gguf, 2048, false, {}},
    {"gemma-4-e2b-it-q4_k_m", "gemma4-e2b", "Gemma 4 E2B IT Q4_K_M", Backend::LlamaCpp, Category::Language, 3106738272LL, "https://huggingface.co/unsloth/gemma-4-E2B-it-GGUF/resolve/0314792d7f1f7e229411f620751375812bb9faf2/gemma-4-E2B-it-Q4_K_M.gguf", Format::Gguf, 4096, false, {}},
    {"gemma-4-e4b-it-q4_k_m", "gemma4-e4b", "Gemma 4 E4B IT Q4_K_M", Backend::LlamaCpp, Category::Language, 4977171584LL, "https://huggingface.co/unsloth/gemma-4-E4B-it-GGUF/resolve/bfc15c382204943c3a8fff0c750b94ae2364d7a3/gemma-4-E4B-it-Q4_K_M.gguf", Format::Gguf, 4096, false, {}},
    {"gemma-4-12b-it-q4_k_m", "gemma4-12b", "Gemma 4 12B IT Q4_K_M", Backend::LlamaCpp, Category::Language, 7121861440LL, "https://huggingface.co/unsloth/gemma-4-12b-it-GGUF/resolve/fc034cfff751157913579611efad8462ac1be606/gemma-4-12b-it-Q4_K_M.gguf", Format::Gguf, 4096, false, {}},
    {"gemma-4-26b-a4b-it-q4_k_xl", "gemma4-26b-a4b", "Gemma 4 26B-A4B IT UD-Q4_K_XL (MoE)", Backend::LlamaCpp, Category::Language, 17010980576LL, "https://huggingface.co/unsloth/gemma-4-26B-A4B-it-GGUF/resolve/c099eb48e663fd284577b04978a94ffccb261841/gemma-4-26B-A4B-it-UD-Q4_K_XL.gguf", Format::Gguf, 4096, false, {}},
    {"gemma-4-31b-it-q4_k_m", "gemma4-31b", "Gemma 4 31B IT Q4_K_M", Backend::LlamaCpp, Category::Language, 18323733440LL, "https://huggingface.co/unsloth/gemma-4-31B-it-GGUF/resolve/c1ac76e99d5513b141e8adde7288b85c3f9c32ec/gemma-4-31B-it-Q4_K_M.gguf", Format::Gguf, 4096, false, {}},
    {"gemma-4-31b-it-ud-q2_k_xl", "gemma4-31b-q2", "Gemma 4 31B IT UD-Q2_K_XL", Backend::LlamaCpp, Category::Language, 11774991296LL, "https://huggingface.co/unsloth/gemma-4-31B-it-GGUF/resolve/c1ac76e99d5513b141e8adde7288b85c3f9c32ec/gemma-4-31B-it-UD-Q2_K_XL.gguf", Format::Gguf, 4096, false, {}},
    {"qwen3.6-35b-a3b-q4_k_m", "qwen3.6-35b", "Qwen3.6 35B-A3B UD-Q4_K_M (MoE)", Backend::LlamaCpp, Category::Language, 22134528992LL, "https://huggingface.co/unsloth/Qwen3.6-35B-A3B-GGUF/resolve/a483e9e6cbd595906af30beda3187c2663a1118c/Qwen3.6-35B-A3B-UD-Q4_K_M.gguf", Format::Gguf, 4096, true, {}},
    {"qwen3.8-27b-q4_k_m", "qwen3.8-27b", "Qwen3.8 27B Q4_K_M", Backend::LlamaCpp, Category::Language, 17106775008LL, "https://huggingface.co/unsloth/Qwen3.8-27B-GGUF/resolve/f1bfb127c64f7072bdd2cad55f258b9c8b2910fe/Qwen3.8-27B-Q4_K_M.gguf", Format::Gguf, 4096, true, {}},
    {"granite-4.1-3b-q4_k_m", "granite4.1-3b", "IBM Granite 4.1 3B Q4_K_M", Backend::LlamaCpp, Category::Language, 2099502400LL, "https://huggingface.co/unsloth/granite-4.1-3b-GGUF/resolve/5b88826e4b80789548180f8faab39c5cf68772c9/granite-4.1-3b-Q4_K_M.gguf", Format::Gguf, 4096, false, {}},
    {"granite-4.1-8b-q4_k_m", "granite4.1-8b", "IBM Granite 4.1 8B Q4_K_M", Backend::LlamaCpp, Category::Language, 5347915136LL, "https://huggingface.co/unsloth/granite-4.1-8b-GGUF/resolve/6f9671f73eb03273bc09319194b8a4e810e03a8f/granite-4.1-8b-Q4_K_M.gguf", Format::Gguf, 4096, false, {}},
    {"granite-4.1-30b-q4_k_m", "granite4.1-30b", "IBM Granite 4.1 30B Q4_K_M", Backend::LlamaCpp, Category::Language, 17490241472LL, "https://huggingface.co/unsloth/granite-4.1-30b-GGUF/resolve/6cb34f31b11ca4c1433de1af7391dac46de4e666/granite-4.1-30b-Q4_K_M.gguf", Format::Gguf, 4096, false, {}},
    {"smolvlm2-256m-video-instruct-q8_0", "smolvlm2", "SmolVLM2 256M Video Instruct Q8_0", Backend::LlamaCpp, Category::Multimodal, 440401920LL, "", Format::Gguf, 2048, false, kSmolVlm2Files},
    {"lfm2-vl-450m-q8_0", "lfm2-vl", "LFM2-VL 450M Q8_0", Backend::LlamaCpp, Category::Multimodal, 629145600LL, "", Format::Gguf, 0, false, kLfm2VlFiles},
    {"lfm2.5-vl-3b-q4_k_m", "lfm2.5-vl", "LFM2.5-VL 3B Q4_K_M", Backend::LlamaCpp, Category::Multimodal, 2257563360LL, "", Format::Gguf, 4096, false, kLfm2_5Vl3BFiles},
    {"qwen2-vl-2b-instruct-q4_k_m", "qwen2-vl", "Qwen2-VL 2B Instruct Q4_K_M", Backend::LlamaCpp, Category::Multimodal, 1887436800LL, "", Format::Gguf, 2048, false, kQwen2VlFiles},
    {"fara1.5-4b-q4_k_m", "fara", "Fara1.5 4B Computer-Use Agent Q4_K_M", Backend::LlamaCpp, Category::Multimodal, 3460300800LL, "", Format::Gguf, 4096, false, kFara15GgufFiles},
    {"muse-glimmer-30b-q4_k_xl", "muse-glimmer", "Muse Glimmer 30B UD-Q4_K_XL", Backend::LlamaCpp, Category::Multimodal, 17929907456LL, "", Format::Gguf, 4096, false, kMuseGlimmer30BFiles},
    {"nemotron-3-nano-omni-30b-a3b-reasoning-q4_k_m", "nemotron-omni", "NVIDIA Nemotron-3-Nano-Omni 30B-A3B Reasoning UD-Q4_K_M (vision, MoE)", Backend::LlamaCpp, Category::Multimodal, 25474563776LL, "", Format::Gguf, 4096, true, kNemotronOmniReasoningFiles},
    {"sherpa-onnx-whisper-tiny.en", "whisper-tiny", "Whisper Tiny English (Sherpa-ONNX)", Backend::Sherpa, Category::SpeechRecognition, 78643200LL, "https://github.com/RunanywhereAI/sherpa-onnx/releases/download/runanywhere-models-v1/sherpa-onnx-whisper-tiny.en.tar.gz", Format::Onnx, 0, false, {}},
    {"sherpa-nemo-parakeet-tdt-0.6b-v2-int8", "parakeet-tdt-v2", "NVIDIA Parakeet TDT 0.6B v2 INT8 (Sherpa-ONNX)", Backend::Sherpa, Category::SpeechRecognition, 661190513LL, "", Format::Onnx, 0, false, kSherpaParakeetTdtV2Files},
    {"sherpa-nemo-parakeet-tdt-0.6b-v3-int8", "parakeet-tdt-v3", "NVIDIA Parakeet TDT 0.6B v3 INT8 (Sherpa-ONNX)", Backend::Sherpa, Category::SpeechRecognition, 670478772LL, "", Format::Onnx, 0, false, kSherpaParakeetTdtV3Files},
    {"sherpa-nemo-parakeet-ctc-1.1b-int8", "parakeet-ctc", "NVIDIA Parakeet CTC 1.1B INT8 (Sherpa-ONNX)", Backend::Sherpa, Category::SpeechRecognition, 1110024519LL, "", Format::Onnx, 0, false, kSherpaParakeetCtcFiles},
    {"sherpa-nemo-canary-180m-flash-int8", "canary-180m", "NVIDIA Canary 180M Flash INT8 (Sherpa-ONNX)", Backend::Sherpa, Category::SpeechRecognition, 207170046LL, "", Format::Onnx, 0, false, kSherpaCanary180MFiles},
    {"sherpa-nemotron-3.5-asr-streaming-0.6b-320ms-int8", "nemotron-asr-streaming", "NVIDIA Nemotron 3.5 Streaming ASR 0.6B 320ms INT8 (Sherpa-ONNX)", Backend::Sherpa, Category::SpeechRecognition, 682215471LL, "", Format::Onnx, 0, false, kSherpaNemotronStreamingAsrFiles},
    {"vits-piper-en_US-lessac-medium", "piper", "Piper TTS US English (Lessac Medium)", Backend::Sherpa, Category::SpeechSynthesis, 68157440LL, "https://github.com/RunanywhereAI/sherpa-onnx/releases/download/runanywhere-models-v1/vits-piper-en_US-lessac-medium.tar.gz", Format::Onnx, 0, false, {}},
    {"sherpa-supertonic-3-tts-int8", "supertonic", "Supertone Supertonic v3 TTS INT8 (Sherpa-ONNX)", Backend::Sherpa, Category::SpeechSynthesis, 145295768LL, "", Format::Onnx, 0, false, kSherpaSupertonicV3Files},
    {"silero-vad", "silero", "Silero VAD", Backend::Onnx, Category::VoiceActivityDetection, 2327524LL, "https://github.com/snakers4/silero-vad/raw/master/src/silero_vad/data/silero_vad.onnx", Format::Onnx, 0, false, {}},
    {"diar-streaming-sortformer-4spk-v2.1", "sortformer", "NVIDIA Streaming Sortformer 4-Speaker v2.1", Backend::Onnx, Category::SpeakerDiarization, 492242946LL, "https://huggingface.co/cgus/diar_streaming_sortformer_4spk-v2.1-onnx/resolve/main/diar_streaming_sortformer_4spk-v2.1.onnx", Format::Onnx, 0, false, {}},
    {"segformer-b0-ade-512", "segformer", "SegFormer B0 ADE20K 512 (Semantic Segmentation)", Backend::Onnx, Category::SemanticSegmentation, 15335446LL, "https://huggingface.co/Xenova/segformer-b0-finetuned-ade-512-512/resolve/main/onnx/model.onnx", Format::Onnx, 0, false, {}},
    {"nemotron-3-embed-1b-q4_k_m", "nemotron-3-embed", "NVIDIA Nemotron 3 Embed 1B Q4_K_M", Backend::LlamaCpp, Category::Embedding, 749352096LL, "https://huggingface.co/zenmagnets/Nemotron-3-Embed-1B-Q4_K_M-GGUF/resolve/06df1fde6f7009c91f6cc3cd520081921929a678/nemotron-3-embed-1b-q4_k_m.gguf", Format::Gguf, 0, false, {}},
    {"llama-nemotron-embed-1b-v2-q4_k_m", "llama-nemotron-embed", "NVIDIA Llama Nemotron Embed 1B v2 Q4_K_M", Backend::LlamaCpp, Category::Embedding, 807690624LL, "https://huggingface.co/mykor/llama-nemotron-embed-1b-v2-GGUF/resolve/bf7c9832b1d76f86777379e58b7b74805ee58006/llama-nemotron-embed-1B-v2-Q4_K_M.gguf", Format::Gguf, 0, false, {}},
    {"llama-embed-nemotron-8b-q4_k_m", "llama-embed-nemotron", "NVIDIA Llama Embed Nemotron 8B Q4_K_M", Backend::LlamaCpp, Category::Embedding, 4625233184LL, "https://huggingface.co/mradermacher/llama-embed-nemotron-8b-GGUF/resolve/e7ae3cbae4f7693bbd75ec959bf293f39e1f2e25/llama-embed-nemotron-8b.Q4_K_M.gguf", Format::Gguf, 0, false, {}},
    {"all-minilm-l6-v2", "minilm", "All-MiniLM-L6-v2 (Embeddings)", Backend::Onnx, Category::Embedding, 94371840LL, "", Format::Onnx, 0, false, kMiniLmFiles},
    {"bge-reranker-v2-m3-q4_k_m", "bge-reranker", "BGE Reranker v2-m3 Q4_K_M (Reranking)", Backend::LlamaCpp, Category::Embedding, 438376864LL, "https://huggingface.co/gpustack/bge-reranker-v2-m3-GGUF/resolve/main/bge-reranker-v2-m3-Q4_K_M.gguf", Format::Gguf, 0, false, {}},
    {"stable-diffusion-v1-5-coreml", "sd15", "Stable Diffusion 1.5 (CoreML)", Backend::NeuRT, Category::ImageGeneration, 1565721769LL, "https://huggingface.co/apple/coreml-stable-diffusion-v1-5-palettized/resolve/main/coreml-stable-diffusion-v1-5-palettized_split_einsum_v2_compiled.zip", Format::Mlpackage, 0, false, {}},
    {"mlx-qwen3-0.6b-4bit", "mlx-qwen3", "Qwen3 0.6B 4-bit (MLX)", Backend::Mlx, Category::Language, 351383618LL, "", Format::Safetensors, 4096, true, kMlxQwen3_06BFiles},
    {"mlx-maple-preview-2bit", "mlx-maple-preview", "DeepGrove Maple Preview 2-bit (MLX)", Backend::Mlx, Category::Language, 5330252282LL, "", Format::Safetensors, 128000, true, kMlxMaplePreviewFiles},
    {"mlx-llama-3.1-nemotron-nano-8b-v1-4bit", "mlx-nemotron-nano", "NVIDIA Llama 3.1 Nemotron Nano 8B 4-bit (MLX)", Backend::Mlx, Category::Language, 4534806075LL, "", Format::Safetensors, 131072, false, kMlxNemotronNano8BFiles},
    {"mlx-nemotron-mini-4b-instruct-4bit", "mlx-nemotron-mini", "NVIDIA Nemotron Mini 4B Instruct 4-bit (MLX)", Backend::Mlx, Category::Language, 2392679103LL, "", Format::Safetensors, 4096, false, kMlxNemotronMini4BFiles},
    {"mlx-bonsai-1.7b-1bit", "mlx-bonsai-1.7b", "MLX Bonsai-1.7B 1-bit", Backend::Mlx, Category::Language, 269060904LL, "", Format::Safetensors, 4096, true, kMlxBonsai1_7B1BitFiles},
    {"mlx-bonsai-4b-1bit", "mlx-bonsai-4b", "MLX Bonsai-4B 1-bit", Backend::Mlx, Category::Language, 628865840LL, "", Format::Safetensors, 4096, true, kMlxBonsai4B1BitFiles},
    {"mlx-bonsai-8b-1bit", "mlx-bonsai-8b", "MLX Bonsai-8B 1-bit", Backend::Mlx, Category::Language, 1280131424LL, "", Format::Safetensors, 4096, true, kMlxBonsai8B1BitFiles},
    {"mlx-bonsai-27b-1bit", "mlx-bonsai", "MLX Bonsai-27B 1-bit", Backend::Mlx, Category::Language, 5129115752LL, "", Format::Safetensors, 4096, true, kMlxBonsai27B1BitFiles},
    {"mlx-ternary-bonsai-1.7b-2bit", "mlx-ternary-bonsai-1.7b", "MLX Ternary-Bonsai-1.7B 2-bit", Backend::Mlx, Category::Language, 484049216LL, "", Format::Safetensors, 4096, true, kMlxTernaryBonsai1_7B2BitFiles},
    {"mlx-ternary-bonsai-4b-2bit", "mlx-ternary-bonsai-4b", "MLX Ternary-Bonsai-4B 2-bit", Backend::Mlx, Category::Language, 1131565944LL, "", Format::Safetensors, 4096, true, kMlxTernaryBonsai4B2BitFiles},
    {"mlx-ternary-bonsai-8b-2bit", "mlx-ternary-bonsai-8b", "MLX Ternary-Bonsai-8B 2-bit", Backend::Mlx, Category::Language, 2303661704LL, "", Format::Safetensors, 4096, true, kMlxTernaryBonsai8B2BitFiles},
    {"mlx-ternary-bonsai-27b-2bit", "mlx-ternary-bonsai-27b", "MLX Ternary-Bonsai-27B 2-bit", Backend::Mlx, Category::Language, 8490785104LL, "", Format::Safetensors, 4096, true, kMlxTernaryBonsai27B2BitFiles},
    {"mlx-llama-3.2-1b-instruct-4bit", "mlx-llama3.2", "Llama 3.2 1B Instruct 4-bit (MLX)", Backend::Mlx, Category::Language, 712575975LL, "", Format::Safetensors, 0, false, kMlxLlama32_1BFiles},
    {"mlx-qwen2-vl-2b-instruct-4bit", "mlx-qwen2-vl", "Qwen2-VL 2B Instruct 4-bit (MLX)", Backend::Mlx, Category::Multimodal, 1261853827LL, "", Format::Safetensors, 2048, false, kMlxQwen2Vl2BFiles},
    {"mlx-fastvlm-0.5b-bf16", "mlx-fastvlm", "FastVLM 0.5B bf16 (MLX)", Backend::Mlx, Category::Multimodal, 1256926974LL, "", Format::Safetensors, 2048, false, kMlxFastVlm05BFiles},
    {"mlx-lfm2.5-vl-3b-4bit", "mlx-lfm2.5-vl", "LFM2.5-VL 3B 4-bit (MLX)", Backend::Mlx, Category::Multimodal, 2388258432LL, "", Format::Safetensors, 4096, false, kMlxLfm2_5Vl3BFiles},
    {"mlx-qwen3-embedding-0.6b-4bit-dwq", "mlx-qwen3-embed", "Qwen3 Embedding 0.6B 4-bit DWQ (MLX)", Backend::Mlx, Category::Embedding, 351230811LL, "", Format::Safetensors, 0, false, kMlxQwen3Embedding06BFiles},
    {"mlx-qwen3-asr-0.6b-8bit", "mlx-qwen3-asr", "Qwen3-ASR 0.6B 8-bit (MLX)", Backend::Mlx, Category::SpeechRecognition, 1010773761LL, "", Format::Safetensors, 0, false, kMlxQwen3Asr06BFiles},
    {"mlx-glm-asr-nano-2512-4bit", "mlx-glm-asr", "GLM-ASR Nano 2512 4-bit (MLX)", Backend::Mlx, Category::SpeechRecognition, 1288437789LL, "", Format::Safetensors, 0, false, kMlxGlmAsrNano2512Files},
    {"mlx-parakeet-ctc-1.1b", "mlx-parakeet-ctc", "NVIDIA Parakeet CTC 1.1B (MLX)", Backend::Mlx, Category::SpeechRecognition, 4250718357LL, "", Format::Safetensors, 0, false, kMlxParakeetCtc11BFiles},
    {"mlx-parakeet-tdt-0.6b-v2", "mlx-parakeet-tdt-v2", "NVIDIA Parakeet TDT 0.6B v2 (MLX)", Backend::Mlx, Category::SpeechRecognition, 2471596080LL, "", Format::Safetensors, 0, false, kMlxParakeetTdtV2Files},
    {"mlx-parakeet-tdt-0.6b-v3", "mlx-parakeet-tdt-v3", "NVIDIA Parakeet TDT 0.6B v3 (MLX)", Backend::Mlx, Category::SpeechRecognition, 2508532829LL, "", Format::Safetensors, 0, false, kMlxParakeetTdtV3Files},
    {"mlx-parakeet-rnnt-1.1b", "mlx-parakeet-rnnt", "NVIDIA Parakeet RNNT 1.1B (MLX)", Backend::Mlx, Category::SpeechRecognition, 4282283914LL, "", Format::Safetensors, 0, false, kMlxParakeetRnnt11BFiles},
    {"mlx-nemotron-3.5-asr-streaming-0.6b-8bit", "mlx-nemotron-asr", "NVIDIA Nemotron 3.5 Streaming ASR 0.6B 8-bit (MLX)", Backend::Mlx, Category::SpeechRecognition, 755758528LL, "", Format::Safetensors, 0, false, kMlxNemotronStreamingAsrFiles},
    {"mlx-qwen3-tts-12hz-0.6b-base-8bit", "mlx-qwen3-tts", "Qwen3-TTS 12Hz 0.6B Base 8-bit (MLX)", Backend::Mlx, Category::SpeechSynthesis, 1991299138LL, "", Format::Safetensors, 0, false, kMlxQwen3Tts06BBaseFiles},
    {"mlx-soprano-1.1-80m-5bit", "mlx-soprano", "Soprano 1.1 80M 5-bit (MLX)", Backend::Mlx, Category::SpeechSynthesis, 82220814LL, "", Format::Safetensors, 0, false, kMlxSoprano1180M5BitFiles},
    {"mlx-gemma-4-e2b-it-4bit", "mlx-gemma4-e2b", "Gemma 4 E2B IT 4-bit (MLX)", Backend::Mlx, Category::Language, 3550670554LL, "", Format::Safetensors, 4096, false, kMlxGemma4E2BFiles},
    {"mlx-gemma-4-e4b-it-qat-4bit", "mlx-gemma4-e4b", "Gemma 4 E4B IT QAT 4-bit (MLX)", Backend::Mlx, Category::Language, 6798307742LL, "", Format::Safetensors, 4096, false, kMlxGemma4E4BFiles},
    {"mlx-gemma-4-12b-it-qat-4bit", "mlx-gemma4-12b", "Gemma 4 12B IT QAT 4-bit (MLX)", Backend::Mlx, Category::Language, 10987772430LL, "", Format::Safetensors, 4096, false, kMlxGemma4_12BFiles},
    {"mlx-gemma-4-26b-a4b-it-4bit", "mlx-gemma4-26b-a4b", "Gemma 4 26B-A4B IT 4-bit (MLX, MoE)", Backend::Mlx, Category::Language, 15341205776LL, "", Format::Safetensors, 4096, false, kMlxGemma4_26BA4BFiles},
    {"mlx-gemma-4-31b-it-4bit", "mlx-gemma4-31b", "Gemma 4 31B IT 4-bit (MLX)", Backend::Mlx, Category::Language, 18412016676LL, "", Format::Safetensors, 4096, false, kMlxGemma4_31BFiles},
    {"mlx-qwen3.6-35b-a3b-4bit", "mlx-qwen3.6-35b", "Qwen3.6 35B-A3B 4-bit (MLX, MoE)", Backend::Mlx, Category::Language, 20402204271LL, "", Format::Safetensors, 4096, true, kMlxQwen3_6_35BA3BFiles},
    {"mlx-qwen3.8-27b-4bit", "mlx-qwen3.8-27b", "Qwen3.8 27B 4-bit (MLX)", Backend::Mlx, Category::Language, 16054541349LL, "", Format::Safetensors, 4096, true, kMlxQwen3_8_27BFiles},
    {"mlx-granite-4.1-3b-4bit", "mlx-granite4.1-3b", "IBM Granite 4.1 3B 4-bit (MLX)", Backend::Mlx, Category::Language, 2127162429LL, "", Format::Safetensors, 4096, false, kMlxGranite4_1_3BFiles},
    {"mlx-granite-4.1-8b-4bit", "mlx-granite4.1-8b", "IBM Granite 4.1 8B 4-bit (MLX)", Backend::Mlx, Category::Language, 5238406779LL, "", Format::Safetensors, 4096, false, kMlxGranite4_1_8BFiles},
    {"mlx-granite-4.1-30b-4bit", "mlx-granite4.1-30b", "IBM Granite 4.1 30B 4-bit (MLX)", Backend::Mlx, Category::Language, 18041976573LL, "", Format::Safetensors, 4096, false, kMlxGranite4_1_30BFiles},
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
    return !model.url.empty() || !model.files.empty();
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
