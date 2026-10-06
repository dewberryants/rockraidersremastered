#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "config/Config.h"

namespace rr {

// Per-type object statistics from "Lego*::Stats::<TypeName>". Most numeric keys hold one value per
// upgrade level ("01.100:01.100:..."); a single value applies to all levels.
namespace StatsFlag1 {
constexpr uint32_t SingleWidthDig = 0x1, ProcessOre = 0x2, ProcessCrystal = 0x4, StoreObjects = 0x8,
                   SmallTeleporter = 0x10, BigTeleporter = 0x20, WaterTeleporter = 0x40, CauseSlip = 0x200,
                   RandomMove = 0x400, CanScare = 0x800, RandomEnterWall = 0x1000, ScaredByPlayer = 0x2000,
                   SnaxULike = 0x4000, GrabMinifigure = 0x8000, CanClearRubble = 0x10000, CanBeDriven = 0x20000,
                   CanScareScorpion = 0x40000, CanSteal = 0x80000, ToolStore = 0x100000, Flocks = 0x200000,
                   FlocksDebug = 0x400000, FlocksSmooth = 0x800000, CrossWater = 0x1000000, CrossLand = 0x2000000,
                   FlocksOnGround = 0x4000000, FlocksQuickDestroy = 0x8000000, FlocksAnimateByPitch = 0x10000000,
                   RouteAvoidance = 0x20000000, BumpDamage = 0x40000000, ManTeleporter = 0x80000000;
}
namespace StatsFlag2 {
constexpr uint32_t UpgradeBuilding = 0x2, TrainPilot = 0x4, TrainSailor = 0x8, TrainDriver = 0x10,
                   TrainDynamite = 0x20, TrainRepair = 0x40, TrainScanner = 0x80, Tracker = 0x100,
                   PowerBuilding = 0x200, SelfPowered = 0x400, UseBigTeleporter = 0x800,
                   UseSmallTeleporter = 0x1000, UseWaterTeleporter = 0x2000, AttackPaths = 0x4000,
                   SplitOnZeroHealth = 0x8000, CanBeHitByFence = 0x10000, CanDoubleSelect = 0x20000,
                   CanBeShotAt = 0x40000, DrainPower = 0x80000, UseHoles = 0x100000, CrossLava = 0x200000,
                   UseLegoManTeleporter = 0x400000, DamageCausesCallToArms = 0x800000, CanFreeze = 0x1000000,
                   CanLaser = 0x2000000, CanPush = 0x4000000, DontShowDamage = 0x8000000,
                   RemoveReinforcement = 0x10000000, DontShowOnRadar = 0x20000000, InvisibleDriver = 0x40000000,
                   Unselectable = 0x80000000;
}
namespace StatsFlag3 {
constexpr uint32_t VehicleCanBeCarried = 0x2, CanStrafe = 0x4, ClassAsLarge = 0x8, GetOutAtLand = 0x10,
                   GetInAtLand = 0x20, TakeCarryingDrivers = 0x40, ShowHealthBar = 0x80, NeedsPilot = 0x100,
                   EnterToolStore = 0x200;
}

struct ObjectStats {
    int levels = 1;
    std::vector<uint32_t> flags1, flags2, flags3;              // per level
    std::unordered_map<std::string, std::vector<std::string>> values;  // lower-case key -> per-level text

    float Num(std::string_view key, int level, float fallback = 0.0f) const;
    std::string Str(std::string_view key, int level = 0) const;
    bool Has(std::string_view key) const;
    uint32_t F1(int level) const { return flags1.empty() ? 0 : flags1[size_t(Clamp(level))]; }
    uint32_t F2(int level) const { return flags2.empty() ? 0 : flags2[size_t(Clamp(level))]; }
    uint32_t F3(int level) const { return flags3.empty() ? 0 : flags3[size_t(Clamp(level))]; }
    int Clamp(int level) const { return level < 0 ? 0 : (level >= levels ? levels - 1 : level); }
};

class StatsTable {
public:
    void Load(const Config& cfg, std::string_view gameName);
    const ObjectStats* Find(std::string_view typeName) const;

private:
    std::unordered_map<std::string, ObjectStats> stats_;  // lower-case type name
};

StatsTable& GetStats();

}  // namespace rr
