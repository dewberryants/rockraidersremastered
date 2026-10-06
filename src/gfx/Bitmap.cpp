#include "gfx/Bitmap.h"

#include <cstdlib>

#include "core/Log.h"
#include "fs/FileSystem.h"

namespace rr {

namespace {
uint16_t U16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
uint32_t U32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
int32_t S32(const uint8_t* p) { return static_cast<int32_t>(U32(p)); }

// Expands a 5/6-bit channel to 8 bits.
uint8_t Expand(uint32_t v, int bits) { return static_cast<uint8_t>((v << (8 - bits)) | (v >> (2 * bits - 8))); }
}  // namespace

std::optional<Bitmap> DecodeBmp(const std::vector<uint8_t>& d) {
    if (d.size() < 54 || d[0] != 'B' || d[1] != 'M') return std::nullopt;
    const uint32_t pixelOffset = U32(&d[10]);
    const uint32_t infoSize = U32(&d[14]);
    if (infoSize < 40 || 14 + infoSize > d.size()) return std::nullopt;
    const int32_t w = S32(&d[18]);
    const int32_t hRaw = S32(&d[22]);
    const uint16_t bpp = U16(&d[28]);
    const uint32_t compression = U32(&d[30]);
    uint32_t colorsUsed = U32(&d[46]);
    if (w <= 0 || hRaw == 0 || w > 16384 || std::abs(hRaw) > 16384) return std::nullopt;
    if (compression != 0 && compression != 3) {
        RR_WARN("BMP compression %u not supported", compression);
        return std::nullopt;
    }

    Bitmap bmp;
    bmp.width = w;
    bmp.height = std::abs(hRaw);
    bmp.bitsPerPixel = bpp;
    const bool bottomUp = hRaw > 0;
    const size_t stride = ((size_t(w) * bpp + 31) / 32) * 4;
    if (pixelOffset + stride * bmp.height > d.size()) return std::nullopt;

    if (bpp <= 8) {
        if (colorsUsed == 0) colorsUsed = 1u << bpp;
        const size_t palOffset = 14 + infoSize;
        if (palOffset + colorsUsed * 4 > d.size()) return std::nullopt;
        bmp.palette.resize(256);
        for (uint32_t i = 0; i < colorsUsed && i < 256; ++i) {
            const uint8_t* e = &d[palOffset + i * 4];
            bmp.palette[i] = {e[2], e[1], e[0]};
        }
        bmp.indices.resize(size_t(w) * bmp.height);
    }
    bmp.rgba.resize(size_t(w) * bmp.height * 4);

    // 16-bit images are X1R5G5B5 unless BI_BITFIELDS says the green mask is 6 bits wide.
    const bool rgb565 = bpp == 16 && compression == 3 && 14 + infoSize + 12 <= d.size() && U32(&d[58]) == 0x07E0;

    for (int y = 0; y < bmp.height; ++y) {
        const int srcRow = bottomUp ? bmp.height - 1 - y : y;
        const uint8_t* row = &d[pixelOffset + stride * srcRow];
        for (int x = 0; x < w; ++x) {
            uint8_t* out = &bmp.rgba[(size_t(y) * w + x) * 4];
            out[3] = 255;
            if (bpp <= 8) {
                const int bitPos = x * bpp;
                const uint8_t byte = row[bitPos / 8];
                const int shift = 8 - bpp - (bitPos % 8);
                const uint8_t idx = static_cast<uint8_t>((byte >> shift) & ((1 << bpp) - 1));
                bmp.indices[size_t(y) * w + x] = idx;
                out[0] = bmp.palette[idx].r;
                out[1] = bmp.palette[idx].g;
                out[2] = bmp.palette[idx].b;
            } else if (bpp == 16) {
                const uint32_t v = U16(row + x * 2);
                if (rgb565) {
                    out[0] = Expand((v >> 11) & 31, 5);
                    out[1] = Expand((v >> 5) & 63, 6);
                    out[2] = Expand(v & 31, 5);
                } else {
                    out[0] = Expand((v >> 10) & 31, 5);
                    out[1] = Expand((v >> 5) & 31, 5);
                    out[2] = Expand(v & 31, 5);
                }
            } else if (bpp == 24 || bpp == 32) {
                const uint8_t* p = row + x * (bpp / 8);
                out[0] = p[2];
                out[1] = p[1];
                out[2] = p[0];
            } else {
                return std::nullopt;
            }
        }
    }
    return bmp;
}

std::optional<Bitmap> LoadBmp(std::string_view gamePath) {
    auto data = Fs().ReadAll(gamePath);
    if (!data) {
        RR_WARN("Image %.*s not found", int(gamePath.size()), gamePath.data());
        return std::nullopt;
    }
    auto bmp = DecodeBmp(*data);
    if (!bmp) RR_WARN("Image %.*s could not be decoded", int(gamePath.size()), gamePath.data());
    return bmp;
}

}  // namespace rr
