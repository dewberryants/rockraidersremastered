#include "game/objects/GameObject.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <unordered_set>

#include "config/Config.h"
#include "core/Log.h"
#include "core/Random.h"
#include "fs/Path.h"
#include "game/Construction.h"
#include "game/objects/ObjectRegistry.h"
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

constexpr int kReinforceHits = 4;  // Lego.cfg Main::ReinforceHits (not set in the shipped config)

// Whether frame f was passed going from t0 to t1 (every loop for looping animations).
bool FramePassed(float f, float t0, float t1, bool loop, float frames) {
    if (t1 <= t0) return false;
    if (loop && frames > 0.0f) return std::floor((t1 - f) / frames) > std::floor((t0 - f) / frames);
    return t0 < f && f <= t1;
}

void PlaySfxAt(std::string_view name, const Vec3f& at) {
    int id = 0;
    if (GetSfx().GetType(name, id)) GetSfx().PlayAt(id, Vec3{at.x, at.y, at.z});
}

// Put-down sound of a carried object: SFX_PlaceOre, SFX_PlaceCrystal or SFX_Place.
void PlacedSound(const GameObject& item) {
    PlaySfxAt(item.type == ObjType::Ore            ? "SFX_PlaceOre"
              : item.type == ObjType::PowerCrystal ? "SFX_PlaceCrystal"
                                                   : "SFX_Place",
              item.position);
}

// Logs a debug message the first time it comes up (for problems found while drawing).
void DebugOnce(const std::string& msg) {
    static std::unordered_set<std::string> seen;
    if (seen.insert(msg).second) RR_DEBUG("%s", msg.c_str());
}

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

bool GameObject::PlayNamedAnim(std::string_view name) {
    if (!info || !info->model.activities) return false;
    const Activity* a = info->model.activities->Find(name);
    if (!a) return false;
    anim = a;
    animTime = 0.0f;
    animFresh = true;
    return true;
}

void GameObject::Face(Vec2 dir) {
    if (dir.x == 0.0f && dir.y == 0.0f) return;
    const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
    faceDir = {dir.x / len, dir.y / len};
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

GameObject* ObjectList::PickObject(const Vec3f& origin, const Vec3f& dir, bool (*accept)(const GameObject&)) const {
    GameObject* best = nullptr;
    float bestT = 0.0f;
    for (const auto& op : objects_) {
        GameObject& o = *op;
        if (o.removed || o.hidden || !o.stats || (accept && !accept(o))) continue;
        const float radius = o.stats->Num("PickSphere", o.level);
        if (radius <= 0.0f) continue;
        // TODO(buildings): one sphere per block of the building's shape.
        const Vec3f c = PickCentre(o);
        const Vec3f oc = c - origin;
        const float t = Dot(oc, dir);
        if (t <= 0.0f) continue;
        const Vec3f closest = origin + dir * t;
        if (Length(c - closest) > radius) continue;
        // Units win over the large spheres of buildings they stand beside.
        const bool building = o.type == ObjType::Building;
        if (best && building && best->type != ObjType::Building) continue;
        if (!best || t < bestT || (!building && best->type == ObjType::Building)) {
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
    if (!o.anim) o.PlayNamedAnim("Normal");  // dynamite and other named-activity objects
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
        if (o.type == ObjType::MiniFigure) o.trainTimer += elapsed;
        if (o.driving) {
            // A seated driver only plays its drive animation: the AI and routing leave it alone
            // (flag 0x8 of flags2) and the vehicle sets its activity and position.
            o.MoveAnimation(elapsed, elapsed);
            continue;
        }
        if (o.type == ObjType::Vehicle) {
            // LegoObject_Route_UpdateMovement resets a vehicle's timer while it moves.
            o.stillTime = o.routing ? 0.0f : o.stillTime + elapsed;
        }
        const ActivityType lastActivity = o.activity;
        o.UpdateActivityChange();
        if (o.driver && o.activity != lastActivity) SetDriverAnim(o);
        const Activity* before = o.anim;
        const bool fresh = o.animFresh;
        const float t0 = fresh ? -0.001f : o.animTime;  // a new animation also passes frame 0
        o.animFresh = false;
        o.MoveAnimation(elapsed, CalculateSpeeds(o, elapsed).anim);
        o.triggered = false;
        if (o.anim && o.anim == before) {
            const float frames = o.anim->scene ? float(o.anim->scene->frameCount()) : 0.0f;
            if (o.anim->scene && !o.hidden) PlaySceneSounds(*o.anim->scene, o.anim->looping, t0, o.animTime, o.position);
            // Container_SetAnimationTime: a new activity plays its SAMPLE once.
            if (fresh && !o.hidden && !o.anim->sample.empty()) PlaySfxAt(o.anim->sample, o.position);
            // LegoObject_TriggerFrameCallback: the activity's TRIGGER frame was passed.
            o.triggered = o.anim->trigger > 0 &&
                          FramePassed(float(o.anim->trigger), t0, o.animTime, o.anim->looping, frames);
        }
        // LegoObject_SimpleObject_MoveAnimation: lit dynamite goes off when TickDown ends.
        if (o.type == ObjType::Dynamite && o.ticking && o.anim && o.anim->scene &&
            o.animTime >= float(o.anim->scene->frameCount())) {
            o.ticking = false;
            if (onExplode) onExplode(o);
            continue;
        }
        if (!o.teleportingIn) UpdateEngineSound(o);
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
    for (auto& op : objects_)
        if (op->type == ObjType::Vehicle && !op->removed) UpdateVehicleTilt(*op);
    for (auto& op : objects_)
        if (op->driver && !op->removed) UpdateDriverPosition(*op);
    // LegoObject_UpdateCarrying: carried objects sit on their carrier's carry null.
    for (auto& op : objects_) {
        GameObject& o = *op;
        if (!o.carrier) continue;
        const GameObject& c = *o.carrier;
        Vec2 dir;
        const ObjectModel* cm = c.info ? &c.info->model : nullptr;
        // Vehicles hold each object on carry null CarryStart + slot.
        const bool vehicle = c.type == ObjType::Vehicle;
        const int instance = vehicle ? int(c.stats ? c.stats->Num("CarryStart", c.level) : 0.0f) + o.carrySlot : 0;
        if (cm && cm->activities && NullPoint(c, cm->activities->carryNull, o.position, dir, instance, vehicle)) {
            o.heading = std::atan2(dir.x, dir.y);
        } else {
            o.position = {c.position.x + c.faceDir.x * 4.0f, c.position.y + c.faceDir.y * 4.0f, c.position.z - 8.0f};
            o.heading = c.heading;
        }
    }
}

bool ObjectList::NullPoint(const GameObject& c, const std::string& nullName, Vec3f& pos, Vec2& dir,
                           int instance, bool anyActivity) const {
    const ObjectModel* m = c.info ? &c.info->model : nullptr;
    if (!m || !m->activities || nullName.empty()) return false;
    auto inScene = [&](const Activity* a, float time, float scale) {
        if (!a || !a->scene) return false;
        const LwsScene& scene = *a->scene;
        size_t node = scene.nodes.size();
        int skip = instance;
        for (size_t i = 0; i < scene.nodes.size(); ++i)
            if (EqualsNoCase(scene.nodes[i].name, nullName) && skip-- == 0) {
                node = i;
                break;
            }
        if (node == scene.nodes.size()) return false;
        static LwsPose pose;
        EvaluateLws(scene, time, a->looping, pose);
        if (node >= pose.world.size()) return false;
        const Mat4 w = WorldMatrix(c, scale) * pose.world[node];
        pos = w.TransformPoint({0, 0, 0});
        const Vec3f fwd = w.TransformDir({0, 0, 1});
        dir = (fwd.x != 0.0f || fwd.y != 0.0f) ? Vec2{fwd.x, fwd.y} : c.faceDir;
        return true;
    };
    if (inScene(c.anim, c.animTime, m->activities->scale)) return true;
    if (!anyActivity) return false;
    // Vehicle_FindNull searches the whole activity container: a null missing from the playing
    // animation is found in another one (the drill tip only exists in the drill animation).
    if (m->secondPart && inScene(c.anim2, c.animTime2, m->secondPart->scale)) return true;
    for (const ActivityModel* am : {m->activities.get(), m->secondPart.get()}) {
        if (!am) continue;
        for (const auto& [name, a] : am->activities)
            if (inScene(a.get(), 0.0f, am->scale)) return true;
    }
    return false;
}

bool ObjectList::CanCollect(const GameObject& v) {
    if (v.type != ObjType::Vehicle || !v.stats || !(v.stats->F1(v.level) & StatsFlag1::CrossLand)) return false;
    const ActivityModel* am = v.info ? v.info->model.activities.get() : nullptr;
    return am && !am->carryNull.empty();
}

int ObjectList::CargoCapacity(const GameObject& v) {
    const ActivityModel* am = v.info ? v.info->model.activities.get() : nullptr;
    const int maxCarry = v.stats ? int(v.stats->Num("MaxCarry", v.level)) : 0;
    return std::min(maxCarry, am ? am->carryNullFrames : 0);
}

bool ObjectList::LoadReady(const GameObject& v) {
    if (v.cargo.empty()) return false;
    return int(v.cargo.size()) >= CargoCapacity(v) || v.stillTime >= 75.0f;
}

bool ObjectList::CanDig(const GameObject& o) {
    // LegoObject_Create: vehicles dig if their model names a drill null (flag 0x2).
    if (o.type != ObjType::Vehicle) return true;
    const ObjectModel* m = o.info ? &o.info->model : nullptr;
    if (!m) return false;
    for (const ActivityModel* am : {m->activities.get(), m->secondPart.get()})
        if (am && !am->drillNull.empty()) return true;
    return false;
}

void ObjectList::PlaySceneSounds(const LwsScene& scene, bool loop, float t0, float t1, const Vec3f& at) {
    if (scene.sounds.empty() || t1 <= t0) return;
    const float frames = float(scene.frameCount());
    for (const LwsSound& s : scene.sounds) {
        if (!FramePassed(float(s.start), t0, t1, loop, frames)) continue;
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
    case RouteAction::Reinforce:
        // Four hammer blows (Lego.cfg ReinforceHits, default 4).
        o.state = UnitState::Reinforcing;
        o.SetActivity(ActivityType::Reinforce, kReinforceHits);
        break;
    case RouteAction::PlaceDynamite:
        o.state = UnitState::PlacingDynamite;
        o.SetActivity(ActivityType::Place);
        break;
    case RouteAction::Train:
        // Arrival flag 0x400: Activity_Train plays once; the skill is granted at its end.
        o.state = UnitState::Training;
        o.SetActivity(ActivityType::Train);
        break;
    case RouteAction::Clear:
        o.state = UnitState::Clearing;
        // Minifigures sweep the rubble twice per layer.
        o.SetActivity(ActivityType::Clear, o.type == ObjType::MiniFigure ? 2 : 0);
        break;
    case RouteAction::Collect:
        if (o.type == ObjType::Vehicle) {
            // Vehicles have no pick-up animation: the object goes onto the next carry slot.
            GameObject* t = target;
            const bool ok = t && !t->removed && !t->carrier && int(o.cargo.size()) < CargoCapacity(o);
            if (ok) {
                t->carrier = &o;
                t->carrySlot = int(o.cargo.size());
                o.cargo.push_back(t);
            }
            FinishAction(o, ok);
            break;
        }
        o.state = UnitState::Collecting;
        o.SetActivity(ActivityType::Collect);
        break;
    case RouteAction::Deposit:
        if (o.type == ObjType::Vehicle) {
            // LegoObject_ProccessCarriedObjects: the whole load is handed over at once.
            GameObject* building = target;
            const std::vector<GameObject*> load = std::move(o.cargo);
            o.cargo.clear();
            for (GameObject* item : load) {
                item->carrier = nullptr;
                if (building && onDeposit) onDeposit(o, *item, *building);
                Remove(*item);
            }
            FinishAction(o, building != nullptr);
            break;
        }
        // Buildings that store objects (StoreObjects) are walked into and the load put down
        // (route action 9, Activity_Place); others have it thrown in (action 7, Activity_Deposit).
        o.state = UnitState::Depositing;
        o.SetActivity(target && target->stats && (target->stats->F1(target->level) & StatsFlag1::StoreObjects)
                          ? ActivityType::Place
                          : ActivityType::Deposit);
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
    case RouteAction::GetIn: {
        // Arrived at the vehicle: in at once if the seat is still free.
        GameObject* v = target;
        const bool free = v && !v->removed && !v->driver && !o.driving;
        FinishAction(o, free);
        if (free) Board(o, *v);
        break;
    }
    case RouteAction::BuildPath:
        // LegoObject_TryBuildPath: the path is swept into place with the clearing animation.
        o.state = UnitState::BuildingPath;
        o.SetActivity(ActivityType::Clear);
        break;
    default: FinishAction(o, true); break;
    }
}

namespace {
// Stats sound names ("Drillsound SND_pilotdrill") to sample ids.
// Stats_Initialise: a missing or unknown DrillSound / DrillFadeSound falls back to SFX_Drill /
// SFX_DrillFade; other sounds to none.
int StatSound(const GameObject& o, std::string_view key) {
    int id = 0;
    if (!o.stats) return -1;
    const std::string name = o.stats->Str(key, o.level);
    if (!name.empty() && GetSfx().GetType(name, id)) return id;
    if (EqualsNoCase(key, "Drillsound")) return SFX_Drill;
    if (EqualsNoCase(key, "Drillfadesound")) return SFX_DrillFade;
    return -1;
}
}  // namespace

void ObjectList::FinishAction(GameObject& o, bool success) {
    if (o.drillSound >= 0) {
        // Drilling stopped: the loop is cut and the drill winds down.
        GetSfx().Stop(o.drillSound);
        o.drillSound = -1;
        const int fade = StatSound(o, "Drillfadesound");
        if (fade >= 0) GetSfx().PlayAt(fade, Vec3{o.position.x, o.position.y, o.position.z});
    }
    RR_DEBUG("%s finished action (state %d): %s", o.info->name.c_str(), int(o.state), success ? "ok" : "failed");
    o.state = UnitState::Idle;
    o.stateTarget = nullptr;
    o.SetActivity(o.carried ? ActivityType::CarryStand : ActivityType::Stand);
    if (onActionDone) onActionDone(o, success);
}

void ObjectList::Remove(GameObject& o) {
    o.removed = true;
    if (o.carrier) {
        GameObject& c = *o.carrier;
        if (c.carried == &o) c.carried = nullptr;
        // A vehicle's remaining load closes up its slots.
        c.cargo.erase(std::remove(c.cargo.begin(), c.cargo.end(), &o), c.cargo.end());
        for (size_t i = 0; i < c.cargo.size(); ++i) c.cargo[i]->carrySlot = int(i);
    }
    if (o.carried) o.carried->carrier = nullptr;
    for (GameObject* item : o.cargo) {
        // A removed vehicle leaves its load on the ground.
        item->carrier = nullptr;
        SetPositionAndHeading(*item, item->position.x, item->position.y, item->heading);
    }
    o.cargo.clear();
    o.carrier = o.carried = nullptr;
    // LegoObject_UpdateRemoval: a vehicle and its driver let go of each other.
    if (o.driver) o.driver->driving = nullptr;
    if (o.driving) o.driving->driver = nullptr;
    o.driver = o.driving = nullptr;
    // LegoObject_Remove: the engine loop stops.
    if (o.engineSound >= 0 && GetSoundBank().VoiceOf(o.engineSound) == o.engineVoice) GetSfx().Stop(o.engineSound);
    o.engineSound = -1;
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
        if (instantDrill) time = 0.01f;
        if (o.drillSound < 0) {
            const int id = StatSound(o, "Drillsound");
            if (id >= 0) o.drillSound = GetSfx().PlayAt(id, Vec3{o.position.x, o.position.y, o.position.z}, true);
        }
        // Level_Block_Damage: progress by elapsed over the drill time; the wall breaks past 1.
        // TODO(drilling): drill sounds, single-width tunnels (Level_DestroyWallConnection).
        b.damage += elapsed / time;
        // Seams take four rounds: the wall only goes once the last stage is drilled.
        if (b.damage > 1.0f && level.DestroyWall(o.stateBlock.x, o.stateBlock.y, false)) FinishAction(o, true);
        break;
    }
    case UnitState::Reinforcing:
        // LegoObject_Update_Reinforcing: done once the blows have played out.
        if (!level.CanReinforce(o.stateBlock.x, o.stateBlock.y)) {
            FinishAction(o, false);  // TODO(ai): the game shrugs (Activity_CantDo) first
        } else if (o.animOverrun > 0.0f) {
            level.ReinforceBlock(o.stateBlock.x, o.stateBlock.y);
            FinishAction(o, true);
        }
        break;
    case UnitState::Training:
        if (o.animOverrun > 0.0f) FinishAction(o, true);
        break;
    case UnitState::PlacingDynamite:
        // The dynamite is set down before the wall and lit (TickDown).
        if (o.triggered && o.carried) PlacedSound(*o.carried);
        if (o.animOverrun > 0.0f) {
            GameObject* item = o.carried;
            if (!item || item->type != ObjType::Dynamite) {
                FinishAction(o, false);
                break;
            }
            o.carried = nullptr;
            item->carrier = nullptr;
            item->position = {o.position.x + o.faceDir.x * 6.0f, o.position.y + o.faceDir.y * 6.0f, 0.0f};
            item->position.z = level.map().WorldZ(item->position.x, item->position.y);
            item->heading = o.heading;
            item->ticking = true;
            item->dynamiteTarget = o.stateBlock;
            item->PlayNamedAnim("TickDown");
            FinishAction(o, true);
        }
        break;
    case UnitState::Clearing:
        if (o.animOverrun > 0.0f) {
            // AITask_LiveObject_SetAITaskUnk: a vehicle's one pass clears every layer at once.
            bool clean = level.ClearRubbleLayer(o.stateBlock.x, o.stateBlock.y);
            while (o.type == ObjType::Vehicle && !clean) clean = level.ClearRubbleLayer(o.stateBlock.x, o.stateBlock.y);
            if (clean) {
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
        if (o.triggered && o.carried && o.activity == ActivityType::Place) PlacedSound(*o.carried);
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
        if (o.triggered && o.carried) PlacedSound(*o.carried);
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
    if (node.flags & RouteFlag::Face) {
        static const Vec2 kFace[4] = {{0, 1}, {1, 0}, {0, -1}, {-1, 0}};
        endDir = kFace[node.flags & RouteFlag::FaceDirMask];
    } else if ((node.flags & RouteFlag::Last) && (offset.x != 0.0f || offset.y != 0.0f)) {
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

    // Heading into a wall to drill or reinforce it: stop once the unit reaches the wall block.
    // Vehicles stop when their drill tip is in the wall (LegoObject_GetDrillNullPosition).
    // TODO(drilling): minifigures by their drill null too, instead of a reach from the collision radius.
    const RouteNode& last = o.route.back();
    if ((last.action == RouteAction::Drill || last.action == RouteAction::Reinforce ||
         last.action == RouteAction::PlaceDynamite) &&
        size_t(o.routeIndex) + 1 >= o.route.size()) {
        const float reach = (o.stats ? o.stats->Num("CollRadius", o.level, 5.0f) : 5.0f) * 1.5f;
        Vec2 probe{p.x + o.faceDir.x * reach, p.y + o.faceDir.y * reach};
        if (o.type == ObjType::Vehicle) {
            const ObjectModel& m = o.info->model;
            const ActivityModel* am = m.activities && !m.activities->drillNull.empty() ? m.activities.get()
                                                                                        : m.secondPart.get();
            Vec3f tipPos;
            Vec2 tipDir;
            if (am && NullPoint(o, am->drillNull, tipPos, tipDir, 0, true)) probe = {tipPos.x, tipPos.y};
        }
        BlockPos tip;
        if (level_->map().WorldToBlock(probe.x, probe.y, tip.x, tip.y) && tip == last.block) {
            const BlockPos wall = last.block;
            const uint8_t action = last.action;
            RouteEnd(o, true);
            // Face the wall squarely while working on it.
            const Vec2 c = level_->map().BlockCentre(wall.x, wall.y);
            o.Face({c.x - o.position.x, c.y - o.position.y});
            StartAction(o, action, wall, nullptr);
        }
    }
}

void ObjectList::Draw(Renderer3D& r) const {
    LwsPose pose;
    for (const auto& op : objects_) {
        const GameObject& o = *op;
        if (o.removed || o.hidden || !o.info) continue;
        const ObjectModel& m = o.info->model;
        const Vec3f* tint = nullptr;
        if (o.type == ObjType::PowerCrystal) tint = o.crystalDrained ? &drainedCrystalColour : &crystalColour;
        if (m.activities) {
            const Mat4 world = WorldMatrix(o, m.activities->scale);
            if (o.anim) {
                EvaluateLws(*o.anim->scene, o.animTime, o.anim->looping, pose);
                DrawPose(r, *o.anim->scene, pose, world);
                if (o.type == ObjType::Vehicle || o.type == ObjType::Building)
                    DrawUpgradeParts(r, o, *o.anim->scene, pose, world);
            }
            if (o.anim2 && m.secondPart) {
                EvaluateLws(*o.anim2->scene, o.animTime2, o.anim2->looping, pose);
                DrawPose(r, *o.anim2->scene, pose, WorldMatrix(o, m.secondPart->scale));
            }
        } else if (m.scene) {
            EvaluateLws(*m.scene, o.animTime, true, pose);
            DrawPose(r, *m.scene, pose, ObjectMatrix(o.position, o.heading + o.spin), tint);
        } else if (m.mesh) {
            DrawMesh(r, *m.mesh, ObjectMatrix(o.position, o.heading + o.spin), 0.0f, tint);
        }
    }
}

void ObjectList::DrawUpgradeParts(Renderer3D& r, const GameObject& o, const LwsScene& scene, const LwsPose& pose,
                                  const Mat4& world) const {
    const ActivityModel& am = *o.info->model.activities;
    // The world matrix of the n-th node with a name (Container_FormatPartName + SearchTree).
    auto nodeWorld = [&](const std::string& name, int instance, Mat4& out) {
        int seen = 0;
        for (size_t i = 0; i < scene.nodes.size() && i < pose.world.size(); ++i) {
            if (!EqualsNoCase(scene.nodes[i].name, name)) continue;
            if (seen++ == instance) {
                out = world * pose.world[i];
                return true;
            }
        }
        return false;
    };
    // Vehicle_PopulateWheelNulls: up to six wheels, turning with the distance driven.
    if (am.wheelMesh && !am.wheelNull.empty()) {
        for (int w = 0; w < 6; ++w) {
            Mat4 mw;
            if (!nodeWorld(am.wheelNull, w, mw)) break;
            mw.m[14] += o.wheelDrop[w];  // resting on the floor
            Mat4 spin;
            const float c = std::cos(o.wheelSpin[w]), s = std::sin(o.wheelSpin[w]);
            spin.m[5] = c; spin.m[6] = s; spin.m[9] = -s; spin.m[10] = c;
            DrawMesh(r, *am.wheelMesh, mw * spin);
        }
    }
    // Upgrade_SetUpgradeLevel: the parts of exactly the current level on their nulls (vehicles:
    // the upgrade bits, buildings: the object level).
    // TODO(weapons): the fire and pivot nulls of weapon parts (Mining Laser turret aiming).
    const uint32_t partLevel = o.type == ObjType::Building ? uint32_t(o.level) : o.upgradeLevel;
    for (const ActivityModel::UpgradePart& p : am.upgrades) {
        if (p.levels != partLevel) continue;
        Mat4 mp;
        if (!nodeWorld(p.nullName, p.instance, mp) && !nodeWorld(p.nullName, 0, mp)) {
            DebugOnce(o.info->name + ": no null " + p.nullName + " for part " + p.part);
            continue;
        }
        const ObjectTypeInfo* part = GetObjectRegistry().FindUpgradePart(p.part);
        if (!part) {
            DebugOnce(o.info->name + ": unknown upgrade part " + p.part);
            continue;
        }
        const ObjectModel& pm = part->model;
        if (pm.mesh) {
            DrawMesh(r, *pm.mesh, mp);
        } else if (pm.activities) {
            // Vehicle_SetUpgradeActivity: parts play the vehicle's activity if they have it.
            const Activity* st = pm.activities->Find(ActivityName(o.activity));
            if (!st) st = pm.activities->Find("Activity_Stand");
            if (st && st->scene) {
                LwsPose pp;
                EvaluateLws(*st->scene, o.animTime, true, pp);
                DrawPose(r, *st->scene, pp, mp);
            }
        } else if (pm.scene) {
            LwsPose pp;
            EvaluateLws(*pm.scene, o.animTime, true, pp);
            DrawPose(r, *pm.scene, pp, mp);
        }
    }
}

void ObjectList::UpdateEngineSound(GameObject& o) {
    if (o.type != ObjType::Vehicle && o.type != ObjType::Building) return;
    const Vec3 at{o.position.x, o.position.y, o.position.z};
    if (!o.engineStarted) {
        // LegoObject_FUN_00438720: once a vehicle or building has arrived, its EngineSound loops
        // on it until it is removed, driven or not. Samples have one buffer, so the latest
        // object with the same sound takes it over.
        o.engineStarted = true;
        const int id = StatSound(o, "EngineSound");
        if (id < 0) return;
        o.engineSound = GetSfx().PlayAt(id, at, true);
        o.engineVoice = GetSoundBank().VoiceOf(o.engineSound);
        return;
    }
    if (o.engineSound < 0) return;
    if (GetSoundBank().VoiceOf(o.engineSound) != o.engineVoice) {
        o.engineSound = -1;  // another object restarted the buffer
        return;
    }
    GetSoundBank().SetPlayPosition(o.engineSound, at);
}

Mat4 ObjectList::WorldMatrix(const GameObject& o, float scale) const {
    // LegoObject_UpdateDriverStickyPosition copies the vehicle's whole orientation to its driver.
    const GameObject* tiltOf = o.tilted ? &o : (o.driving && o.driving->tilted ? o.driving : nullptr);
    if (!tiltOf) return ObjectMatrix(o.position, o.heading, scale);
    const Vec3f n = tiltOf->tiltUp;
    Vec3f f{std::sin(o.heading), std::cos(o.heading), 0.0f};
    f = f - n * Dot(f, n);
    const float len = Length(f);
    if (len < 1e-4f) return ObjectMatrix(o.position, o.heading, scale);
    return ObjectMatrix(o.position, f * (1.0f / len), n, scale);
}

void ObjectList::UpdateVehicleTilt(GameObject& v) {
    v.tilted = false;
    for (float& d : v.wheelDrop) d = 0.0f;
    // Another animation may list its wheel nulls in another order: start tracking afresh.
    const int tracked = v.wheelAnim == v.anim ? v.wheelsTracked : 0;
    v.wheelAnim = v.anim;
    v.wheelsTracked = 0;  // stays 0 unless the wheels are found again below
    const ActivityModel* am = v.info ? v.info->model.activities.get() : nullptr;
    if (!level_ || !am || am->wheelNull.empty() || !v.anim || !v.anim->scene) return;
    // The wheel nulls of the playing animation with the vehicle upright (Vehicle_PopulateWheelNulls).
    const LwsScene& scene = *v.anim->scene;
    static LwsPose pose;
    EvaluateLws(scene, v.animTime, v.anim->looping, pose);
    const Mat4 upright = ObjectMatrix(v.position, v.heading, am->scale);
    std::vector<size_t> wheels;
    for (size_t i = 0; i < scene.nodes.size() && i < pose.world.size() && wheels.size() < 6; ++i)
        if (EqualsNoCase(scene.nodes[i].name, am->wheelNull)) wheels.push_back(i);
    if (wheels.size() < 3) return;
    const world::Map3D& map = level_->map();
    Vec3f p[4];
    for (size_t i = 0; i < 4 && i < wheels.size(); ++i) {
        const Vec3f w = (upright * pose.world[wheels[i]]).TransformPoint({0, 0, 0});
        p[i] = {w.x, w.y, map.WorldZ(w.x, w.y)};
    }
    // Two triangles of wheels give the ground normal (+Z is down here).
    Vec3f n = Cross(p[1] - p[0], p[1] - p[2]);
    if (wheels.size() >= 4) {
        Vec3f n2 = Cross(p[3] - p[2], p[3] - p[0]);
        if (Length(n2) > 1e-6f && Length(n) > 1e-6f) n = n * (1.0f / Length(n)) + n2 * (1.0f / Length(n2));
    }
    if (Length(n) < 1e-6f) return;
    n = n * (1.0f / Length(n));
    if (n.z < 0.0f) n = n * -1.0f;  // wheel order decides the sign; the floor's normal points down
    // Smoothed against the last tick's normal (weight 0.4) so the body settles rather than snaps.
    if (Length(v.groundNormal) < 0.5f) v.groundNormal = n;
    n = n + v.groundNormal * 0.4f;
    n = n * (1.0f / Length(n));
    v.groundNormal = n;
    Vec3f f{std::sin(v.heading), std::cos(v.heading), 0.0f};
    f = f - n * Dot(f, n);
    if (Length(f) < 1e-4f) return;
    v.tiltDir = f * (1.0f / Length(f));
    v.tiltUp = n * -1.0f;
    v.tilted = true;
    // Each wheel is then moved down (or up) onto the floor below it, keeping the height its null
    // has above the floor when the vehicle stands upright on flat ground (axle, not tyre, on it).
    const Mat4 tilt = ObjectMatrix(v.position, v.tiltDir, v.tiltUp, am->scale);
    v.wheelsTracked = tracked;
    for (size_t i = 0; i < wheels.size(); ++i) {
        const float rest = v.position.z - (upright * pose.world[wheels[i]]).TransformPoint({0, 0, 0}).z;
        Vec3f w = (tilt * pose.world[wheels[i]]).TransformPoint({0, 0, 0});
        v.wheelDrop[i] = map.WorldZ(w.x, w.y) - rest - w.z;
        w.z += v.wheelDrop[i];
        // The wheel turns by how far it moved, backwards if against the vehicle's facing.
        if (int(i) < v.wheelsTracked && am->wheelRadius > 0.0f) {
            const Vec3f d = w - v.wheelLast[i];
            const float turn = Length(d) / am->wheelRadius;
            v.wheelSpin[i] += Dot(d, v.tiltDir) < 0.0f ? -turn : turn;
        }
        v.wheelLast[i] = w;
    }
    v.wheelsTracked = int(wheels.size());
}

void ObjectList::DropCarried(GameObject& o) {
    std::vector<GameObject*> items = std::move(o.cargo);
    o.cargo.clear();
    if (o.carried) items.insert(items.begin(), o.carried);
    if (items.empty()) return;
    // Work that needed the load stops: deliveries on the way or under way.
    if (o.routing && (o.route.back().action == RouteAction::Deposit || o.route.back().action == RouteAction::Place ||
                      o.route.back().action == RouteAction::PlaceDynamite))
        RouteEnd(o, false);
    if (o.state == UnitState::Depositing || o.state == UnitState::Placing || o.state == UnitState::PlacingDynamite)
        FinishAction(o, false);
    o.carried = nullptr;
    for (GameObject* item : items) {
        item->carrier = nullptr;
        BlockPos b;
        const bool open = level_ && BlockOf(*item, b) && (level_->block(b.x, b.y).flags & world::BlockFlag::Ground) &&
                          level_->block(b.x, b.y).terrain != world::Terrain::Lava &&
                          !(level_->block(b.x, b.y).flags & world::BlockFlag::Building);
        const Vec2 at = open ? Vec2{item->position.x, item->position.y} : Vec2{o.position.x, o.position.y};
        SetPositionAndHeading(*item, at.x, at.y, item->heading);
        if (onDrop) onDrop(o, *item);
    }
    o.deliverZone = 0;
    o.fetchType = ObjType::None;
    if (o.activity == ActivityType::CarryStand) o.SetActivity(ActivityType::Stand);
}

uint32_t ObjectList::DriverAbility(const GameObject& v) {
    if (!v.stats) return Ability::Driver;
    const uint32_t f1 = v.stats->F1(v.level);
    if (v.stats->F3(v.level) & StatsFlag3::NeedsPilot) return Ability::Pilot;
    if (!(f1 & StatsFlag1::CrossLand)) return Ability::Sailor;
    return (f1 & StatsFlag1::CrossWater) ? Ability::Pilot : Ability::Driver;
}

void ObjectList::Board(GameObject& unit, GameObject& v) {
    RR_DEBUG("%s gets into %s", unit.info->name.c_str(), v.info->name.c_str());
    RouteEnd(unit, false);
    unit.driving = &v;
    v.driver = &unit;
    // LegoObject_Callback_HideCertainObjects: drivers of InvisibleDriver vehicles are hidden.
    if (v.stats && (v.stats->F2(v.level) & StatsFlag2::InvisibleDriver)) unit.hidden = true;
    SetDriverAnim(v);
    UpdateDriverPosition(v);
    if (onBoard) onBoard(unit, v);
}

bool ObjectList::GetOut(GameObject& v) {
    GameObject* d = v.driver;
    if (!d || !level_ || !v.stats || !(v.stats->F1(v.level) & StatsFlag1::CanBeDriven)) return false;
    Vec3f p = v.position;
    Vec2 dir = v.faceDir;
    const ActivityModel* am = v.info ? v.info->model.activities.get() : nullptr;
    if (am) NullPoint(v, am->depositNull, p, dir);
    BlockPos b;
    if (!level_->map().WorldToBlock(p.x, p.y, b.x, b.y)) return false;
    // TODO(water): boats put their driver ashore on a land block beside them
    // (LegoObject_UnkGetTerrainGetOutAtLandBlock_FUN_0043a0d0) or head for a dock (AITask_DoDock).
    if (!(v.stats->F1(v.level) & StatsFlag1::CrossLand) ||
        GetCrossTerrainType(*level_, d, b.x, b.y, b.x, b.y, true) == CrossType::Blocked)
        return false;
    RR_DEBUG("%s gets out of %s", d->info->name.c_str(), v.info->name.c_str());
    v.driver = nullptr;
    d->driving = nullptr;
    d->hidden = false;
    SetPositionAndHeading(*d, p.x, p.y, d->heading);
    d->Face(dir);
    d->activity = ActivityType::Count;  // the drive animation gives way to Activity_Stand
    d->SetActivity(ActivityType::Stand);
    d->UpdateActivityChange();
    return true;
}

void ObjectList::SetDriverAnim(const GameObject& v) {
    if (!v.driver || !v.info) return;
    // Activity_Route plays "Activity_<type>"; the rest "<activity><type>" (Activity_StandSmallTruck).
    // The driver keeps its animation if it has none for the vehicle's activity.
    const ActivityType a = v.activity == ActivityType::Count ? ActivityType::Stand : v.activity;
    const std::string name = (a == ActivityType::Route ? std::string("Activity_") : std::string(ActivityName(a))) + v.info->name;
    if (!v.driver->PlayNamedAnim(name)) RR_DEBUG("Driver has no %s", name.c_str());
}

void ObjectList::UpdateDriverPosition(const GameObject& v) {
    GameObject& d = *v.driver;
    // Vehicle_GetDriverNull, or the vehicle itself for models without one (the hoverboard).
    const ActivityModel* am = v.info ? v.info->model.activities.get() : nullptr;
    Vec3f p;
    Vec2 dir;
    if (am && NullPoint(v, am->driverNull, p, dir)) {
        d.position = p;
        d.Face(dir);
    } else {
        d.position = v.position;
        d.heading = v.heading;
        d.faceDir = v.faceDir;
    }
}

void ObjectList::BoardListed(GameObject& unit, const std::string& vehicleName) {
    if (vehicleName.empty() || unit.type != ObjType::MiniFigure) return;
    for (auto& op : objects_) {
        GameObject& v = *op;
        if (v.removed || v.type != ObjType::Vehicle || v.driver || !EqualsNoCase(v.customName, vehicleName)) continue;
        // Lego_LoadOLObjectList: the driver is trained for the vehicle (LegoObject_TrainMiniFigure).
        unit.abilities |= DriverAbility(v);
        Board(unit, v);
        return;
    }
    RR_WARN("Object list: %s drives unknown vehicle %s", unit.customName.c_str(), vehicleName.c_str());
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
    std::vector<std::pair<GameObject*, std::string>> drivers;
    for (size_t i = 0; i < hidden_.size();) {
        const HiddenObject& h = hidden_[i];
        BlockPos b;
        if (level_->map().WorldToBlock(h.position.x, h.position.y, b.x, b.y) && b.x == bx && b.y == by) {
            GameObject* o = PlaceListedObject(*h.info, h.position.x, h.position.y, h.heading);
            o->health = h.health;
            o->customName = h.name;
            if (!h.driving.empty()) drivers.emplace_back(o, h.driving);
            created.push_back(o);
            hidden_.erase(hidden_.begin() + std::ptrdiff_t(i));
        } else {
            ++i;
        }
    }
    // HiddenObject_ExposeBlock: drivers board once everything on the block is out.
    for (auto& [unit, vehicle] : drivers) BoardListed(*unit, vehicle);
    return created;
}

bool ObjectList::LoadObjectList(const std::string& file, world::Level& level, CameraStart& camera) {
    level_ = &level;
    Config ol;
    if (!ol.Load(file)) return false;
    const world::Map3D& map = level.map();
    const float bs = map.blockSize();
    std::vector<std::pair<GameObject*, std::string>> drivers;
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
        // TODO(ai): crystals/ore/dynamite/barriers get a collect task.
        if (!driving.empty()) drivers.emplace_back(o, driving);
    }
    // Drivers get in once every listed object exists.
    for (auto& [unit, vehicle] : drivers) BoardListed(*unit, vehicle);
    RR_INFO("Object list %s: %zu objects, %zu hidden", file.c_str(), objects_.size(), hidden_.size());
    return true;
}

}  // namespace rr
