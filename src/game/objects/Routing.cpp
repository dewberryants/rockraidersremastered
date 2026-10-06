#include "game/objects/Routing.h"

#include <algorithm>
#include <cmath>

#include "game/objects/GameObject.h"
#include "game/world/Level.h"

namespace rr {

namespace {

// Neighbour order used by every grid walk: up, right, down, left.
constexpr int kStepX[4] = {0, 1, 0, -1};
constexpr int kStepY[4] = {-1, 0, 1, 0};

bool InGrid(const world::Level& level, int x, int y) {
    return x >= 0 && y >= 0 && x < level.map().blocksW() && y < level.map().blocksH();
}

bool IsPathBlock(const world::Level& level, int x, int y) {
    return level.InBounds(x, y) && (level.block(x, y).flags & world::BlockFlag::Path);
}

bool BlockOfObject(const world::Level& level, const GameObject& o, int& bx, int& by) {
    return level.map().WorldToBlock(o.position.x, o.position.y, bx, by);
}

}  // namespace

CrossType GetCrossTerrainType(const world::Level& level, const GameObject* obj, int bx1, int by1, int bx2, int by2,
                              bool ignoreOwnBlock) {
    using namespace world;
    // Vehicles that need a driver cannot route on their own.
    // TODO(vehicles): allow once drivers are tracked.
    if (obj && obj->stats && (obj->stats->F1(obj->level) & StatsFlag1::CanBeDriven)) return CrossType::Blocked;

    // Which neighbour block 2 is of block 1 (4 = not a straight neighbour).
    int side = 0;
    while (side < 4 && !(bx2 + kStepX[side] == bx1 && by2 + kStepY[side] == by1)) ++side;

    bool enterToolStore = false, singleWidthDig = false, crossWater = false, crossLand = true, crossLava = false;
    if (obj) {
        if (obj->type == ObjType::ElectricFence) return CrossType::Blocked;
        const uint32_t f1 = obj->stats ? obj->stats->F1(obj->level) : 0;
        const uint32_t f2 = obj->stats ? obj->stats->F2(obj->level) : 0;
        const uint32_t f3 = obj->stats ? obj->stats->F3(obj->level) : 0;
        enterToolStore = f3 & StatsFlag3::EnterToolStore;
        singleWidthDig = f1 & StatsFlag1::SingleWidthDig;
        crossWater = f1 & StatsFlag1::CrossWater;
        crossLand = f1 & StatsFlag1::CrossLand;
        crossLava = f2 & StatsFlag2::CrossLava;
        int ox, oy;
        if (!ignoreOwnBlock && BlockOfObject(level, *obj, ox, oy) && ox == bx1 && oy == by1) return CrossType::Normal;
    }

    const Block& b = level.block(bx1, by1);
    if (b.terrain == Terrain::Lake ? !crossWater : !crossLand) return CrossType::Blocked;
    if (b.terrain == Terrain::Lava && !crossLava && !(b.flags & BlockFlag::LavaSettled)) return CrossType::Blocked;
    if (b.flags & BlockFlag::Building) return CrossType::Blocked;
    if (ignoreOwnBlock && (b.flags & BlockFlag::OuterCorner)) return CrossType::Blocked;
    if (!enterToolStore && (b.flags2 & BlockFlag2::ToolStoreEntrance)) return CrossType::Blocked;

    CrossType result = CrossType::Normal;
    if (!singleWidthDig) {
        if (!(b.flags & BlockFlag::Ground)) return CrossType::Blocked;
    } else {
        if (b.flags & BlockFlag::Wall) result = CrossType::IntoWall;
        if (b.pointState == 0) return CrossType::Blocked;
        if (b.flags & BlockFlag::Wall) {
            // No walking into walls that border lava or water on the left or above.
            for (const Block* n : {&level.block(bx1 - 1, by1), &level.block(bx1, by1 - 1)})
                if (n->terrain == Terrain::Lava || n->terrain == Terrain::Water) return CrossType::Blocked;
        }
        if (side == 0) {
            if ((b.flags & BlockFlag::Diagonal) || (level.block(bx2, by2).flags & BlockFlag::Diagonal))
                result = CrossType::Corner;
        }
    }
    // A diagonal step whose two side blocks are open floor can cut the corner.
    if (bx2 != bx1 && by2 != by1 && (level.block(bx1, by2).flags & BlockFlag::Ground) &&
        (level.block(bx2, by1).flags & BlockFlag::Ground))
        return CrossType::Corner;
    return result;
}

namespace {

// Plain breadth-first search (LegoObject_Route_BuildListWithoutScore).
bool BuildRouteListUnscored(const world::Level& level, const GameObject* obj, BlockPos start, BlockPos target,
                            std::vector<BlockPos>& out, const RouteGoal& goal) {
    const int w = level.map().blocksW(), h = level.map().blocksH();
    std::vector<int> wave(size_t(w) * h, 0);
    auto at = [&](int x, int y) -> int& { return wave[size_t(y) * w + x]; };
    at(start.x, start.y) = 1;
    std::vector<BlockPos> frontier{start}, next;
    int current = 1;
    bool found = false;
    while (true) {
        next.clear();
        for (const BlockPos& c : frontier) {
            for (int d = 0; d < 4; ++d) {
                const int nx = c.x + kStepX[d], ny = c.y + kStepY[d];
                if (!InGrid(level, nx, ny)) continue;
                const CrossType ct = GetCrossTerrainType(level, obj, nx, ny, nx + kStepX[d], ny + kStepY[d], false);
                if (at(nx, ny) != 0 || ct == CrossType::Blocked) continue;
                next.push_back({nx, ny});
                at(nx, ny) = current + 1;
                if (!goal) {
                    if (nx == target.x && ny == target.y) found = true;
                } else if (goal({nx, ny})) {
                    found = true;
                    target = {nx, ny};
                }
            }
        }
        ++current;
        if (found) break;
        if (next.empty()) return false;
        frontier.swap(next);
    }

    // Walk back from the target through descending wave numbers. The search direction carries
    // over between steps, so routes keep going straight where they can.
    out.assign(size_t(current), {});
    out[0] = start;
    out[size_t(current - 1)] = target;
    BlockPos p = target;
    int dir = 0;
    for (int i = current - 1; i > 1; --i) {
        for (int tries = 0; tries < 4; ++tries) {
            const int nx = p.x + kStepX[dir], ny = p.y + kStepY[dir];
            if (InGrid(level, nx, ny) && at(nx, ny) == i) {
                p = {nx, ny};
                out[size_t(i - 1)] = p;
                break;
            }
            dir = (dir + 1) & 3;
        }
    }
    return true;
}

}  // namespace

bool BuildRouteList(const world::Level& level, const GameObject* obj, BlockPos start, BlockPos target,
                    std::vector<BlockPos>& out, const RouteGoal& goal) {
    out.clear();
    const int w = level.map().blocksW(), h = level.map().blocksH();
    if (!InGrid(level, start.x, start.y)) return false;
    if (!goal) {
        if (!InGrid(level, target.x, target.y)) return false;
        if (GetCrossTerrainType(level, obj, target.x, target.y, target.x, target.y, false) == CrossType::Blocked)
            return false;
    }

    // Scored search: cost 1 per step, 0.5 onto power paths; cells are re-opened when a cheaper
    // cost reaches them. The route length is the wave in which the target was first reached.
    std::vector<float> cost(size_t(w) * h, 0.0f);
    auto at = [&](int x, int y) -> float& { return cost[size_t(y) * w + x]; };
    at(start.x, start.y) = 1.0f;
    std::vector<BlockPos> frontier{start}, next;
    int wave = 1, foundWave = 0;
    bool reached = false;
    int state = 0;  // 0 searching, 1 goal accepted, -1 exhausted
    while (state == 0) {
        ++wave;
        next.clear();
        for (const BlockPos& c : frontier) {
            const float base = at(c.x, c.y);
            for (int d = 0; d < 4; ++d) {
                const int nx = c.x + kStepX[d], ny = c.y + kStepY[d];
                if (!InGrid(level, nx, ny)) continue;
                const float step = IsPathBlock(level, nx + kStepX[d], ny + kStepY[d]) ? 0.5f : 1.0f;
                if (GetCrossTerrainType(level, obj, nx, ny, nx + kStepX[d], ny + kStepY[d], false) == CrossType::Blocked)
                    continue;
                float& cell = at(nx, ny);
                if (cell != 0.0f && !(step + base < cell)) continue;
                cell = step + base;
                next.push_back({nx, ny});
                if (!goal) {
                    if (nx == target.x && ny == target.y) {
                        reached = true;
                        foundWave = wave;
                    }
                } else if (goal({nx, ny})) {
                    state = 1;
                    target = {nx, ny};
                }
            }
        }
        if (next.empty()) state = -1;
        frontier.swap(next);
    }
    int count = wave;
    if (reached) {
        state = 1;
        count = foundWave;
    }
    if (state == -1) return false;

    // Walk back choosing the neighbour with the smallest positive cost drop (paths win ties).
    out.assign(size_t(count), {});
    out[0] = start;
    out[size_t(count - 1)] = target;
    BlockPos p = target;
    for (int i = count - 2; i > 0; --i) {
        float best = 10000.0f;
        BlockPos pick = p;
        for (int d = 0; d < 4; ++d) {
            const int nx = p.x + kStepX[d], ny = p.y + kStepY[d];
            if (!InGrid(level, nx, ny)) continue;
            const float drop = at(p.x, p.y) - at(nx, ny);
            if ((drop > 0.0f && drop < best) || (drop == best && IsPathBlock(level, nx, ny))) {
                best = drop;
                pick = {nx, ny};
            }
        }
        out[size_t(i)] = pick;
        p = pick;
    }
    // The scored walk-back can fall short of the start; then use the plain search.
    if (count >= 2) {
        const BlockPos a = out[0], b = out[1];
        const bool adjacent = (a.x == b.x && std::abs(a.y - b.y) == 1) || (a.y == b.y && std::abs(a.x - b.x) == 1);
        if (!adjacent) return BuildRouteListUnscored(level, obj, start, target, out, goal);
    }
    return true;
}

void BezierCurve::Build(Vec2 start, Vec2 startTangent, Vec2 end, Vec2 endTangent, int count) {
    count = std::min(count, kMaxPoints);
    points.assign(size_t(std::max(count, 1)), start);
    // Power-basis form of the cubic with control points start + 3*t0 and end - 3*t1.
    const Vec2 c1{start.x + startTangent.x * 3.0f, start.y + startTangent.y * 3.0f};
    const Vec2 c2{end.x - endTangent.x * 3.0f, end.y - endTangent.y * 3.0f};
    const Vec2 c{(c1.x - start.x) * 3.0f, (c1.y - start.y) * 3.0f};
    const Vec2 b{(c2.x - c1.x) * 3.0f - c.x, (c2.y - c1.y) * 3.0f - c.y};
    const Vec2 a{end.x - start.x - c.x - b.x, end.y - start.y - c.y - b.y};
    for (int i = 1; i < count; ++i) {
        const float t = float(i) * (1.0f / float(count - 1));
        const float t2 = t * t, t3 = t2 * t;
        points[size_t(i)] = {c.x * t + b.x * t2 + a.x * t3 + start.x, c.y * t + b.y * t2 + a.y * t3 + start.y};
    }
}

float BezierCurve::UpdateDistances() {
    lengths.assign(points.size() > 1 ? points.size() - 1 : 0, 0.0f);
    float total = 0.0f;
    for (size_t i = 0; i + 1 < points.size(); ++i) {
        const float dx = points[i].x - points[i + 1].x, dy = points[i].y - points[i + 1].y;
        lengths[i] = std::sqrt(dx * dx + dy * dy);
        total += lengths[i];
    }
    return total;
}

int BezierCurve::Interpolate(float distance, Vec2& out) const {
    const int n = int(points.size());
    float acc = 0.0f;
    int seg = 1;
    while (seg < n) {
        acc += lengths[size_t(seg - 1)];
        if (distance < acc) break;
        ++seg;
    }
    if (seg < n) {
        const float len = lengths[size_t(seg - 1)];
        const float f = (distance - (acc - len)) / len;
        const Vec2& p = points[size_t(seg - 1)];
        const Vec2& q = points[size_t(seg)];
        out = {(q.x - p.x) * f + p.x, (q.y - p.y) * f + p.y};
    } else {
        out = n > 0 ? points[size_t(n - 1)] : Vec2{};
    }
    return seg;
}

}  // namespace rr
