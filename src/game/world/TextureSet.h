#pragma once

#include <array>
#include <memory>
#include <string>

#include "gfx/Renderer3D.h"

namespace rr::world {

// Terrain textures of a level ("Textures::Rock" etc.). Surface texture ids are two hex digits that
// name the file: id 0x35 -> "<texturebasename>35.bmp" (Detail_LoadTextureSet).
class TextureSet {
public:
    bool Load(const std::string& baseName, int width, int height);
    const Texture3D* Get(uint8_t id) const {
        const int col = id >> 4, row = id & 0xF;
        if (col >= width_ || row >= height_) return nullptr;
        return textures_[size_t(row) * 16 + col].get();
    }

private:
    int width_ = 0, height_ = 0;
    std::array<std::unique_ptr<Texture3D>, 256> textures_;
};

}  // namespace rr::world
