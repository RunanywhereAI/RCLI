#pragma once

#include "core/types.h"
#include "core/constants.h"
#include "core/memory_pool.h"
#include "core/ring_buffer.h"
#include "engines/stt_engine.h"
#include "engines/llm_engine.h"
#include "engines/metalrt_engine.h"
#include "engines/metalrt_stt_engine.h"
#include "engines/metalrt_tts_engine.h"
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

enum class LlmBackend { AUTO, LLAMACPP, METALRT };

struct PipelineConfig {
    SttConfig        stt;
    OfflineSttConfig offline_stt;  // Whisper for file pipeline
    LlmConfig        llm;
    MetalRTEngineConfig metalrt;
    MetalRTSttConfig metalrt_stt;
    MetalRTTtsConfig metalrt_tts;
    TtsConfig        tts;
    VadConfig        vad;
    AudioConfig      audio;

    LlmBackend       llm_backend = LlmBackend::AUTO;

    size_t memory_pool_size    = 64 * 1024 * 1024;  // 64MB
    size_t audio_ring_capacity = 16384 * 10;         // ~10 sec at 16kHz
    size_t tts_ring_capacity   = 24000 * 60;         // ~60 sec at 24kHz (Kokoro)

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
    MetalRTEngine& metalrt_llm() { return metalrt_; }
    MetalRTSttEngine& metalrt_stt() { return metalrt_stt_; }
    MetalRTTtsEngine& metalrt_tts() { return metalrt_tts_; }
    TtsEngine&  tts()  { return tts_; }
    VadEngine&  vad()  { return vad_; }
    ToolEngine& tools() { return tools_; }
    AudioIO&    audio() { return audio_; }
    RingBuffer<float>* playback_ring_buffer() { return playback_rb_.get(); }

    // Active LLM backend
    LlmBackend active_llm_backend() const { return active_backend_; }
    bool using_metalrt() const { return active_backend_ == LlmBackend::METALRT; }

    // Update the base system prompt (e.g. when personality changes)
    void set_system_prompt(const std::string& prompt) { config_.system_prompt = prompt; }

    // Re-cache the system prompt after tool definitions or personality change
    void recache_system_prompt();

    // Change TTS voice at runtime (personality switch)
    void set_tts_voice(const std::string& voice_name);

    // Hot-swap the LLM model at runtime (pipeline must be IDLE)
    bool reload_llm(const LlmConfig& new_config);

    // Hot-swap the LLM backend (switch between llama.cpp and MetalRT)
    bool switch_backend(LlmBackend backend);

    // State
    PipelineState state() const { return state_.load(std::memory_order_relaxed); }

    // Timings from last pipeline run
    const PipelineTimings& last_timings() const { return timings_; }

    // Set state change callback
    void set_state_callback(StateCallback cb) { state_cb_ = std::move(cb); }

    // Set transcript callback (fired for partial and final STT results in live mode)
    using TranscriptCallback = std::function<void(const std::string& text, bool is_final)>;
    void set_transcript_callback(TranscriptCallback cb) { transcript_cb_ = std::move(cb); }

    // Set response callback (fired when LLM generates a response in live/voice mode)
    using ResponseCallback = std::function<void(const std::string& response)>;
    void set_response_callback(ResponseCallback cb) { response_cb_ = std::move(cb); }

    // Clear live conversation history
    void clear_history() { live_history_.clear(); }

    // Whether MetalRT STT/TTS are active and should be used
    bool using_metalrt_stt() const { return using_metalrt() && metalrt_stt_initialized_; }
    bool using_metalrt_tts() const { return using_metalrt() && metalrt_tts_initialized_; }

    // --- Barge-in control ---
    void set_barge_in_enabled(bool enabled) { barge_in_enabled_.store(enabled, std::memory_order_release); }
    bool barge_in_enabled() const { return barge_in_enabled_.load(std::memory_order_relaxed); }

    // Get the interrupted response text (for "continue" feature)
    std::string interrupted_response() const {
        std::lock_guard<std::mutex> lock(barge_in_mutex_);
        return interrupted_response_;
    }

    // --- Voice mode ---
    bool start_voice_mode(const std::string& wake_phrase = "jarvis");
    void stop_voice_mode();

    // Set barge-in callback (fired when user interrupts TTS)
    using BargeInCallback = std::function<void(const std::string& interrupted_text, int chars_spoken)>;
    void set_barge_in_callback(BargeInCallback cb) { barge_in_cb_ = std::move(cb); }

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
    MetalRTEngine    metalrt_;
    MetalRTSttEngine metalrt_stt_;
    MetalRTTtsEngine metalrt_tts_;
    bool metalrt_stt_initialized_ = false;
    bool metalrt_tts_initialized_ = false;
    TtsEngine        tts_;
    VadEngine        vad_;
    ToolEngine       tools_;
    AudioIO          audio_;
    LlmBackend       active_backend_ = LlmBackend::LLAMACPP;

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
    ResponseCallback response_cb_;

    // Live mode threads
    std::thread stt_thread_;
    std::thread llm_thread_;
    std::atomic<bool> live_running_{false};

    // Inter-thread communication
    std::mutex              text_mutex_;
    std::condition_variable text_cv_;
    std::string             pending_text_;
    bool                    text_ready_ = false;

    // --- Barge-in state ---
    std::atomic<bool> barge_in_enabled_{false};
    std::atomic<bool> barge_in_triggered_{false};  // STT thread signals LLM/TTS to abort
    std::atomic<bool> tts_cancel_flag_{false};     // TTS worker checks this to abort synthesis
    mutable std::mutex barge_in_mutex_;
    std::string interrupted_response_;             // full LLM response that was interrupted
    std::string interrupted_query_;                // user query that was interrupted
    int interrupted_chars_spoken_ = 0;             // how many chars of response were spoken
    int64_t interrupted_at_us_ = 0;                // timestamp for expiry
    BargeInCallback barge_in_cb_;

    // --- Voice mode ---
    std::string wake_phrase_;
    bool voice_mode_active_ = false;

    // Barge-in detection (called from stt_thread during SPEAKING state)
    void check_barge_in(const float* audio, int num_samples);

    // Conversation history for live mode (accessed only from llm_thread_fn)
    std::vector<std::pair<std::string, std::string>> live_history_;
};

} // namespace rastack
