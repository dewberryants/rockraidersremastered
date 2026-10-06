#include "game/Messages.h"

#include <algorithm>

#include "config/Config.h"
#include "core/Log.h"
#include "fs/Path.h"
#include "game/objects/GameObject.h"

namespace rr {

namespace {
const char* const kMessageNames[int(MessageType::Count)] = {
    "Null", "Select", "Selected", "ClearSelection", "Deselect", "Goto", "RockMonsterGoto",
    "RockMonsterGotoComplete", "UserGoto", "FirstPerson", "TrackObject", "TopView", "PlaySample", "Dig",
    "DigComplete", "Repair", "RepairComplete", "Reinforce", "ReinforceComplete", "RockFall", "RockFallComplete",
    "GenerateCrystal", "GenerateCrystalComplete", "CollectCrystal", "CollectCrystalComplete", "CrystalToRefinery",
    "CrystalToRefineryComplete", "GenerateOre", "GenerateOreComplete", "CollectOre", "CollectOreComplete", "Clear",
    "ClearComplete", "ClearFallIn", "ClearFallInComplete", "GenerateRockMonster", "GenerateRockMonsterComplete",
    "GatherRock", "GatherRockComplete", "PickRandomFloor", "PickRandomFloorComplete", "AttackBuilding",
    "AttackBuildingComplete", "GetIn", "ManSelectedAndMonsterClicked", "FollowAttack", "CollectTool",
    "ReduceSelection", "BuildPath", "BuildPathComplete", "Train", "TrainComplete", "GenerateCrystalAndOre",
    "GenerateCrystalAndOreComplete", "GenerateFromCryOre", "GenerateFromCryOreComplete", "Upgrade",
    "UpgradeComplete", "ClearBuilding", "ClearBuildingComplete", "ClearInitial", "ClearInitialComplete",
    "ClearRemovePath", "ClearRemovePathComplete", "Debug_DestroyAll",
};
}  // namespace

MessageType MessageTypeFromName(std::string_view name) {
    if (StartsWithNoCase(name, "Message_")) name.remove_prefix(8);
    for (int i = 0; i < int(MessageType::Count); ++i)
        if (EqualsNoCase(name, kMessageNames[i])) return MessageType(i);
    return MessageType::Count;
}

bool MessageQueue::LoadTranslation(const std::string& file) {
    translation_.clear();
    if (file.empty()) return false;
    Config ptl;
    if (!ptl.Load(file)) return false;
    for (Config::ItemRef it = ptl.FindArray("Lego*"); it != Config::kNone; it = ptl.Next(it)) {
        const MessageType from = MessageTypeFromName(ptl.item(it).key);
        const MessageType to = MessageTypeFromName(ptl.item(it).value);
        if (from == MessageType::Count || to == MessageType::Count) {
            RR_WARN("%s: unknown event pair %s %s", file.c_str(), ptl.item(it).key.c_str(), ptl.item(it).value.c_str());
            continue;
        }
        translation_.emplace_back(from, to);
    }
    return true;
}

void MessageQueue::Post(MessageType type, GameObject* object, uint32_t arg, const BlockPos* block) {
    std::vector<Message>& buf = buffers_[current_];
    if (buf.size() >= kMaxPerBuffer) return;
    Message m;
    m.type = type;
    m.object = object;
    m.arg = arg;
    if (block) m.block = *block;
    buf.push_back(m);
}

void MessageQueue::Update(const std::function<void(const Message&)>& handler) {
    const int batch = current_;
    current_ ^= 1;
    buffers_[current_].clear();
    for (Message m : buffers_[batch]) {
        for (const auto& [from, to] : translation_) {
            if (m.type != from) continue;
            m.type = to;
            break;
        }
        handler(m);
    }
    buffers_[batch].clear();
}

void MessageQueue::RemoveObject(const GameObject* object) {
    for (auto& buf : buffers_)
        buf.erase(std::remove_if(buf.begin(), buf.end(), [&](const Message& m) { return m.object == object; }),
                  buf.end());
}

void MessageQueue::Clear() {
    buffers_[0].clear();
    buffers_[1].clear();
}

bool Selection::IsSelectable(const GameObject& o) {
    // TODO(objects): also not while teleporting, frozen, being carried or driven off-map.
    return (o.flags3 & ObjFlag3::Selectable) && !o.removed && !o.hidden;
}

bool Selection::Select(GameObject& o) {
    if (!IsSelectable(o) || IsSelected(o) || units_.size() >= kMaxUnits) return false;
    units_.push_back(&o);
    o.flags3 |= ObjFlag3::Selected;
    return true;
}

bool Selection::Deselect(GameObject& o) {
    auto it = std::find(units_.begin(), units_.end(), &o);
    if (it == units_.end()) return false;
    // Swap-remove like the original, so the last unit takes the freed slot.
    *it = units_.back();
    units_.pop_back();
    o.flags3 &= ~ObjFlag3::Selected;
    return true;
}

void Selection::Clear() {
    for (GameObject* o : units_) o->flags3 &= ~ObjFlag3::Selected;
    units_.clear();
}

bool Selection::IsSelected(const GameObject& o) const {
    return std::find(units_.begin(), units_.end(), &o) != units_.end();
}

}  // namespace rr
