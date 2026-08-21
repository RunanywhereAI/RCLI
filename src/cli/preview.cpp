#include "cli/preview.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "cli/output.h"
#include "media/png.h"

namespace rcli::out {
namespace {

bool Truecolor() {
    const char* colorterm = std::getenv("COLORTERM");
    return colorterm != nullptr && (std::strstr(colorterm, "truecolor") != nullptr ||
                                    std::strstr(colorterm, "24bit") != nullptr);
}

}  // namespace

std::string Preview(const std::string& path, int columns) {
    if (!Colored() || !Truecolor()) {
        return {};
    }
    const media::Bitmap bitmap = media::ReadPng(path);
    if (!bitmap.failure.empty()) {
        return {};
    }
    const int width = columns > 0 && columns < bitmap.width ? columns : bitmap.width;
    const int rows = bitmap.height * width / bitmap.width / 2;
    if (rows <= 0) {
        return {};
    }

    std::string canvas;
    canvas.reserve(static_cast<std::size_t>(rows) * width * 40);
    char cell[48];
    for (int row = 0; row < rows; ++row) {
        for (int x = 0; x < width; ++x) {
            const int sx = x * bitmap.width / width;
            auto at = [&](int pixel_y) {
                const int sy = pixel_y * bitmap.height / (rows * 2);
                return &bitmap.rgb[(static_cast<std::size_t>(sy) * bitmap.width + sx) * 3];
            };
            const std::uint8_t* top = at(row * 2);
            const std::uint8_t* bottom = at(row * 2 + 1);
            std::snprintf(cell, sizeof(cell), "\033[38;2;%d;%d;%d;48;2;%d;%d;%dm▀", top[0],
                          top[1], top[2], bottom[0], bottom[1], bottom[2]);
            canvas += cell;
        }
        canvas += "\033[0m\n";
    }
    return canvas;
}

}  // namespace rcli::out
