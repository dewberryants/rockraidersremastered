#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/Math3D.h"
#include "game/objects/ObjectRegistry.h"
#include "game/objects/ObjectTypes.h"
#include "game/objects/Routing.h"
#include "game/objects/Stats.h"

namespace rr {

class Renderer3D;
namespace world {
class Level;
}

// Object state flags set at creation (LegoObject flags3 in the original numbering).
namespace ObjFlag3 {
constexpr uint32_t CanTurn = 0x8;              // model has Activity_TurnLeft
constexpr uint32_t Selectable = 0x100;         // cleared by Unselectable stats
constexpr uint32_t SimpleObject = 0x20000;     // single container (crystals, ore, dynamite...)
constexpr uint32_t CanGather = 0x4000000;      // model has Activity_Gather
constexpr uint32_t CanRouteRubble = 0x10000000;  // model has Activity_RouteRubble
}  // namespace ObjFlag3

// A live game object (LegoObject). Grows as the game systems are implemented.
struct GameObject {
    ObjType type = ObjType::None;
    int id = 0;
    const ObjectTypeInfo* info = nullptr;
    const ObjectStats* stats = nullptr;
    int level = 0;  // upgrade/object level for stats lookups
    std::string customName;

    Vec3f position;
    float heading = 0.0f;  // radians, 0 = facing +Y (north)

    // Activity: SetActivity requests one; UpdateActivityChange applies it to the model.
    ActivityType nextActivity = ActivityType::Stand;
    ActivityType activity = ActivityType::Count;  // none applied yet
    uint32_t animRepeat = 0;     // extra loops before the animation counts as finished
    float animOverrun = 0.0f;    // > 0 once the current animation has played to the end
    float animTime = 0.0f;       // container time in frames (not wrapped; the scene wraps it)
    float animTime2 = 0.0f;      // second part of two-part vehicles
    const Activity* anim = nullptr;
    const Activity* anim2 = nullptr;

    // Routing: the block route, the curve towards the current node and the distance along it.
    std::vector<RouteNode> route;
    int routeIndex = 0;
    bool routing = false;      // following a route
    bool routeLastLeg = false; // moving towards the final node
    BezierCurve curve;
    float curveLength = 0.0f, curveDist = 0.0f, curveOverrun = 0.0f;
    Vec2 endTangent;           // end direction of the current curve
    Vec2 faceDir{0.0f, 1.0f};  // facing direction in the ground plane

    float health = 100.0f;
    float energy = 100.0f;
    uint32_t flags3 = 0;
    bool hidden = false;
    bool removed = false;
    float spin = 0.0f;  // crystals rotate about their vertical axis while lying on the ground

    void SetActivity(ActivityType a, uint32_t repeat = 0) {
        nextActivity = a;
        animRepeat = repeat;
        animOverrun = 0.0f;
    }
    // Applies a pending activity; returns true if the model has an animation for it.
    bool UpdateActivityChange();
    // Advances the current animation. elapsed is in game units (25 per second = one LWS frame);
    // animSpeed is the already scaled amount for the main part (ObjectSpeeds::anim).
    void MoveAnimation(float elapsed, float animSpeed);
    bool HasActivity(ActivityType a) const;
    // Faces a ground direction (heading 0 = +Y, increasing towards +X).
    void Face(Vec2 dir);
};

// Per-tick distances from LegoObject_CalculateSpeeds: route = ground covered, anim = animation
// frames advanced (route speed over the animation's TransCoef, so steps match the ground).
struct ObjectSpeeds {
    float route = 0.0f, anim = 0.0f;
};

// Object placed in a hidden cavern; created when the cavern is uncovered (HiddenObject_Add).
struct HiddenObject {
    const ObjectTypeInfo* info = nullptr;
    Vec2 position;
    float heading = 0.0f;
    float health = 100.0f;
    std::string name, driving;
};

// The level's live objects (LegoObject list) with creation, update and drawing.
class ObjectList {
public:
    GameObject* Create(const ObjectTypeInfo& info);
    void SetPositionAndHeading(GameObject& o, float x, float y, float heading);
    ObjectSpeeds CalculateSpeeds(const GameObject& o, float elapsed) const;

    // Routing (LegoObject_Route_Begin/End/UpdateMovement). finalPos places the last node exactly.
    bool RouteBegin(GameObject& o, const std::vector<BlockPos>& blocks, const Vec2* finalPos = nullptr);
    void RouteEnd(GameObject& o, bool completed);
    // Builds a block route from the object's position and starts it.
    bool RouteToBlock(GameObject& o, BlockPos target, const Vec2* finalPos = nullptr);
    bool BlockOf(const GameObject& o, BlockPos& out) const;
    void Update(float elapsed);
    void Draw(Renderer3D& r) const;
    void Clear();

    // Lego_LoadOLObjectList. Returns false if the list is malformed. The TVCamera entry is
    // returned through camera (block-relative world coordinates and heading).
    struct CameraStart {
        bool found = false;
        float x = 0, y = 0, heading = 0;
    };
    bool LoadObjectList(const std::string& file, world::Level& level, CameraStart& camera);

    const std::vector<std::unique_ptr<GameObject>>& objects() const { return objects_; }
    const std::vector<HiddenObject>& hiddenObjects() const { return hidden_; }

private:
    void UpdateMovement(GameObject& o, float elapsed);
    void BuildRouteCurve(GameObject& o, bool skipCornerNode);

    world::Level* level_ = nullptr;
    std::vector<std::unique_ptr<GameObject>> objects_;
    std::vector<HiddenObject> hidden_;
};

}  // namespace rr
