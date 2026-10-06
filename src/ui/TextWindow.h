#pragma once

#include <string>
#include <string_view>

#include "gfx/Font.h"
#include "gfx/Renderer2D.h"

namespace rr {

// A rectangular text area (Gods98 TextWindow): text is word-wrapped greedily at whitespace to the
// window width, '\n' forces a line break and '\a' separates pages. Lines are centred by default.
// When there is more text than fits, the newest lines are shown.
class TextWindow {
public:
    TextWindow() = default;
    TextWindow(const Font* font, const RectF& area) : font_(font), area_(area) {}

    void SetFont(const Font* font) { font_ = font; }
    void SetArea(const RectF& area) { area_ = area; }
    const RectF& area() const { return area_; }
    void EnableCentering(bool on) { centred_ = on; }

    void Clear() { text_.clear(); }
    void Print(std::string_view text) { text_ += text; }
    // Draws the text; scrollFromEnd shifts the visible window back by that many lines.
    // Returns the y coordinate just below the last drawn line.
    float Draw(Renderer2D& r, int scrollFromEnd = 0, ColorF tint = {}) const;
    bool empty() const { return text_.empty(); }

private:
    const Font* font_ = nullptr;
    RectF area_{};
    bool centred_ = true;
    std::string text_;
};

}  // namespace rr
