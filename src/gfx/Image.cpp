#include "gfx/Image.h"

#include "core/Log.h"

namespace rr {

namespace {
// Colour as it ends up on a 16-bit RGB565 surface.
inline uint16_t To565(uint8_t r, uint8_t g, uint8_t b) {
    return uint16_t(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}
inline bool InRange565(const uint8_t* p, Rgb8 lo, Rgb8 hi) {
    const uint16_t v = To565(p[0], p[1], p[2]);
    const uint16_t l = To565(lo.r, lo.g, lo.b), h = To565(hi.r, hi.g, hi.b);
    const int r = v >> 11, g = (v >> 5) & 63, b = v & 31;
    return r >= (l >> 11) && r <= (h >> 11) && g >= ((l >> 5) & 63) && g <= ((h >> 5) & 63) && b >= (l & 31) &&
           b <= (h & 31);
}
}  // namespace

std::unique_ptr<Image> Image::FromBitmap(Bitmap bmp) {
    auto img = std::make_unique<Image>();
    if (bmp.paletted()) {
        img->penZero_ = bmp.palette[0];
        img->pen255_ = bmp.palette[255];
    }
    img->bmp_ = std::move(bmp);
    return img;
}

std::unique_ptr<Image> Image::Load(std::string_view gamePath) {
    auto bmp = LoadBmp(gamePath);
    if (!bmp) return nullptr;
    return FromBitmap(std::move(*bmp));
}

void Image::SetPenZeroTrans() { SetupTrans(penZero_, penZero_); }

void Image::SetupTrans(Rgb8 low, Rgb8 high) {
    keyed_ = true;
    keyLow_ = low;
    keyHigh_ = high;
    dirty_ = true;
}

void Image::ClearTrans() {
    keyed_ = false;
    dirty_ = true;
}

void Image::Rebuild() const {
    if (!keyed_) {
        texture_.CreateRgba(bmp_.width, bmp_.height, bmp_.rgba.data());
    } else {
        std::vector<uint8_t> px = bmp_.rgba;
        for (size_t i = 0; i < px.size(); i += 4)
            if (InRange565(&px[i], keyLow_, keyHigh_)) px[i + 3] = 0;
        texture_.CreateRgba(bmp_.width, bmp_.height, px.data());
    }
    dirty_ = false;
}

const Texture& Image::texture() const {
    if (dirty_) Rebuild();
    return texture_;
}

void Image::Draw(Renderer2D& r, float x, float y, const RectF* src, const float* dstW, const float* dstH,
                 ColorF tint) const {
    const RectF s = src ? *src : RectF{0, 0, float(bmp_.width), float(bmp_.height)};
    const RectF d{x, y, dstW ? *dstW : s.w, dstH ? *dstH : s.h};
    r.DrawTexture(texture(), d, s, tint);
}

void Image::DrawStretched(Renderer2D& r, const RectF& dst, const RectF* src, ColorF tint) const {
    const RectF s = src ? *src : RectF{0, 0, float(bmp_.width), float(bmp_.height)};
    r.DrawTexture(texture(), dst, s, tint);
}

}  // namespace rr
