#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Capture a single frame from the default camera and save as JPEG.
// output_path: where to save the JPEG (e.g. "/tmp/rcli_camera.jpg").
// Returns 0 on success, -1 on failure.
int camera_capture_photo(const char* output_path);

#ifdef __cplusplus
}
#endif
