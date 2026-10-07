#include "game/PowerGrid.h"

#include <algorithm>

#include "core/Log.h"
#include "game/objects/GameObject.h"
#include "game/world/Level.h"

namespace rr {

namespace {

constexpr int kDx[4] = {0, 1, 0, -1}, kDy[4] = {-1, 0, 1, 0};

// Power travels over power paths and the solid blocks of buildings.
bool Conducts(const world::Level& level, int x, int y) {
    if (!level.InBounds(x, y)) return false;
    const uint32_t f = level.block(x, y).flags;
    return (f & world::BlockFlag::Path) || (f & world::BlockFlag::Building);
}

bool IsStation(const GameObject& o) {
    return o.stats && (o.stats->F2(o.level) & StatsFlag2::PowerBuilding);
}

bool IsSelfPowered(const GameObject& o) {
    return o.stats && (o.stats->F2(o.level) & (StatsFlag2::PowerBuilding | StatsFlag2::SelfPowered));
}

}  // namespace

int PowerGrid::Update(world::Level& level, ObjectList& objects, int storedCrystals) {
    if (!dirty_ && storedCrystals == lastStored_) return used_;
    dirty_ = false;
    lastStored_ = storedCrystals;

    // Level_PowerGrid_UnpowerPoweredBlocks: start from an unpowered network.
    for (const BlockPos& p : powered_) {
        level.block(p.x, p.y).flags2 &= ~world::BlockFlag2::Powered;
        level.UpdateSurface(p.x, p.y);
    }
    powered_.clear();
    used_ = 0;

    auto isLive = [](const GameObject& o) {
        return o.type == ObjType::Building && !o.removed && !o.hidden && !o.powerOff;
    };
    // Stations with crystals in store power everything connected to them.
    for (const auto& op : objects.objects()) {
        GameObject& o = *op;
        if (!isLive(o) || !IsStation(o) || storedCrystals <= 0) continue;
        BlockPos start;
        if (!objects.BlockOf(o, start)) continue;
        std::vector<BlockPos> stack{start};
        while (!stack.empty()) {
            const BlockPos b = stack.back();
            stack.pop_back();
            for (int d = 0; d < 4; ++d) {
                const int x = b.x + kDx[d], y = b.y + kDy[d];
                if (!Conducts(level, x, y) || (level.block(x, y).flags2 & world::BlockFlag2::Powered)) continue;
                level.block(x, y).flags2 |= world::BlockFlag2::Powered;
                powered_.push_back({x, y});
                stack.push_back({x, y});
            }
        }
    }

    // Consumers draw their drain from a station reachable over the network
    // (Construction_PowerGrid_DrainAdjacentBlocks).
    for (const auto& op : objects.objects()) {
        GameObject& o = *op;
        if (o.type != ObjType::Building) continue;
        if (!isLive(o)) {
            o.powered = false;
            continue;
        }
        if (IsSelfPowered(o)) {
            o.powered = true;
            continue;
        }
        const int drain = o.stats ? int(o.stats->Num("CrystalDrain", o.level)) : 0;
        BlockPos start;
        bool found = false;
        if (objects.BlockOf(o, start)) {
            std::vector<BlockPos> visited{start}, stack{start};
            while (!stack.empty() && !found) {
                const BlockPos b = stack.back();
                stack.pop_back();
                for (int d = 0; d < 4 && !found; ++d) {
                    const BlockPos n{b.x + kDx[d], b.y + kDy[d]};
                    if (!level.InBounds(n.x, n.y)) continue;
                    if ((level.block(n.x, n.y).flags2 & world::BlockFlag2::GeneratePower) && storedCrystals > 0 &&
                        used_ + drain <= storedCrystals) {
                        found = true;
                        break;
                    }
                    if (!Conducts(level, n.x, n.y) || std::find(visited.begin(), visited.end(), n) != visited.end())
                        continue;
                    visited.push_back(n);
                    stack.push_back(n);
                }
            }
        }
        o.powered = found;
        if (found) used_ += drain;
        // TODO(interface): unpowered buildings show the power-off bubble (Bubble_ShowPowerOff).
    }

    // Surfaces of blocks whose power changed.
    for (const BlockPos& p : powered_) level.UpdateSurface(p.x, p.y);
    RR_DEBUG("Power grid: %zu powered blocks, %d crystals in use of %d", powered_.size(), used_, storedCrystals);
    return used_;
}

}  // namespace rr
