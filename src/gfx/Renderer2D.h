#pragma once

#include <cstdint>
#include <vector>

#include "gfx/Texture.h"

namespace rr {

struct RectF {
    float x = 0, y = 0, w = 0, h = 0;
};

struct ColorF {
    float r = 1, g = 1, b = 1, a = 1;
};

// Batched 2D drawing in the game's logical 640x480 coordinate space (origin top-left).
// The logical screen is letterboxed into the window at a 4:3 aspect ratio.
class Renderer2D {
public:
    bool Init();
    void Shutdown();

    // Sets up the viewport for a frame; call before any drawing.
    void BeginFrame(int drawableWidth, int drawableHeight);
    void Flush();

    void DrawTexture(const Texture& tex, const RectF& dst, const RectF& src, ColorF tint = {});
    void DrawTexture(const Texture& tex, float x, float y, ColorF tint = {});
    void FillRect(const RectF& dst, ColorF color);

    // Window pixel -> logical coordinate (for mouse input).
    void WindowToLogical(int wx, int wy, float& lx, float& ly) const;

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
};

}  // namespace rr
