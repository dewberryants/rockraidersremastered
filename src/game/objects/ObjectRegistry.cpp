#include "game/objects/ObjectRegistry.h"

#include "core/Log.h"
#include "fs/FileSystem.h"
#include "fs/Path.h"

namespace rr {

ObjectRegistry& GetObjectRegistry() {
    static ObjectRegistry instance;
    return instance;
}

ObjectModel LoadObjectModel(const std::string& path, std::initializer_list<ModelKind> kinds,
                            const std::string& sharedObjects) {
    ObjectModel m;
    if (path.empty()) return m;
    for (ModelKind kind : kinds) {
        switch (kind) {
        case ModelKind::Act: m.activities = LoadActivityModel(path, sharedObjects); break;
        case ModelKind::Lws: m.scene = LoadLws(path, sharedObjects); break;
        case ModelKind::Lwo: m.mesh = LoadLwo(path); break;
        }
        if (m.valid()) return m;
    }
    RR_WARN("Object model %s not loaded", path.c_str());
    return m;
}

namespace {
// Lego_LoadMiscObjects: key, container type, and the object type it backs (None = effect only).
struct MiscEntry {
    const char* key;
    ModelKind kind;
    ObjType type;
    int id;
};
const MiscEntry kMisc[] = {
    {"Boulder", ModelKind::Lwo, ObjType::Boulder, 0},
    {"BoulderExplodeIce", ModelKind::Lws, ObjType::None, 0},
    {"BoulderExplode", ModelKind::Lws, ObjType::None, 0},
    {"SmashPath", ModelKind::Lws, ObjType::None, 0},
    {"Crystal", ModelKind::Lwo, ObjType::PowerCrystal, 0},
    {"Dynamite", ModelKind::Act, ObjType::Dynamite, 0},
    {"OohScary", ModelKind::Act, ObjType::OohScary, 0},
    {"Barrier", ModelKind::Act, ObjType::Barrier, 0},
    {"Ore", ModelKind::Lwo, ObjType::Ore, 0},
    {"ProcessedOre", ModelKind::Lwo, ObjType::Ore, 1},
    {"ElectricFence", ModelKind::Lwo, ObjType::ElectricFence, 0},
    {"ElectricFenceStud", ModelKind::Lws, ObjType::ElectricFenceStud, 0},
    {"SpiderWeb", ModelKind::Act, ObjType::SpiderWeb, 0},
    {"RechargeSparkle", ModelKind::Lws, ObjType::None, 0},
    {"MiniTeleportUp", ModelKind::Lws, ObjType::None, 0},
    {"IceCube", ModelKind::Act, ObjType::IceCube, 0},
    {"Pusher", ModelKind::Lws, ObjType::Pusher, 0},
    {"Freezer", ModelKind::Lws, ObjType::Freezer, 0},
    {"LaserShot", ModelKind::Lws, ObjType::LaserShot, 0},
    {"Explosion", ModelKind::Lws, ObjType::None, 0},
    {"UpgradeEffect", ModelKind::Lws, ObjType::None, 0},
};
}  // namespace

void ObjectRegistry::Load(const Config& cfg, std::string_view gameName) {
    types_.clear();
    misc_.clear();
    const std::string shared = cfg.GetString(Config::Id(gameName, "Main", "SharedObjects"), "");

    // Typed lists: the id of a type is its position in its block.
    struct List {
        const char* block;
        ObjType type;
    };
    for (const List& l : {List{"VehicleTypes", ObjType::Vehicle}, List{"MiniFigureTypes", ObjType::MiniFigure},
                          List{"RockMonsterTypes", ObjType::RockMonster}, List{"BuildingTypes", ObjType::Building},
                          List{"UpgradeTypes", ObjType::UpgradePart}}) {
        int id = 0;
        for (Config::ItemRef it = cfg.FindArray(Config::Id(gameName, l.block)); it != Config::kNone; it = cfg.Next(it)) {
            ObjectTypeInfo info;
            info.type = l.type;
            info.id = id++;
            info.name = cfg.item(it).key;
            // "Vehicles\WalkerLegs,Vehicles\WalkerBody": a second activity set rides on the first.
            const auto parts = SplitTokens(cfg.item(it).value, ",");
            // Upgrade_Part_Load tries LWO, then ACT; the unit types are always activity sets.
            auto load = [&](std::string_view p) {
                const std::string s(p);
                return l.type == ObjType::UpgradePart ? LoadObjectModel(s, {ModelKind::Lwo, ModelKind::Act}, shared)
                                                      : LoadObjectModel(s, {ModelKind::Act}, shared);
            };
            info.model = load(parts[0]);
            if (parts.size() > 1) info.model.secondPart = load(parts[1]).activities;
            types_.push_back(std::move(info));
        }
    }

    for (const MiscEntry& e : kMisc) {
        const std::string path = cfg.GetString(Config::Id(gameName, "MiscObjects", e.key), "");
        ObjectModel m = LoadObjectModel(path, {e.kind}, shared);
        if (e.type != ObjType::None) {
            ObjectTypeInfo info;
            info.type = e.type;
            info.id = e.id;
            info.name = e.type == ObjType::PowerCrystal ? "PowerCrystal" : e.key;
            info.model = m;
            types_.push_back(std::move(info));
        }
        misc_.emplace_back(e.key, std::move(m));
    }
    ObjectTypeInfo path;
    path.type = ObjType::Path;
    path.name = "Path";
    types_.push_back(std::move(path));
    tvCamera_.type = ObjType::TVCamera;
    tvCamera_.name = "TVCamera";
    RR_INFO("Object registry: %zu types, %zu misc models", types_.size(), misc_.size());
}

const ObjectTypeInfo* ObjectRegistry::Find(std::string_view name) const {
    // Lego_GetObjectByName order: rock monsters, TVCamera, the fixed names, vehicles, minifigures,
    // buildings. Upgrade parts and effect-only models are not reachable by name.
    auto scan = [&](ObjType type) -> const ObjectTypeInfo* {
        for (const auto& t : types_)
            if (t.type == type && EqualsNoCase(t.name, name)) return &t;
        return nullptr;
    };
    if (const ObjectTypeInfo* t = scan(ObjType::RockMonster)) return t;
    if (EqualsNoCase(name, "TVCamera")) return &tvCamera_;
    for (const auto& t : types_) {
        switch (t.type) {
        case ObjType::Vehicle:
        case ObjType::MiniFigure:
        case ObjType::RockMonster:
        case ObjType::Building:
        case ObjType::UpgradePart:
        case ObjType::ElectricFenceStud:
        case ObjType::IceCube: continue;
        default:
            if (EqualsNoCase(t.name, name)) return &t;
        }
    }
    for (ObjType type : {ObjType::Vehicle, ObjType::MiniFigure, ObjType::Building})
        if (const ObjectTypeInfo* t = scan(type)) return t;
    return nullptr;
}

const ObjectTypeInfo* ObjectRegistry::FindUpgradePart(std::string_view name) const {
    for (const auto& t : types_)
        if (t.type == ObjType::UpgradePart && EqualsNoCase(t.name, name)) return &t;
    return nullptr;
}

const ObjectTypeInfo* ObjectRegistry::Get(ObjType type, int id) const {
    if (type == ObjType::TVCamera) return &tvCamera_;
    for (const auto& t : types_)
        if (t.type == type && t.id == id) return &t;
    return nullptr;
}

const ObjectModel* ObjectRegistry::Misc(std::string_view name) const {
    for (const auto& [n, m] : misc_)
        if (EqualsNoCase(n, name)) return &m;
    return nullptr;
}

int ObjectRegistry::CountOf(ObjType type) const {
    int n = 0;
    for (const auto& t : types_)
        if (t.type == type) ++n;
    return n;
}

}  // namespace rr
