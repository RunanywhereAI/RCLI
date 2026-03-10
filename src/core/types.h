#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>
#include <chrono>
#include <functional>
#include <vector>

namespace rastack {

// --- Cache-line alignment for zero false-sharing ---
static constexpr size_t CACHE_LINE_SIZE = 64;

// --- Pipeline states ---
enum class PipelineState : uint8_t {
    IDLE,
    LISTENING,
    PROCESSING,
    SPEAKING,
    INTERRUPTED,
    BARGE_IN,       // User speech detected during TTS playback
    VOICE_IDLE      // Pure voice mode: always-listening, waiting for wake word
};

inline const char* pipeline_state_str(PipelineState s) {
    switch (s) {
        case PipelineState::IDLE:        return "IDLE";
        case PipelineState::LISTENING:   return "LISTENING";
        case PipelineState::PROCESSING:  return "PROCESSING";
        case PipelineState::SPEAKING:    return "SPEAKING";
        case PipelineState::INTERRUPTED: return "INTERRUPTED";
        case PipelineState::BARGE_IN:    return "BARGE_IN";
        case PipelineState::VOICE_IDLE:  return "VOICE_IDLE";
    }
    return "UNKNOWN";
}

// --- Audio types ---
struct alignas(CACHE_LINE_SIZE) AudioChunk {
    float*   data;          // PCM samples (float32, mono)
    uint32_t num_samples;   // number of samples in this chunk
    uint32_t sample_rate;   // e.g., 16000 for STT, 22050 for TTS
    bool     is_final;      // last chunk in stream?
    int64_t  timestamp_us;  // capture timestamp (microseconds)
};

// --- Text segment from STT ---
struct TextSegment {
    std::string text;
    bool        is_final;    // false = partial, true = final
    float       confidence;  // 0.0 - 1.0
    int64_t     timestamp_us;
};

// --- LLM token output ---
struct TokenOutput {
    std::string text;        // decoded text for this token
    int32_t     token_id;    // raw token ID
    bool        is_eos;      // end of sequence?
    bool        is_tool_call;// part of a tool call?
};

// --- Tool types ---
struct ToolCall {
    std::string name;
    std::string arguments_json;
};

struct ToolResult {
    std::string name;
    std::string result_json;
    bool        success;
};

// --- Benchmark timing ---
struct TimingInfo {
    int64_t start_us;
    int64_t end_us;
    int64_t duration_us() const { return end_us - start_us; }
    double  duration_ms() const { return duration_us() / 1000.0; }
};

// --- Callbacks ---
using SttCallback     = std::function<void(const TextSegment&)>;
using TokenCallback   = std::function<void(const TokenOutput&)>;
using SentenceCallback = std::function<void(const std::string& sentence)>;
using StateCallback   = std::function<void(PipelineState old_state, PipelineState new_state)>;

// --- Utility ---
inline int64_t now_us() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
}

// =============================================================================
// RAG Types (used by vector_index, bm25_index, hybrid_retriever, etc.)
// =============================================================================

struct ChunkMeta {
    uint32_t chunk_id;
    uint32_t doc_id;
    uint16_t page_number;
    uint16_t chunk_index;
    uint32_t text_offset;
    uint32_t text_length;
};

struct RetrievalResult {
    uint32_t    chunk_id;
    float       score;
    float       vector_score;
    float       bm25_score;
    std::string_view text;
    ChunkMeta   meta;
};

struct RAGConfig {
    std::string index_path;
    std::string embedding_model_path;
    int         embedding_gpu_layers = 99;
    int         embedding_n_threads  = 2;
    int         embedding_n_batch    = 512;
    int         embedding_dim        = 384;
    int         top_k = 5;
    int         vector_candidates = 20;
    int         bm25_candidates = 20;
    float       rrf_k = 60.0f;
    size_t      cache_size_mb = 256;
};

} // namespace rastack
