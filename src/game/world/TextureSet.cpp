#include "game/world/TextureSet.h"

#include <algorithm>
#include <cstdio>

#include "fs/FileSystem.h"

namespace rr::world {

bool TextureSet::Load(const std::string& baseName, int width, int height) {
    width_ = std::min(width, 16);
    height_ = std::min(height, 16);
    int loaded = 0;
    for (int row = 0; row < height_; ++row) {
        for (int col = 0; col < width_; ++col) {
            char suffix[8];
            std::snprintf(suffix, sizeof(suffix), "%x%x.bmp", col, row);
            const std::string path = baseName + suffix;
            if (!Fs().Exists(path)) continue;  // sets are sparse
            textures_[size_t(row) * 16 + col] = Texture3D::Load(path);
            if (textures_[size_t(row) * 16 + col]) ++loaded;
        }
    }
    return loaded > 0;
}

}  // namespace rr::world
