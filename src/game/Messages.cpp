#include "game/Messages.h"

#include <algorithm>

#include "game/objects/GameObject.h"

namespace rr {

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
    for (const Message& m : buffers_[batch]) handler(m);
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
