#include "game/objects/ObjectTypes.h"

namespace rr {

const char* ActivityName(ActivityType a) {
    static const char* const kNames[int(ActivityType::Count)] = {
        "Activity_Stand", "Activity_Route", "Activity_RouteRubble", "Activity_RunPanic", "Activity_Drill",
        "Activity_Teleport", "Activity_Walk", "Activity_Reinforce", "Activity_Reverse", "Activity_TurnLeft",
        "Activity_TurnRight", "Activity_CantDo", "Activity_Emerge", "Activity_Enter", "Activity_EnterRein",
        "Activity_Collect", "Activity_Gather", "Activity_Carry", "Activity_CarryRubble", "Activity_Throw",
        "Activity_CarryTurnLeft", "Activity_CarryTurnRight", "Activity_CarryStand", "Activity_HitLeft",
        "Activity_HitRight", "Activity_HitFront", "Activity_HitBack", "Activity_HitHard", "Activity_Dynamite",
        "Activity_Deposit", "Activity_Clear", "Activity_Place", "Activity_Repair", "Activity_Slip", "Activity_Rest",
        "Activity_Eat", "Activity_Stamp", "Activity_ThrowMan", "Activity_ThrownByRockMonster", "Activity_GetUp",
        "Activity_BuildPath", "Activity_Upgrade", "Activity_Explode", "Activity_Unpowered", "Activity_FireLaser",
        "Activity_Freezed", "Activity_FreezeStart", "Activity_FreezeMelt", "Activity_Recharge", "Activity_WakeUp",
        "Activity_Train", "Activity_FloatOn", "Activity_FloatOff", "Activity_Opening", "Activity_Closing",
        "Activity_Open", "Activity_Waiting1", "Activity_Waiting2", "Activity_Waiting3", "Activity_Waiting4",
        "Activity_Waiting5", "Activity_Waiting6", "Activity_Waiting7", "Activity_Waiting8", "Activity_Waiting9",
        "Activity_Waiting10", "Activity_Waiting11", "Activity_Waiting12", "Activity_Waiting13", "Activity_Waiting14",
        "Activity_Waiting15", "Activity_Waiting16", "Activity_Waiting17", "Activity_Waiting18", "Activity_Waiting19",
        "Activity_Waiting20", "Activity_Waiting21", "Activity_Crumble", "Activity_TeleportIn",
    };
    const int i = int(a);
    return (i >= 0 && i < int(ActivityType::Count)) ? kNames[i] : "Activity_Stand";
}

const char* ToolTypeName(ToolType t) {
    static const char* const kNames[int(ToolType::Count)] = {
        "LegoObject_ToolType_Drill",  "LegoObject_ToolType_Spade",     "LegoObject_ToolType_Hammer",
        "LegoObject_ToolType_Spanner", "LegoObject_ToolType_Laser",    "LegoObject_ToolType_PusherGun",
        "LegoObject_ToolType_BirdScarer", "LegoObject_ToolType_FreezerGun",
    };
    const int i = int(t);
    return (i >= 0 && i < int(ToolType::Count)) ? kNames[i] : "";
}

}  // namespace rr
