#include "gfx/PngWriter.h"

#include <algorithm>
#include <cstdio>
#include <vector>

#include "gfx/GL.h"

namespace rr {

namespace {
uint32_t Crc32(const uint8_t* data, size_t len, uint32_t crc = 0) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t n = 0; n < 256; ++n) {
            uint32_t c = n;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[n] = c;
        }
        init = true;
    }
    crc = ~crc;
    for (size_t i = 0; i < len; ++i) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

void PutU32BE(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(uint8_t(v >> 24));
    out.push_back(uint8_t(v >> 16));
    out.push_back(uint8_t(v >> 8));
    out.push_back(uint8_t(v));
}

void Chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& payload) {
    PutU32BE(out, uint32_t(payload.size()));
    const size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), payload.begin(), payload.end());
    PutU32BE(out, Crc32(out.data() + start, out.size() - start));
}
}  // namespace

bool WritePng(const std::string& path, int width, int height, const uint8_t* rgba) {
    std::vector<uint8_t> raw;
    raw.reserve(size_t(height) * (size_t(width) * 4 + 1));
    for (int y = 0; y < height; ++y) {
        raw.push_back(0);  // filter: none
        raw.insert(raw.end(), rgba + size_t(y) * width * 4, rgba + size_t(y + 1) * width * 4);
    }

    // zlib stream with stored deflate blocks (max 65535 bytes each).
    std::vector<uint8_t> z = {0x78, 0x01};
    uint32_t a = 1, b = 0;
    for (uint8_t v : raw) {
        a = (a + v) % 65521;
        b = (b + a) % 65521;
    }
    size_t pos = 0;
    do {
        const size_t len = std::min<size_t>(65535, raw.size() - pos);
        const bool last = pos + len == raw.size();
        z.push_back(last ? 1 : 0);
        z.push_back(uint8_t(len));
        z.push_back(uint8_t(len >> 8));
        z.push_back(uint8_t(~len));
        z.push_back(uint8_t(~len >> 8));
        z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + len);
        pos += len;
    } while (pos < raw.size());
    PutU32BE(z, (b << 16) | a);

    std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    std::vector<uint8_t> ihdr;
    PutU32BE(ihdr, uint32_t(width));
    PutU32BE(ihdr, uint32_t(height));
    ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});  // 8-bit RGBA
    Chunk(png, "IHDR", ihdr);
    Chunk(png, "IDAT", z);
    Chunk(png, "IEND", {});

    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(png.data(), 1, png.size(), f) == png.size();
    std::fclose(f);
    return ok;
}

bool SaveFramebufferPng(const std::string& path, int width, int height) {
    std::vector<uint8_t> pixels(size_t(width) * height * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    std::vector<uint8_t> flipped(pixels.size());
    for (int y = 0; y < height; ++y) {
        std::copy_n(&pixels[size_t(height - 1 - y) * width * 4], size_t(width) * 4, &flipped[size_t(y) * width * 4]);
    }
    for (size_t i = 3; i < flipped.size(); i += 4) flipped[i] = 255;
    return WritePng(path, width, height, flipped.data());
}

}  // namespace rr
