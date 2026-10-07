#pragma once

#include <SDL.h>

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "config/Config.h"
#include "game/objects/Routing.h"
#include "gfx/Image.h"

namespace rr {

class Input;
struct ObjectTypeInfo;

// Icon menu items (Interface_MenuItemType); ids index the game's item name table.
enum class MenuItem : int {
    BackToDefault, TeleportMan, BuildBuilding, BuildSmallVehicle, BuildLargeVehicle, LayPath, RemovePath,
    RepairLava, GeologistTest, ClearRubble, Dam, Dig, Reinforce, Dynamite, PlaceFence, DeselectDig,
    CancelConstruction, SelectMan, SelectVehicle, SelectBuilding, VehiclePickUp, MinifigurePickUp, UnLoadVehicle,
    UnLoadMinifigure, GetIn, GetOut, GotoDock, LegoManGoto, VehicleGoto, VehicleDig, LegoManDig, GoFeed,
    DeleteMan, DropBirdScarer, PowerOn, PowerOff, Repair, MakeTeleporter, EjectCrystal, EjectOre, DeleteBuilding,
    DeleteVehicle, DeleteElectricFence, Attack, GotoTopView, GotoFirstPerson, GotoSecondPerson, TrackObject,
    GetTool, GetDrill, GetSpade, GetHammer, GetSpanner, GetLaser, GetPusherGun, GetFreezerGun, GetBirdScarer,
    TrainSkill, TrainDriver, TrainEngineer, TrainGeologist, TrainPilot, TrainSailor, TrainDynamite, UpgradeMan,
    UpgradeBuilding, UpgradeVehicle, UpgradeEngine, UpgradeDrill, UpgradeScan, UpgradeCarry, Encyclopedia,
    ClearSelection, Build,
    Count
};

// Icon menus (Interface_MenuType). The building/vehicle build menus list object types instead of
// fixed items.
enum class MenuType : int {
    Main, Ground, Erode, PlaceFence, Construction, Rubble, Unused6, Wall, Creature, LegoMan, Vehicle,
    WaterVehicle, UnmannedVehicle, Building, ElectricFence, FirstPerson, GetTool, TrainSkill, UpgradeVehicle,
    BuildBuilding, BuildSmallVehicle, BuildLargeVehicle,
    // Not icon menus: the priority list and encyclopedia panels take the menu's place.
    Priorities, Encyclopedia,
    Count
};

const char* MenuItemName(MenuItem item);  // "Interface_MenuItem_Dig"

// The right-hand icon panel (Interface_*): loads item images and panel surrounds from the
// InterfaceImages / InterfaceSurroundImages / InterfaceBackButton config, stacks the current
// menu's icons from the panel origin and reports clicks.
class IconPanel {
public:
    struct ItemDef {
        std::unique_ptr<Image> normal, disabled, pressed;
        std::string toolTip, disabledToolTip;  // display text (underscores already spaces)
        std::string toolTipSfx, disabledToolTipSfx;
        SDL_Scancode key = SDL_SCANCODE_UNKNOWN;
        bool flag = false;  // optional trailing TRUE in the config
    };

    void Load(const Config& cfg, std::string_view gameName, float originX = 565.0f, float originY = 18.0f);

    // Interface_OpenMenu: the current menu slides off to the right, the new one comes back on
    // (_Interface_OpenMenu_SetNext). Reopening the shown menu only updates the block.
    void OpenMenu(MenuType menu, const BlockPos* block = nullptr);
    void BackToMain() { OpenMenu(MenuType::Main); }
    MenuType menu() const { return menu_; }
    // Interface_FUN_0041e9f0: advances the slide by the unscaled frame time.
    void Update(float elapsedAbs);
    bool Settled() const { return slide_ == Slide::Settled; }
    // The priority list / encyclopedia panel opens (true) once the menu is off screen, and
    // closes (false) when another menu is chosen.
    std::function<void(MenuType, bool)> onPanelMenu;
    const BlockPos& block() const { return block_; }

    // Decides per item whether it is shown enabled (the game greys out unusable items).
    std::function<bool(MenuItem)> isEnabled;
    // Called when an enabled item is clicked or its shortcut key pressed.
    std::function<void(MenuItem)> onItem;
    // Build menus list object types instead (InterfaceBuildImages): availability and choice.
    std::function<bool(const ObjectTypeInfo&)> isBuildEnabled;
    std::function<void(const ObjectTypeInfo&)> onBuild;

    // Returns true if the mouse is over the panel (the world should ignore the click).
    bool HandleInput(const Input& in);
    void Draw(Renderer2D& r) const;
    // Tooltip of the hovered item, empty if none.
    const std::string& hoverToolTip() const { return hoverToolTip_; }

private:
    struct BuildDef {
        const ObjectTypeInfo* type = nullptr;
        std::unique_ptr<Image> normal, disabled, pressed;
        SDL_Scancode key = SDL_SCANCODE_UNKNOWN;
        std::string toolTip;  // the object's display name (ObjectNames)
    };
    // One icon of the current menu: a fixed item or a buildable object type.
    struct Entry {
        MenuItem item = MenuItem::Count;
        const BuildDef* build = nullptr;
    };
    std::vector<Entry> Entries() const;
    const Image* EntryImage(const Entry& e, bool enabled, bool pressed) const;
    bool EntryEnabled(const Entry& e) const;
    void Activate(const Entry& e);

    const std::vector<MenuItem>& Items(MenuType m) const { return menus_[size_t(m)]; }
    bool HasBackButton(MenuType m) const { return m != MenuType::Main; }
    // Position of icon i of the current menu (stacked downwards from the origin).
    RectF ItemRect(size_t index) const;

    std::array<ItemDef, size_t(MenuItem::Count)> items_;
    std::vector<BuildDef> builds_;
    std::array<std::vector<MenuItem>, size_t(MenuType::Count)> menus_;
    struct Surround {
        std::unique_ptr<Image> image, imageNoBack;
        float x = 0, y = 0;            // with back button
        float noBackX = 0, noBackY = 0;
        float altX = 0, altY = 0;      // position of imageNoBack
    };
    std::array<Surround, 12> surrounds_;  // by item count (1..11)
    std::unique_ptr<Image> backHighlight_, backPressed_;
    float backX_ = 28, backY_ = 28;
    std::string backToolTip_;

    float originX_ = 565, originY_ = 18;
    float x_ = 565;  // current x of the menu (originX_ when on screen)
    enum class Slide { Settled, Out, In, Away } slide_ = Slide::Settled;
    MenuType next_ = MenuType::Main;
    BlockPos nextBlock_;
    bool panelOpen_ = false;  // a panel menu is open
    MenuType menu_ = MenuType::Main;
    BlockPos block_;
    int hover_ = -1;  // icon index under the mouse, -2 = back button
    bool pressed_ = false;
    std::string hoverToolTip_;
};

}  // namespace rr
