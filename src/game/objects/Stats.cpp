#include "game/objects/Stats.h"

#include <algorithm>
#include <cstdlib>

#include "core/Log.h"
#include "fs/Path.h"

namespace rr {

StatsTable& GetStats() {
    static StatsTable instance;
    return instance;
}

namespace {
struct FlagBit {
    const char* key;
    int word;  // 1..3
    uint32_t bit;
};
// Boolean stats keys and the flag bits Stats_Initialise sets for them.
const FlagBit kFlagBits[] = {
    {"SingleWidthDig", 1, StatsFlag1::SingleWidthDig}, {"ProcessOre", 1, StatsFlag1::ProcessOre},
    {"ProcessCrystal", 1, StatsFlag1::ProcessCrystal}, {"StoreObjects", 1, StatsFlag1::StoreObjects},
    {"ManTeleporter", 1, StatsFlag1::ManTeleporter}, {"SmallTeleporter", 1, StatsFlag1::SmallTeleporter},
    {"BigTeleporter", 1, StatsFlag1::BigTeleporter}, {"WaterTeleporter", 1, StatsFlag1::WaterTeleporter},
    {"SnaxULike", 1, StatsFlag1::SnaxULike}, {"ToolStore", 1, StatsFlag1::ToolStore},
    {"CauseSlip", 1, StatsFlag1::CauseSlip}, {"RandomMove", 1, StatsFlag1::RandomMove},
    {"CanScare", 1, StatsFlag1::CanScare}, {"CanScareScorpion", 1, StatsFlag1::CanScareScorpion},
    {"RandomEnterWall", 1, StatsFlag1::RandomEnterWall}, {"ScaredByPlayer", 1, StatsFlag1::ScaredByPlayer},
    {"GrabMinifigure", 1, StatsFlag1::GrabMinifigure}, {"CanBeDriven", 1, StatsFlag1::CanBeDriven},
    {"CanSteal", 1, StatsFlag1::CanSteal}, {"CanClearRubble", 1, StatsFlag1::CanClearRubble},
    {"RouteAvoidance", 1, StatsFlag1::RouteAvoidance}, {"BumpDamage", 1, StatsFlag1::BumpDamage},
    {"Flocks", 1, StatsFlag1::Flocks}, {"Flocks_Debug", 1, StatsFlag1::FlocksDebug},
    {"Flocks_Smooth", 1, StatsFlag1::FlocksSmooth}, {"CrossWater", 1, StatsFlag1::CrossWater},
    {"CrossLand", 1, StatsFlag1::CrossLand}, {"Flocks_OnGround", 1, StatsFlag1::FlocksOnGround},
    {"Flocks_QuickDestroy", 1, StatsFlag1::FlocksQuickDestroy},
    {"Flocks_AnimateByPitch", 1, StatsFlag1::FlocksAnimateByPitch},
    {"UpgradeBuilding", 2, StatsFlag2::UpgradeBuilding}, {"CanBeShotAt", 2, StatsFlag2::CanBeShotAt},
    {"UseBigTeleporter", 2, StatsFlag2::UseBigTeleporter}, {"UseSmallTeleporter", 2, StatsFlag2::UseSmallTeleporter},
    {"UseWaterTeleporter", 2, StatsFlag2::UseWaterTeleporter},
    {"UseLegoManTeleporter", 2, StatsFlag2::UseLegoManTeleporter}, {"TrainPilot", 2, StatsFlag2::TrainPilot},
    {"TrainSailor", 2, StatsFlag2::TrainSailor}, {"TrainDriver", 2, StatsFlag2::TrainDriver},
    {"TrainDynamite", 2, StatsFlag2::TrainDynamite}, {"TrainRepair", 2, StatsFlag2::TrainRepair},
    {"TrainScanner", 2, StatsFlag2::TrainScanner}, {"Tracker", 2, StatsFlag2::Tracker},
    // PowerBuilding sets both the power-building and self-powered bits (0x600).
    {"PowerBuilding", 2, StatsFlag2::PowerBuilding | StatsFlag2::SelfPowered},
    {"SelfPowered", 2, StatsFlag2::SelfPowered}, {"AttackPaths", 2, StatsFlag2::AttackPaths},
    {"SplitOnZeroHealth", 2, StatsFlag2::SplitOnZeroHealth}, {"CanBeHitByFence", 2, StatsFlag2::CanBeHitByFence},
    {"CanDoubleSelect", 2, StatsFlag2::CanDoubleSelect}, {"DrainPower", 2, StatsFlag2::DrainPower},
    {"UseHoles", 2, StatsFlag2::UseHoles}, {"CrossLava", 2, StatsFlag2::CrossLava},
    {"DamageCausesCallToArms", 2, StatsFlag2::DamageCausesCallToArms}, {"CanFreeze", 2, StatsFlag2::CanFreeze},
    {"CanLaser", 2, StatsFlag2::CanLaser}, {"CanPush", 2, StatsFlag2::CanPush},
    {"DontShowDamage", 2, StatsFlag2::DontShowDamage}, {"RemoveReinforcement", 2, StatsFlag2::RemoveReinforcement},
    {"DontShowOnRadar", 2, StatsFlag2::DontShowOnRadar}, {"InvisibleDriver", 2, StatsFlag2::InvisibleDriver},
    {"Unselectable", 2, StatsFlag2::Unselectable},
    {"VehicleCanBeCarried", 3, StatsFlag3::VehicleCanBeCarried}, {"CanStrafe", 3, StatsFlag3::CanStrafe},
    {"ClassAsLarge", 3, StatsFlag3::ClassAsLarge}, {"GetOutAtLand", 3, StatsFlag3::GetOutAtLand},
    {"GetInAtLand", 3, StatsFlag3::GetInAtLand}, {"TakeCarryingDrivers", 3, StatsFlag3::TakeCarryingDrivers},
    {"ShowHealthBar", 3, StatsFlag3::ShowHealthBar}, {"NeedsPilot", 3, StatsFlag3::NeedsPilot},
    {"EnterToolStore", 3, StatsFlag3::EnterToolStore},
};
}  // namespace

float ObjectStats::Num(std::string_view key, int level, float fallback) const {
    auto it = values.find(ToLower(key));
    if (it == values.end() || it->second.empty()) return fallback;
    return float(std::atof(it->second[size_t(Clamp(level))].c_str()));
}

std::string ObjectStats::Str(std::string_view key, int level) const {
    auto it = values.find(ToLower(key));
    if (it == values.end() || it->second.empty()) return {};
    return it->second[size_t(Clamp(level))];
}

bool ObjectStats::Has(std::string_view key) const { return values.count(ToLower(key)) != 0; }

void StatsTable::Load(const Config& cfg, std::string_view gameName) {
    stats_.clear();
    for (Config::ItemRef it = cfg.FindArray(Config::Id(gameName, "Stats")); it != Config::kNone; it = cfg.Next(it)) {
        const std::string type = cfg.item(it).key;
        ObjectStats s;
        s.levels = std::max(1, cfg.GetInt(Config::Id(gameName, "Stats", type, "Levels")).value_or(1));
        s.flags1.assign(size_t(s.levels), 0);
        s.flags2.assign(size_t(s.levels), 0);
        s.flags3.assign(size_t(s.levels), 0);
        // Every entry of the type block, split per level (missing levels repeat the last value).
        for (Config::ItemRef kv = cfg.FindArray(Config::Id(gameName, "Stats", type)); kv != Config::kNone;
             kv = cfg.Next(kv)) {
            const auto parts = SplitTokens(cfg.item(kv).value, ":");
            std::vector<std::string> perLevel;
            for (int l = 0; l < s.levels; ++l)
                perLevel.emplace_back(parts.empty() ? std::string() : std::string(parts[std::min(size_t(l), parts.size() - 1)]));
            s.values[ToLower(cfg.item(kv).key)] = std::move(perLevel);
        }
        for (const FlagBit& f : kFlagBits) {
            auto vit = s.values.find(ToLower(f.key));
            if (vit == s.values.end()) continue;
            for (int l = 0; l < s.levels; ++l) {
                if (ParseBool(vit->second[size_t(l)]) != BoolTri::True) continue;
                (f.word == 1 ? s.flags1 : f.word == 2 ? s.flags2 : s.flags3)[size_t(l)] |= f.bit;
            }
        }
        stats_[ToLower(type)] = std::move(s);
    }
    RR_INFO("Stats for %zu object types", stats_.size());
}

const ObjectStats* StatsTable::Find(std::string_view typeName) const {
    auto it = stats_.find(ToLower(typeName));
    return it == stats_.end() ? nullptr : &it->second;
}

}  // namespace rr
