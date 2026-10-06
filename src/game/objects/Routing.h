#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "core/Math3D.h"

namespace rr {

struct GameObject;
namespace world {
class Level;
}

struct BlockPos {
    int x = 0, y = 0;
    bool operator==(const BlockPos& o) const { return x == o.x && y == o.y; }
    bool operator!=(const BlockPos& o) const { return !(*this == o); }
};

// How an object may step from block 2 into block 1 (Lego_GetCrossTerrainType).
enum class CrossType : int {
    Blocked = 0,
    Normal = 1,
    Corner = 2,    // diagonal or corner step: the route may cut the corner
    IntoWall = 3,  // a single-width digger walking up to a wall to drill it
};
CrossType GetCrossTerrainType(const world::Level& level, const GameObject* obj, int bx1, int by1, int bx2, int by2,
                              bool ignoreOwnBlock);

// Grid route search over 4-neighbours (LegoObject_Route_BuildList). Steps onto power paths cost
// half; if the scored result does not start with a single step it falls back to a plain
// breadth-first search. With a goal callback, the first wave containing an accepted block ends the
// search and the last accepted block becomes the target.
using RouteGoal = std::function<bool(const BlockPos&)>;
bool BuildRouteList(const world::Level& level, const GameObject* obj, BlockPos start, BlockPos target,
                    std::vector<BlockPos>& out, const RouteGoal& goal = nullptr);

// Cubic curve sampled into straight segments (BezierCurve_*). Tangents are relative: the inner
// control points are start + 3 * startTangent and end - 3 * endTangent.
struct BezierCurve {
    static constexpr int kMaxPoints = 50;
    std::vector<Vec2> points;
    std::vector<float> lengths;  // lengths[i] = |points[i+1] - points[i]|

    void Build(Vec2 start, Vec2 startTangent, Vec2 end, Vec2 endTangent, int count);
    float UpdateDistances();  // returns the total length
    // Point at a distance along the curve; returns the segment index used.
    int Interpolate(float distance, Vec2& out) const;
};

// One step of an object's route (block plus position inside it, 0..1).
struct RouteNode {
    BlockPos block;
    Vec2 offset{0.5f, 0.5f};
    uint8_t flags = 0;   // RouteFlag
    uint8_t action = 0;  // what to do on arrival (drill, collect, ...); 0 = nothing
};
namespace RouteFlag {
constexpr uint8_t Last = 0x8;         // final node reached through a zero-length route
constexpr uint8_t CutCorner = 0x10;   // direction changes with a corner step: skipped while moving
}

}  // namespace rr
