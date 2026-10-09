#include "game/ai/Tasks.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "core/Log.h"
#include "core/Random.h"
#include "fs/Path.h"
#include "game/objects/GameObject.h"
#include "game/world/Level.h"

namespace rr {

namespace {

const char* const kPriorityNames[int(PriorityType::Count)] = {
    "Crystal", "Ore", "DefaultCollect", "Destruction", "Construction", "Request", "Reinforce", "Repair",
    "Clearing", "Storage", "Refining", "HealthLow", "GetIn", "Upgrade", "BuildPath", "AttackRockMonster",
    "Barrier", "FindLoad", "Recharge", "UpgradeBuilding", "Gather", "Steal", "Punch", "Depart", "AttackPath",
    "AttackObject", "Congregate",
};

constexpr float kRetryTime = 75.0f;  // game units before a task whose route failed is offered again

// Block route that also covers standing in the target block already (a one-node route).
bool RouteBlocks(const world::Level& level, const GameObject& unit, BlockPos from, BlockPos to,
                 std::vector<BlockPos>& out) {
    if (from == to) {
        out.assign(1, from);
        return true;
    }
    return BuildRouteList(level, &unit, from, to, out);
}

bool IsStorageBuilding(const GameObject& o) {
    if (o.type != ObjType::Building || !o.stats || o.removed || o.hidden) return false;
    const uint32_t f1 = o.stats->F1(o.level);
    return (f1 & StatsFlag1::ToolStore) || (f1 & StatsFlag1::StoreObjects);
}

GameObject* NearestToolStore(const ObjectList& objects, const GameObject& u) {
    GameObject* best = nullptr;
    float bestD = 0.0f;
    for (const auto& bp : objects.objects()) {
        const GameObject& b = *bp;
        if (b.type != ObjType::Building || b.removed || b.hidden || !b.stats) continue;
        if (!(b.stats->F1(b.level) & StatsFlag1::ToolStore)) continue;
        const float dx = b.position.x - u.position.x, dy = b.position.y - u.position.y;
        if (!best || dx * dx + dy * dy < bestD) {
            best = bp.get();
            bestD = dx * dx + dy * dy;
        }
    }
    return best;
}

GameObject* NearestStore(const ObjectList& objects, const GameObject& u) {
    GameObject* best = nullptr;
    float bestD = 0.0f;
    for (const auto& bp : objects.objects()) {
        if (!IsStorageBuilding(*bp)) continue;
        const float dx = bp->position.x - u.position.x, dy = bp->position.y - u.position.y;
        const float d = dx * dx + dy * dy;
        if (!best || d < bestD) {
            best = bp.get();
            bestD = d;
        }
    }
    return best;
}

}  // namespace

const char* PriorityTypeName(PriorityType p) {
    static std::array<std::string, size_t(PriorityType::Count)> names;
    std::string& n = names[size_t(p)];
    if (n.empty()) n = std::string("AI_Priority_") + kPriorityNames[size_t(p)];
    return n.c_str();
}

float DrillTime(const GameObject& unit, int terrain) {
    if (!unit.stats) return 0.0f;
    using world::Terrain;
    const char* key = nullptr;
    switch (Terrain(terrain)) {
    case Terrain::Hard: key = "HardDrillTime"; break;
    case Terrain::Medium: key = "MedDrillTime"; break;
    case Terrain::Loose: key = "LooseDrillTime"; break;
    case Terrain::Soil: key = "SoilDrillTime"; break;
    case Terrain::OreSeam:
    case Terrain::CrystalSeam: key = "SeamDrillTime"; break;
    default: return 0.0f;
    }
    return unit.stats->Num(key, unit.level) * 25.0f;
}

void TaskSystem::Reset() {
    tasks_.clear();
    priorityValue_.fill(0);
    priorityOff_.fill(false);
    order_.clear();
    initialOrder_.clear();
    initialOff_.clear();
}

void TaskSystem::LoadPriorities(const Config& cfg, std::string_view levelBlock) {
    int index = 0;
    for (Config::ItemRef it = cfg.FindArray(Config::Id(levelBlock, "Priorities")); it != Config::kNone;
         it = cfg.Next(it)) {
        for (int p = 0; p < int(PriorityType::Count); ++p) {
            if (!EqualsNoCase(cfg.item(it).key, PriorityTypeName(PriorityType(p)))) continue;
            priorityValue_[size_t(p)] = 80 - 5 * index;
            priorityOff_[size_t(p)] = ParseBool(cfg.item(it).value) != BoolTri::True;
            order_.push_back(PriorityType(p));
            initialOff_.push_back(priorityOff_[size_t(p)]);
            ++index;
            break;
        }
    }
    initialOrder_ = order_;
}

void TaskSystem::MovePriorityUp(size_t slot) {
    if (slot == 0 || slot >= order_.size()) return;
    std::swap(order_[slot], order_[slot - 1]);
    priorityValue_[size_t(order_[slot])] = 80 - 5 * int(slot);
    priorityValue_[size_t(order_[slot - 1])] = 85 - 5 * int(slot);
}

void TaskSystem::MovePriorityTop(size_t slot) {
    if (slot == 0 || slot >= order_.size()) return;
    const PriorityType moved = order_[slot];
    for (size_t i = slot; i > 0; --i) {
        order_[i] = order_[i - 1];
        priorityValue_[size_t(order_[i])] = 80 - 5 * int(i);
    }
    order_[0] = moved;
    priorityValue_[size_t(moved)] = 80;
}

void TaskSystem::TogglePriority(size_t slot) {
    if (slot >= order_.size()) return;
    bool& off = priorityOff_[size_t(order_[slot])];
    off = !off;
    // TODO(interface): switching off crystals or ore shows Text_CrystalOreDisabled.
}

void TaskSystem::ResetPriorities() {
    order_ = initialOrder_;
    for (size_t i = 0; i < order_.size(); ++i) {
        priorityValue_[size_t(order_[i])] = 80 - 5 * int(i);
        priorityOff_[size_t(order_[i])] = initialOff_[i];
    }
}

namespace {
Task* NewTask(std::vector<std::unique_ptr<Task>>& tasks, TaskType type, PriorityType prio, int value) {
    auto t = std::make_unique<Task>();
    t->type = type;
    t->priorityType = prio;
    t->priority = value;
    // New tasks go to the front, like the game's list (newest first among equal priorities).
    tasks.insert(tasks.begin(), std::move(t));
    return tasks.front().get();
}
}  // namespace

Task* TaskSystem::CreateDig(const world::Level& level, BlockPos block, bool userRequested) {
    const world::Block& b = level.block(block.x, block.y);
    if (b.terrain == world::Terrain::Immovable || b.terrain == world::Terrain::Lava) return nullptr;
    if (!(b.flags & world::BlockFlag::Visible)) return nullptr;  // only surveyed walls
    for (const auto& t : tasks_)
        if (t->type == TaskType::Dig && t->block == block && !t->remove) return t.get();
    Task* t = NewTask(tasks_, TaskType::Dig, PriorityType::Destruction,
                      priorityValue_[size_t(PriorityType::Destruction)]);
    t->block = block;
    t->userRequested = userRequested;
    return t;
}

Task* TaskSystem::CreateReinforce(const world::Level& level, BlockPos block) {
    const world::Block& b = level.block(block.x, block.y);
    if (!(b.flags & world::BlockFlag::Wall) || (b.flags & world::BlockFlag::Reinforced)) return nullptr;
    for (const auto& t : tasks_)
        if (t->type == TaskType::Reinforce && t->block == block && !t->remove) return t.get();
    Task* t = NewTask(tasks_, TaskType::Reinforce, PriorityType::Reinforce,
                      priorityValue_[size_t(PriorityType::Reinforce)]);
    t->block = block;
    return t;
}

Task* TaskSystem::CreateDynamite(BlockPos wall, GameObject& dynamite) {
    Task* t = NewTask(tasks_, TaskType::Dynamite, PriorityType::Destruction,
                      priorityValue_[size_t(PriorityType::Destruction)]);
    t->block = wall;
    t->target = &dynamite;
    return t;
}

Task* TaskSystem::CreateTrain(GameObject& building, uint32_t ability, GameObject* forUnit) {
    // The task's priority class follows the skill.
    PriorityType p = PriorityType::Upgrade;
    switch (ability) {
    case Ability::Pilot:
    case Ability::Sailor:
    case Ability::Driver: p = PriorityType::GetIn; break;
    case Ability::Dynamite: p = PriorityType::Destruction; break;
    case Ability::Repair: p = PriorityType::Repair; break;
    case Ability::Scanner: p = PriorityType::Construction; break;
    default: break;
    }
    Task* t = NewTask(tasks_, TaskType::Train, p, priorityValue_[size_t(p)]);
    t->target = &building;
    t->ability = ability;
    t->forUnit = forUnit;
    return t;
}

Task* TaskSystem::CreateUpgrade(GameObject& building, int upgradeType, GameObject& unit) {
    Task* t = NewTask(tasks_, TaskType::Upgrade, PriorityType::Upgrade, priorityValue_[size_t(PriorityType::Upgrade)]);
    t->target = &building;
    t->upgradeType = upgradeType;
    t->forUnit = &unit;
    return t;
}

bool TaskSystem::StationTaken(const GameObject& station, const GameObject& vehicle) const {
    for (const auto& t : tasks_)
        if (t->type == TaskType::Upgrade && t->target == &station && !t->remove && t->forUnit &&
            t->forUnit != &vehicle && t->forUnit->type == ObjType::Vehicle)
            return true;
    return false;
}

Task* TaskSystem::CreateFindDriver(GameObject& vehicle) {
    if (HasFindDriver(vehicle)) return nullptr;
    Task* t = NewTask(tasks_, TaskType::FindDriver, PriorityType::GetIn, priorityValue_[size_t(PriorityType::GetIn)]);
    t->target = &vehicle;
    t->ability = ObjectList::DriverAbility(vehicle);
    return t;
}

bool TaskSystem::HasFindDriver(const GameObject& vehicle) const {
    for (const auto& t : tasks_)
        if (t->type == TaskType::FindDriver && t->target == &vehicle && !t->remove) return true;
    return false;
}

std::vector<GameObject*> TaskSystem::CancelBlockTasks(BlockPos block, TaskType type) {
    std::vector<GameObject*> units;
    for (auto& t : tasks_) {
        if (t->type != type || !(t->block == block) || t->remove) continue;
        t->remove = true;
        if (t->assigned) units.push_back(t->assigned);
    }
    return units;
}

bool TaskSystem::HasBlockTask(BlockPos block, TaskType type) const {
    for (const auto& t : tasks_)
        if (t->type == type && t->block == block && !t->remove) return true;
    return false;
}

Task* TaskSystem::CreateClear(BlockPos block) {
    for (const auto& t : tasks_)
        if (t->type == TaskType::Clear && t->block == block && !t->remove) return t.get();
    Task* t = NewTask(tasks_, TaskType::Clear, PriorityType::Clearing, priorityValue_[size_t(PriorityType::Clearing)]);
    t->block = block;
    return t;
}

Task* TaskSystem::CreateCollect(GameObject& object) {
    PriorityType p = PriorityType::DefaultCollect;
    switch (object.type) {
    case ObjType::PowerCrystal: p = PriorityType::Crystal; break;
    case ObjType::Ore: p = PriorityType::Ore; break;
    case ObjType::Barrier: p = PriorityType::Barrier; break;
    case ObjType::Dynamite: p = PriorityType::Destruction; break;
    default: break;
    }
    Task* t = NewTask(tasks_, TaskType::Collect, p, priorityValue_[size_t(p)]);
    t->target = &object;
    return t;
}

Task* TaskSystem::CreateRequest(int zone, ObjType resource) {
    Task* t = NewTask(tasks_, TaskType::Request, PriorityType::Construction,
                      priorityValue_[size_t(PriorityType::Construction)]);
    t->zone = zone;
    t->resource = resource;
    return t;
}

Task* TaskSystem::CreateGetTool(ToolType tool, GameObject* forUnit) {
    Task* t = NewTask(tasks_, TaskType::GetTool, PriorityType::DefaultCollect, 99);
    t->tool = tool;
    t->forUnit = forUnit;
    return t;
}

Task* TaskSystem::CreateBuildPath(BlockPos block) {
    Task* t = NewTask(tasks_, TaskType::BuildPath, PriorityType::BuildPath, priorityValue_[size_t(PriorityType::BuildPath)]);
    t->block = block;
    return t;
}

void TaskSystem::RemoveZone(int zone) {
    for (auto& t : tasks_)
        if (t->type == TaskType::Request && t->zone == zone) t->remove = true;
}

Task* TaskSystem::TaskOf(const GameObject& unit) const {
    for (const auto& t : tasks_)
        if (t->assigned == &unit && !t->remove) return t.get();
    return nullptr;
}

Vec2 TaskSystem::TaskPosition(const Task& t, const world::Level& level) const {
    if (t.target) return {t.target->position.x, t.target->position.y};
    return level.map().BlockCentre(t.block.x, t.block.y);
}

bool TaskSystem::CanDo(const Task& t, const GameObject& unit, const world::Level& level) const {
    if (t.retryTimer > 0.0f || t.remove || t.assigned) return false;
    if (t.forUnit && t.forUnit != &unit) return false;
    if (priorityOff_[size_t(t.priorityType)] && t.type != TaskType::GetTool) return false;
    // AITask_FUN_00404ef0: vehicles take tasks only with a driver.
    // TODO(ai): rock monsters take tasks too.
    if (!unit.stats) return false;
    if (unit.type != ObjType::MiniFigure && !(unit.type == ObjType::Vehicle && unit.driver)) return false;
    if (unit.carried) return false;
    // LegoObject_TaskHasTool: minifigures need the task's tool on their belt.
    if (const ToolType need = ToolFor(t.type);
        unit.type == ObjType::MiniFigure && need != ToolType::Count && !unit.HasTool(need))
        return false;
    return CanDoIgnoringTools(t, unit, level);
}

bool TaskSystem::CanDoIgnoringTools(const Task& t, const GameObject& unit, const world::Level& level) const {
    // Vehicles dig (with a drill null), clear rubble (CanClearRubble) and collect (carriers).
    if (unit.type == ObjType::Vehicle) {
        if (t.type == TaskType::Collect) return VehicleCanCollect(t, unit, objects_);
        if (t.type != TaskType::Dig && t.type != TaskType::Clear && t.type != TaskType::Upgrade) return false;
    }
    if (t.type == TaskType::Dig && !ObjectList::CanDig(unit)) return false;
    switch (t.type) {
    case TaskType::Dig: {
        const world::Block& b = level.block(t.block.x, t.block.y);
        if (!(b.flags & world::BlockFlag::Wall) || (b.flags & world::BlockFlag::Ground)) return false;
        if (b.flags & world::BlockFlag::OuterCorner) return false;
        if (b.terrain == world::Terrain::Immovable || b.terrain == world::Terrain::Water ||
            b.terrain == world::Terrain::RechargeSeam)
            return false;
        if (t.block.x >= level.width() - 2 || t.block.y >= level.height() - 2) return false;
        if (t.singleWidth && !(unit.stats->F1(unit.level) & StatsFlag1::SingleWidthDig)) return false;
        return DrillTime(unit, int(b.terrain)) > 0.0f;
    }
    case TaskType::Reinforce:
        return (unit.flags3 & ObjFlag3::CanReinforce) && level.CanReinforce(t.block.x, t.block.y);
    case TaskType::Clear: {
        if (!(unit.stats->F1(unit.level) & StatsFlag1::CanClearRubble)) return false;
        const world::Block& b = level.block(t.block.x, t.block.y);
        return (b.flags & world::BlockFlag::Ground) && !(b.flags & world::BlockFlag::CleanFloor);
    }
    case TaskType::Collect: return t.target && !t.target->removed && !t.target->carrier && !t.target->hidden;
    case TaskType::Request: return !hasStock || hasStock(t.resource);
    case TaskType::BuildPath: return !(level.block(t.block.x, t.block.y).flags & world::BlockFlag::Path);
    case TaskType::GetTool: return t.tool != ToolType::Count && !unit.HasTool(t.tool);
    case TaskType::Train: return t.target && !t.target->removed && !(unit.abilities & t.ability);
    case TaskType::Upgrade:
        if (!t.target || t.target->removed) return false;
        if (unit.type == ObjType::Vehicle)
            return ObjectList::CanUpgradeType(unit, t.upgradeType, true) && !StationTaken(*t.target, unit);
        return unit.stats->levels > unit.level + 1;
    case TaskType::FindDriver:
        // LegoObject_TryFindDriver_FUN_00440690: a trained unit on foot, a free seat.
        // TODO(vehicles): TakeCarryingDrivers vehicles take units that carry something.
        return (unit.abilities & t.ability) && !unit.driving && t.target && !t.target->removed &&
               !t.target->driver && !t.target->teleportingIn;
    case TaskType::Dynamite:
        return (unit.abilities & Ability::Dynamite) && t.target && !t.target->removed && !t.target->ticking &&
               (!t.target->carrier || t.target->carrier == &unit) && level.CanDynamite(t.block.x, t.block.y);
    default: return false;
    }
}

bool TaskSystem::VehicleCanCollect(const Task& t, const GameObject& v, const ObjectList* objects) const {
    // AITask_FUN_00404ef0 (collect): a carrier with room, the same kind of object as its load,
    // plain ore or a charged crystal, and a building that takes it (LegoObject_FUN_00439e40).
    // TODO(vehicles): the cats carry on water; vehicles pick up other objects (barriers...).
    const GameObject* item = t.target;
    if (!item || item->removed || item->carrier || item->hidden || !ObjectList::CanCollect(v)) return false;
    if (ObjectList::LoadReady(v) || int(v.cargo.size()) >= ObjectList::CargoCapacity(v)) return false;
    if (item->type != ObjType::Ore && item->type != ObjType::PowerCrystal) return false;
    if (item->id != 0 || (item->type == ObjType::PowerCrystal && item->crystalDrained)) return false;
    if (!v.cargo.empty() && v.cargo.front()->type != item->type) return false;
    return objects && DepositBuilding(*objects, v, item->type);
}

GameObject* TaskSystem::DepositBuilding(const ObjectList& objects, const GameObject& u, ObjType load) const {
    // Vehicles that may not drive into tool stores take resources to a building that processes
    // them (LegoObject_FindResourceProcessingBuilding).
    const bool processorOnly =
        u.type == ObjType::Vehicle && !(u.stats && (u.stats->F3(u.level) & StatsFlag3::EnterToolStore));
    if (!processorOnly) return NearestStore(objects, u);
    const uint32_t need = load == ObjType::Ore ? StatsFlag1::ProcessOre : StatsFlag1::ProcessCrystal;
    GameObject* best = nullptr;
    float bestD = 0.0f;
    for (const auto& bp : objects.objects()) {
        const GameObject& b = *bp;
        if (b.type != ObjType::Building || b.removed || b.hidden || !b.stats || !(b.stats->F1(b.level) & need)) continue;
        const float dx = b.position.x - u.position.x, dy = b.position.y - u.position.y;
        if (!best || dx * dx + dy * dy < bestD) {
            best = bp.get();
            bestD = dx * dx + dy * dy;
        }
    }
    return best;
}

bool TaskSystem::Start(Task& t, GameObject& unit, ObjectList& objects, world::Level& level) {
    BlockPos from;
    if (!objects.BlockOf(unit, from)) return false;
    std::vector<BlockPos> route;
    switch (t.type) {
    case TaskType::Dynamite:
        if (unit.carried != t.target) {
            // First collect the dynamite where the tool store handed it out.
            BlockPos tb;
            if (!t.target || !objects.BlockOf(*t.target, tb)) return false;
            const Vec2 at{t.target->position.x, t.target->position.y};
            if (!RouteBlocks(level, unit, from, tb, route) || !objects.RouteBegin(unit, route, &at)) return false;
            unit.route.back().action = RouteAction::Collect;
            unit.stateTarget = t.target;
            break;
        }
        [[fallthrough]];  // carrying it: off to the wall (LegoObject_TryDynamite)
    case TaskType::Dig:
    case TaskType::Reinforce: {
        // Approach from a free side; the starting side rotates so units spread around a wall.
        BlockPos approach{-1, -1};
        static const int kDx[4] = {0, 1, 0, -1}, kDy[4] = {-1, 0, 1, 0};
        ++approachCounter_;
        for (int i = 0; i < 4 && approach.x < 0; ++i) {
            const int d = int((approachCounter_ + unsigned(i)) & 3);
            const BlockPos n{t.block.x + kDx[d], t.block.y + kDy[d]};
            const world::Block& nb = level.block(n.x, n.y);
            if ((nb.flags & world::BlockFlag::Wall) && !(nb.flags & world::BlockFlag::Ground)) continue;
            if (GetCrossTerrainType(level, &unit, n.x, n.y, n.x, n.y, false) != CrossType::Blocked) approach = n;
        }
        if (approach.x < 0 || !BuildRouteList(level, &unit, from, approach, route)) return false;
        route.push_back(t.block);  // walk on into the wall; movement stops at the face
        if (!objects.RouteBegin(unit, route)) return false;
        if (t.type == TaskType::Reinforce) {
            unit.route.back().action = RouteAction::Reinforce;  // LegoObject_TryReinforceBlock
            break;
        }
        if (t.type == TaskType::Dynamite) {
            unit.route.back().action = RouteAction::PlaceDynamite;
            break;
        }
        unit.route.back().action = RouteAction::Drill;
        unit.singleWidthDig = t.singleWidth;
        break;
    }
    case TaskType::Clear:
        if (!RouteBlocks(level, unit, from, t.block, route) || !objects.RouteBegin(unit, route)) return false;
        unit.route.back().action = RouteAction::Clear;
        break;
    case TaskType::Upgrade:
        if (unit.type == ObjType::Vehicle) {
            // LegoObject_TryUpgrade: to the block of the station's deposit null, arriving in the
            // direction from the station's block to it in block coordinates. Block y runs
            // against world y, so the vehicle ends up facing the station.
            GameObject* station = t.target;
            const ActivityModel* am = station && station->info ? station->info->model.activities.get() : nullptr;
            Vec3f np;
            Vec2 nd;
            BlockPos sb, to;
            if (!am || !objects.BlockOf(*station, sb) || !objects.NullPoint(*station, am->depositNull, np, nd) ||
                !level.map().WorldToBlock(np.x, np.y, to.x, to.y))
                return false;
            if (!RouteBlocks(level, unit, from, to, route) || !objects.RouteBegin(unit, route)) return false;
            const int dx = to.x - sb.x, dy = to.y - sb.y;
            const uint8_t face = std::abs(dx) <= std::abs(dy) ? (dy <= 0 ? 2 : 0) : (dx <= 0 ? 3 : 1);
            unit.route.back().action = RouteAction::Upgrade;
            unit.route.back().flags |= RouteFlag::Face | face;
            unit.stateTarget = station;
            unit.upgradeType = t.upgradeType;
            break;
        }
        [[fallthrough]];  // minifigures: LegoObject_TryTrain with the upgrade action
    case TaskType::Train: {
        // LegoObject_TryTrain: on a block beside the building, 4 units from the shared edge at a
        // random point along it, facing the building.
        BlockPos bb;
        if (!t.target || !objects.BlockOf(*t.target, bb)) return false;
        static const int kDx[4] = {0, 1, 0, -1}, kDy[4] = {-1, 0, 1, 0};
        const world::Map3D& map = level.map();
        const float bs = map.blockSize();
        bool routed = false;
        ++approachCounter_;
        for (int i = 0; i < 4 && !routed; ++i) {
            const int d = int((approachCounter_ + unsigned(i)) & 3);
            const BlockPos n{bb.x + kDx[d], bb.y + kDy[d]};
            if (!level.InBounds(n.x, n.y) ||
                GetCrossTerrainType(level, &unit, n.x, n.y, n.x, n.y, false) == CrossType::Blocked)
                continue;
            if (!RouteBlocks(level, unit, from, n, route)) continue;
            const Vec2 nc = map.BlockCentre(n.x, n.y), bc = map.BlockCentre(bb.x, bb.y);
            const Vec2 dir{(bc.x - nc.x) / bs, (bc.y - nc.y) / bs};  // towards the building
            const float along = (random::RandRange(0.1f, 0.9f) - 0.5f) * bs;
            const Vec2 at{nc.x + dir.x * (bs * 0.5f - 4.0f) - dir.y * along,
                          nc.y + dir.y * (bs * 0.5f - 4.0f) + dir.x * along};
            if (!objects.RouteBegin(unit, route, &at)) return false;
            const uint8_t face = dir.y > 0.5f ? 0 : dir.x > 0.5f ? 1 : dir.y < -0.5f ? 2 : 3;
            unit.route.back().action = t.type == TaskType::Upgrade ? RouteAction::Upgrade : RouteAction::Train;
            unit.route.back().flags |= RouteFlag::Face | face;
            unit.stateTarget = t.target;
            routed = true;
        }
        if (!routed) return false;
        break;
    }
    case TaskType::FindDriver: {
        // LegoObject_TryFindDriver_FUN_00440690: walk onto the vehicle's position and arrive
        // facing the way it faces.
        // TODO(vehicles): GetInAtLand boats are boarded from the land block beside them.
        BlockPos tb;
        if (!t.target || !objects.BlockOf(*t.target, tb)) return false;
        const Vec2 at{t.target->position.x, t.target->position.y};
        if (!RouteBlocks(level, unit, from, tb, route) || !objects.RouteBegin(unit, route, &at)) return false;
        const Vec2 f = t.target->faceDir;
        const uint8_t face = std::fabs(f.x) <= std::fabs(f.y) ? (f.y <= 0.0f ? 2 : 0) : (f.x <= 0.0f ? 3 : 1);
        unit.route.back().action = RouteAction::GetIn;
        unit.route.back().flags |= RouteFlag::Face | face;
        unit.stateTarget = t.target;
        break;
    }
    case TaskType::Collect: {
        BlockPos tb;
        if (!t.target || !objects.BlockOf(*t.target, tb)) return false;
        const Vec2 at{t.target->position.x, t.target->position.y};
        if (!RouteBlocks(level, unit, from, tb, route) || !objects.RouteBegin(unit, route, &at)) return false;
        unit.route.back().action = RouteAction::Collect;
        unit.stateTarget = t.target;
        break;
    }
    case TaskType::BuildPath:
        if (!RouteBlocks(level, unit, from, t.block, route) || !objects.RouteBegin(unit, route)) return false;
        unit.route.back().action = RouteAction::BuildPath;
        break;
    case TaskType::GetTool: {
        // LegoObject_DoGetTool: walk into the nearest tool store.
        // TODO(models): the store's tool null for each tool instead of the store block.
        GameObject* store = NearestToolStore(objects, unit);
        BlockPos sb;
        if (!store || !objects.BlockOf(*store, sb)) return false;
        if (!RouteBlocks(level, unit, from, sb, route) || !objects.RouteBegin(unit, route)) return false;
        unit.route.back().action = RouteAction::GetTool;
        unit.wantedTool = t.tool;
        break;
    }
    case TaskType::Request: {
        // Off to the nearest storage building to take the resource out.
        GameObject* store = NearestStore(objects, unit);
        BlockPos sb;
        if (!store || !objects.BlockOf(*store, sb)) return false;
        if (!RouteBlocks(level, unit, from, sb, route) || !objects.RouteBegin(unit, route)) return false;
        unit.route.back().action = RouteAction::Fetch;
        unit.stateTarget = store;
        unit.fetchType = t.resource;
        unit.deliverZone = t.zone;
        break;
    }
    default: return false;
    }
    t.assigned = &unit;
    t.started = true;
    return true;
}

void TaskSystem::Update(ObjectList& objects, world::Level& level, float elapsed) {
    objects_ = &objects;
    for (auto& t : tasks_) {
        if (t->retryTimer > 0.0f) t->retryTimer -= elapsed;
        if (t->target && t->target->removed) t->remove = true;
        if (t->type == TaskType::FindDriver && t->target && t->target->driver && !t->assigned) t->remove = true;
        // An upgrade the unit has meanwhile got (ordered twice) or can no longer take.
        if (t->type == TaskType::Upgrade && !t->assigned && t->forUnit &&
            (t->forUnit->removed || !t->forUnit->stats ||
             (t->forUnit->type == ObjType::Vehicle ? !ObjectList::CanUpgradeType(*t->forUnit, t->upgradeType, true)
                                                   : t->forUnit->stats->levels <= t->forUnit->level + 1)))
            t->remove = true;
        // A unit that stopped working on its task without finishing it (route abandoned, unit
        // selected, removed...) gives the task back.
        if (GameObject* u = t->assigned) {
            if (u->removed || (!u->routing && u->state == UnitState::Idle && !u->teleportingIn)) {
                t->assigned = nullptr;
                t->started = false;
            }
        }
    }
    tasks_.erase(std::remove_if(tasks_.begin(), tasks_.end(), [](const auto& t) { return t->remove; }), tasks_.end());

    std::vector<GameObject*> idle;
    for (const auto& op : objects.objects()) {
        GameObject& u = *op;
        if (u.removed || u.hidden || u.driving) continue;
        if (u.type != ObjType::MiniFigure && !(u.type == ObjType::Vehicle && u.driver)) continue;
        if (u.routing || u.state != UnitState::Idle || u.teleportingIn || TaskOf(u)) continue;
        if (u.flags3 & ObjFlag3::Selected) continue;  // TODO(ai): tasks given to selected units
        if (u.carried && u.deliverZone) {
            // A resource fetched for a construction site goes there (Construction_Zone_PlaceResource).
            BlockPos from, to;
            std::vector<BlockPos> route;
            if (zoneBlock && zoneBlock(u.deliverZone, to) && objects.BlockOf(u, from) &&
                RouteBlocks(level, u, from, to, route) && objects.RouteBegin(u, route)) {
                u.route.back().action = RouteAction::Place;
            } else {
                u.deliverZone = 0;  // the site is gone: the load goes back to storage
            }
            continue;
        }
        // A vehicle's load goes to storage once it is full or the vehicle has stood still.
        GameObject* load = u.carried ? u.carried : (ObjectList::LoadReady(u) ? u.cargo.front() : nullptr);
        if (load) {
            // Deliver what it carries to the nearest store (AITask_DoDeposit,
            // LegoObject_TryDepositCarried). Buildings that store objects are walked into, to their
            // tool null (always instance 4); others are approached at their deposit null, facing
            // into the building.
            GameObject* best = DepositBuilding(objects, u, load->type);
            BlockPos from, to;
            std::vector<BlockPos> route;
            Vec3f np;
            Vec2 nd;
            const bool stores = best && best->stats && (best->stats->F1(best->level) & StatsFlag1::StoreObjects);
            const ActivityModel* am = best && best->info ? best->info->model.activities.get() : nullptr;
            bool atNull = am && objects.NullPoint(*best, stores ? am->toolNull : am->depositNull, np, nd, stores ? 4 : 0) &&
                          level.map().WorldToBlock(np.x, np.y, to.x, to.y);
            const Vec2 endPos{np.x, np.y};
            const bool haveFrom = best && objects.BlockOf(u, from);
            if (atNull && !(haveFrom && RouteBlocks(level, u, from, to, route))) {
                RR_DEBUG("Deposit null of %s at %d,%d is out of reach", best->info->name.c_str(), to.x, to.y);
                atNull = false;
            }
            if (haveFrom && (atNull || (objects.BlockOf(*best, to) && RouteBlocks(level, u, from, to, route))) &&
                objects.RouteBegin(u, route, atNull ? &endPos : nullptr)) {
                u.route.back().action = RouteAction::Deposit;
                if (atNull && !stores) {
                    const uint8_t dir = std::fabs(nd.x) <= std::fabs(nd.y) ? (nd.y <= 0.0f ? 0 : 2)
                                                                            : (nd.x <= 0.0f ? 1 : 3);
                    u.route.back().flags |= RouteFlag::Face | dir;
                }
                u.stateTarget = best;
                RR_DEBUG("%s takes %s to %s", u.info->name.c_str(), load->info->name.c_str(), best->info->name.c_str());
            }
            // TODO(ai): verify what units do with a load when there is nowhere to deliver it.
            continue;
        }
        idle.push_back(&u);
    }
    if (idle.empty()) return;

    // Highest priority first; the list already holds newer tasks before older ones.
    std::vector<Task*> order;
    for (auto& t : tasks_) {
        if (t->type != TaskType::GetTool) t->priority = priorityValue_[size_t(t->priorityType)];
        if (!t->assigned && !t->remove) order.push_back(t.get());
    }
    std::stable_sort(order.begin(), order.end(), [](const Task* a, const Task* b) { return a->priority > b->priority; });

    for (Task* t : order) {
        if (idle.empty()) break;
        const Vec2 tp = TaskPosition(*t, level);
        // The nearest capable unit takes the task (straight-line distance, like the game).
        size_t bestIdx = idle.size();
        float bestD = 0.0f;
        for (size_t i = 0; i < idle.size(); ++i) {
            const GameObject& u = *idle[i];
            if (!CanDo(*t, u, level)) continue;
            const float dx = tp.x - u.position.x, dy = tp.y - u.position.y;
            const float d = std::sqrt(dx * dx + dy * dy);
            if (bestIdx == idle.size() || d < bestD) {
                bestIdx = i;
                bestD = d;
            }
        }
        if (bestIdx == idle.size()) {
            // Nobody has the tool: AITask_DoGetTool_FromTask sends a unit for it.
            const ToolType need = ToolFor(t->type);
            if (need != ToolType::Count && !t->toolRequested) {
                for (GameObject* u : idle) {
                    if (!CanDoIgnoringTools(*t, *u, level) || HasGetToolTask(need)) continue;
                    CreateGetTool(need);
                    t->toolRequested = true;
                    break;
                }
            }
            continue;
        }
        GameObject& unit = *idle[bestIdx];
        if (!Start(*t, unit, objects, level)) {
            RR_DEBUG("Task %d could not start for %s", int(t->type), unit.info->name.c_str());
            t->retryTimer = kRetryTime;
            continue;
        }
        RR_DEBUG("Task %d at %d,%d -> %s", int(t->type), t->block.x, t->block.y, unit.info->name.c_str());
        idle[bestIdx] = idle.back();
        idle.pop_back();
    }
}

ToolType TaskSystem::ToolFor(TaskType t) {
    // Stats_AddToolTaskType: the only tasks that need a tool.
    switch (t) {
    case TaskType::Dig: return ToolType::Drill;
    case TaskType::Clear: return ToolType::Spade;
    case TaskType::Reinforce: return ToolType::Hammer;
    case TaskType::Repair: return ToolType::Spanner;
    default: return ToolType::Count;
    }
}

bool TaskSystem::HasGetToolTask(ToolType tool) const {
    for (const auto& t : tasks_)
        if (t->type == TaskType::GetTool && t->tool == tool && !t->remove && !t->forUnit) return true;
    return false;
}

void TaskSystem::TaskFinished(GameObject& unit, bool success) {
    Task* t = TaskOf(unit);
    if (!t) return;
    if (!success) {
        t->assigned = nullptr;
        t->started = false;
        t->retryTimer = kRetryTime;
        return;
    }
    t->remove = true;
    if (t->type == TaskType::Dig) {
        // AITask_RemoveWallReferences: every request for the destroyed wall goes.
        for (auto& o : tasks_)
            if ((o->type == TaskType::Dig || o->type == TaskType::Reinforce) && o->block == t->block) o->remove = true;
    } else if (t->type == TaskType::Reinforce) {
        // AITask_RemoveReinforceReferences.
        for (auto& o : tasks_)
            if (o->type == TaskType::Reinforce && o->block == t->block) o->remove = true;
    }
}

void TaskSystem::RemoveObject(const GameObject& o) {
    for (auto& t : tasks_) {
        if (t->target == &o) t->remove = true;
        if (t->assigned == &o) {
            t->assigned = nullptr;
            t->started = false;
        }
    }
}

}  // namespace rr
