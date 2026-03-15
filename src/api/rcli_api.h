#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// RCLI C API — Unified bridging layer for macOS, iOS, Python, etc.
// =============================================================================

typedef void* RCLIHandle;

// --- Callback types ---

// Real-time transcript updates (streaming STT)
typedef void (*RCLITranscriptCallback)(const char* text, int is_final, void* user_data);

// Pipeline state changes (0=idle, 1=listening, 2=processing, 3=speaking)
typedef void (*RCLIStateCallback)(int old_state, int new_state, void* user_data);

// Action execution results
typedef void (*RCLIActionCallback)(const char* action_name, const char* result_json, int success, void* user_data);

// Tool call trace — opt-in observability for the LLM tool-calling pipeline.
// Approach: a push callback rather than polling/log scraping, so the TUI (or any
// consumer) can display trace events in real-time without parsing stderr.
// Events: "detected" (tool call parsed from LLM output), "result" (execution complete)
// The callback fires synchronously on the rcli_process_command() thread; consumers
// that touch UI must post back to the render thread (e.g. screen_->Post()).
typedef void (*RCLIToolTraceCallback)(const char* event, const char* tool_name,
                                      const char* data, int success, void* user_data);

// Generic event callback (for file processing, timings)
// Events: "state_change", "timings"
typedef void (*RCLIEventCallback)(const char* event, const char* data, void* user_data);

// --- Lifecycle ---

// Create a RCLI engine instance. config_json can be NULL for defaults.
// Returns NULL on failure.
RCLIHandle rcli_create(const char* config_json);

// Destroy the engine and free all resources
void rcli_destroy(RCLIHandle handle);

// Initialize all engines (STT, LLM, TTS, VAD, Actions).
// models_dir: path to directory containing model files
// gpu_layers: number of LLM layers to offload to GPU (99 = all, 0 = CPU only)
// Returns 0 on success, non-zero on failure.
int rcli_init(RCLIHandle handle, const char* models_dir, int gpu_layers);

// Check if engine is initialized and ready
int rcli_is_ready(RCLIHandle handle);

// --- Voice Pipeline (live mode, macOS) ---

// Start listening (mic → STT → LLM → TTS → speaker)
int rcli_start_listening(RCLIHandle handle);

// Stop listening
int rcli_stop_listening(RCLIHandle handle);

// Process a text command (skip STT, go directly to LLM → action → TTS)
// Returns the response text. Caller must NOT free the returned pointer.
const char* rcli_process_command(RCLIHandle handle, const char* text);

// --- Push-to-talk (capture → Whisper transcription) ---

// Start mic capture only (no STT streaming). Call before user starts speaking.
int rcli_start_capture(RCLIHandle handle);

// Stop capture and transcribe with offline Whisper.
// Returns the transcript text. Caller must NOT free the returned pointer.
const char* rcli_stop_capture_and_transcribe(RCLIHandle handle);

// Speak text via TTS (non-streaming, uses afplay)
int rcli_speak(RCLIHandle handle, const char* text);

// Speak text via TTS with sentence-level streaming through CoreAudio ring buffer.
// Splits text into sentences, synthesizes each, and streams to audio output.
// callback fires "first_audio" and "complete" events. Blocks until audio finishes.
int rcli_speak_streaming(RCLIHandle handle, const char* text,
                         RCLIEventCallback callback, void* user_data);

// Streaming LLM → TTS: generates LLM response with sentence-level TTS streaming.
// Audio plays via CoreAudio ring buffer as sentences complete, not after the full
// response. The callback fires events:
//   "first_audio"  — first TTS sentence queued (data = TTFA ms as string)
//   "response"     — full LLM response text (data = complete response string)
//   "tts_stats"    — per-sentence TTS stats (data = JSON: text, synth_ms, rtf)
//   "complete"     — pipeline finished (data = JSON: total_tts_ms, sentences, ttfa_ms)
// Returns the full response text. Caller must NOT free.
const char* rcli_process_and_speak(RCLIHandle handle, const char* text,
                                    RCLIEventCallback callback, void* user_data);

// Stop TTS playback immediately (interrupt current speech)
void rcli_stop_speaking(RCLIHandle handle);

// Check if TTS audio is currently playing (afplay process is alive)
int rcli_is_speaking(RCLIHandle handle);

// Stop all ongoing processing: cancels LLM generation, stops TTS, stops STT.
// Safe to call from any thread. Non-blocking.
void rcli_stop_processing(RCLIHandle handle);

// Clear conversation history (start a fresh conversation within the session)
void rcli_clear_history(RCLIHandle handle);

// --- Personality ---

// Get the current personality key (e.g. "default", "quirky", "cynical", "nerdy", "professional")
const char* rcli_get_personality(RCLIHandle handle);

// Set personality and persist to config. Takes effect on next LLM call.
// Returns 0 on success, -1 if key is invalid.
int rcli_set_personality(RCLIHandle handle, const char* personality_key);

// Get the last transcript from STT
const char* rcli_get_transcript(RCLIHandle handle);

// --- File Pipeline (iOS, testing) ---

// Process a WAV file through the full pipeline (STT → LLM → TTS)
// input_wav: path to 16kHz mono WAV file
// output_wav: path where TTS output WAV will be written
// callback: called for pipeline events (may be called from worker thread)
// Returns 0 on success, non-zero on error
int rcli_process_wav(RCLIHandle handle,
                          const char* input_wav,
                          const char* output_wav,
                          RCLIEventCallback callback,
                          void* user_data);

// Get JSON string with timings from last pipeline run
// Caller must free() the returned string. Returns NULL if no run completed.
char* rcli_get_timings(RCLIHandle handle);

// --- RAG ---

// Ingest documents from a directory into the RAG index
int rcli_rag_ingest(RCLIHandle handle, const char* dir_path);

// Load a previously-built RAG index for querying. Call once at startup.
// index_path: directory containing the RAG index files.
// Returns 0 on success, non-zero on failure.
int rcli_rag_load_index(RCLIHandle handle, const char* index_path);

// Query the RAG system (returns retrieved context + LLM response)
const char* rcli_rag_query(RCLIHandle handle, const char* query);

// Clear the RAG index from memory (unload retriever + embeddings).
// After this call, queries go to plain LLM instead of RAG.
void rcli_rag_clear(RCLIHandle handle);

// --- Actions (macOS) ---

// Execute a named action with JSON arguments
const char* rcli_action_execute(RCLIHandle handle, const char* action_name, const char* args_json);

// List all available actions (returns JSON array)
const char* rcli_action_list(RCLIHandle handle);

// Enable or disable an action for LLM visibility (1 = enable, 0 = disable).
// Re-syncs tool definitions and re-caches the system prompt.
// Returns 0 on success, -1 on failure.
int rcli_set_action_enabled(RCLIHandle handle, const char* name, int enabled);

// Query whether an action is currently enabled. Returns 1 if enabled, 0 if not.
int rcli_is_action_enabled(RCLIHandle handle, const char* name);

// Get the number of currently enabled actions.
int rcli_num_actions_enabled(RCLIHandle handle);

// Persist action enable/disable preferences to ~/.rcli/actions.json.
// Returns 0 on success, -1 on failure.
int rcli_save_action_preferences(RCLIHandle handle);

// Disable all actions, clear tool definitions from LLM, and re-cache system prompt.
// Puts the engine into pure conversation mode.
void rcli_disable_all_actions(RCLIHandle handle);

// Restore actions to their default_enabled state (boot defaults).
void rcli_reset_actions_to_defaults(RCLIHandle handle);

// --- Model Hot-Swap ---

// Switch the active LLM model at runtime without restarting.
// model_id: slug from the model registry (e.g. "qwen3-0.6b", "lfm2-1.2b").
// Unloads the current model, loads the new one, re-detects the model profile,
// re-caches the system prompt with the correct tool-calling format, and
// persists the selection.
// Returns 0 on success, -1 on failure.
int rcli_switch_llm(RCLIHandle handle, const char* model_id);

// --- Barge-In & Voice Mode ---

// Enable/disable voice barge-in (user can interrupt TTS mid-speech)
void rcli_set_barge_in_enabled(RCLIHandle handle, int enabled);

// Check if barge-in is enabled
int rcli_is_barge_in_enabled(RCLIHandle handle);

// Start pure voice mode (always-listening with wake word detection)
// wake_phrase: trigger phrase (e.g. "jarvis"). NULL defaults to "jarvis".
// Returns 0 on success, -1 on failure.
int rcli_start_voice_mode(RCLIHandle handle, const char* wake_phrase);

// Stop voice mode and return to idle
void rcli_stop_voice_mode(RCLIHandle handle);

// Get the interrupted response text (for "continue" feature after barge-in)
// Returns empty string if no interrupted response. Caller must NOT free.
const char* rcli_get_interrupted_response(RCLIHandle handle);

// --- Callbacks ---

// Set callback for real-time transcript updates
void rcli_set_transcript_callback(RCLIHandle handle, RCLITranscriptCallback cb, void* user_data);

// Set callback for pipeline state changes
void rcli_set_state_callback(RCLIHandle handle, RCLIStateCallback cb, void* user_data);

// Set callback for LLM responses in live/voice mode
typedef void (*RCLIResponseCallback)(const char* response, void* user_data);
void rcli_set_response_callback(RCLIHandle handle, RCLIResponseCallback cb, void* user_data);

// Set callback for action results
void rcli_set_action_callback(RCLIHandle handle, RCLIActionCallback cb, void* user_data);

// Set callback for tool call trace events (detected / result)
void rcli_set_tool_trace_callback(RCLIHandle handle, RCLIToolTraceCallback cb, void* user_data);

// --- State ---

// Get current pipeline state (0=idle, 1=listening, 2=processing, 3=speaking, 4=interrupted)
int rcli_get_state(RCLIHandle handle);

// Get engine info as JSON (model names, device info, etc.)
const char* rcli_get_info(RCLIHandle handle);

// Get current microphone audio level (0.0 - 1.0 RMS, for waveform display)
float rcli_get_audio_level(RCLIHandle handle);

// Returns 1 if using Parakeet TDT (high-accuracy), 0 if using Whisper (default)
int rcli_is_using_parakeet(RCLIHandle handle);

// Get the name of the active LLM model. Caller must NOT free.
const char* rcli_get_llm_model(RCLIHandle handle);

// Returns the runtime LLM engine: "MetalRT" or "llama.cpp". Caller must NOT free.
const char* rcli_get_active_engine(RCLIHandle handle);

// Get the name of the active TTS voice. Caller must NOT free.
const char* rcli_get_tts_model(RCLIHandle handle);

// Get the name of the active offline STT model. Caller must NOT free.
const char* rcli_get_stt_model(RCLIHandle handle);

// Context window usage from last LLM call.
// out_prompt_tokens: total tokens consumed by the last prompt (history + system + user).
// out_ctx_size: model's configured context window size.
// Both output pointers are optional (pass NULL to skip).
void rcli_get_context_info(RCLIHandle handle, int* out_prompt_tokens, int* out_ctx_size);

// --- VLM (Vision Language Model) ---

// Initialize the VLM engine with the default VLM model.
// Lazily downloads the model if not present. Thread-safe.
// Returns 0 on success, -1 on failure.
int rcli_vlm_init(RCLIHandle handle);

// Analyze an image with an optional text prompt.
// image_path: absolute path to an image file (jpg, png, bmp, gif, webp, tga).
// prompt: text prompt (e.g. "Describe this image"). NULL defaults to "Describe this image in detail."
// Returns the analysis text. Caller must NOT free the returned pointer.
const char* rcli_vlm_analyze(RCLIHandle handle, const char* image_path, const char* prompt);

// Check if the VLM engine is initialized and ready for image analysis.
// Returns 1 if ready, 0 if not.
int rcli_vlm_is_ready(RCLIHandle handle);

// Get the name of the active VLM backend (e.g. "llama.cpp (Metal GPU)" or "MetalRT").
// Returns "" if VLM is not initialized.
const char* rcli_vlm_backend_name(RCLIHandle handle);

// Get the name of the active VLM model (e.g. "Qwen3 VL 2B Instruct").
// Returns "" if VLM is not initialized.
const char* rcli_vlm_model_name(RCLIHandle handle);

// VLM performance stats from the last analysis call.
typedef struct {
    double   gen_tok_per_sec;     // Generation tokens/second
    int      generated_tokens;    // Total tokens generated
    double   total_time_sec;      // Total wall time (image encode + prompt eval + generation)
    double   image_encode_ms;     // Time to encode image through vision projector
    double   first_token_ms;      // Time-to-first-token (prompt eval + image encode)
} RCLIVlmStats;

// Get stats from the last VLM analysis. Returns 0 on success.
int rcli_vlm_get_stats(RCLIHandle handle, RCLIVlmStats* out_stats);

// Swap MetalRT LLM out and VLM in on the GPU (for visual mode).
// Unloads the LLM model, loads the MetalRT VLM model.
// Returns 0 on success, -1 on failure.
int rcli_vlm_enter(RCLIHandle handle);

// Swap MetalRT VLM out and LLM back in on the GPU (exit visual mode).
// Unloads the VLM model, reloads the LLM and re-caches the system prompt.
// Returns 0 on success, -1 on failure.
int rcli_vlm_exit(RCLIHandle handle);

// Streaming VLM image analysis (use after rcli_vlm_enter).
// Fires callback with events: "token", "response", "stats".
// Returns 0 on success, -1 on failure.
int rcli_vlm_analyze_stream(RCLIHandle handle, const char* image_path,
                            const char* prompt,
                            RCLIEventCallback callback, void* user_data);

#ifdef __cplusplus
}
#endif
