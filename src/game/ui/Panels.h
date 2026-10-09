#pragma once

#include <array>
#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "config/Config.h"
#include "core/Math3D.h"
#include "gfx/Image.h"

namespace rr {

class Font;
class Input;

// Screen panels of the level interface (Panel_Type).
enum class PanelType : int {
    Radar, RadarFill, RadarOverlay, Messages, MessagesSide, CrystalSideBar, TopPanel, Information, PriorityList,
    CameraControl, InfoDock, Encyclopedia,
    Count
};

// Panel buttons (PanelButton_*), named "<panel>_<button>" in the config.
enum class PanelButton : int {
    RadarToggle, RadarTaggedObjectView, RadarZoomIn, RadarZoomOut, RadarMapView, InfoDockGoto, InfoDockClose,
    TopPanelOptions, TopPanelPriorities, TopPanelCallToArms, InformationToggle, InformationFunction,
    PriorityListDisable1, PriorityListDisable2, PriorityListDisable3, PriorityListDisable4, PriorityListDisable5,
    PriorityListDisable6, PriorityListDisable7, PriorityListDisable8, PriorityListDisable9, PriorityListUpOne1,
    PriorityListUpOne2, PriorityListUpOne3, PriorityListUpOne4, PriorityListUpOne5, PriorityListUpOne6,
    PriorityListUpOne7, PriorityListUpOne8, PriorityListClose, PriorityListReset, EncyclopediaClose,
    CameraControlZoomIn, CameraControlZoomOut, CameraControlCycleBuildings, CameraControlRotate,
    CrystalSideBarOre, CrystalSideBarCrystals,
    Count
};

// The level HUD panels (Panel_*): images that slide between an open and a closed position,
// their buttons, and the crystal side bar with its crystal column and ore meter.
class Panels {
public:
    void Load(const Config& cfg, std::string_view gameName);
    void Reset();  // every panel closed (Panel_ResetAll)

    void Toggle(PanelType p);
    void SetOpen(PanelType p, bool open) { panels_[size_t(p)].open = open; }
    // Closed and done sliding (Panel_IsFullyClosed).
    bool IsFullyClosed(PanelType p) const {
        const Panel& q = panels_[size_t(p)];
        return !q.open && q.x == q.closedX && q.y == q.closedY;
    }
    // Current position in 640-wide layout coordinates, and the anchor offset it is drawn with.
    Vec2 Position(PanelType p) const { return {std::round(panels_[size_t(p)].x), std::round(panels_[size_t(p)].y)}; }
    float AnchorX(PanelType p) const { return panels_[size_t(p)].anchorX; }
    void SetAnchor(Renderer2D& r, PanelType p) { AnchorFor(r, p); }
    bool IsOpen(PanelType p) const { return panels_[size_t(p)].open; }
    // Open and done sliding (Panel_IsFullyOpen).
    bool IsFullyOpen(PanelType p) const {
        const Panel& q = panels_[size_t(p)];
        return q.open && q.x == q.openX && q.y == q.openY;
    }
    // Radar shows the map (true) or the tagged-object view; the fill only covers the latter.
    bool radarMapView = false;

    // Hover/press state and clicks; returns true if the mouse is over a panel.
    bool HandleInput(const Input& in);
    std::function<void(PanelButton)> onButton;
    const std::string& hoverToolTip() const { return hoverToolTip_; }

    struct Resources {
        int crystals = 0, usedCrystals = 0, quota = 26;
        int ore = 0, studs = 0;
    };
    // elapsed: real time in game units (panels slide at the same speed regardless of game speed).
    // The icon menu panel is drawn between these two calls.
    void DrawPanels(Renderer2D& r, float elapsed);
    void DrawSideBar(Renderer2D& r, const Font* font, const Resources& res, float elapsed);
    // Text_Update: the message panel at its fixed place with the air meter on it
    // (Panel_AirMeter_DrawJuice / DrawOxygenLow); `warning` shows the flashing no-air sign.
    void DrawMessagePanel(Renderer2D& r, float oxygen, bool warning);

private:
    struct Button {
        PanelButton type = PanelButton::Count;
        std::unique_ptr<Image> normal, highlight, pressed;
        RectF rect;  // relative to the panel
        std::string toolTip;
    };
    struct Panel {
        std::unique_ptr<Image> image;
        float openX = 0, openY = 0, closedX = 0, closedY = 0;
        float x = 0, y = 0;  // current (sliding) position
        bool open = false;
        // Drawn on screen (only those take the mouse), and where: x + anchorX is the screen x.
        bool shown = false;
        float anchorX = 0;
        std::vector<Button> buttons;
    };
    void DrawPanel(Renderer2D& r, PanelType p, float elapsed);
    void AnchorFor(Renderer2D& r, PanelType t);
    bool OverImage(const Panel& p, float mx, float my) const;

    std::array<Panel, size_t(PanelType::Count)> panels_;
    std::unique_ptr<Image> smallCrystal_, usedCrystal_, noSmallCrystal_, oreMeter_, airJuice_, noAir_;
    float oreMeterX_ = 615, oreMeterY_ = 434, oreMeterLength_ = 423;
    float oreMeterFill_ = 0.0f;  // 0..1
    int oreMeterCount_ = 0;      // ore units the fill reflects
    int hoverPanel_ = -1, hoverButton_ = -1;
    bool pressed_ = false;
    std::string hoverToolTip_;
    std::array<int, 3> crystalHistory_{}, usedHistory_{};
};

}  // namespace rr
