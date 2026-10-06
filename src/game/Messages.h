#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "game/objects/Routing.h"

namespace rr {

struct GameObject;

// Game events (Message_Type). Values follow the order of the original's message names; NERPs
// scripts and the interface post them, Message_Update dispatches them one tick later.
enum class MessageType : int {
    Null, Select, Selected, ClearSelection, Deselect, Goto, RockMonsterGoto, RockMonsterGotoComplete, UserGoto,
    FirstPerson, TrackObject, TopView, PlaySample, Dig, DigComplete, Repair, RepairComplete, Reinforce,
    ReinforceComplete, RockFall, RockFallComplete, GenerateCrystal, GenerateCrystalComplete, CollectCrystal,
    CollectCrystalComplete, CrystalToRefinery, CrystalToRefineryComplete, GenerateOre, GenerateOreComplete,
    CollectOre, CollectOreComplete, Clear, ClearComplete, ClearFallIn, ClearFallInComplete, GenerateRockMonster,
    GenerateRockMonsterComplete, GatherRock, GatherRockComplete, PickRandomFloor, PickRandomFloorComplete,
    AttackBuilding, AttackBuildingComplete, GetIn, ManSelectedAndMonsterClicked, FollowAttack, CollectTool,
    ReduceSelection, BuildPath, BuildPathComplete, Train, TrainComplete, GenerateCrystalAndOre,
    GenerateCrystalAndOreComplete, GenerateFromCryOre, GenerateFromCryOreComplete, Upgrade, UpgradeComplete,
    ClearBuilding, ClearBuildingComplete, ClearInitial, ClearInitialComplete, ClearRemovePath,
    ClearRemovePathComplete, DebugDestroyAll,
    Count
};

struct Message {
    MessageType type = MessageType::Null;
    GameObject* object = nullptr;
    uint32_t arg = 0;
    BlockPos block;
};

// Events posted while one batch is processed go into the other buffer, so every event is handled
// on the update after it was posted.
class MessageQueue {
public:
    static constexpr size_t kMaxPerBuffer = 0x800;

    void Post(MessageType type, GameObject* object = nullptr, uint32_t arg = 0, const BlockPos* block = nullptr);
    // Takes the pending batch; new posts during handling land in the next batch.
    void Update(const std::function<void(const Message&)>& handler);
    // Drops pending events referring to an object that is being removed.
    void RemoveObject(const GameObject* object);
    void Clear();

private:
    std::vector<Message> buffers_[2];
    int current_ = 0;
};

// The selected units (up to 100); the first one is the primary unit whose menu is shown.
class Selection {
public:
    static constexpr size_t kMaxUnits = 100;

    static bool IsSelectable(const GameObject& o);
    // Adds a unit; returns true if it was newly selected.
    bool Select(GameObject& o);
    bool Deselect(GameObject& o);
    void Clear();
    bool IsSelected(const GameObject& o) const;
    GameObject* Primary() const { return units_.empty() ? nullptr : units_.front(); }
    const std::vector<GameObject*>& units() const { return units_; }
    bool empty() const { return units_.empty(); }

private:
    std::vector<GameObject*> units_;
};

}  // namespace rr
