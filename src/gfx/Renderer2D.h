#pragma once

#include <cstdint>
#include <vector>

#include "gfx/Texture.h"

namespace rr {

struct RectF {
    float x = 0, y = 0, w = 0, h = 0;
    bool Contains(float px, float py) const { return px >= x && py >= y && px < x + w && py < y + h; }
};

struct ColorF {
    float r = 1, g = 1, b = 1, a = 1;
};

// Horizontal placement of a UI element designed for the original 640-wide screen.
enum class Anchor { Left, Center, Right };

// Batched 2D drawing in the game's logical coordinate space (origin top-left, 480 units high).
//
// Classic layout (default): the logical screen is exactly 640x480, letterboxed at 4:3 like the
// original. Widescreen layout (remaster option): the logical width grows with the window's aspect
// ratio; UI code shifts elements with SetAnchor so left/right-docked panels follow the screen edges.
class Renderer2D {
public:
    bool Init();
    void Shutdown();

    void SetLayout(bool widescreen, bool smoothScaling) {
        widescreen_ = widescreen;
        smooth_ = smoothScaling;
    }
    // Sets up the viewport for a frame; call before any drawing.
    void BeginFrame(int drawableWidth, int drawableHeight);
    void Flush();

    // Logical canvas size (640x480 in classic layout).
    float uiWidth() const { return logicalW_; }
    float uiHeight() const { return 480.0f; }
    // Offset applied to subsequent draws so 640-based coordinates land at the given anchor.
    void SetAnchor(Anchor a);
    float AnchorOffset(Anchor a) const;
    void SetOffset(float dx, float dy) {
        offsetX_ = dx;
        offsetY_ = dy;
    }

    void DrawTexture(const Texture& tex, const RectF& dst, const RectF& src, ColorF tint = {});
    void DrawTexture(const Texture& tex, float x, float y, ColorF tint = {});
    void FillRect(const RectF& dst, ColorF color);
    void DrawRectOutline(const RectF& r, ColorF color, float thickness = 1.0f);
    void DrawLine(float x0, float y0, float x1, float y1, ColorF color);

    // Clip rectangle in logical coordinates (unaffected by the anchor offset); w<=0 disables.
    void SetClip(const RectF& clip);
    void ClearClip() { SetClip({0, 0, 0, 0}); }

    // Window pixel -> logical coordinate (for mouse input).
    void WindowToLogical(int wx, int wy, float& lx, float& ly) const;
    // Pixel rectangle of the logical screen inside the window (for the 3D viewport).
    void ViewportPixels(int& x, int& y, int& w, int& h) const {
        x = viewX_;
        y = viewY_;
        w = viewW_;
        h = viewH_;
    }

private:
    struct Vertex {
        float x, y, u, v, r, g, b, a;
    };
    void Push(unsigned texture, const RectF& dst, float u0, float v0, float u1, float v1, ColorF c);

    unsigned program_ = 0, vao_ = 0, vbo_ = 0;
    int locScreen_ = -1, locTex_ = -1;
    unsigned whiteTex_ = 0;
    unsigned currentTex_ = 0;
    std::vector<Vertex> verts_;
    int viewX_ = 0, viewY_ = 0, viewW_ = 640, viewH_ = 480;
    int drawW_ = 640, drawH_ = 480;
    float logicalW_ = 640.0f;
    float offsetX_ = 0, offsetY_ = 0;
    bool widescreen_ = false;
    bool smooth_ = false;
    RectF clip_{};
};

}  // namespace rr
