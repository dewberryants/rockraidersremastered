#include "game/ai/Tasks.h"

#include <algorithm>
#include <cmath>

#include "core/Log.h"
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
}

void TaskSystem::LoadPriorities(const Config& cfg, std::string_view levelBlock) {
    int index = 0;
    for (Config::ItemRef it = cfg.FindArray(Config::Id(levelBlock, "Priorities")); it != Config::kNone;
         it = cfg.Next(it)) {
        for (int p = 0; p < int(PriorityType::Count); ++p) {
            if (!EqualsNoCase(cfg.item(it).key, PriorityTypeName(PriorityType(p)))) continue;
            priorityValue_[size_t(p)] = 80 - 5 * index;
            priorityOff_[size_t(p)] = ParseBool(cfg.item(it).value) != BoolTri::True;
            ++index;
            break;
        }
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
    // TODO(ai): vehicles and rock monsters take tasks too; trained skills.
    if (unit.type != ObjType::MiniFigure || !unit.stats) return false;
    if (unit.carried) return false;
    // LegoObject_TaskHasTool: minifigures need the task's tool on their belt.
    if (const ToolType need = ToolFor(t.type); need != ToolType::Count && !unit.HasTool(need)) return false;
    return CanDoIgnoringTools(t, unit, level);
}

bool TaskSystem::CanDoIgnoringTools(const Task& t, const GameObject& unit, const world::Level& level) const {
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
    case TaskType::Clear: {
        if (!(unit.stats->F1(unit.level) & StatsFlag1::CanClearRubble)) return false;
        const world::Block& b = level.block(t.block.x, t.block.y);
        return (b.flags & world::BlockFlag::Ground) && !(b.flags & world::BlockFlag::CleanFloor);
    }
    case TaskType::Collect: return t.target && !t.target->removed && !t.target->carrier && !t.target->hidden;
    case TaskType::Request: return !hasStock || hasStock(t.resource);
    case TaskType::BuildPath: return !(level.block(t.block.x, t.block.y).flags & world::BlockFlag::Path);
    case TaskType::GetTool: return t.tool != ToolType::Count && !unit.HasTool(t.tool);
    default: return false;
    }
}

bool TaskSystem::Start(Task& t, GameObject& unit, ObjectList& objects, world::Level& level) {
    BlockPos from;
    if (!objects.BlockOf(unit, from)) return false;
    std::vector<BlockPos> route;
    switch (t.type) {
    case TaskType::Dig: {
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
        unit.route.back().action = RouteAction::Drill;
        unit.singleWidthDig = t.singleWidth;
        break;
    }
    case TaskType::Clear:
        if (!RouteBlocks(level, unit, from, t.block, route) || !objects.RouteBegin(unit, route)) return false;
        unit.route.back().action = RouteAction::Clear;
        break;
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
    for (auto& t : tasks_) {
        if (t->retryTimer > 0.0f) t->retryTimer -= elapsed;
        if (t->target && t->target->removed) t->remove = true;
    }
    tasks_.erase(std::remove_if(tasks_.begin(), tasks_.end(), [](const auto& t) { return t->remove; }), tasks_.end());

    std::vector<GameObject*> idle;
    for (const auto& op : objects.objects()) {
        GameObject& u = *op;
        if (u.removed || u.hidden || u.type != ObjType::MiniFigure) continue;
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
        if (u.carried) {
            // Deliver what it carries to the nearest store (AITask_DoDeposit).
            GameObject* best = NearestStore(objects, u);
            BlockPos from, to;
            std::vector<BlockPos> route;
            if (best && objects.BlockOf(u, from) && objects.BlockOf(*best, to) &&
                RouteBlocks(level, u, from, to, route) && objects.RouteBegin(u, route)) {
                u.route.back().action = RouteAction::Deposit;
                u.stateTarget = best;
                RR_DEBUG("%s takes %s to %s", u.info->name.c_str(), u.carried->info->name.c_str(), best->info->name.c_str());
            }
            // TODO(ai): verify what units do with a load when there is nowhere to deliver it.
            continue;
        }
        idle.push_back(&u);
    }
    if (idle.empty()) return;

    // Highest priority first; the list already holds newer tasks before older ones.
    std::vector<Task*> order;
    for (auto& t : tasks_)
        if (!t->assigned && !t->remove) order.push_back(t.get());
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
            if (o->type == TaskType::Dig && o->block == t->block) o->remove = true;
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
