#pragma once

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "config/Config.h"
#include "gfx/Flic.h"
#include "gfx/Font.h"
#include "gfx/Image.h"
#include "platform/Input.h"

namespace rr::front {

// Front-end menus as defined in Lego.cfg "Menu" blocks (MainMenuFull, SaveMenu, PausedMenu,
// OptionsMenu). Behaviour follows the original Front_* module: hover selects, the left button
// activates on release, the Right/Left arrow keys step the hovered item, and every activation
// plays SFX_Okay.

enum class ItemType { Cycle = 0, Trigger = 1, TextInput = 2, Slider = 3, RealSlider = 4, Next = 5, Select = 6 };

struct Menu;

// What an item writes to and calls (the original passed (value*, callback) pairs to the loader,
// consumed by Cycle/Trigger/Slider/RealSlider items in file order).
struct Binding {
    int* intValue = nullptr;
    float* realValue = nullptr;
    std::function<void(int)> onInt;    // cycle / slider callbacks
    std::function<void(float)> onReal;
    std::function<void()> onTrigger;
};

struct CycleData {
    std::vector<std::string> names;
    int* value = nullptr;
    int x2 = 0, y2 = 0;
    std::function<void(int)> callback;
};

struct TriggerData {
    int* value = nullptr;
    bool end = false;
    std::function<void()> callback;
};

struct SliderData {
    int* value = nullptr;
    int min = 0, max = 0;
    int x2 = 0, y2 = 0;
    std::function<void(int)> callback;
    // OffBar, OnBar, LeftCap, RightCap, Plus, Minus, PlusHi, MinusHi
    std::array<const Image*, 8> images{};
};

struct RealSliderData {
    float* value = nullptr;
    float min = 0, max = 0, step = 0;
    int x2 = 0, y2 = 0;
    std::function<void(float)> callback;
};

struct TextInputData {
    std::string buffer;
    int caret = 0;
    int maxLength = 0;
    int x2 = 0, y2 = 0;
};

// One entry of a select list (level picker caverns, save slots).
struct SelectEntry {
    bool isText = false, isImage = false, enabled = false;
    std::string text;
    std::array<const Image*, 3> images{};  // 0 hovered, 1 normal, 2 locked
    std::array<int, 3> widths{}, heights{};
    int x = 0, y = 0;
    bool frontEndOpen = false;
};

struct SelectData {
    int* value = nullptr;
    std::string upText, downText;  // scroll markers
    int x = 0, y = 0;
    int lineHeight = 0, visibleCount = 0;
    int upX = 0, upY = 0, downX = 0, downY = 0;
    int scroll = 0;
    std::vector<SelectEntry> entries;
    // Called with (elapsed, index) while an entry is hovered and when it is chosen.
    std::function<void(float, int)> callback;
    Menu* next = nullptr;  // menu to go to after choosing; null closes the screen
};

struct MenuItem {
    ItemType type = ItemType::Next;
    std::string banner;
    const Font* loFont = nullptr;
    const Font* hiFont = nullptr;
    int x = 0, y = 0;
    int loCentre = 0, hiCentre = 0;  // -width/2 when the menu auto-centres
    bool notInTutorial = false;
    // Image buttons (Trigger/Next with bitmaps)
    bool isImage = false;
    const Image* loImage = nullptr;
    const Image* hiImage = nullptr;
    std::string toolTip;

    CycleData cycle;
    TriggerData trigger;
    SliderData slider;
    RealSliderData real;
    TextInputData text;
    SelectData select;
    Menu* next = nullptr;
};

struct MenuOverlay {
    std::string flicPath;
    int sfx = 0;
    int x = 0, y = 0;
};

struct Menu {
    std::string configPath;  // e.g. "Menu::MainMenuFull::Menu1"
    std::string title, fullName;
    const Font* menuFont = nullptr;
    int x = 0, y = 0;
    bool autoCentre = false, displayTitle = false, canScroll = false, playRandom = false;
    int titleCentre = 0;
    bool anchored = false;
    int anchorX = 0, anchorY = 0;
    const Image* image = nullptr;      // "MenuImage"
    const Image* imageDark = nullptr;  // "MenuImageDark"
    bool imagePositioned = false;
    float imageX = 0, imageY = 0;
    std::vector<std::unique_ptr<MenuItem>> items;
    std::vector<MenuOverlay> overlays;

    // Runtime state
    int hovered = -1;
    bool closed = false;
};

struct MenuSet {
    std::vector<std::unique_ptr<Menu>> menus;
    // Finds a menu by title; '_' and ' ' compare equal (titles are stored with spaces).
    int IndexOf(std::string_view title) const;
    Menu* menu(int i) const { return i >= 0 && i < int(menus.size()) ? menus[size_t(i)].get() : nullptr; }
};

// Shared image/font cache keyed by file name (Front_Cache).
class AssetCache {
public:
    const Image* LoadImage(std::string_view path);
    const Font* LoadFont(std::string_view path);

private:
    std::unordered_map<std::string, std::unique_ptr<Image>> images_;
    std::unordered_map<std::string, std::unique_ptr<Font>> fonts_;
};

// Loads "Lego*::Menu::<name>". Bindings are handed to items that need one, in file order.
std::unique_ptr<MenuSet> LoadMenuSet(const Config& cfg, std::string_view gameName, std::string_view name,
                                     std::vector<Binding> bindings, AssetCache& cache);

std::string ReplaceUnderscores(std::string_view s);

// Per-frame menu logic and drawing for one menu; returns the menu to show next (may be the same).
struct MenuContext {
    Input* input = nullptr;
    float mouseX = 0, mouseY = 0;
    bool mouseReleased = false;  // left button went up this tick
    bool rightKey = false, leftKey = false;
    bool tutorialActive = false;  // disables NotInTuto items
    int scrollX = 0, scrollY = 0;  // level picker background scroll
    std::function<void()> playOkay;
    std::function<void(const std::string&)> showToolTip;
};

Menu* UpdateMenuInput(Menu& menu, MenuContext& ctx, float elapsed);
// Drawing happens in the original's order: background, (overlays), title + items, (level text
// panel), bitmap buttons, (pointer).
void DrawMenuBackground(const Menu& menu, const MenuContext& ctx, Renderer2D& r);
void DrawMenuItems(Menu& menu, MenuContext& ctx, Renderer2D& r, float elapsed, int screenWidth);
void DrawMenuButtons(const Menu& menu, MenuContext& ctx, Renderer2D& r);
// Scrolls tall level-picker backgrounds when the mouse is near the top/bottom edge.
void UpdateMenuScroll(const Menu& menu, MenuContext& ctx);

}  // namespace rr::front
