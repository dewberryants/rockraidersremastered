#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace rr {

struct Rgb8 {
    uint8_t r = 0, g = 0, b = 0;
};

// A decoded Windows BMP. Paletted images keep their indices and palette (the game keys
// transparency and font glyph boundaries on palette entries); every image also has RGBA8 pixels,
// stored top-down.
struct Bitmap {
    int width = 0;
    int height = 0;
    int bitsPerPixel = 0;
    std::vector<uint8_t> rgba;     // width*height*4, top-down
    std::vector<uint8_t> indices;  // width*height, top-down, only for <= 8 bpp
    std::vector<Rgb8> palette;     // only for <= 8 bpp

    bool paletted() const { return !indices.empty(); }
    Rgb8 PixelRgb(int x, int y) const {
        const uint8_t* p = &rgba[(size_t(y) * width + x) * 4];
        return {p[0], p[1], p[2]};
    }
};

std::optional<Bitmap> DecodeBmp(const std::vector<uint8_t>& data);
std::optional<Bitmap> LoadBmp(std::string_view gamePath);

}  // namespace rr
