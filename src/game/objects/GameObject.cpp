#include "game/objects/GameObject.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "config/Config.h"
#include "core/Log.h"
#include "core/Random.h"
#include "fs/Path.h"
#include "game/Construction.h"
#include "game/Sfx.h"
#include "game/ai/Tasks.h"
#include "game/world/Level.h"
#include "gfx/Renderer3D.h"
#include "gfx/model/ModelDraw.h"

namespace rr {

namespace {

const Activity* FindAnim(const std::shared_ptr<ActivityModel>& model, ActivityType a) {
    return model ? model->Find(ActivityName(a)) : nullptr;
}

float FrameCount(const Activity* a) { return a && a->scene ? float(a->scene->frameCount()) : 0.0f; }

// Container_SetAnimationTime: returns how far the time ran past the last frame (0 if not).
float Overrun(const Activity* a, float time) {
    const float frames = FrameCount(a);
    if (frames <= 0.0f) return 0.0f;
    return time > frames - 1.0f ? time - (frames - 1.0f) : 0.0f;
}

}  // namespace

bool GameObject::HasActivity(ActivityType a) const {
    return info && (FindAnim(info->model.activities, a) || FindAnim(info->model.secondPart, a));
}

bool GameObject::UpdateActivityChange() {
    if (nextActivity == activity) return false;
    // TODO(ai): activities other than stand/route/walk/turn/carry put the AI task into an
    // animation wait (AITask_DoAnimationWait).
    bool found = false;
    if (info && info->model.activities) {
        // Vehicle/Creature/Building_SetActivity: switch every part that has the activity.
        if (const Activity* a = FindAnim(info->model.activities, nextActivity)) {
            anim = a;
            animTime = 0.0f;
            animFresh = true;
            found = true;
        }
        if (const Activity* a = FindAnim(info->model.secondPart, nextActivity)) {
            anim2 = a;
            animTime2 = 0.0f;
            found = true;
        }
    }
    animOverrun = 0.0f;
    activity = nextActivity;
    return found;
}

bool GameObject::HasTool(ToolType t) const {
    return std::find(tools.begin(), tools.end(), t) != tools.end();
}

void GameObject::EquipTool(ToolType t) {
    const int capacity = stats ? int(stats->Num("NumOfToolsCanCarry", level)) : 0;
    if (capacity <= 0 || HasTool(t)) return;
    // TODO(weapons): beam weapons replace the beam weapon already carried.
    if (int(tools.size()) >= capacity) tools.erase(tools.begin());
    tools.push_back(t);
}

void GameObject::Face(Vec2 dir) {
    if (dir.x == 0.0f && dir.y == 0.0f) return;
    faceDir = dir;
    heading = std::atan2(dir.x, dir.y);
}

void GameObject::MoveAnimation(float elapsed, float animSpeed) {
    if (flags3 & ObjFlag3::SimpleObject) {
        // LegoObject_SimpleObject_MoveAnimation (boulders rolling, crystals spinning...).
        if (type == ObjType::PowerCrystal) spin += elapsed * 0.01f;  // TODO(carry): not while carried
        animTime += elapsed;
        return;
    }
    if (!anim && !anim2) return;
    // Vehicle_MoveAnimation / Creature_MoveAnimation: the overrun counts from the last part moved,
    // minus the frames of any extra repeats.
    float over = 0.0f;
    if (anim) {
        animTime += animSpeed;
        over = Overrun(anim, animTime);
    }
    if (anim2) {
        animTime2 += elapsed;
        over = Overrun(anim2, animTime2);
    }
    if (animRepeat > 1 && over != 0.0f) over -= FrameCount(anim ? anim : anim2) * float(animRepeat - 1);
    animOverrun = over;
}

ObjectSpeeds ObjectList::CalculateSpeeds(const GameObject& o, float elapsed) const {
    ObjectSpeeds sp;
    if ((o.flags3 & ObjFlag3::SimpleObject) || o.type == ObjType::UpgradePart || !o.stats) {
        sp.anim = elapsed;
        return sp;
    }
    // TODO(movement): drilling, carrying, reversing and stealth slow-downs (route speed flags).
    float route = o.stats->Num("RouteSpeed", o.level);
    BlockPos b;
    if (BlockOf(o, b)) {
        const world::Block& blk = level_->block(b.x, b.y);
        // Rubble: each layer takes a quarter of (1 - RubbleCoef); clean floor has none.
        const int layers =
            (blk.flags & world::BlockFlag::CleanFloor) ? 0 : int(blk.flags & world::BlockFlag::RubbleMask) + 1;
        float coef = 1.0f - (1.0f - o.stats->Num("RubbleCoef", o.level, 1.0f)) * (float(layers) * 0.25f);
        if (blk.flags & world::BlockFlag::Path) coef *= o.stats->Num("PathCoef", o.level, 1.0f);
        // TODO(lakes): minifigures wading through lake blocks slow down with depth.
        if (blk.flags & world::BlockFlag::Ground) route *= coef;
    }
    const float transCoef = o.anim ? o.anim->transCoef : 0.0f;
    sp.anim = (transCoef == 0.0f ? 1.0f : route / transCoef) * elapsed;
    sp.route = route * elapsed;
    return sp;
}

Vec3f ObjectList::PickCentre(const GameObject& o) const {
    const float ground = level_ ? level_->map().WorldZ(o.position.x, o.position.y) : o.position.z;
    const float height = o.stats ? o.stats->Num("CollHeight", o.level) : 0.0f;
    return {o.position.x, o.position.y, ground - height * 0.5f};
}

GameObject* ObjectList::PickObject(const Vec3f& origin, const Vec3f& dir) const {
    GameObject* best = nullptr;
    float bestT = 0.0f;
    for (const auto& op : objects_) {
        GameObject& o = *op;
        if (o.removed || o.hidden || !o.stats) continue;
        const float radius = o.stats->Num("PickSphere", o.level);
        if (radius <= 0.0f) continue;
        // TODO(buildings): one sphere per block of the building's shape.
        const Vec3f c = PickCentre(o);
        const Vec3f oc = c - origin;
        const float t = Dot(oc, dir);
        if (t <= 0.0f) continue;
        const Vec3f closest = origin + dir * t;
        if (Length(c - closest) > radius) continue;
        if (!best || t < bestT) {
            best = &o;
            bestT = t;
        }
    }
    return best;
}

bool ObjectList::BlockOf(const GameObject& o, BlockPos& out) const {
    return level_ && level_->map().WorldToBlock(o.position.x, o.position.y, out.x, out.y);
}

GameObject* ObjectList::Create(const ObjectTypeInfo& info) {
    auto obj = std::make_unique<GameObject>();
    GameObject& o = *obj;
    o.type = info.type;
    o.id = info.id;
    o.info = &info;
    o.stats = GetStats().Find(info.name);
    o.health = 100.0f;
    o.energy = 100.0f;

    // LegoObject_Create: per-type initial flags. Only the bits used so far are named.
    switch (o.type) {
    case ObjType::Vehicle: o.flags3 |= 0x441190; break;
    case ObjType::MiniFigure: o.flags3 |= 0x4401f5 | 0x8000; break;  // tools below
    case ObjType::RockMonster: o.flags3 |= 0x8440200 | 0x80000000u; break;
    case ObjType::Building: o.flags3 |= 0x40100; break;
    case ObjType::UpgradePart: o.flags3 |= 0x80000; break;
    default:
        o.flags3 |= ObjFlag3::SimpleObject;
        if (o.type == ObjType::ElectricFence) o.flags3 |= 0x60100;
        if (o.type == ObjType::Ore || o.type == ObjType::PowerCrystal) o.flags3 |= 0x100100;
        else if (o.type == ObjType::Dynamite || o.type == ObjType::OohScary) o.flags3 |= 0x40000;
        break;
    }
    // Probe which optional animations the model has, then settle on Activity_Stand.
    o.SetActivity(ActivityType::TurnLeft);
    if (o.UpdateActivityChange()) o.flags3 |= ObjFlag3::CanTurn;
    o.SetActivity(ActivityType::Gather);
    if (o.UpdateActivityChange()) o.flags3 |= ObjFlag3::CanGather;
    o.SetActivity(ActivityType::RouteRubble);
    if (o.UpdateActivityChange()) o.flags3 |= ObjFlag3::CanRouteRubble;
    o.SetActivity(ActivityType::Stand);
    o.UpdateActivityChange();
    if (o.stats && (o.stats->F2(o.level) & StatsFlag2::Unselectable)) o.flags3 &= ~ObjFlag3::Selectable;
    if (o.type == ObjType::MiniFigure) o.EquipTool(ToolType::Drill);  // every Rock Raider starts with a drill
    // TODO(objects): tools, flocks, power grid, AI tasks for buildings/monsters, counts per type.

    objects_.push_back(std::move(obj));
    return objects_.back().get();
}

void ObjectList::SetPositionAndHeading(GameObject& o, float x, float y, float heading) {
    o.position.x = x;
    o.position.y = y;
    // TODO(lakes): minifigures stand in lake water (LegoObject_GetWorldZCallback_Lake).
    o.position.z = level_ ? level_->map().WorldZ(x, y) : 0.0f;
    o.heading = heading;
}

void ObjectList::Update(float elapsed) {
    for (auto& op : objects_) {
        GameObject& o = *op;
        if (o.removed) continue;
        o.UpdateActivityChange();
        const Activity* before = o.anim;
        const float t0 = o.animFresh ? -0.001f : o.animTime;  // a new animation also passes frame 0
        o.animFresh = false;
        o.MoveAnimation(elapsed, CalculateSpeeds(o, elapsed).anim);
        if (o.anim && o.anim == before && o.anim->scene && !o.hidden)
            PlaySceneSounds(*o.anim->scene, o.anim->looping, t0, o.animTime, o.position);
        if (o.teleportingIn) {
            // Arrived once the beam-in animation has played through.
            if (o.animOverrun > 0.0f) {
                o.teleportingIn = false;
                o.SetActivity(ActivityType::Stand);
            }
            continue;
        }
        if (o.routing) {
            UpdateMovement(o, elapsed);
            // TODO(activities): rubble-route and panic variants.
            if (o.routing) o.SetActivity(o.carried ? ActivityType::Carry : ActivityType::Route);
        } else if (o.state != UnitState::Idle) {
            UpdateAction(o, elapsed);
        } else if (o.carried && o.activity == ActivityType::Stand) {
            o.SetActivity(ActivityType::CarryStand);
        }
    }
    // Carried objects ride in front of their carrier.
    // TODO(carry): attach to the carrier's carry null instead of a fixed offset.
    for (auto& op : objects_) {
        GameObject& o = *op;
        if (!o.carrier) continue;
        const GameObject& c = *o.carrier;
        o.position = {c.position.x + c.faceDir.x * 4.0f, c.position.y + c.faceDir.y * 4.0f, c.position.z - 8.0f};
        o.heading = c.heading;
    }
}

void ObjectList::PlaySceneSounds(const LwsScene& scene, bool loop, float t0, float t1, const Vec3f& at) {
    if (scene.sounds.empty() || t1 <= t0) return;
    const float frames = float(scene.frameCount());
    for (const LwsSound& s : scene.sounds) {
        const float f = float(s.start);
        // Passed this tick: between the old and new time, every loop for looping animations.
        bool passed;
        if (loop && frames > 0.0f)
            passed = std::floor((t1 - f) / frames) > std::floor((t0 - f) / frames);
        else
            passed = t0 < f && f <= t1;
        if (!passed) continue;
        int id = 0;
        // TODO(audio): ranges keep playing until their end frame (Sound3D_StopSound there).
        if (GetSfx().GetType(s.sample, id)) {
            GetSfx().PlayAt(id, Vec3{at.x, at.y, at.z});
            RR_DEBUG("Scene sound %s", s.sample.c_str());
        }
    }
}

void ObjectList::StartAction(GameObject& o, uint8_t action, BlockPos block, GameObject* target) {
    RR_DEBUG("%s starts action %d at %d,%d", o.info->name.c_str(), int(action), block.x, block.y);
    o.stateBlock = block;
    o.stateTarget = target;
    switch (action) {
    case RouteAction::Drill:
        o.state = UnitState::Drilling;
        o.SetActivity(ActivityType::Drill);
        break;
    case RouteAction::Clear:
        o.state = UnitState::Clearing;
        // Minifigures sweep the rubble twice per layer.
        o.SetActivity(ActivityType::Clear, o.type == ObjType::MiniFigure ? 2 : 0);
        break;
    case RouteAction::Collect:
        o.state = UnitState::Collecting;
        o.SetActivity(ActivityType::Collect);
        break;
    case RouteAction::Deposit:
        o.state = UnitState::Depositing;
        o.SetActivity(ActivityType::Deposit);
        break;
    case RouteAction::Fetch:
        o.state = UnitState::Fetching;
        o.SetActivity(ActivityType::Collect);
        break;
    case RouteAction::Place:
        o.state = UnitState::Placing;
        o.SetActivity(ActivityType::Place);
        break;
    case RouteAction::GetTool:
        o.state = UnitState::GettingTool;
        o.SetActivity(ActivityType::Collect);
        break;
    case RouteAction::BuildPath:
        // LegoObject_TryBuildPath: the path is swept into place with the clearing animation.
        o.state = UnitState::BuildingPath;
        o.SetActivity(ActivityType::Clear);
        break;
    default: FinishAction(o, true); break;
    }
}

void ObjectList::FinishAction(GameObject& o, bool success) {
    RR_DEBUG("%s finished action (state %d): %s", o.info->name.c_str(), int(o.state), success ? "ok" : "failed");
    o.state = UnitState::Idle;
    o.stateTarget = nullptr;
    o.SetActivity(o.carried ? ActivityType::CarryStand : ActivityType::Stand);
    if (onActionDone) onActionDone(o, success);
}

void ObjectList::Remove(GameObject& o) {
    o.removed = true;
    if (o.carrier) o.carrier->carried = nullptr;
    if (o.carried) o.carried->carrier = nullptr;
    o.carrier = o.carried = nullptr;
    for (auto& op : objects_) {
        if (op->stateTarget == &o) op->stateTarget = nullptr;
        if (op->teleportService == &o) op->teleportService = nullptr;
    }
}

void ObjectList::UpdateAction(GameObject& o, float elapsed) {
    world::Level& level = *level_;
    switch (o.state) {
    case UnitState::Drilling: {
        world::Block& b = level.block(o.stateBlock.x, o.stateBlock.y);
        if ((b.flags & world::BlockFlag::Ground) || !(b.flags & world::BlockFlag::Wall)) {
            FinishAction(o, true);  // someone else finished it
            break;
        }
        o.SetActivity(ActivityType::Drill);
        float time = DrillTime(o, int(b.terrain));
        if (time <= 0.0f) {
            FinishAction(o, false);
            break;
        }
        if (o.singleWidthDig) time *= 0.5f;
        // Level_Block_Damage: progress by elapsed over the drill time; the wall breaks past 1.
        // TODO(drilling): drill sounds, single-width tunnels (Level_DestroyWallConnection).
        b.damage += elapsed / time;
        // Seams take four rounds: the wall only goes once the last stage is drilled.
        if (b.damage > 1.0f && level.DestroyWall(o.stateBlock.x, o.stateBlock.y, false)) FinishAction(o, true);
        break;
    }
    case UnitState::Clearing:
        if (o.animOverrun > 0.0f) {
            if (level.ClearRubbleLayer(o.stateBlock.x, o.stateBlock.y)) {
                FinishAction(o, true);
            } else {
                o.activity = ActivityType::Count;  // replay the sweep for the next layer
                o.SetActivity(ActivityType::Clear, o.type == ObjType::MiniFigure ? 2 : 0);
            }
        }
        break;
    case UnitState::Collecting:
        if (o.animOverrun > 0.0f) {
            GameObject* t = o.stateTarget;
            if (t && !t->removed && !t->carrier && !o.carried) {
                o.carried = t;
                t->carrier = &o;
                FinishAction(o, true);
            } else {
                FinishAction(o, false);
            }
        }
        break;
    case UnitState::Depositing:
        if (o.animOverrun > 0.0f) {
            GameObject* building = o.stateTarget;
            if (o.carried && building) {
                GameObject& item = *o.carried;
                o.carried = nullptr;
                item.carrier = nullptr;
                if (onDeposit) onDeposit(o, item, *building);
                Remove(item);
                FinishAction(o, true);
            } else {
                FinishAction(o, false);
            }
        }
        break;
    case UnitState::Fetching:
        if (o.animOverrun > 0.0f) {
            GameObject* item = onFetch ? onFetch(o) : nullptr;
            if (item) {
                o.carried = item;
                item->carrier = &o;
            } else {
                o.deliverZone = 0;
            }
            FinishAction(o, item != nullptr);
        }
        break;
    case UnitState::Placing:
        if (o.animOverrun > 0.0f) {
            GameObject* item = o.carried;
            if (item) {
                o.carried = nullptr;
                item->carrier = nullptr;
                // Left on the site where the unit put it down.
                item->position = {o.position.x + o.faceDir.x * 6.0f, o.position.y + o.faceDir.y * 6.0f, 0.0f};
                item->position.z = level.map().WorldZ(item->position.x, item->position.y);
                if (onPlace) onPlace(o, *item);
            }
            o.deliverZone = 0;
            FinishAction(o, item != nullptr);
        }
        break;
    case UnitState::GettingTool:
        if (o.animOverrun > 0.0f) {
            if (o.wantedTool != ToolType::Count) o.EquipTool(o.wantedTool);
            o.wantedTool = ToolType::Count;
            FinishAction(o, true);
        }
        break;
    case UnitState::BuildingPath:
        if (o.animOverrun > 0.0f) {
            if (onBuildPath) onBuildPath(o, o.stateBlock);
            FinishAction(o, true);
        }
        break;
    case UnitState::Idle: break;
    }
}

bool ObjectList::RouteToBlock(GameObject& o, BlockPos target, const Vec2* finalPos) {
    BlockPos start;
    if (!level_ || !BlockOf(o, start)) return false;
    std::vector<BlockPos> blocks;
    if (!BuildRouteList(*level_, &o, start, target, blocks)) return false;
    return RouteBegin(o, blocks, finalPos);
}

namespace {
// Direction from block b to neighbour a (8-neighbourhood), -1 if not adjacent.
int NeighbourDirection(BlockPos a, BlockPos b) {
    const int dx = b.x - a.x, dy = b.y - a.y;
    static const int kDx[8] = {0, 1, 0, -1, 1, 1, -1, -1};
    static const int kDy[8] = {-1, 0, 1, 0, -1, 1, 1, -1};
    for (int i = 0; i < 8; ++i)
        if (kDx[i] == dx && kDy[i] == dy) return i;
    return -1;
}
}  // namespace

bool ObjectList::RouteBegin(GameObject& o, const std::vector<BlockPos>& blocks, const Vec2* finalPos) {
    if (!level_ || blocks.empty()) return false;
    // TODO(objects): refuse while the object is busy (driving, being carried, frozen, ...).
    const bool avoidance =
        o.stats && (o.stats->F1(o.level) & StatsFlag1::RouteAvoidance) && !(o.flags3 & 0x1000);
    const float bs = level_->map().blockSize();
    std::vector<RouteNode> nodes(blocks.size());
    int prevDir = -1;
    for (size_t i = 0; i < blocks.size(); ++i) {
        RouteNode& n = nodes[i];
        n.block = blocks[i];
        if (finalPos && i + 1 == blocks.size()) {
            // Position inside the final block as a 0..1 fraction.
            const Vec2 c = level_->map().BlockCentre(n.block.x, n.block.y);
            n.offset = {(finalPos->x - c.x) / bs + 0.5f, (c.y - finalPos->y) / bs + 0.5f};
        } else if (avoidance) {
            // Spread units across the block so they do not all walk the centre line.
            n.offset.x = random::RandRange(0.3f, 0.7f);
            n.offset.y = random::RandRange(0.3f, 0.7f);
        }
        if (i + 1 < blocks.size()) {
            const int dir = NeighbourDirection(blocks[i + 1], blocks[i]);
            if (dir == -1) return false;  // not a connected route
            if (i != 0 && dir != prevDir &&
                GetCrossTerrainType(*level_, &o, blocks[i + 1].x, blocks[i + 1].y, blocks[i - 1].x,
                                    blocks[i - 1].y, false) == CrossType::Corner)
                n.flags |= RouteFlag::CutCorner;
            prevDir = dir;
        }
    }
    RouteEnd(o, false);
    o.route = std::move(nodes);
    o.routing = true;
    o.routeIndex = 0;
    o.curveDist = 0.0f;
    o.curveLength = 0.0f;
    return true;
}

void ObjectList::RouteEnd(GameObject& o, bool completed) {
    if (!o.route.empty()) {
        // TODO(ai): abandoned routes release carried boulders, busy blocks and dynamite tasks;
        // AITask_Route_End reports the result to the task system.
        if (completed && o.type == ObjType::MiniFigure) o.Face(o.endTangent);
        o.route.clear();
    }
    o.routeIndex = 0;
    o.curveLength = o.curveDist = o.curveOverrun = 0.0f;
    o.routing = false;
    o.routeLastLeg = false;
    o.SetActivity(ActivityType::Stand);
}

void ObjectList::BuildRouteCurve(GameObject& o, bool skipCornerNode) {
    const world::Map3D& map = level_->map();
    const float bs = map.blockSize();
    // The curve bends from the block the object comes from towards the node position.
    BlockPos from;
    if (skipCornerNode) {
        from = o.route[size_t(o.routeIndex)].block;
        ++o.routeIndex;
    } else if (!BlockOf(o, from)) {
        from = o.route[size_t(o.routeIndex)].block;
    }
    const RouteNode& node = o.route[size_t(o.routeIndex)];
    const Vec2 nodeCentre = map.BlockCentre(node.block.x, node.block.y);
    const Vec2 fromCentre = map.BlockCentre(from.x, from.y);
    // TODO(drilling): approaching a wall stops short of it (and nudges into inner corners).
    const Vec2 offset{(node.offset.x - 0.5f) * bs, (node.offset.y - 0.5f) * bs};
    const Vec2 target{nodeCentre.x + offset.x, nodeCentre.y - offset.y};
    const Vec2 pos{o.position.x, o.position.y};

    Vec2 endDir;
    if ((node.flags & RouteFlag::Last) && (offset.x != 0.0f || offset.y != 0.0f)) {
        endDir = offset;
    } else {
        endDir = {target.x - fromCentre.x, target.y - fromCentre.y};
        if (endDir.x == 0.0f && endDir.y == 0.0f) endDir = {target.x - pos.x, target.y - pos.y};
    }
    // TODO(movement): sharp turns play TurnLeft/TurnRight first, units can reverse (CanTurn).

    const float dx = pos.x - target.x, dy = pos.y - target.y;
    const float len = std::sqrt(dx * dx + dy * dy) * 0.2f;
    if (len <= 0.1f) {
        o.curve.points.assign(1, pos);
    } else {
        Vec2 startDir = o.faceDir;
        if (!std::isfinite(startDir.x)) startDir = {1.0f, 0.0f};
        auto setLength = [](Vec2 v, float l) {
            const float m = std::sqrt(v.x * v.x + v.y * v.y) / l;
            return Vec2{v.x / m, v.y / m};
        };
        startDir = setLength(startDir, len);
        endDir = setLength(endDir, len * 1.6f);
        o.curve.Build(pos, startDir, target, endDir, BezierCurve::kMaxPoints);
        // TODO(movement): curves clipping solid blocks are straightened (Route_CurveSolid).
    }
    o.curveLength = o.curve.UpdateDistances();
    o.endTangent = endDir;
    o.curveDist = o.curveOverrun;
}

void ObjectList::UpdateMovement(GameObject& o, float elapsed) {
    if (o.route.empty()) {
        RouteEnd(o, false);
        return;
    }
    if (o.curveDist >= o.curveLength) {
        if (o.routeLastLeg) {
            const RouteNode last = o.route.back();
            o.routeLastLeg = false;
            RouteEnd(o, true);
            if (last.action != RouteAction::None) StartAction(o, last.action, last.block, o.stateTarget);
            return;
        }
        bool skip = false;
        if (o.route.size() < 2) {
            o.routeLastLeg = true;
            o.route[size_t(o.routeIndex)].flags |= RouteFlag::Last;
        } else {
            ++o.routeIndex;
            const RouteNode& n = o.route[size_t(o.routeIndex)];
            skip = (n.flags & RouteFlag::CutCorner) && n.action != 1;
        }
        BuildRouteCurve(o, skip);
        if (size_t(o.routeIndex) + 1 >= o.route.size()) o.routeLastLeg = true;
    }

    const ObjectSpeeds sp = CalculateSpeeds(o, elapsed);
    o.curveDist += sp.route;
    if (o.curveDist > o.curveLength) {
        o.curveOverrun = o.curveDist - o.curveLength;
        o.curveDist = o.curveLength;
    }
    Vec2 p;
    o.curve.Interpolate(o.curveDist, p);
    // TODO(collision): bumping into other objects (LegoObject_DoCollisionCallbacks).
    // LegoObject_UpdateRoutingVectors: face along the step just taken.
    const Vec2 step{p.x - o.position.x, p.y - o.position.y};
    if (step.x != 0.0f || step.y != 0.0f) {
        const float m = std::sqrt(step.x * step.x + step.y * step.y);
        o.Face({step.x / m, step.y / m});
    }
    o.position.x = p.x;
    o.position.y = p.y;
    o.position.z = level_->map().WorldZ(p.x, p.y);

    // Heading into a wall to drill: stop once the drill reaches the wall block.
    // TODO(drilling): use the model's drill null instead of a reach from the collision radius.
    const RouteNode& last = o.route.back();
    if (last.action == RouteAction::Drill && size_t(o.routeIndex) + 1 >= o.route.size()) {
        const float reach = (o.stats ? o.stats->Num("CollRadius", o.level, 5.0f) : 5.0f) * 1.5f;
        BlockPos tip;
        if (level_->map().WorldToBlock(p.x + o.faceDir.x * reach, p.y + o.faceDir.y * reach, tip.x, tip.y) &&
            tip == last.block) {
            const BlockPos wall = last.block;
            RouteEnd(o, true);
            // Face the wall squarely while drilling.
            const Vec2 c = level_->map().BlockCentre(wall.x, wall.y);
            o.Face({c.x - o.position.x, c.y - o.position.y});
            StartAction(o, RouteAction::Drill, wall, nullptr);
        }
    }
}

void ObjectList::Draw(Renderer3D& r) const {
    LwsPose pose;
    for (const auto& op : objects_) {
        const GameObject& o = *op;
        if (o.removed || o.hidden || !o.info) continue;
        const ObjectModel& m = o.info->model;
        if (m.activities) {
            const Mat4 world = ObjectMatrix(o.position, o.heading, m.activities->scale);
            if (o.anim) {
                EvaluateLws(*o.anim->scene, o.animTime, o.anim->looping, pose);
                DrawPose(r, *o.anim->scene, pose, world);
            }
            if (o.anim2 && m.secondPart) {
                EvaluateLws(*o.anim2->scene, o.animTime2, o.anim2->looping, pose);
                DrawPose(r, *o.anim2->scene, pose, ObjectMatrix(o.position, o.heading, m.secondPart->scale));
            }
        } else if (m.scene) {
            EvaluateLws(*m.scene, o.animTime, true, pose);
            DrawPose(r, *m.scene, pose, ObjectMatrix(o.position, o.heading + o.spin));
        } else if (m.mesh) {
            DrawMesh(r, *m.mesh, ObjectMatrix(o.position, o.heading + o.spin));
        }
    }
}

void ObjectList::Clear() {
    objects_.clear();
    hidden_.clear();
}

GameObject* ObjectList::PlaceListedObject(const ObjectTypeInfo& info, float x, float y, float headingDeg) {
    GameObject* o = Create(info);
    if (info.type == ObjType::Building) {
        // Lego_LoadOLObjectList: the heading in eighths of a turn, rounded up to quarters.
        const int eighths = int(headingDeg * (1.0f / 180.0f) * kPi * (1.0f / (2.0f * kPi)) * 8.0f);
        const int dir = ((eighths & 1) + eighths) >> 1;
        BlockPos b;
        level_->map().WorldToBlock(x, y, b.x, b.y);
        PlaceBuilding(*o, b, dir);
    } else {
        SetPositionAndHeading(*o, x, y, headingDeg * (1.0f / 180.0f) * kPi);
    }
    return o;
}

void ObjectList::PlaceBuilding(GameObject& o, BlockPos origin, int dir) {
    // TODO(construction): flatten the ground under the footprint (Map3D_FlattenShapeVertices).
    const Vec2 c = level_->map().BlockCentre(origin.x, origin.y);
    SetPositionAndHeading(o, c.x, c.y, float(dir) * kPi * 0.5f);
    if (!o.info) return;
    RR_DEBUG("%s placed at block %d,%d facing %d", o.info->name.c_str(), origin.x, origin.y, dir);
    const std::vector<BlockPos> blocks = BuildingFootprint(*o.info, origin, dir);
    // Construction_Zone_CompleteBuilding: a repeated point is the path tile; the rest is the
    // building (walkable for units allowed into tool stores, solid otherwise).
    const bool toolStore = o.stats && (o.stats->F1(o.level) & StatsFlag1::ToolStore);
    const bool power = o.stats && (o.stats->F2(o.level) & StatsFlag2::PowerBuilding);
    for (size_t i = 0; i < blocks.size(); ++i) {
        if (i + 1 < blocks.size() && blocks[i] == blocks[i + 1]) {
            level_->SetBuildingBlock(blocks[i].x, blocks[i].y, world::Level::BuildingBlock::Path, false);
            ++i;
            continue;
        }
        level_->SetBuildingBlock(blocks[i].x, blocks[i].y,
                                 toolStore ? world::Level::BuildingBlock::ToolStore : world::Level::BuildingBlock::Solid,
                                 power);
    }
}

std::vector<GameObject*> ObjectList::ExposeBlock(int bx, int by) {
    std::vector<GameObject*> created;
    for (size_t i = 0; i < hidden_.size();) {
        const HiddenObject& h = hidden_[i];
        BlockPos b;
        if (level_->map().WorldToBlock(h.position.x, h.position.y, b.x, b.y) && b.x == bx && b.y == by) {
            GameObject* o = PlaceListedObject(*h.info, h.position.x, h.position.y, h.heading);
            o->health = h.health;
            o->customName = h.name;
            created.push_back(o);
            hidden_.erase(hidden_.begin() + std::ptrdiff_t(i));
        } else {
            ++i;
        }
    }
    return created;
}

bool ObjectList::LoadObjectList(const std::string& file, world::Level& level, CameraStart& camera) {
    level_ = &level;
    Config ol;
    if (!ol.Load(file)) return false;
    const world::Map3D& map = level.map();
    const float bs = map.blockSize();
    for (Config::ItemRef it = ol.FindArray("Lego*"); it != Config::kNone; it = ol.Next(it)) {
        const std::string key = ol.item(it).key;
        const std::string base = Config::Id("Lego*", key);
        const auto typeName = ol.GetString(Config::Id(base, "type"));
        // Every entry needs type, xPos, yPos and heading; a broken entry ends the list.
        if (!typeName || ol.Find(Config::Id(base, "xPos")) == Config::kNone ||
            ol.Find(Config::Id(base, "yPos")) == Config::kNone || ol.Find(Config::Id(base, "heading")) == Config::kNone)
            return false;
        const float bx = float(std::atof(ol.GetString(Config::Id(base, "xPos"), "").c_str())) - 1.0f;
        const float by = float(std::atof(ol.GetString(Config::Id(base, "yPos"), "").c_str())) - 1.0f;
        const float headingDeg = float(std::atof(ol.GetString(Config::Id(base, "heading"), "").c_str()));
        float health = float(std::atof(ol.GetString(Config::Id(base, "health"), "").c_str()));
        if (health == 0.0f) health = 100.0f;
        const std::string driving = ol.GetString(Config::Id(base, "driving"), "");

        // Block coordinates (1-based, fractional) to world: centre of the block plus the offset.
        const int ux = int(bx), uy = int(by);
        const Vec2 centre = map.BlockCentre(ux, uy);
        const float x = centre.x + (bx - (float(ux) + 0.5f)) * bs;
        const float y = centre.y - (by - (float(uy) + 0.5f)) * bs;
        const float heading = headingDeg * (1.0f / 180.0f) * kPi;

        const ObjectTypeInfo* info = GetObjectRegistry().Find(*typeName);
        if (!info) {
            RR_WARN("Object list %s: unknown type %s", file.c_str(), std::string(*typeName).c_str());
            return false;
        }
        if (info->type == ObjType::TVCamera) {
            camera.found = true;
            camera.x = x;
            camera.y = y;
            camera.heading = heading;
            continue;
        }
        const bool exposed = ux < level.width() - 1 && uy < level.height() - 1 &&
                             (level.block(ux, uy).flags & world::BlockFlag::Ground);
        if (!exposed) {
            hidden_.push_back({info, {x, y}, headingDeg, health, key, driving});
            continue;
        }
        GameObject* o = PlaceListedObject(*info, x, y, headingDeg);
        o->health = health;
        o->customName = key;
        // TODO(ai): crystals/ore/dynamite/barriers get a collect task; drivers board "driving".
    }
    RR_INFO("Object list %s: %zu objects, %zu hidden", file.c_str(), objects_.size(), hidden_.size());
    return true;
}

}  // namespace rr
