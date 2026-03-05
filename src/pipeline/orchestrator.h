#pragma once

#include "core/types.h"
#include "core/constants.h"
#include "core/memory_pool.h"
#include "core/ring_buffer.h"
#include "engines/stt_engine.h"
#include "engines/llm_engine.h"
#include "engines/tts_engine.h"
#include "engines/vad_engine.h"
#include "pipeline/sentence_detector.h"
#include "tools/tool_engine.h"
#include "audio/audio_io.h"
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <string>
#include <vector>

namespace rastack {

struct PipelineConfig {
    SttConfig        stt;
    OfflineSttConfig offline_stt;  // Whisper for file pipeline
    LlmConfig        llm;
    TtsConfig        tts;
    VadConfig        vad;
    AudioConfig      audio;

    size_t memory_pool_size    = 64 * 1024 * 1024;  // 64MB
    size_t audio_ring_capacity = 16384 * 10;         // ~10 sec at 16kHz
    size_t tts_ring_capacity   = 22050 * 30;         // ~30 sec at 22050Hz

    std::string system_prompt = RCLI_SYSTEM_PROMPT;
};

struct PipelineTimings {
    int64_t stt_latency_us        = 0;  // STT final result latency
    int64_t stt_audio_samples     = 0;  // Audio samples fed to STT (for RTF calc)
    int64_t llm_first_token_us    = 0;  // LLM time to first token
    int64_t llm_total_us          = 0;  // LLM total generation time
    int64_t tts_first_sentence_us = 0;  // TTS first sentence synthesis
    int64_t e2e_latency_us        = 0;  // Speech end → first audio out
    int64_t total_us              = 0;  // Total pipeline time
};

class Orchestrator {
public:
    Orchestrator();
    ~Orchestrator();

    // Initialize all engines
    bool init(const PipelineConfig& config);

    // --- File mode pipeline ---
    // Run full pipeline on a WAV file
    bool run_file_pipeline(const std::string& input_wav, const std::string& output_wav);

    // Stream pipeline: outputs audio chunks to stdout as they are synthesized
    bool run_stream_pipeline(const std::string& input_wav);

    // --- Live mode pipeline ---
    // Start live pipeline (mic → STT → LLM → TTS → speaker)
    bool start_live();
    void stop_live();

    // --- Push-to-talk helpers ---
    // Start mic capture only (no STT/LLM threads). Audio accumulates in the ring buffer.
    bool start_capture();
    // Stop mic capture and transcribe accumulated audio using offline Whisper.
    // Returns the transcript text (empty if no speech detected).
    std::string stop_capture_and_transcribe();

    // --- Individual engine access (for benchmarking) ---
    SttEngine&  stt()  { return stt_; }
    OfflineSttEngine& offline_stt() { return offline_stt_; }
    LlmEngine&  llm()  { return llm_; }
    TtsEngine&  tts()  { return tts_; }
    VadEngine&  vad()  { return vad_; }
    ToolEngine& tools() { return tools_; }
    AudioIO&    audio() { return audio_; }

    // Re-cache the system prompt after tool definitions change
    void recache_system_prompt();

    // Hot-swap the LLM model at runtime (pipeline must be IDLE)
    bool reload_llm(const LlmConfig& new_config);

    // State
    PipelineState state() const { return state_.load(std::memory_order_relaxed); }

    // Timings from last pipeline run
    const PipelineTimings& last_timings() const { return timings_; }

    // Set state change callback
    void set_state_callback(StateCallback cb) { state_cb_ = std::move(cb); }

    // Set transcript callback (fired for partial and final STT results in live mode)
    using TranscriptCallback = std::function<void(const std::string& text, bool is_final)>;
    void set_transcript_callback(TranscriptCallback cb) { transcript_cb_ = std::move(cb); }

    // Clear live conversation history
    void clear_history() { live_history_.clear(); }

private:
    void set_state(PipelineState new_state);

    // Live mode thread functions
    void stt_thread_fn();
    void llm_thread_fn();
    void tts_thread_fn();

    // Engines
    SttEngine        stt_;
    OfflineSttEngine offline_stt_;  // Whisper for file pipeline
    LlmEngine        llm_;
    TtsEngine        tts_;
    VadEngine        vad_;
    ToolEngine       tools_;
    AudioIO          audio_;

    // Infrastructure
    std::unique_ptr<MemoryPool>       pool_;
    std::unique_ptr<RingBuffer<float>> capture_rb_;
    std::unique_ptr<RingBuffer<float>> playback_rb_;

    // Config
    PipelineConfig config_;
    PipelineTimings timings_;

    // State
    std::atomic<PipelineState> state_{PipelineState::IDLE};
    StateCallback state_cb_;
    TranscriptCallback transcript_cb_;

    // Live mode threads
    std::thread stt_thread_;
    std::thread llm_thread_;
    std::atomic<bool> live_running_{false};

    // Inter-thread communication
    std::mutex              text_mutex_;
    std::condition_variable text_cv_;
    std::string             pending_text_;
    bool                    text_ready_ = false;

    // Conversation history for live mode (accessed only from llm_thread_fn)
    std::vector<std::pair<std::string, std::string>> live_history_;
};

} // namespace rastack
