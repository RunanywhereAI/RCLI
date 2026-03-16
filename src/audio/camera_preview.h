#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Launch the camera preview window (floating PIP with live feed).
// Returns 0 on success, -1 on failure.
int camera_preview_start(void);

// Stop the camera preview window and clean up.
void camera_preview_stop(void);

// Returns 1 if the camera preview is currently running.
int camera_preview_active(void);

// Freeze the live feed and capture the current frame to a JPEG file.
// Returns 0 on success, -1 on failure.
int camera_preview_capture(const char* output_path);

// Capture the current frame to a JPEG file WITHOUT freezing the live feed.
// The camera keeps streaming. Ideal for auto-analysis loops.
// Returns 0 on success, -1 on failure.
int camera_preview_snap(const char* output_path);

// Freeze the live feed (without capturing). Shows "FROZEN" badge.
// Returns 0 on success, -1 on failure.
int camera_preview_freeze(void);

// Resume the live camera feed after a freeze.
// Returns 0 on success, -1 on failure.
int camera_preview_unfreeze(void);

#ifdef __cplusplus
}
#endif
