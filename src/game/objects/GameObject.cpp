#include "game/objects/GameObject.h"

#include <cmath>
#include <cstdlib>

#include "config/Config.h"
#include "core/Log.h"
#include "fs/Path.h"
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

void GameObject::MoveAnimation(float elapsed) {
    // LegoObject_CalculateSpeeds: unit animations play at route speed / TransCoef so walk cycles
    // match their movement; simple objects animate at game speed.
    float animSpeed = elapsed;
    if (!(flags3 & ObjFlag3::SimpleObject) && type != ObjType::UpgradePart && stats) {
        const float transCoef = anim ? anim->transCoef : 0.0f;
        float routeSpeed = stats->Num("RouteSpeed", level);
        // TODO(movement): carrying/stealth/slowed flags, rubble, path and lake coefficients.
        animSpeed = (transCoef == 0.0f ? 1.0f : routeSpeed / transCoef) * elapsed;
    }

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
    case ObjType::MiniFigure: o.flags3 |= 0x4401f5 | 0x8000; break;
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
    for (auto& o : objects_) {
        if (o->removed) continue;
        o->UpdateActivityChange();
        o->MoveAnimation(elapsed);
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
            hidden_.push_back({info, {x, y}, heading, health, key, driving});
            continue;
        }
        GameObject* o = nullptr;
        if (info->type == ObjType::Building) {
            // TODO(construction): Construction_SpawnBuilding (shape points, foundations, paths).
            o = Create(*info);
            const int dir = int(std::lround(headingDeg / 90.0f)) & 3;
            SetPositionAndHeading(*o, centre.x, centre.y, float(dir) * kPi * 0.5f);
        } else {
            o = Create(*info);
            SetPositionAndHeading(*o, x, y, heading);
        }
        o->health = health;
        o->customName = key;
        // TODO(ai): crystals/ore/dynamite/barriers get a collect task; drivers board "driving".
    }
    RR_INFO("Object list %s: %zu objects, %zu hidden", file.c_str(), objects_.size(), hidden_.size());
    return true;
}

}  // namespace rr
