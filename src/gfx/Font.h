#pragma once

#include <array>
#include <memory>
#include <string>
#include <string_view>

#include "gfx/Image.h"

namespace rr {

// Bitmap font in the game's format (Gods98 Font):
//  - a BMP divided into a 10 x 19 grid of equal cells holding characters 32..221 in reading order;
//  - palette colour 0 is transparent;
//  - in each cell's top pixel row, trailing pixels of palette colour 255 mark unused width, which
//    gives the proportional glyph width;
//  - line height is the cell height, tab stops are 8 space-widths apart.
class Font {
public:
    static constexpr int kColumns = 10;
    static constexpr int kRows = 19;
    static constexpr int kFirstChar = 32;
    static constexpr int kGlyphCount = kColumns * kRows;

    static std::unique_ptr<Font> Load(std::string_view gamePath);

    // Draws (when render is true) and returns the widest line in pixels; lines counts '\n'-separated lines.
    int Print(Renderer2D* r, float x, float y, std::string_view text, int* lines = nullptr, ColorF tint = {}) const;
    int Draw(Renderer2D& r, float x, float y, std::string_view text, ColorF tint = {}) const {
        return Print(&r, x, y, text, nullptr, tint);
    }
    int StringWidth(std::string_view text) const { return Print(nullptr, 0, 0, text); }
    int CharWidth(unsigned char c) const;
    int height() const { return lineHeight_; }

private:
    struct Glyph {
        float x, y, w, h;
    };
    std::unique_ptr<Image> image_;
    std::array<Glyph, kGlyphCount> glyphs_{};
    int lineHeight_ = 0;
    int tabWidth_ = 0;
};

}  // namespace rr
