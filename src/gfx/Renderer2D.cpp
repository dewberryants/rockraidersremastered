#include "gfx/Renderer2D.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "gfx/GL.h"
#include "gfx/Shader.h"

namespace rr {

namespace {
const char* kVertex = R"(#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUv;
layout(location = 2) in vec4 aColor;
uniform vec2 uScreen;
out vec2 vUv;
out vec4 vColor;
void main() {
    vUv = aUv;
    vColor = aColor;
    gl_Position = vec4(aPos.x / uScreen.x * 2.0 - 1.0, 1.0 - aPos.y / uScreen.y * 2.0, 0.0, 1.0);
}
)";

const char* kFragment = R"(#version 330 core
in vec2 vUv;
in vec4 vColor;
uniform sampler2D uTex;
out vec4 oColor;
void main() {
    vec4 t = texture(uTex, vUv);
    if (t.a < 0.004) discard;
    oColor = t * vColor;
}
)";
}  // namespace

bool Renderer2D::Init() {
    program_ = gl::CompileProgram(kVertex, kFragment, "Renderer2D");
    if (!program_) return false;
    locScreen_ = glGetUniformLocation(program_, "uScreen");
    locTex_ = glGetUniformLocation(program_, "uTex");

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, u)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, r)));
    glBindVertexArray(0);

    const uint8_t white[4] = {255, 255, 255, 255};
    glGenTextures(1, &whiteTex_);
    glBindTexture(GL_TEXTURE_2D, whiteTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    return true;
}

void Renderer2D::Shutdown() {
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (program_) glDeleteProgram(program_);
    if (whiteTex_) glDeleteTextures(1, &whiteTex_);
    vbo_ = vao_ = program_ = whiteTex_ = 0;
}

void Renderer2D::BeginFrame(int drawableWidth, int drawableHeight) {
    drawW_ = drawableWidth;
    drawH_ = drawableHeight;
    if (widescreen_) {
        // Fill the window; the logical height stays 480 and the width follows the aspect ratio.
        logicalW_ = std::max(640.0f, 480.0f * float(drawableWidth) / float(std::max(1, drawableHeight)));
        const float scale = std::min(drawableWidth / logicalW_, drawableHeight / 480.0f);
        viewW_ = int(std::lround(logicalW_ * scale));
        viewH_ = int(std::lround(480.0f * scale));
    } else {
        logicalW_ = 640.0f;
        const float scale = std::min(drawableWidth / 640.0f, drawableHeight / 480.0f);
        viewW_ = int(std::lround(640.0f * scale));
        viewH_ = int(std::lround(480.0f * scale));
    }
    viewX_ = (drawableWidth - viewW_) / 2;
    viewY_ = (drawableHeight - viewH_) / 2;
    glViewport(viewX_, viewY_, viewW_, viewH_);
    verts_.clear();
    currentTex_ = 0;
    offsetX_ = offsetY_ = 0;
    clip_ = {};
    glDisable(GL_SCISSOR_TEST);
}

float Renderer2D::AnchorOffset(Anchor a) const {
    switch (a) {
        case Anchor::Left: return 0.0f;
        case Anchor::Center: return (logicalW_ - 640.0f) * 0.5f;
        case Anchor::Right: return logicalW_ - 640.0f;
    }
    return 0.0f;
}

void Renderer2D::SetAnchor(Anchor a) {
    offsetX_ = AnchorOffset(a);
    offsetY_ = 0;
}

void Renderer2D::SetClip(const RectF& clip) {
    Flush();
    clip_ = clip;
    if (clip.w <= 0 || clip.h <= 0) {
        glDisable(GL_SCISSOR_TEST);
        return;
    }
    const float sx = viewW_ / logicalW_, sy = viewH_ / 480.0f;
    glEnable(GL_SCISSOR_TEST);
    glScissor(viewX_ + int(clip.x * sx), viewY_ + int((480.0f - clip.y - clip.h) * sy), int(std::ceil(clip.w * sx)),
              int(std::ceil(clip.h * sy)));
}

void Renderer2D::Flush() {
    if (verts_.empty()) return;
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(program_);
    glUniform2f(locScreen_, logicalW_, 480.0f);
    glUniform1i(locTex_, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, currentTex_);
    const GLint filter = smooth_ ? GL_LINEAR : GL_NEAREST;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(verts_.size() * sizeof(Vertex)), verts_.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, GLsizei(verts_.size()));
    glBindVertexArray(0);
    verts_.clear();
}

void Renderer2D::Push(unsigned texture, const RectF& dst, float u0, float v0, float u1, float v1, ColorF c) {
    if (texture != currentTex_) {
        Flush();
        currentTex_ = texture;
    }
    const RectF d{dst.x + offsetX_, dst.y + offsetY_, dst.w, dst.h};
    const Vertex a{d.x, d.y, u0, v0, c.r, c.g, c.b, c.a};
    const Vertex b{d.x + d.w, d.y, u1, v0, c.r, c.g, c.b, c.a};
    const Vertex cc{d.x + d.w, d.y + d.h, u1, v1, c.r, c.g, c.b, c.a};
    const Vertex e{d.x, d.y + d.h, u0, v1, c.r, c.g, c.b, c.a};
    verts_.insert(verts_.end(), {a, b, cc, a, cc, e});
}

void Renderer2D::DrawTexture(const Texture& tex, const RectF& dst, const RectF& src, ColorF tint) {
    if (!tex.valid()) return;
    const float iw = 1.0f / tex.width(), ih = 1.0f / tex.height();
    Push(tex.id(), dst, src.x * iw, src.y * ih, (src.x + src.w) * iw, (src.y + src.h) * ih, tint);
}

void Renderer2D::DrawTexture(const Texture& tex, float x, float y, ColorF tint) {
    DrawTexture(tex, {x, y, float(tex.width()), float(tex.height())}, {0, 0, float(tex.width()), float(tex.height())},
                tint);
}

void Renderer2D::FillRect(const RectF& dst, ColorF color) { Push(whiteTex_, dst, 0, 0, 1, 1, color); }

void Renderer2D::DrawRectOutline(const RectF& r, ColorF color, float t) {
    FillRect({r.x, r.y, r.w, t}, color);
    FillRect({r.x, r.y + r.h - t, r.w, t}, color);
    FillRect({r.x, r.y, t, r.h}, color);
    FillRect({r.x + r.w - t, r.y, t, r.h}, color);
}

void Renderer2D::DrawLine(float x0, float y0, float x1, float y1, ColorF color) {
    // Lines are rare in the UI (selection boxes, radar); draw them as 1-unit dot runs.
    const float dx = x1 - x0, dy = y1 - y0;
    const int steps = int(std::max(std::fabs(dx), std::fabs(dy)));
    if (steps == 0) {
        FillRect({x0, y0, 1, 1}, color);
        return;
    }
    if (dy == 0) {
        FillRect({std::min(x0, x1), y0, std::fabs(dx) + 1, 1}, color);
        return;
    }
    if (dx == 0) {
        FillRect({x0, std::min(y0, y1), 1, std::fabs(dy) + 1}, color);
        return;
    }
    for (int i = 0; i <= steps; ++i) FillRect({x0 + dx * i / steps, y0 + dy * i / steps, 1, 1}, color);
}

void Renderer2D::WindowToLogical(int wx, int wy, float& lx, float& ly) const {
    lx = (wx - viewX_) * logicalW_ / float(viewW_);
    ly = (wy - viewY_) * 480.0f / float(viewH_);
}

}  // namespace rr
