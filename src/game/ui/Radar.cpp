#include "game/ui/Radar.h"

#include <algorithm>
#include <cmath>

#include "game/objects/GameObject.h"
#include "game/world/Level.h"

namespace rr {

namespace {

ColorF Rgb(int r, int g, int b) { return {float(r) / 255.0f, float(g) / 255.0f, float(b) / 255.0f, 1.0f}; }

// RadarMap_GetBlockColour. Returns false for blocks the radar leaves black.
bool BlockColour(const world::Level& level, int bx, int by, ColorF& out) {
    namespace BlockFlag = world::BlockFlag;
    using world::Terrain;
    const world::Block& b = level.block(bx, by);
    const int t = int(b.terrain);
    const bool wall = (b.flags & BlockFlag::Wall) && !(b.flags & BlockFlag::Ground);
    // Seams look the same wherever they are.
    auto seam = [&]() -> bool {
        switch (b.terrain) {
        case Terrain::OreSeam: out = Rgb(155, 65, 14); return true;
        case Terrain::CrystalSeam: out = Rgb(180, 255, 0); return true;
        case Terrain::RechargeSeam: out = Rgb(255, 255, 0); return true;
        default: return false;
        }
    };
    if (wall) {
        if (seam()) return true;
        // Rock faces by hardness (immovable .. soil), single-width tunnel walls in a lighter set.
        static const int kWall[5][3] = {{86, 2, 136}, {115, 30, 168}, {144, 60, 200}, {174, 89, 233}, {199, 123, 255}};
        static const int kTunnel[5][3] = {{136, 2, 125}, {168, 30, 158}, {200, 60, 192}, {233, 89, 226}, {255, 123, 251}};
        if (t < 1 || t > 5) return true;  // the game keeps the previous colour; black here
        const int* c = (b.flags & 0x40000) ? kTunnel[t - 1] : kWall[t - 1];
        out = Rgb(c[0], c[1], c[2]);
        return true;
    }
    if (!(b.flags & (BlockFlag::Wall | BlockFlag::Ground))) {
        // Solid rock is only shown once surveyed.
        if (!(b.flags & BlockFlag::Visible)) return false;
        if (b.flags & BlockFlag::HiddenCavern) {
            out = Rgb(108, 0, 0);
            return true;
        }
        if (seam()) return true;
        static const int kSolid[5][3] = {{0, 47, 0}, {0, 93, 0}, {0, 175, 12}, {97, 203, 86}, {164, 227, 149}};
        if (t >= 1 && t <= 5) {
            out = Rgb(kSolid[t - 1][0], kSolid[t - 1][1], kSolid[t - 1][2]);
            return true;
        }
        if (b.terrain == Terrain::Lava) out = Rgb(255, 90, 0);
        else if (b.terrain == Terrain::Water) out = Rgb(41, 96, 255);
        else if (b.terrain == Terrain::Lake) out = Rgb(0, 47, 181);
        return true;
    }
    // Floor.
    switch (b.terrain) {
    case Terrain::Lava: out = Rgb(255, 90, 0); return true;
    case Terrain::Water: out = Rgb(41, 96, 255); return true;
    case Terrain::Lake: out = Rgb(0, 47, 181); return true;
    default: break;
    }
    if (b.flags & BlockFlag::Foundation) out = Rgb(191, 191, 191);
    else if (b.flags & BlockFlag::Path) out = (b.flags2 & world::BlockFlag2::Powered) ? Rgb(255, 255, 0) : Rgb(157, 155, 0);
    else out = Rgb(45, 0, 75);
    return true;
}

// RadarMap_CanShowObject: units and resources, not effects, buildings or debris.
bool ShownOnRadar(const GameObject& o) {
    switch (o.type) {
    case ObjType::ElectricFenceStud:
    case ObjType::Boulder:
    case ObjType::Dynamite:
    case ObjType::OohScary:
    case ObjType::Pusher:
    case ObjType::Freezer:
    case ObjType::LaserShot:
    case ObjType::Barrier:
    case ObjType::IceCube:
    case ObjType::Building:
    case ObjType::SpiderWeb: return false;
    default: break;
    }
    return !(o.stats && (o.stats->F2(o.level) & StatsFlag2::DontShowOnRadar));
}

}  // namespace

void RadarMap::Draw(Renderer2D& r, const world::Level& level, const ObjectList& objects, Vec2 centre) const {
    const world::Map3D& map = level.map();
    const float bs = map.blockSize();
    const float scale = zoom / bs;  // screen units per world unit
    // RadarMap_TransformRect: world -> radar screen (world +Y is up on the radar).
    auto toScreen = [&](float wx, float wy, float& sx, float& sy) {
        sx = (wx - centre.x) * scale + rect.w * 0.5f + rect.x;
        sy = rect.h * 0.5f + rect.y - (wy - centre.y) * scale;
    };
    r.SetClip(rect);
    r.FillRect(rect, {0, 0, 0, 1});

    const float halfW = rect.w * 0.5f / zoom * bs, halfH = rect.h * 0.5f / zoom * bs;
    // Blocks under the radar window: block (0, 0) has its top-left corner at the map origin.
    const Vec2 c0 = map.BlockCentre(0, 0);
    const float originX = c0.x - bs * 0.5f, originY = c0.y + bs * 0.5f;
    const int x0 = std::max(0, int(std::floor((centre.x - halfW - originX) / bs)));
    const int x1 = std::min(map.blocksW() - 1, int(std::floor((centre.x + halfW - originX) / bs)));
    const int y0 = std::max(0, int(std::floor((originY - (centre.y + halfH)) / bs)));
    const int y1 = std::min(map.blocksH() - 1, int(std::floor((originY - (centre.y - halfH)) / bs)));

    for (int by = y0; by <= y1; ++by) {
        for (int bx = x0; bx <= x1; ++bx) {
            ColorF c;
            if (!BlockColour(level, bx, by, c)) continue;
            // TODO(interface): the flashing (advisor-highlighted) block is drawn red.
            const Vec2 bc = map.BlockCentre(bx, by);
            float sx, sy;
            toScreen(bc.x - bs * 0.5f, bc.y + bs * 0.5f, sx, sy);
            const float size = bs * scale - 1.0f;  // one unit gap between blocks
            r.FillRect({sx, sy, size, size}, c);
            if ((level.block(bx, by).flags & world::BlockFlag::Reinforced)) {
                const ColorF w{1, 1, 1, 1};
                r.DrawRectOutline({sx, sy, size - 1.0f, size - 1.0f}, w);
            }
        }
    }

    for (const auto& op : objects.objects()) {
        const GameObject& o = *op;
        if (o.removed || o.hidden || !ShownOnRadar(o)) continue;
        if (std::abs(o.position.x - centre.x) > halfW || std::abs(o.position.y - centre.y) > halfH) continue;
        float f = 1.0f / 3.0f;
        ColorF c = Rgb(255, 204, 0);
        if (o.type == ObjType::RockMonster) {
            c = Rgb(255, 0, 0);
        } else if (o.type == ObjType::PowerCrystal || o.type == ObjType::Ore) {
            c = Rgb(0, 255, 0);
            f = 0.25f;
        }
        float sx, sy;
        toScreen(o.position.x - f * 0.5f * bs, o.position.y + f * 0.5f * bs, sx, sy);
        r.FillRect({sx, sy, f * bs * scale, f * bs * scale}, c);
    }
    // TODO(radar): survey dot circles and the camera's view outline.
    r.ClearClip();
}

}  // namespace rr
