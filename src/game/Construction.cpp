#include "game/Construction.h"

#include <cmath>
#include <cstdlib>

#include "core/Log.h"
#include "fs/Path.h"
#include "game/objects/GameObject.h"
#include "game/objects/ObjectRegistry.h"
#include "game/world/Level.h"

namespace rr {

void Dependencies::Load(const Config& cfg, std::string_view gameName) {
    entries_.clear();
    for (Config::ItemRef it = cfg.FindArray(Config::Id(gameName, "Dependencies")); it != Config::kNone;
         it = cfg.Next(it)) {
        std::string_view key = cfg.item(it).key;
        Entry e;
        if (StartsWithNoCase(key, "AlwaysCheck:")) {
            key.remove_prefix(12);
        } else if (StartsWithNoCase(key, "HitOnceStay:")) {
            key.remove_prefix(12);
            e.stay = true;
        } else {
            continue;
        }
        e.object = GetObjectRegistry().Find(key);
        if (!e.object) continue;
        for (std::string_view req : SplitTokens(cfg.item(it).value, ",")) {
            const auto parts = SplitTokens(req, ":");
            Requirement r;
            r.type = GetObjectRegistry().Find(parts[0]);
            if (parts.size() > 1) r.level = std::atoi(std::string(parts[1]).c_str());
            if (r.type) e.requires.push_back(r);
        }
        entries_.push_back(std::move(e));
    }
}

void Dependencies::ResetLevel() {
    for (Entry& e : entries_) e.unlocked = false;
}

bool Dependencies::Met(const ObjectTypeInfo& type, const ObjectList& objects) {
    for (Entry& e : entries_) {
        if (e.object != &type) continue;
        if (e.unlocked) return true;
        for (const Requirement& r : e.requires) {
            bool found = false;
            for (const auto& o : objects.objects()) {
                if (o->removed || o->hidden || o->info != r.type) continue;
                if (o->level >= r.level) {
                    found = true;
                    break;
                }
            }
            if (!found) return false;
        }
        if (e.stay) e.unlocked = true;
        return true;
    }
    return true;  // no dependency entry: always available
}

std::vector<BlockPos> BuildingFootprint(const ObjectTypeInfo& type, BlockPos origin, int dir) {
    std::vector<BlockPos> blocks;
    if (!type.model.activities) {
        blocks.push_back(origin);
        return blocks;
    }
    for (const auto& [sx, sy] : type.model.activities->shape) {
        BlockPos p{sx, sy};
        switch (dir & 3) {
        case 1: p = {-sy, sx}; break;
        case 2: p = {-sx, -sy}; break;
        case 3: p = {sy, -sx}; break;
        default: break;
        }
        blocks.push_back({p.x + origin.x, p.y + origin.y});
    }
    return blocks;
}

namespace {

// Level_CanBuildOnBlock for one footprint block.
bool CanBuildOnBlock(const world::Level& level, BlockPos p, bool pathTile) {
    using namespace world;
    if (!level.InBounds(p.x, p.y)) return false;
    const Block& b = level.block(p.x, p.y);
    if (b.flags2 & BlockFlag2::SlugHole) return false;
    if (b.flags & BlockFlag::Path) return false;  // power paths are not built over
    // TODO(construction): sites in progress, lake path tiles for water buildings, fences.
    if (b.terrain == Terrain::Lake && !pathTile) return false;
    if (b.terrain == Terrain::Lava) return false;
    if (!(b.flags & BlockFlag::Ground) || !(b.flags & BlockFlag::CleanFloor)) return false;
    if (b.flags & (BlockFlag::Building | BlockFlag::Foundation | BlockFlag::BuildingPath)) return false;
    static const int kDx[4] = {0, 1, 0, -1}, kDy[4] = {-1, 0, 1, 0};
    for (int i = 0; i < 4; ++i)
        if (level.InBounds(p.x + kDx[i], p.y + kDy[i]) &&
            level.block(p.x + kDx[i], p.y + kDy[i]).terrain == Terrain::RechargeSeam)
            return false;
    return true;
}

}  // namespace

bool CanPlaceBuilding(const world::Level& level, const std::vector<BlockPos>& footprint) {
    using namespace world;
    static const int kDx[4] = {0, 1, 0, -1}, kDy[4] = {-1, 0, 1, 0};
    // The building has to join the power path network: a power path (not another building's
    // path tile) beside its path tile, or beside any block when it has no path tile.
    bool hasPathTile = false, touchesPath = false, pathTileTouches = false;
    for (size_t i = 0; i < footprint.size(); ++i) {
        const bool pathTile = i + 1 < footprint.size() && footprint[i] == footprint[i + 1];
        bool touches = false;
        for (int d = 0; d < 4; ++d) {
            const int x = footprint[i].x + kDx[d], y = footprint[i].y + kDy[d];
            if (!level.InBounds(x, y)) continue;
            const Block& n = level.block(x, y);
            if ((n.flags & BlockFlag::Path) && !(n.flags & BlockFlag::BuildingPath)) touches = true;
        }
        touchesPath |= touches;
        if (pathTile) {
            hasPathTile = true;
            pathTileTouches |= touches;
            ++i;
        }
    }
    if (!(hasPathTile ? pathTileTouches : touchesPath)) return false;
    for (size_t i = 0; i < footprint.size(); ++i) {
        const bool pathTile = i + 1 < footprint.size() && footprint[i] == footprint[i + 1];
        if (!CanBuildOnBlock(level, footprint[i], pathTile)) return false;
        if (pathTile) ++i;
    }
    // TODO(construction): ground height tolerance (Map3D_CheckBuildingTolerance).
    return true;
}

}  // namespace rr
