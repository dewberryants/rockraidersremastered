#include "gfx/Font.h"

#include <algorithm>

#include "core/Log.h"

namespace rr {

std::unique_ptr<Font> Font::Load(std::string_view gamePath) {
    auto image = Image::Load(gamePath);
    if (!image) return nullptr;
    const Bitmap& bmp = image->bitmap();
    if (!bmp.paletted()) {
        RR_WARN("Font %.*s is not a paletted bitmap", int(gamePath.size()), gamePath.data());
        return nullptr;
    }
    image->SetPenZeroTrans();

    auto font = std::make_unique<Font>();
    const int cellW = bmp.width / kColumns;
    const int cellH = bmp.height / kRows;
    // Width markers are compared as display colours, like the original (palette 255's colour).
    const Rgb8 marker = bmp.palette[255];
    for (int row = 0; row < kRows; ++row) {
        for (int col = 0; col < kColumns; ++col) {
            const int cx = col * cellW, cy = row * cellH;
            int w = cellW;
            for (int off = cellW - 1; off > 0; --off) {
                const Rgb8 p = bmp.PixelRgb(cx + off, cy);
                if (p.r != marker.r || p.g != marker.g || p.b != marker.b) break;
                --w;
            }
            font->glyphs_[row * kColumns + col] = {float(cx), float(cy), float(w), float(cellH)};
        }
    }
    font->lineHeight_ = cellH;
    font->tabWidth_ = int(font->glyphs_[0].w) * 8;
    font->image_ = std::move(image);
    return font;
}

int Font::CharWidth(unsigned char c) const {
    const int idx = int(c) - kFirstChar;
    if (idx < 0 || idx >= kGlyphCount) return 0;
    return int(glyphs_[idx].w);
}

int Font::Print(Renderer2D* r, float x, float y, std::string_view text, int* lines, ColorF tint) const {
    int lineWidth = 0, widest = 0, lineCount = 1;
    float penY = y;
    for (char ch : text) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (c == '\n') {
            widest = std::max(widest, lineWidth);
            penY += float(lineHeight_);
            lineWidth = 0;
            ++lineCount;
            continue;
        }
        if (c == '\t') {
            if (tabWidth_ > 0) lineWidth += tabWidth_ - lineWidth % tabWidth_;
            continue;
        }
        const int idx = int(c) - kFirstChar;
        if (idx < 0 || idx >= kGlyphCount) continue;
        const Glyph& g = glyphs_[idx];
        if (r) {
            const RectF src{g.x, g.y, g.w, g.h};
            r->DrawTexture(image_->texture(), {x + float(lineWidth), penY, g.w, g.h}, src, tint);
        }
        lineWidth += int(g.w);
    }
    if (lines) *lines = lineCount;
    return std::max(widest, lineWidth);
}

}  // namespace rr
