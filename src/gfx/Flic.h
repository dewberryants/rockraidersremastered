#pragma once

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

#include "gfx/Renderer2D.h"
#include "gfx/Texture.h"

namespace rr {

// Player for the game's FLIC animations (.flh: 16-bit "hi colour" FLIC, magic 0xAF43, RGB555 pixels;
// also classic 8-bit FLI/FLC). Frames advance at 25 per second of game time, like the original
// which stepped whenever floor(ms * 0.025) changed. Animations play once unless looping is asked for.
class Flic {
public:
    static std::unique_ptr<Flic> Load(std::string_view gamePath);

    int width() const { return width_; }
    int height() const { return height_; }
    int frameCount() const { return frameCount_; }
    int currentFrame() const { return frame_; }
    bool finished() const { return finished_; }

    // Advances by game time (elapsed in 1/25 s units). Returns false when a non-looping
    // animation has passed its last frame.
    bool Advance(float elapsed, bool loop = false);
    // Decodes the next frame immediately; returns false at the end (or wraps when looping).
    bool StepFrame(bool loop);
    void Restart();

    // Draws the current frame; when transparent, black (palette 0 / RGB 0) is see-through.
    void Draw(Renderer2D& r, float x, float y, bool transparent = false);
    void DrawStretched(Renderer2D& r, const RectF& dst, bool transparent = false);

private:
    bool DecodeFrame();
    void DoChunk(uint16_t type, const uint8_t* data, size_t size);
    void Upload(bool transparent);

    std::vector<uint8_t> file_;
    int width_ = 0, height_ = 0, depth_ = 16, frameCount_ = 0;
    uint16_t magic_ = 0;
    size_t firstFrame_ = 128, nextFrame_ = 128;
    int frame_ = -1;
    bool finished_ = false;
    float clock_ = 0.0f;
    std::vector<uint16_t> pixels16_;  // RGB555 (16-bit flics)
    std::vector<uint8_t> pixels8_;    // palette indices (8-bit flics)
    uint8_t palette_[256][3]{};
    std::vector<uint8_t> rgba_;
    Texture texture_;
    bool uploaded_ = false, uploadedTrans_ = false;
};

}  // namespace rr
