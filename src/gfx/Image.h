#pragma once

#include <memory>
#include <string_view>

#include "gfx/Bitmap.h"
#include "gfx/Renderer2D.h"
#include "gfx/Texture.h"

namespace rr {

// A 2D image as the game uses it (Gods98 Image): a bitmap shown with optional colour-key
// transparency. The original converted images to a 16-bit display surface and keyed on that, so
// key comparisons here are done at 5-6-5 precision to reproduce which pixels disappear.
class Image {
public:
    static std::unique_ptr<Image> Load(std::string_view gamePath);
    static std::unique_ptr<Image> FromBitmap(Bitmap bmp);

    // Palette entry 0 becomes transparent (Image_SetPenZeroTrans).
    void SetPenZeroTrans();
    // Every pixel whose colour lies in [low, high] per channel becomes transparent (Image_SetupTrans).
    void SetupTrans(Rgb8 low, Rgb8 high);
    void ClearTrans();

    // Draws at logical coordinates; src/dstSize optional like Image_DisplayScaled.
    void Draw(Renderer2D& r, float x, float y, const RectF* src = nullptr, const float* dstW = nullptr,
              const float* dstH = nullptr, ColorF tint = {}) const;
    void DrawStretched(Renderer2D& r, const RectF& dst, const RectF* src = nullptr, ColorF tint = {}) const;

    int width() const { return bmp_.width; }
    int height() const { return bmp_.height; }
    const Bitmap& bitmap() const { return bmp_; }
    Rgb8 penZero() const { return penZero_; }
    Rgb8 pen255() const { return pen255_; }
    const Texture& texture() const;

private:
    void Rebuild() const;

    Bitmap bmp_;
    Rgb8 penZero_{}, pen255_{};
    bool keyed_ = false;
    Rgb8 keyLow_{}, keyHigh_{};
    mutable Texture texture_;
    mutable bool dirty_ = true;
};

}  // namespace rr
