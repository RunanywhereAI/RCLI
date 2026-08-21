#ifndef RCLI_MEDIA_PNG_H
#define RCLI_MEDIA_PNG_H

#include <cstdint>
#include <string>
#include <vector>

namespace rcli::media {

struct Bitmap {
    int width = 0;
    int height = 0;
    /// Three bytes per pixel.
    std::vector<std::uint8_t> rgb;
    /// Empty when the read succeeded.
    std::string failure;
};

/// Reads 8-bit RGB or RGBA, non-interlaced — what the engines here produce.
/// Anything else is reported rather than guessed at.
Bitmap ReadPng(const std::string& path);

/// Writes 8-bit RGB. Needed because diffusion hands back raw pixels: the
/// media_type on the result says PNG, the bytes are width*height*4 of RGBA,
/// and writing those to a .png file produces something no viewer will open.
bool WritePng(const std::string& path, const std::uint8_t* rgb, int width, int height,
              std::string* error);

}  // namespace rcli::media

#endif  // RCLI_MEDIA_PNG_H
