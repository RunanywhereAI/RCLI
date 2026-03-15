#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// --- Visual Mode (overlay frame) ---

// Show the visual overlay window. User can drag/resize it over content.
// x, y, w, h: initial position and size in screen coordinates (0 = defaults).
void screen_capture_show_overlay(int x, int y, int w, int h);

// Hide the visual overlay window.
void screen_capture_hide_overlay(void);

// Returns 1 if the overlay is currently visible.
int screen_capture_overlay_active(void);

// Capture the screen region behind the overlay (hides overlay briefly).
// Returns 0 on success, -1 on failure.
int screen_capture_overlay_region(const char* output_path);

// --- Legacy capture functions ---

// Capture the frontmost/active window and save as JPEG.
int screen_capture_active_window(const char* output_path);

// Capture the window behind our own terminal (for voice triggers).
int screen_capture_behind_terminal(const char* output_path);

// Capture the entire main display and save as JPEG (fallback).
int screen_capture_full_screen(const char* output_path);

// Convenience: tries overlay if active, then active window, then full screen.
int screen_capture_screenshot(const char* output_path);

// Get the name of the app targeted by screen_capture_behind_terminal.
const char* screen_capture_target_app_name(char* buf, int buf_size);

#ifdef __cplusplus
}
#endif
