#pragma once

#include <cstdint>
#include <functional>
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
constexpr uint32_t CanReinforce = 0x4;         // may reinforce walls (LiveObject_CanReinforceBlock)
constexpr uint32_t CanTurn = 0x8;              // model has Activity_TurnLeft
constexpr uint32_t CanYesSir = 0x80;           // answers when selected (SFX_YesSir)
constexpr uint32_t Selectable = 0x100;         // cleared by Unselectable stats
constexpr uint32_t SimpleObject = 0x20000;     // single container (crystals, ore, dynamite...)
constexpr uint32_t Selected = 0x200000;
constexpr uint32_t CanGather = 0x4000000;      // model has Activity_Gather
constexpr uint32_t CanRouteRubble = 0x10000000;  // model has Activity_RouteRubble
}  // namespace ObjFlag3

// Trained skills of minifigures (LegoObject_AbilityFlags).
namespace Ability {
constexpr uint32_t Pilot = 0x1, Sailor = 0x2, Driver = 0x4, Dynamite = 0x8, Repair = 0x10, Scanner = 0x20;
constexpr uint32_t All = 0x3f;
}  // namespace Ability

// What a unit is busy doing after arriving somewhere.
enum class UnitState : uint8_t {
    Idle, Drilling, Clearing, Collecting, Depositing, Fetching, Placing, BuildingPath, GettingTool, Reinforcing,
    Training, PlacingDynamite
};

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
    bool animFresh = false;      // anim just started: its frame-0 sound cues are still due

    // Routing: the block route, the curve towards the current node and the distance along it.
    std::vector<RouteNode> route;
    int routeIndex = 0;
    bool routing = false;      // following a route
    bool routeLastLeg = false; // moving towards the final node
    BezierCurve curve;
    float curveLength = 0.0f, curveDist = 0.0f, curveOverrun = 0.0f;
    Vec2 endTangent;           // end direction of the current curve
    Vec2 faceDir{0.0f, 1.0f};  // facing direction in the ground plane

    // Work in progress (drilling a wall, clearing rubble, picking up / delivering an object).
    UnitState state = UnitState::Idle;
    BlockPos stateBlock;
    GameObject* stateTarget = nullptr;
    bool singleWidthDig = false;
    GameObject* carried = nullptr;  // object this unit carries
    GameObject* carrier = nullptr;  // unit carrying this object
    int deliverZone = 0;                   // construction site the carried resource is for
    ObjType fetchType = ObjType::None;     // resource to take from storage for deliverZone
    bool teleportingIn = false;            // playing Activity_TeleportIn after arriving
    std::vector<ToolType> tools;           // minifigure tool belt, oldest first
    ToolType wantedTool = ToolType::Count; // tool to pick up at the tool store
    int drillSound = -1;                   // play handle of the looping drill sound
    bool triggered = false;                // the animation passed its TRIGGER frame this tick
    uint32_t abilities = 0;                // trained skills (Ability)
    bool ticking = false;                  // dynamite: lit, explodes when TickDown ends
    BlockPos dynamiteTarget{-1, -1};       // dynamite: the wall it blows up
    float trainTimer = 0.0f;               // time since the last training (LegoObject_UpdateElapsedTimes)
    bool crystalDrained = false;           // power crystal used up (UnpoweredCrystalRGB)
    bool powered = false;                  // building: drawing power (or generating it)
    bool powerOff = false;                 // building: switched off by the player
    GameObject* teleportService = nullptr; // teleport building: the unit it is bringing in

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
    bool HasTool(ToolType t) const;
    // LegoObject_MiniFigure_EquipTool: a full belt drops its oldest tool.
    void EquipTool(ToolType t);
    // Faces a ground direction (heading 0 = +Y, increasing towards +X).
    void Face(Vec2 dir);
    // Plays an activity of the model by its own name (simple objects such as dynamite use
    // "Normal" / "TickDown" rather than Activity_*).
    bool PlayNamedAnim(std::string_view name);
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
    float heading = 0.0f;  // degrees, as in the object list
    float health = 100.0f;
    std::string name, driving;
};

// The level's live objects (LegoObject list) with creation, update and drawing.
class ObjectList {
public:
    GameObject* Create(const ObjectTypeInfo& info);
    // Creates an object standing at a world position (LegoObject_CreateInWorld).
    GameObject* Spawn(const ObjectTypeInfo& info, float x, float y, float heading = 0.0f) {
        GameObject* o = Create(info);
        SetPositionAndHeading(*o, x, y, heading);
        return o;
    }
    void SetPositionAndHeading(GameObject& o, float x, float y, float heading);
    ObjectSpeeds CalculateSpeeds(const GameObject& o, float elapsed) const;

    // Routing (LegoObject_Route_Begin/End/UpdateMovement). finalPos places the last node exactly.
    bool RouteBegin(GameObject& o, const std::vector<BlockPos>& blocks, const Vec2* finalPos = nullptr);
    void RouteEnd(GameObject& o, bool completed);
    // Builds a block route from the object's position and starts it.
    bool RouteToBlock(GameObject& o, BlockPos target, const Vec2* finalPos = nullptr);
    bool BlockOf(const GameObject& o, BlockPos& out) const;
    // Starts the work a route action asks for (called when a route reaches its last node).
    void StartAction(GameObject& o, uint8_t action, BlockPos block, GameObject* target);
    // Ends the unit's work; success reports to the task system.
    void FinishAction(GameObject& o, bool success);
    // Notified when a unit finished (or gave up) the work its task asked for.
    std::function<void(GameObject&, bool)> onActionDone;
    // Taking a resource out of storage: returns the object now carried, or null if none is left.
    std::function<GameObject*(GameObject& unit)> onFetch;
    // A carried resource put down on its construction site.
    std::function<void(GameObject& unit, GameObject& item)> onPlace;
    // A unit finished laying the power path on a block.
    std::function<void(GameObject& unit, BlockPos block)> onBuildPath;
    // Notified when a carried object is delivered to a building (resources).
    std::function<void(GameObject& unit, GameObject& delivered, GameObject& building)> onDeposit;
    void Remove(GameObject& o);
    // Pick sphere centre: half the collision height above the ground under the object.
    Vec3f PickCentre(const GameObject& o) const;
    // Nearest object whose pick sphere the ray passes through (LegoObject_DoPickSphereSelection);
    // with a filter, only objects it accepts.
    GameObject* PickObject(const Vec3f& origin, const Vec3f& dir, bool (*accept)(const GameObject&) = nullptr) const;
    bool instantDrill = false;  // debug cheat: walls break on the first drill tick
    // Dynamite whose TickDown has run out.
    std::function<void(GameObject& dynamite)> onExplode;
    // LegoObject_SetCrystalPoweredColour: power crystals are drawn in one of these colours
    // (Lego.cfg PowerCrystalRGB / UnpoweredCrystalRGB).
    Vec3f crystalColour{0.0f, 1.0f, 0.0f}, drainedCrystalColour{1.0f, 0.0f, 1.0f};
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
    // Creates the hidden objects standing on a block that has just opened up.
    std::vector<GameObject*> ExposeBlock(int bx, int by);
    // World position and facing (the null's +Z in the ground plane) of a named null in the
    // object's current animation (Container_SearchTree on the activity container); instance
    // picks among nulls sharing the name (Building_FindNull's frameNo).
    bool NullPoint(const GameObject& o, const std::string& nullName, Vec3f& pos, Vec2& dir, int instance = 0) const;
    // Puts a building on its origin block facing a direction (0..3) and claims its footprint.
    void PlaceBuilding(GameObject& o, BlockPos origin, int dir);

private:
    void PlaySceneSounds(const LwsScene& scene, bool loop, float t0, float t1, const Vec3f& at);
    GameObject* PlaceListedObject(const ObjectTypeInfo& info, float x, float y, float headingDeg);
    void UpdateMovement(GameObject& o, float elapsed);
    void UpdateAction(GameObject& o, float elapsed);

    void BuildRouteCurve(GameObject& o, bool skipCornerNode);

    world::Level* level_ = nullptr;
    std::vector<std::unique_ptr<GameObject>> objects_;
    std::vector<HiddenObject> hidden_;
};

}  // namespace rr
