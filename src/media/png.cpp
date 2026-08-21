#include "media/png.h"

#include <cstring>
#include <fstream>
#include <vector>

#include <zlib.h>

namespace rcli::media {
namespace {

std::uint32_t Be32(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) | p[3];
}

int Paeth(int a, int b, int c) {
    const int p = a + b - c;
    const int pa = p > a ? p - a : a - p;
    const int pb = p > b ? p - b : b - p;
    const int pc = p > c ? p - c : c - p;
    if (pa <= pb && pa <= pc) {
        return a;
    }
    return pb <= pc ? b : c;
}

/// Enough PNG to show what a diffusion model produced: 8-bit RGB or RGBA, no
/// interlacing, which is what every engine here writes. Anything else is
/// reported rather than guessed at.
Bitmap ReadPngImpl(const std::string& path) {
    Bitmap out;
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        out.failure = "cannot read " + path;
        return out;
    }
    const std::string bytes((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
    const auto* data = reinterpret_cast<const std::uint8_t*>(bytes.data());
    if (bytes.size() < 8 || std::memcmp(data, "\x89PNG\r\n\x1a\n", 8) != 0) {
        out.failure = "not a PNG";
        return out;
    }

    std::string deflated;
    int channels = 0;
    std::size_t offset = 8;
    while (offset + 8 <= bytes.size()) {
        const std::uint32_t length = Be32(data + offset);
        const char* type = bytes.data() + offset + 4;
        const std::uint8_t* body = data + offset + 8;
        if (offset + 12 + length > bytes.size()) {
            break;
        }
        if (std::memcmp(type, "IHDR", 4) == 0 && length >= 13) {
            out.width = static_cast<int>(Be32(body));
            out.height = static_cast<int>(Be32(body + 4));
            const int depth = body[8];
            const int colour = body[9];
            if (depth != 8 || (colour != 2 && colour != 6) || body[12] != 0) {
                out.failure = "unsupported PNG (needs 8-bit RGB or RGBA, no interlacing)";
                return out;
            }
            channels = colour == 6 ? 4 : 3;
        } else if (std::memcmp(type, "IDAT", 4) == 0) {
            deflated.append(reinterpret_cast<const char*>(body), length);
        } else if (std::memcmp(type, "IEND", 4) == 0) {
            break;
        }
        offset += 12 + length;
    }
    if (out.width <= 0 || out.height <= 0 || channels == 0 || deflated.empty()) {
        out.failure = "the PNG has no image data";
        return out;
    }

    const std::size_t stride = static_cast<std::size_t>(out.width) * channels;
    std::vector<std::uint8_t> raw((stride + 1) * out.height);
    uLongf size = static_cast<uLongf>(raw.size());
    if (uncompress(raw.data(), &size, reinterpret_cast<const Bytef*>(deflated.data()),
                   static_cast<uLong>(deflated.size())) != Z_OK) {
        out.failure = "the PNG data would not decompress";
        return out;
    }

    // Undo the per-scanline filters in place, then drop alpha.
    std::vector<std::uint8_t> flat(stride * out.height);
    for (int y = 0; y < out.height; ++y) {
        const std::uint8_t filter = raw[(stride + 1) * y];
        const std::uint8_t* src = raw.data() + (stride + 1) * y + 1;
        std::uint8_t* line = flat.data() + stride * y;
        const std::uint8_t* prior = y > 0 ? flat.data() + stride * (y - 1) : nullptr;
        for (std::size_t x = 0; x < stride; ++x) {
            const int a = x >= static_cast<std::size_t>(channels) ? line[x - channels] : 0;
            const int b = prior != nullptr ? prior[x] : 0;
            const int c = (prior != nullptr && x >= static_cast<std::size_t>(channels))
                              ? prior[x - channels]
                              : 0;
            int value = src[x];
            switch (filter) {
                case 1: value += a; break;
                case 2: value += b; break;
                case 3: value += (a + b) / 2; break;
                case 4: value += Paeth(a, b, c); break;
                default: break;
            }
            line[x] = static_cast<std::uint8_t>(value);
        }
    }

    out.rgb.resize(static_cast<std::size_t>(out.width) * out.height * 3);
    for (std::size_t i = 0, n = static_cast<std::size_t>(out.width) * out.height; i < n; ++i) {
        out.rgb[i * 3 + 0] = flat[i * channels + 0];
        out.rgb[i * 3 + 1] = flat[i * channels + 1];
        out.rgb[i * 3 + 2] = flat[i * channels + 2];
    }
    return out;
}


}  // namespace

Bitmap ReadPng(const std::string& path) {
    return ReadPngImpl(path);
}

bool WritePng(const std::string& path, const std::uint8_t* rgb, int width, int height,
              std::string* error) {
    // One IDAT holding every scanline with filter 0. A diffusion output is a
    // photograph, not a screenshot: per-line filter heuristics would buy a few
    // percent and cost the only reason this is short enough to read.
    std::string raw;
    raw.reserve(static_cast<std::size_t>(height) * (width * 3 + 1));
    for (int y = 0; y < height; ++y) {
        raw.push_back('\0');
        raw.append(reinterpret_cast<const char*>(rgb) + static_cast<std::size_t>(y) * width * 3,
                   static_cast<std::size_t>(width) * 3);
    }
    uLongf bound = compressBound(static_cast<uLong>(raw.size()));
    std::vector<std::uint8_t> deflated(bound);
    if (compress(deflated.data(), &bound, reinterpret_cast<const Bytef*>(raw.data()),
                 static_cast<uLong>(raw.size())) != Z_OK) {
        if (error != nullptr) {
            *error = "could not compress the image";
        }
        return false;
    }
    deflated.resize(bound);

    std::ofstream file(path, std::ios::binary);
    if (!file) {
        if (error != nullptr) {
            *error = "could not open " + path;
        }
        return false;
    }
    auto chunk = [&file](const char* type, const std::uint8_t* body, std::size_t size) {
        const std::uint8_t length[4] = {
            static_cast<std::uint8_t>(size >> 24), static_cast<std::uint8_t>(size >> 16),
            static_cast<std::uint8_t>(size >> 8), static_cast<std::uint8_t>(size)};
        file.write(reinterpret_cast<const char*>(length), 4);
        file.write(type, 4);
        if (size > 0) {
            file.write(reinterpret_cast<const char*>(body), static_cast<std::streamsize>(size));
        }
        uLong crc = crc32(0, reinterpret_cast<const Bytef*>(type), 4);
        if (size > 0) {
            crc = crc32(crc, body, static_cast<uInt>(size));
        }
        const std::uint8_t tail[4] = {
            static_cast<std::uint8_t>(crc >> 24), static_cast<std::uint8_t>(crc >> 16),
            static_cast<std::uint8_t>(crc >> 8), static_cast<std::uint8_t>(crc)};
        file.write(reinterpret_cast<const char*>(tail), 4);
    };

    file.write("\x89PNG\r\n\x1a\n", 8);
    std::uint8_t ihdr[13] = {};
    const std::uint32_t w = static_cast<std::uint32_t>(width);
    const std::uint32_t h = static_cast<std::uint32_t>(height);
    ihdr[0] = static_cast<std::uint8_t>(w >> 24); ihdr[1] = static_cast<std::uint8_t>(w >> 16);
    ihdr[2] = static_cast<std::uint8_t>(w >> 8);  ihdr[3] = static_cast<std::uint8_t>(w);
    ihdr[4] = static_cast<std::uint8_t>(h >> 24); ihdr[5] = static_cast<std::uint8_t>(h >> 16);
    ihdr[6] = static_cast<std::uint8_t>(h >> 8);  ihdr[7] = static_cast<std::uint8_t>(h);
    ihdr[8] = 8;   // bit depth
    ihdr[9] = 2;   // truecolour
    chunk("IHDR", ihdr, sizeof(ihdr));
    chunk("IDAT", deflated.data(), deflated.size());
    chunk("IEND", nullptr, 0);
    return static_cast<bool>(file);
}

}  // namespace rcli::media
