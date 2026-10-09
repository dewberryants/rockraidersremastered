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
// What a unit does when it reaches the last node of its route (route action ids of the game).
namespace RouteAction {
constexpr uint8_t None = 0, Drill = 1, Reinforce = 2, Collect = 3, Clear = 4, Throw = 5, Repair = 6, Deposit = 7,
                  Drop = 8, Place = 9, Enter = 10, Eat = 11, GetIn = 12, Train = 14;
constexpr uint8_t Fetch = 100;      // ours: take a resource out of a storage building
constexpr uint8_t BuildPath = 101;  // ours: turn a laid path into a power path
constexpr uint8_t GetTool = 102;    // ours: pick up a tool at a tool store
constexpr uint8_t PlaceDynamite = 103;  // ours: put carried dynamite down at a wall (game: action 9)
}
namespace RouteFlag {
// The unit arrives moving along a fixed direction, given in the low two bits:
// 0 = +Y, 1 = +X, 2 = -Y, 3 = -X (LegoObject_Route_UpdateMovement).
constexpr uint8_t Face = 0x4, FaceDirMask = 0x3;
constexpr uint8_t Last = 0x8;         // final node reached through a zero-length route
constexpr uint8_t CutCorner = 0x10;   // direction changes with a corner step: skipped while moving
}

}  // namespace rr
