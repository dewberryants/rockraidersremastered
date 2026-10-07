#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "config/Config.h"
#include "game/objects/ObjectTypes.h"
#include "game/objects/Routing.h"

namespace rr {

struct GameObject;
class ObjectList;
namespace world {
class Level;
}

// Kinds of work units can be given (AITask_Type).
enum class TaskType : int {
    Goto, Follow, FollowAttack, Collect, Gather, Deposit, Dump, Request, Dig, Dynamite, Repair, Reinforce, Clear,
    Wait, AnimationWait, ElecFence, Eat, GotoEat, FindDriver, GetTool, BirdScarer, Upgrade, BuildPath, Train,
    Depart, AttackPath, AttackRockMonster, Recharge, Dock, AttackObject, FindLoad,
    Count
};

// Priority classes; each level orders them in its Priorities block (AI_Priority_*).
enum class PriorityType : int {
    Crystal, Ore, DefaultCollect, Destruction, Construction, Request, Reinforce, Repair, Clearing, Storage,
    Refining, HealthLow, GetIn, Upgrade, BuildPath, AttackRockMonster, Barrier, FindLoad, Recharge,
    UpgradeBuilding, Gather, Steal, Punch, Depart, AttackPath, AttackObject, Congregate,
    // AI_Priority_Train / AI_Priority_GetTool in level lists are not recognised by the game.
    Count
};
const char* PriorityTypeName(PriorityType p);  // "AI_Priority_Crystal"

struct Task {
    TaskType type = TaskType::Goto;
    BlockPos block;
    GameObject* target = nullptr;
    PriorityType priorityType = PriorityType::DefaultCollect;
    int priority = 0;          // 0..99, higher first
    float retryTimer = 0.0f;   // not offered to units while > 0
    bool userRequested = false;
    bool singleWidth = false;  // dig a one-unit tunnel (single-width diggers only)
    bool remove = false;
    GameObject* assigned = nullptr;
    bool started = false;
    // Request: bring a resource of this type from storage to construction site `zone`.
    ObjType resource = ObjType::None;
    int zone = 0;
    ToolType tool = ToolType::Count;   // GetTool: the tool to fetch
    uint32_t ability = 0;              // Train: the skill to learn (Ability)
    bool toolRequested = false;        // a GetTool job has been raised for this task
    GameObject* forUnit = nullptr;     // only this unit may take the task
};

// The pending task list and the per-unit task assignment (AITask_*).
class TaskSystem {
public:
    void Reset();
    // Priorities_LoadLevel: values 80, 75, 70, ... in list order; FALSE switches a type off.
    void LoadPriorities(const Config& cfg, std::string_view levelBlock);

    // The level's priority list as the priority panel shows it (Priorities_*). Slot i is worth
    // 80 - 5i; tasks take their type's current value on every update.
    const std::vector<PriorityType>& priorityOrder() const { return order_; }
    bool PriorityOff(PriorityType p) const { return priorityOff_[size_t(p)]; }
    void MovePriorityUp(size_t slot);   // Priorities_MovePriorityUp: swap with the slot above
    void MovePriorityTop(size_t slot);  // Priorities_MovePriorityTop
    void TogglePriority(size_t slot);   // Priorities_TurnPriorityOff (toggles)
    void ResetPriorities();             // Priorities_Reset: the level's order and switches

    Task* CreateDig(const world::Level& level, BlockPos block, bool userRequested);
    Task* CreateClear(BlockPos block);
    // AITask_DoReinforce_AtBlockPos.
    Task* CreateReinforce(const world::Level& level, BlockPos block);
    // AITask_DoTrain_Target: learn a skill at a building that trains it.
    Task* CreateTrain(GameObject& building, uint32_t ability, GameObject* forUnit);
    // Removes the block's pending tasks of a type; returns the units that were on them.
    std::vector<GameObject*> CancelBlockTasks(BlockPos block, TaskType type);
    bool HasBlockTask(BlockPos block, TaskType type) const;
    Task* CreateCollect(GameObject& object);
    // AITask_DoRequest_ObjectType: fetch one resource from storage for a construction site.
    Task* CreateRequest(int zone, ObjType resource);
    // AITask_DoBuildPath_AtPosition: lay the power path once its resources are on the block.
    Task* CreateBuildPath(BlockPos block);
    // AITask_DoGetTool: a unit fetches a tool from a tool store (optionally a given unit).
    Task* CreateGetTool(ToolType tool, GameObject* forUnit = nullptr);
    void RemoveZone(int zone);

    // Game hooks: stock left in storage, and where a construction site takes deliveries.
    std::function<bool(ObjType)> hasStock;
    std::function<bool(int zone, BlockPos& block)> zoneBlock;

    // AITask_UpdateAll: assigns pending tasks to idle units and starts them.
    void Update(ObjectList& objects, world::Level& level, float elapsed);
    // Called by the object system when a unit finished or abandoned its task.
    void TaskFinished(GameObject& unit, bool success);
    // Drops tasks and assignments that refer to a removed object.
    void RemoveObject(const GameObject& o);

    Task* TaskOf(const GameObject& unit) const;
    // Gives a task straight to a unit (orders for selected units); false if it cannot start.
    bool AssignTo(Task& t, GameObject& unit, ObjectList& objects, world::Level& level) {
        return !t.assigned && Start(t, unit, objects, level);
    }
    const std::vector<std::unique_ptr<Task>>& tasks() const { return tasks_; }

private:
    bool CanDo(const Task& t, const GameObject& unit, const world::Level& level) const;
    bool CanDoIgnoringTools(const Task& t, const GameObject& unit, const world::Level& level) const;
    bool HasGetToolTask(ToolType tool) const;
    static ToolType ToolFor(TaskType t);
    bool Start(Task& t, GameObject& unit, ObjectList& objects, world::Level& level);
    Vec2 TaskPosition(const Task& t, const world::Level& level) const;

    std::vector<std::unique_ptr<Task>> tasks_;
    std::array<int, size_t(PriorityType::Count)> priorityValue_{};
    std::array<bool, size_t(PriorityType::Count)> priorityOff_{};
    std::vector<PriorityType> order_, initialOrder_;
    std::vector<bool> initialOff_;
    unsigned approachCounter_ = 0;  // rotates the side units approach walls from
};

// Drill time for a terrain in game units (0 = cannot drill); stats seconds are scaled by 25.
float DrillTime(const GameObject& unit, int terrain);

}  // namespace rr
