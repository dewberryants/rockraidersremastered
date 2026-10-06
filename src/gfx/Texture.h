#pragma once

#include <cstdint>
#include <optional>

#include "gfx/Bitmap.h"

namespace rr {

struct ColorKey {
    uint8_t r = 0, g = 0, b = 0;
};

// A GL texture created from a Bitmap. When a colour key is given, matching pixels become fully
// transparent (the original used DirectDraw source colour keys for the same purpose).
class Texture {
public:
    Texture() = default;
    ~Texture();
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
    Texture(Texture&& other) noexcept;
    Texture& operator=(Texture&& other) noexcept;

    bool Create(const Bitmap& bmp, std::optional<ColorKey> key = std::nullopt, bool linearFilter = false);
    bool CreateRgba(int width, int height, const uint8_t* rgba, bool linearFilter = false);
    void Update(const uint8_t* rgba);  // same size as created
    void Release();

    unsigned id() const { return id_; }
    int width() const { return width_; }
    int height() const { return height_; }
    bool valid() const { return id_ != 0; }

private:
    unsigned id_ = 0;
    int width_ = 0;
    int height_ = 0;
};

}  // namespace rr
