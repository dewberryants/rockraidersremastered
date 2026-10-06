#pragma once

#include <cstdint>
#include <string_view>

namespace rr {

// Object categories (LegoObject_Type in the original; values match its numbering).
enum class ObjType : int {
    None = 0,
    Vehicle = 1,
    MiniFigure = 2,
    RockMonster = 3,
    Building = 4,
    Boulder = 5,
    PowerCrystal = 6,
    Ore = 7,  // id 0 = ore, id 1 = processed ore (building studs)
    Dynamite = 8,
    Barrier = 9,
    UpgradePart = 10,
    ElectricFence = 11,
    SpiderWeb = 12,
    OohScary = 13,
    ElectricFenceStud = 14,
    Path = 15,
    Pusher = 16,
    Freezer = 17,
    IceCube = 18,
    LaserShot = 19,
    Count = 20,
    TVCamera = -1,  // only appears in object lists
};

// Activity ids; the names (index order) are the keys used in .ae files.
enum class ActivityType : int {
    Stand, Route, RouteRubble, RunPanic, Drill, Teleport, Walk, Reinforce, Reverse, TurnLeft, TurnRight, CantDo,
    Emerge, Enter, EnterRein, Collect, Gather, Carry, CarryRubble, Throw, CarryTurnLeft, CarryTurnRight, CarryStand,
    HitLeft, HitRight, HitFront, HitBack, HitHard, Dynamite, Deposit, Clear, Place, Repair, Slip, Rest, Eat, Stamp,
    ThrowMan, ThrownByRockMonster, GetUp, BuildPath, Upgrade, Explode, Unpowered, FireLaser, Freezed, FreezeStart,
    FreezeMelt, Recharge, WakeUp, Train, FloatOn, FloatOff, Opening, Closing, Open, Waiting1, Waiting2, Waiting3,
    Waiting4, Waiting5, Waiting6, Waiting7, Waiting8, Waiting9, Waiting10, Waiting11, Waiting12, Waiting13,
    Waiting14, Waiting15, Waiting16, Waiting17, Waiting18, Waiting19, Waiting20, Waiting21, Crumble, TeleportIn,
    Count
};

const char* ActivityName(ActivityType a);  // "Activity_Stand"

enum class ToolType : int { Drill, Spade, Hammer, Spanner, Laser, PusherGun, BirdScarer, FreezerGun, Count };
const char* ToolTypeName(ToolType t);  // "LegoObject_ToolType_Drill"

enum class AbilityType : int { Pilot, Sailor, Driver, Dynamite, Repair, Scanner, Count };

}  // namespace rr
