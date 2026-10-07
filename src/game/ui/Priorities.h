#pragma once

#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/Math3D.h"
#include "game/ai/Tasks.h"
#include "gfx/Image.h"

namespace rr {

class Config;
class Input;
class Renderer2D;

// The contents of the priority list panel (Priorities_*): one image per priority of the level's
// list at PrioritiesImagePositions, drawn normal, pressed or switched off (PriorityImages).
// Clicking an image moves that priority to the top; the panel's own buttons switch priorities
// off, move them up one and reset the list.
class PriorityPanel {
public:
    void Load(const Config& cfg, std::string_view gameName);

    // Priorities_HandleInput; panelX/panelY is the panel's screen position. Returns true over an
    // image (the click belongs to the panel).
    bool HandleInput(const Input& in, float panelX, float panelY, TaskSystem& tasks);
    // Priorities_Draw; panelX/panelY in the renderer's current anchor space.
    void Draw(Renderer2D& r, float panelX, float panelY, const TaskSystem& tasks) const;
    const std::string& hoverToolTip() const { return hoverToolTip_; }

private:
    struct PriorityImage {
        std::unique_ptr<Image> normal, pressed, off;
        std::string name;  // display name (underscores as spaces)
    };
    std::array<PriorityImage, size_t(PriorityType::Count)> images_;
    std::vector<Vec2> positions_;  // per list slot, relative to the panel
    int hover_ = -1, pressed_ = -1;  // list slots; -1 = none
    std::string hoverToolTip_;
};

}  // namespace rr
