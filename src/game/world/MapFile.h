#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace rr {

// Level grid file (.map): "MAP " magic, u32 data size, u32 width, u32 height, then width*height
// u16 values in rows. Used for heights (SurfaceMap), predug state, terrain type, crystal/ore,
// paths, erosion, emerge triggers, tutorial block pointers and fall-ins.
struct MapFile {
    int width = 0;
    int height = 0;
    std::vector<uint16_t> values;

    uint16_t At(int x, int y) const { return values[size_t(y) * width + x]; }
    static std::optional<MapFile> Load(std::string_view gamePath);
};

}  // namespace rr
