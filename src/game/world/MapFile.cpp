#include "game/world/MapFile.h"

#include <cstring>

#include "core/Log.h"
#include "fs/FileSystem.h"

namespace rr {

std::optional<MapFile> MapFile::Load(std::string_view gamePath) {
    if (gamePath.empty()) return std::nullopt;
    auto bytes = Fs().ReadAll(gamePath);
    if (!bytes || bytes->size() < 16 || std::memcmp(bytes->data(), "MAP ", 4) != 0) {
        RR_WARN("Map %.*s missing or invalid", int(gamePath.size()), gamePath.data());
        return std::nullopt;
    }
    const uint8_t* d = bytes->data();
    auto u32 = [&](size_t o) {
        return uint32_t(d[o]) | (uint32_t(d[o + 1]) << 8) | (uint32_t(d[o + 2]) << 16) | (uint32_t(d[o + 3]) << 24);
    };
    MapFile m;
    m.width = int(u32(8));
    m.height = int(u32(12));
    const size_t count = size_t(m.width) * size_t(m.height);
    if (m.width <= 0 || m.height <= 0 || 16 + count * 2 > bytes->size()) return std::nullopt;
    m.values.resize(count);
    for (size_t i = 0; i < count; ++i) m.values[i] = uint16_t(d[16 + i * 2] | (d[17 + i * 2] << 8));
    return m;
}

}  // namespace rr
