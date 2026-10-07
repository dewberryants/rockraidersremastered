#include "game/ui/Priorities.h"

#include <algorithm>
#include <cstdlib>

#include "config/Config.h"
#include "core/Log.h"
#include "fs/Path.h"
#include "gfx/Renderer2D.h"
#include "platform/Input.h"

namespace rr {

void PriorityPanel::Load(const Config& cfg, std::string_view gameName) {
    // "AI_Priority_Crystal  Collect_Energy_Crystals|PriSFX_Crystals:<normal>:<pressed>:<off>"
    for (Config::ItemRef it = cfg.FindArray(Config::Id(gameName, "PriorityImages")); it != Config::kNone;
         it = cfg.Next(it)) {
        for (int p = 0; p < int(PriorityType::Count); ++p) {
            if (!EqualsNoCase(cfg.item(it).key, PriorityTypeName(PriorityType(p)))) continue;
            const auto parts = SplitTokens(cfg.item(it).value, ":");
            PriorityImage& img = images_[size_t(p)];
            if (!parts.empty()) {
                std::string name(SplitTokens(parts[0], "|")[0]);
                std::replace(name.begin(), name.end(), '_', ' ');
                img.name = name;
                // TODO(audio): the |sound after the name is spoken with the tooltip.
            }
            if (parts.size() > 1) img.normal = Image::Load(parts[1]);
            if (parts.size() > 2) img.pressed = Image::Load(parts[2]);
            if (parts.size() > 3) img.off = Image::Load(parts[3]);
            break;
        }
    }
    // "1  41,26": positions of the list slots relative to the panel.
    positions_.clear();
    for (Config::ItemRef it = cfg.FindArray(Config::Id(gameName, "PrioritiesImagePositions")); it != Config::kNone;
         it = cfg.Next(it)) {
        const int slot = std::atoi(cfg.item(it).key.c_str());
        const auto xy = SplitTokens(cfg.item(it).value, ",");
        if (slot < 1 || xy.size() < 2) continue;
        if (positions_.size() < size_t(slot)) positions_.resize(size_t(slot));
        positions_[size_t(slot - 1)] = {float(std::atof(std::string(xy[0]).c_str())),
                                        float(std::atof(std::string(xy[1]).c_str()))};
    }
}

bool PriorityPanel::HandleInput(const Input& in, float panelX, float panelY, TaskSystem& tasks) {
    const float mx = in.mouseX(), my = in.mouseY();
    hover_ = -1;
    hoverToolTip_.clear();
    const auto& order = tasks.priorityOrder();
    for (size_t i = 0; i < order.size() && i < positions_.size(); ++i) {
        const PriorityImage& img = images_[size_t(order[i])];
        if (!img.normal) continue;
        const RectF rc{panelX + positions_[i].x, panelY + positions_[i].y, float(img.normal->width()),
                       float(img.normal->height())};
        if (!rc.Contains(mx, my)) continue;
        hover_ = int(i);
        hoverToolTip_ = img.name;
        break;
    }
    // Priorities_ChangePriorityPressed: press on an image, release on the same one to move it
    // to the top (the panel opens in move-to-top mode).
    if (in.leftClicked()) pressed_ = hover_;
    if (in.leftReleased()) {
        if (pressed_ >= 0 && pressed_ == hover_) tasks.MovePriorityTop(size_t(hover_));
        pressed_ = -1;
    }
    if (!in.leftDown() && !in.leftReleased()) pressed_ = -1;
    return hover_ >= 0;
}

void PriorityPanel::Draw(Renderer2D& r, float panelX, float panelY, const TaskSystem& tasks) const {
    const auto& order = tasks.priorityOrder();
    for (size_t i = 0; i < order.size() && i < positions_.size(); ++i) {
        const PriorityImage& img = images_[size_t(order[i])];
        const Image* shown = tasks.PriorityOff(order[i]) ? img.off.get()
                             : (pressed_ == int(i) && hover_ == int(i)) ? img.pressed.get()
                                                                        : img.normal.get();
        if (shown) shown->Draw(r, panelX + positions_[i].x, panelY + positions_[i].y);
    }
    // Interface_DrawHoverOutline: two green frames just outside the hovered image.
    if (hover_ >= 0 && size_t(hover_) < order.size()) {
        const PriorityImage& img = images_[size_t(order[size_t(hover_)])];
        if (img.normal) {
            const float x = panelX + positions_[size_t(hover_)].x, y = panelY + positions_[size_t(hover_)].y;
            const float w = float(img.normal->width() - 1), h = float(img.normal->height() - 1);
            for (float d : {1.0f, 2.0f})
                r.DrawRectOutline({x - d, y - d, w + 2.0f * d, h + 2.0f * d}, ColorF{0, 1, 0, 1});
        }
    }
}

}  // namespace rr
