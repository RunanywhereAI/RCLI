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
    {"qwen3-0.6b", "qwen3", "Qwen3 0.6B Q8_0", Backend::LlamaCpp, Modality::Language, 670040064LL},
    {"qwen3-1.7b-q4_k_m", "qwen3-1.7b", "Qwen3 1.7B Q4_K_M", Backend::LlamaCpp, Modality::Language, 1289748480LL},
    {"qwen3-4b-q4_k_m", "qwen3-4b", "Qwen3 4B Q4_K_M", Backend::LlamaCpp, Modality::Language, 2684354560LL},
    {"bonsai-1.7b-q1_0", "bonsai-1.7b", "Bonsai-1.7B 1-bit Q1_0 (CPU)", Backend::LlamaCpp, Modality::Language, 248302272LL},
    {"bonsai-4b-q1_0", "bonsai-4b", "Bonsai-4B 1-bit Q1_0 (CPU)", Backend::LlamaCpp, Modality::Language, 572270624LL},
    {"bonsai-8b-q1_0", "bonsai-8b", "Bonsai-8B 1-bit Q1_0 (CPU)", Backend::LlamaCpp, Modality::Language, 1158654496LL},
    {"bonsai-27b-q1_0", "bonsai-27b", "Bonsai-27B 1-bit Q1_0 (CPU)", Backend::LlamaCpp, Modality::Language, 3803452480LL},
    {"ternary-bonsai-1.7b-q2_0-g64", "ternary-bonsai-1.7b", "Ternary-Bonsai-1.7B Q2_0 g64", Backend::LlamaCpp, Modality::Language, 490163968LL},
    {"ternary-bonsai-4b-q2_0-g64", "ternary-bonsai-4b", "Ternary-Bonsai-4B Q2_0 g64", Backend::LlamaCpp, Modality::Language, 1137806656LL},
    {"ternary-bonsai-8b-q2_0-g64", "ternary-bonsai-8b", "Ternary-Bonsai-8B Q2_0 g64", Backend::LlamaCpp, Modality::Language, 2310125920LL},
    {"maple-preview-tq1_0-q4_k", "maple-preview", "DeepGrove Maple Preview TQ1_0 + Q4_K head (CPU)", Backend::LlamaCpp, Modality::Language, 4984016416LL},
    {"llama-3.2-3b", "llama3.2", "Llama 3.2 3B Instruct Q4_K_M", Backend::LlamaCpp, Modality::Language, 2118123520LL},
    {"lfm2-350m-q8_0", "lfm2", "LiquidAI LFM2 350M Q8_0", Backend::LlamaCpp, Modality::Language, 419430400LL},
    {"smollm2-360m-q8_0", "smollm2", "SmolLM2 360M Q8_0", Backend::LlamaCpp, Modality::Language, 404750336LL},
    {"gemma-4-e2b-it-q4_k_m", "gemma4-e2b", "Gemma 4 E2B IT Q4_K_M", Backend::LlamaCpp, Modality::Language, 3106738272LL},
    {"gemma-4-e4b-it-q4_k_m", "gemma4-e4b", "Gemma 4 E4B IT Q4_K_M", Backend::LlamaCpp, Modality::Language, 4977171584LL},
    {"gemma-4-12b-it-q4_k_m", "gemma4-12b", "Gemma 4 12B IT Q4_K_M", Backend::LlamaCpp, Modality::Language, 7121861440LL},
    {"gemma-4-26b-a4b-it-q4_k_xl", "gemma4-26b-a4b", "Gemma 4 26B-A4B IT UD-Q4_K_XL (MoE)", Backend::LlamaCpp, Modality::Language, 17010980576LL},
    {"gemma-4-31b-it-q4_k_m", "gemma4-31b", "Gemma 4 31B IT Q4_K_M", Backend::LlamaCpp, Modality::Language, 18323733440LL},
    {"gemma-4-31b-it-ud-q2_k_xl", "gemma4-31b-q2", "Gemma 4 31B IT UD-Q2_K_XL", Backend::LlamaCpp, Modality::Language, 11774991296LL},
    {"qwen3.6-35b-a3b-q4_k_m", "qwen3.6-35b", "Qwen3.6 35B-A3B UD-Q4_K_M (MoE)", Backend::LlamaCpp, Modality::Language, 22134528992LL},
    {"qwen3.8-27b-q4_k_m", "qwen3.8-27b", "Qwen3.8 27B Q4_K_M", Backend::LlamaCpp, Modality::Language, 17106775008LL},
    {"granite-4.1-3b-q4_k_m", "granite4.1-3b", "IBM Granite 4.1 3B Q4_K_M", Backend::LlamaCpp, Modality::Language, 2099502400LL},
    {"granite-4.1-8b-q4_k_m", "granite4.1-8b", "IBM Granite 4.1 8B Q4_K_M", Backend::LlamaCpp, Modality::Language, 5347915136LL},
    {"granite-4.1-30b-q4_k_m", "granite4.1-30b", "IBM Granite 4.1 30B Q4_K_M", Backend::LlamaCpp, Modality::Language, 17490241472LL},
    {"smolvlm2-256m-video-instruct-q8_0", "smolvlm2", "SmolVLM2 256M Video Instruct Q8_0", Backend::LlamaCpp, Modality::Vision, 440401920LL},
    {"lfm2-vl-450m-q8_0", "lfm2-vl", "LFM2-VL 450M Q8_0", Backend::LlamaCpp, Modality::Vision, 629145600LL},
    {"lfm2.5-vl-3b-q4_k_m", "lfm2.5-vl", "LFM2.5-VL 3B Q4_K_M", Backend::LlamaCpp, Modality::Vision, 2257563360LL},
    {"qwen2-vl-2b-instruct-q4_k_m", "qwen2-vl", "Qwen2-VL 2B Instruct Q4_K_M", Backend::LlamaCpp, Modality::Vision, 1887436800LL},
    {"fara1.5-4b-q4_k_m", "fara", "Fara1.5 4B Computer-Use Agent Q4_K_M", Backend::LlamaCpp, Modality::Vision, 3460300800LL},
    {"muse-glimmer-30b-q4_k_xl", "muse-glimmer", "Muse Glimmer 30B UD-Q4_K_XL", Backend::LlamaCpp, Modality::Vision, 17929907456LL},
    {"nemotron-3-nano-omni-30b-a3b-reasoning-q4_k_m", "nemotron-omni", "NVIDIA Nemotron-3-Nano-Omni 30B-A3B Reasoning UD-Q4_K_M (vision, MoE)", Backend::LlamaCpp, Modality::Vision, 25474563776LL},
    {"sherpa-onnx-whisper-tiny.en", "whisper-tiny", "Whisper Tiny English (Sherpa-ONNX)", Backend::Sherpa, Modality::Speech, 78643200LL},
    {"sherpa-nemo-parakeet-tdt-0.6b-v2-int8", "parakeet-tdt-v2", "NVIDIA Parakeet TDT 0.6B v2 INT8 (Sherpa-ONNX)", Backend::Sherpa, Modality::Speech, 661190513LL},
    {"sherpa-nemo-parakeet-tdt-0.6b-v3-int8", "parakeet-tdt-v3", "NVIDIA Parakeet TDT 0.6B v3 INT8 (Sherpa-ONNX)", Backend::Sherpa, Modality::Speech, 670478772LL},
    {"sherpa-nemo-parakeet-ctc-1.1b-int8", "parakeet-ctc", "NVIDIA Parakeet CTC 1.1B INT8 (Sherpa-ONNX)", Backend::Sherpa, Modality::Speech, 1110024519LL},
    {"sherpa-nemo-canary-180m-flash-int8", "canary-180m", "NVIDIA Canary 180M Flash INT8 (Sherpa-ONNX)", Backend::Sherpa, Modality::Speech, 207170046LL},
    {"sherpa-nemotron-3.5-asr-streaming-0.6b-320ms-int8", "nemotron-asr-streaming", "NVIDIA Nemotron 3.5 Streaming ASR 0.6B 320ms INT8 (Sherpa-ONNX)", Backend::Sherpa, Modality::Speech, 682215471LL},
    {"vits-piper-en_US-lessac-medium", "piper", "Piper TTS US English (Lessac Medium)", Backend::Sherpa, Modality::Voice, 68157440LL},
    {"sherpa-supertonic-3-tts-int8", "supertonic", "Supertone Supertonic v3 TTS INT8 (Sherpa-ONNX)", Backend::Sherpa, Modality::Voice, 145295768LL},
    {"silero-vad", "silero", "Silero VAD", Backend::Onnx, Modality::Voice, 2327524LL},
    {"diar-streaming-sortformer-4spk-v2.1", "sortformer", "NVIDIA Streaming Sortformer 4-Speaker v2.1", Backend::Onnx, Modality::Speech, 492242946LL},
    {"segformer-b0-ade-512", "segformer", "SegFormer B0 ADE20K 512 (Semantic Segmentation)", Backend::Onnx, Modality::Vision, 15335446LL},
    {"nemotron-3-embed-1b-q4_k_m", "nemotron-3-embed", "NVIDIA Nemotron 3 Embed 1B Q4_K_M", Backend::LlamaCpp, Modality::Embedding, 749352096LL},
    {"llama-nemotron-embed-1b-v2-q4_k_m", "llama-nemotron-embed", "NVIDIA Llama Nemotron Embed 1B v2 Q4_K_M", Backend::LlamaCpp, Modality::Embedding, 807690624LL},
    {"llama-embed-nemotron-8b-q4_k_m", "llama-embed-nemotron", "NVIDIA Llama Embed Nemotron 8B Q4_K_M", Backend::LlamaCpp, Modality::Embedding, 4625233184LL},
    {"all-minilm-l6-v2", "minilm", "All-MiniLM-L6-v2 (Embeddings)", Backend::Onnx, Modality::Embedding, 94371840LL},
    {"bge-reranker-v2-m3-q4_k_m", "bge-reranker", "BGE Reranker v2-m3 Q4_K_M (Reranking)", Backend::LlamaCpp, Modality::Embedding, 438376864LL},
    {"stable-diffusion-v1-5-coreml", "sd15", "Stable Diffusion 1.5 (CoreML)", Backend::NeuRT, Modality::Image, 1258291200LL},
    {"mlx-qwen3-0.6b-4bit", "mlx-qwen3", "Qwen3 0.6B 4-bit (MLX)", Backend::Mlx, Modality::Language, 351383618LL},
    {"mlx-maple-preview-2bit", "mlx-maple-preview", "DeepGrove Maple Preview 2-bit (MLX)", Backend::Mlx, Modality::Language, 5330252282LL},
    {"mlx-llama-3.1-nemotron-nano-8b-v1-4bit", "mlx-nemotron-nano", "NVIDIA Llama 3.1 Nemotron Nano 8B 4-bit (MLX)", Backend::Mlx, Modality::Language, 4534806075LL},
    {"mlx-nemotron-mini-4b-instruct-4bit", "mlx-nemotron-mini", "NVIDIA Nemotron Mini 4B Instruct 4-bit (MLX)", Backend::Mlx, Modality::Language, 2392679103LL},
    {"mlx-bonsai-1.7b-1bit", "mlx-bonsai-1.7b", "MLX Bonsai-1.7B 1-bit", Backend::Mlx, Modality::Language, 269060904LL},
    {"mlx-bonsai-4b-1bit", "mlx-bonsai-4b", "MLX Bonsai-4B 1-bit", Backend::Mlx, Modality::Language, 628865840LL},
    {"mlx-bonsai-8b-1bit", "mlx-bonsai-8b", "MLX Bonsai-8B 1-bit", Backend::Mlx, Modality::Language, 1280131424LL},
    {"mlx-bonsai-27b-1bit", "mlx-bonsai", "MLX Bonsai-27B 1-bit", Backend::Mlx, Modality::Language, 5129115752LL},
    {"mlx-ternary-bonsai-1.7b-2bit", "mlx-ternary-bonsai-1.7b", "MLX Ternary-Bonsai-1.7B 2-bit", Backend::Mlx, Modality::Language, 484049216LL},
    {"mlx-ternary-bonsai-4b-2bit", "mlx-ternary-bonsai-4b", "MLX Ternary-Bonsai-4B 2-bit", Backend::Mlx, Modality::Language, 1131565944LL},
    {"mlx-ternary-bonsai-8b-2bit", "mlx-ternary-bonsai-8b", "MLX Ternary-Bonsai-8B 2-bit", Backend::Mlx, Modality::Language, 2303661704LL},
    {"mlx-ternary-bonsai-27b-2bit", "mlx-ternary-bonsai-27b", "MLX Ternary-Bonsai-27B 2-bit", Backend::Mlx, Modality::Language, 8490785104LL},
    {"mlx-llama-3.2-1b-instruct-4bit", "mlx-llama3.2", "Llama 3.2 1B Instruct 4-bit (MLX)", Backend::Mlx, Modality::Language, 712575975LL},
    {"mlx-qwen2-vl-2b-instruct-4bit", "mlx-qwen2-vl", "Qwen2-VL 2B Instruct 4-bit (MLX)", Backend::Mlx, Modality::Vision, 1261853827LL},
    {"mlx-fastvlm-0.5b-bf16", "mlx-fastvlm", "FastVLM 0.5B bf16 (MLX)", Backend::Mlx, Modality::Vision, 1256926974LL},
    {"mlx-lfm2.5-vl-3b-4bit", "mlx-lfm2.5-vl", "LFM2.5-VL 3B 4-bit (MLX)", Backend::Mlx, Modality::Vision, 2388258432LL},
    {"mlx-qwen3-embedding-0.6b-4bit-dwq", "mlx-qwen3-embed", "Qwen3 Embedding 0.6B 4-bit DWQ (MLX)", Backend::Mlx, Modality::Embedding, 351230811LL},
    {"mlx-qwen3-asr-0.6b-8bit", "mlx-qwen3-asr", "Qwen3-ASR 0.6B 8-bit (MLX)", Backend::Mlx, Modality::Speech, 1010773761LL},
    {"mlx-glm-asr-nano-2512-4bit", "mlx-glm-asr", "GLM-ASR Nano 2512 4-bit (MLX)", Backend::Mlx, Modality::Speech, 1288437789LL},
    {"mlx-parakeet-ctc-1.1b", "mlx-parakeet-ctc", "NVIDIA Parakeet CTC 1.1B (MLX)", Backend::Mlx, Modality::Speech, 4250718357LL},
    {"mlx-parakeet-tdt-0.6b-v2", "mlx-parakeet-tdt-v2", "NVIDIA Parakeet TDT 0.6B v2 (MLX)", Backend::Mlx, Modality::Speech, 2471596080LL},
    {"mlx-parakeet-tdt-0.6b-v3", "mlx-parakeet-tdt-v3", "NVIDIA Parakeet TDT 0.6B v3 (MLX)", Backend::Mlx, Modality::Speech, 2508532829LL},
    {"mlx-parakeet-rnnt-1.1b", "mlx-parakeet-rnnt", "NVIDIA Parakeet RNNT 1.1B (MLX)", Backend::Mlx, Modality::Speech, 4282283914LL},
    {"mlx-nemotron-3.5-asr-streaming-0.6b-8bit", "mlx-nemotron-asr", "NVIDIA Nemotron 3.5 Streaming ASR 0.6B 8-bit (MLX)", Backend::Mlx, Modality::Speech, 755758528LL},
    {"mlx-qwen3-tts-12hz-0.6b-base-8bit", "mlx-qwen3-tts", "Qwen3-TTS 12Hz 0.6B Base 8-bit (MLX)", Backend::Mlx, Modality::Voice, 1991299138LL},
    {"mlx-soprano-1.1-80m-5bit", "mlx-soprano", "Soprano 1.1 80M 5-bit (MLX)", Backend::Mlx, Modality::Voice, 82220814LL},
    {"mlx-gemma-4-e2b-it-4bit", "mlx-gemma4-e2b", "Gemma 4 E2B IT 4-bit (MLX)", Backend::Mlx, Modality::Language, 3550670554LL},
    {"mlx-gemma-4-e4b-it-qat-4bit", "mlx-gemma4-e4b", "Gemma 4 E4B IT QAT 4-bit (MLX)", Backend::Mlx, Modality::Language, 6798307742LL},
    {"mlx-gemma-4-12b-it-qat-4bit", "mlx-gemma4-12b", "Gemma 4 12B IT QAT 4-bit (MLX)", Backend::Mlx, Modality::Language, 10987772430LL},
    {"mlx-gemma-4-26b-a4b-it-4bit", "mlx-gemma4-26b-a4b", "Gemma 4 26B-A4B IT 4-bit (MLX, MoE)", Backend::Mlx, Modality::Language, 15341205776LL},
    {"mlx-gemma-4-31b-it-4bit", "mlx-gemma4-31b", "Gemma 4 31B IT 4-bit (MLX)", Backend::Mlx, Modality::Language, 18412016676LL},
    {"mlx-qwen3.6-35b-a3b-4bit", "mlx-qwen3.6-35b", "Qwen3.6 35B-A3B 4-bit (MLX, MoE)", Backend::Mlx, Modality::Language, 20402204271LL},
    {"mlx-qwen3.8-27b-4bit", "mlx-qwen3.8-27b", "Qwen3.8 27B 4-bit (MLX)", Backend::Mlx, Modality::Language, 16054541349LL},
    {"mlx-granite-4.1-3b-4bit", "mlx-granite4.1-3b", "IBM Granite 4.1 3B 4-bit (MLX)", Backend::Mlx, Modality::Language, 2127162429LL},
    {"mlx-granite-4.1-8b-4bit", "mlx-granite4.1-8b", "IBM Granite 4.1 8B 4-bit (MLX)", Backend::Mlx, Modality::Language, 5238406779LL},
    {"mlx-granite-4.1-30b-4bit", "mlx-granite4.1-30b", "IBM Granite 4.1 30B 4-bit (MLX)", Backend::Mlx, Modality::Language, 18041976573LL},
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

std::string_view Label(Modality modality) {
    switch (modality) {
        case Modality::Language: return "language";
        case Modality::Vision: return "vision";
        case Modality::Speech: return "speech";
        case Modality::Voice: return "voice";
        case Modality::Embedding: return "embedding";
        case Modality::Image: return "image";
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
