#include "game/ui/Interface.h"

#include <algorithm>
#include <cstdlib>

#include "core/Log.h"
#include "fs/Path.h"
#include "game/objects/ObjectRegistry.h"
#include "game/objects/Stats.h"
#include "game/Sfx.h"
#include "platform/Input.h"

namespace rr {

namespace {

const char* const kItemNames[int(MenuItem::Count)] = {
    "BackToDefault", "TeleportMan", "BuildBuilding", "BuildSmallVehicle", "BuildLargeVehicle", "LayPath",
    "RemovePath", "RepairLava", "GeologistTest", "ClearRubble", "Dam", "Dig", "Reinforce", "Dynamite",
    "PlaceFence", "DeselectDig", "CancelConstruction", "SelectMan", "SelectVehicle", "SelectBuilding",
    "VehiclePickUp", "MinifigurePickUp", "UnLoadVehicle", "UnLoadMinifigure", "GetIn", "GetOut", "GotoDock",
    "LegoManGoto", "VehicleGoto", "VehicleDig", "LegoManDig", "GoFeed", "DeleteMan", "DropBirdScarer", "PowerOn",
    "PowerOff", "Repair", "MakeTeleporterPrimary", "EjectCrystal", "EjectOre", "DeleteBuilding", "DeleteVehicle",
    "DeleteElectricFence", "Attack", "GotoTopView", "GotoFirstPerson", "GotoSecondPerson", "TrackObject",
    "GetTool", "GetDrill", "GetSpade", "GetHammer", "GetSpanner", "GetLaser", "GetPusherGun", "GetFreezerGun",
    "GetBirdScarer", "TrainSkill", "TrainDriver", "TrainEngineer", "TrainGeologist", "TrainPilot", "TrainSailor",
    "TrainDynamite", "UpgradeMan", "UpgradeBuilding", "UpgradeVehicle", "UpgradeEngine", "UpgardeDrill",
    "UpgardeScan", "UpgardeCarry", "Encyclopedia", "ClearSelection", "Build",
};

// Gods98 key names used for shortcuts ("KEY_B", "KEY_ONE", ...).
SDL_Scancode KeyFromName(std::string_view name) {
    if (!StartsWithNoCase(name, "KEY_")) return SDL_SCANCODE_UNKNOWN;
    name.remove_prefix(4);
    if (name.size() == 1 && name[0] >= 'A' && name[0] <= 'Z') return SDL_Scancode(SDL_SCANCODE_A + (name[0] - 'A'));
    static const char* const kDigits[] = {"ZERO", "ONE", "TWO", "THREE", "FOUR", "FIVE", "SIX", "SEVEN", "EIGHT", "NINE"};
    for (int i = 0; i < 10; ++i)
        if (EqualsNoCase(name, kDigits[i])) return i == 0 ? SDL_SCANCODE_0 : SDL_Scancode(SDL_SCANCODE_1 + i - 1);
    std::string n(name);
    return SDL_GetScancodeFromName(n.c_str());
}

std::string Spaces(std::string_view s) {
    std::string out(s);
    std::replace(out.begin(), out.end(), '_', ' ');
    return out;
}

std::unique_ptr<Image> LoadKeyed(std::string_view path) {
    if (path.empty() || EqualsNoCase(path, "NULL")) return nullptr;
    auto img = Image::Load(path);
    if (img) img->SetupTrans({0, 0, 0}, {0, 0, 0});  // black is transparent (Image_SetupTrans 0..0)
    return img;
}

}  // namespace

const char* MenuItemName(MenuItem item) {
    static std::array<std::string, size_t(MenuItem::Count)> names;
    std::string& n = names[size_t(item)];
    if (n.empty()) n = std::string("Interface_MenuItem_") + kItemNames[size_t(item)];
    return n.c_str();
}

void IconPanel::Load(const Config& cfg, std::string_view gameName, float originX, float originY) {
    originX_ = originX;
    originY_ = originY;
    x_ = originX;
    slide_ = Slide::Settled;

    // Item images: normal:disabled:pressed:tooltip[|sfx]:disabledTooltip[|sfx]:key[:TRUE]
    for (Config::ItemRef it = cfg.FindArray(Config::Id(gameName, "InterfaceImages")); it != Config::kNone;
         it = cfg.Next(it)) {
        const std::string& key = cfg.item(it).key;
        int id = -1;
        for (int i = 0; i < int(MenuItem::Count); ++i)
            if (EqualsNoCase(key, MenuItemName(MenuItem(i)))) id = i;
        if (id < 0) continue;
        const auto f = SplitTokens(cfg.item(it).value, ":");
        ItemDef& d = items_[size_t(id)];
        if (f.size() > 0) d.normal = LoadKeyed(f[0]);
        if (f.size() > 1) d.disabled = LoadKeyed(f[1]);
        if (f.size() > 2) d.pressed = LoadKeyed(f[2]);
        auto tip = [](std::string_view v, std::string& text, std::string& sfx) {
            const auto p = SplitTokens(v, "|");
            text = Spaces(p[0]);
            if (p.size() > 1) sfx = std::string(p[1]);
        };
        if (f.size() > 3) tip(f[3], d.toolTip, d.toolTipSfx);
        if (f.size() > 4) tip(f[4], d.disabledToolTip, d.disabledToolTipSfx);
        if (f.size() > 5) d.key = KeyFromName(f[5]);
        if (f.size() > 6) d.flag = ParseBool(f[6]) == BoolTri::True;
    }

    // Panel surrounds by item count: image:x:y:backX:backY:imageWithoutBack:x:y
    for (int n = 1; n < int(surrounds_.size()); ++n) {
        const auto v = cfg.GetString(Config::Id(gameName, "InterfaceSurroundImages", std::to_string(n)));
        if (!v) continue;
        const auto f = SplitTokens(*v, ":");
        Surround& s = surrounds_[size_t(n)];
        auto num = [&](size_t i) { return i < f.size() ? float(std::atoi(std::string(f[i]).c_str())) : 0.0f; };
        if (!f.empty() && !EqualsNoCase(f[0], "NULL")) {
            s.image = LoadKeyed(f[0]);
            s.x = num(1);
            s.y = num(2);
            s.noBackX = num(3);  // where the back button sits
            s.noBackY = num(4);
        }
        if (f.size() > 5 && !EqualsNoCase(f[5], "NULL")) {
            s.imageNoBack = LoadKeyed(f[5]);
            s.altX = num(6);
            s.altY = num(7);
        }
    }
    // Back button: width:height:highlight:pressed:tooltip
    if (auto v = cfg.GetString(Config::Id(gameName, "InterfaceBackButton"))) {
        const auto f = SplitTokens(*v, ":");
        if (f.size() >= 4) {
            backX_ = float(std::atoi(std::string(f[0]).c_str()));
            backY_ = float(std::atoi(std::string(f[1]).c_str()));
            backHighlight_ = LoadKeyed(f[2]);
            backPressed_ = LoadKeyed(f[3]);
        }
        if (f.size() >= 5) backToolTip_ = Spaces(f[4]);
    }

    // Fixed menu contents (Interface_AddAllMenuItems).
    using M = MenuItem;
    auto set = [&](MenuType t, std::initializer_list<MenuItem> l) { menus_[size_t(t)] = l; };
    set(MenuType::Main, {M::TeleportMan, M::BuildBuilding, M::BuildSmallVehicle, M::BuildLargeVehicle});
    set(MenuType::Ground, {M::LayPath, M::RemovePath, M::PlaceFence});
    set(MenuType::Erode, {M::RepairLava});
    set(MenuType::PlaceFence, {M::PlaceFence});
    set(MenuType::Construction, {M::CancelConstruction});
    set(MenuType::Rubble, {M::ClearRubble, M::PlaceFence});
    set(MenuType::Wall, {M::Dig, M::Reinforce, M::Dynamite, M::DeselectDig});
    set(MenuType::Creature, {M::Attack, M::TrackObject});
    set(MenuType::Vehicle, {M::UnLoadVehicle, M::VehiclePickUp, M::UpgradeVehicle, M::GetOut, M::GotoFirstPerson,
                            M::GotoSecondPerson, M::DeleteVehicle});
    set(MenuType::WaterVehicle,
        {M::UnLoadVehicle, M::VehiclePickUp, M::GetOut, M::GotoFirstPerson, M::GotoSecondPerson, M::GotoDock});
    set(MenuType::UnmannedVehicle, {M::GetIn, M::DeleteVehicle});
    set(MenuType::LegoMan, {M::GoFeed, M::UnLoadMinifigure, M::MinifigurePickUp, M::GetTool, M::DropBirdScarer,
                            M::UpgradeMan, M::TrainSkill, M::GotoFirstPerson, M::GotoSecondPerson, M::DeleteMan});
    set(MenuType::Building, {M::Repair, M::PowerOn, M::UpgradeBuilding, M::DeleteBuilding});
    set(MenuType::ElectricFence, {M::DeleteElectricFence});
    set(MenuType::FirstPerson, {M::GotoTopView, M::GotoSecondPerson, M::GotoFirstPerson});
    set(MenuType::GetTool, {M::GetDrill, M::GetSpade, M::GetHammer, M::GetSpanner, M::GetLaser, M::GetPusherGun,
                            M::GetFreezerGun, M::GetBirdScarer});
    set(MenuType::TrainSkill,
        {M::TrainDriver, M::TrainEngineer, M::TrainGeologist, M::TrainPilot, M::TrainSailor, M::TrainDynamite});
    set(MenuType::UpgradeVehicle, {M::UpgradeEngine, M::UpgradeDrill, M::UpgradeScan, M::UpgradeCarry});

    // Build icons: "<type> normal:disabled:pressed:key" (Interface_LoadBuildItems).
    for (Config::ItemRef it = cfg.FindArray(Config::Id(gameName, "InterfaceBuildImages")); it != Config::kNone;
         it = cfg.Next(it)) {
        const ObjectTypeInfo* type = GetObjectRegistry().Find(cfg.item(it).key);
        if (!type || (type->type != ObjType::Building && type->type != ObjType::Vehicle)) continue;
        const auto f = SplitTokens(cfg.item(it).value, ":");
        BuildDef d;
        d.type = type;
        if (f.size() > 0) d.normal = LoadKeyed(f[0]);
        if (f.size() > 1) d.disabled = LoadKeyed(f[1]);
        if (f.size() > 2) d.pressed = LoadKeyed(f[2]);
        if (f.size() > 3) d.key = KeyFromName(f[3]);
        d.toolTip = Spaces(cfg.GetString(Config::Id(gameName, "ObjectNames", type->name), type->name));
        builds_.push_back(std::move(d));
    }
}

namespace {
// Interface_SetScrollParameters(565, 18, 640 + 10, 18, 750): off-screen x and the slide speed
// per 1/25 s of unscaled time.
constexpr float kMenuOffX = 650.0f, kMenuSlideSpeed = 750.0f * 0.04f;

bool IsPanelMenu(MenuType m) { return m == MenuType::Priorities || m == MenuType::Encyclopedia; }

void PlayInterfaceSfx(std::string_view name) {
    int id = 0;
    if (GetSfx().GetType(name, id)) GetSfx().PlayNormal(id);
}
}  // namespace

void IconPanel::OpenMenu(MenuType menu, const BlockPos* block) {
    const bool shown = slide_ == Slide::Settled || slide_ == Slide::Away;
    if (shown && menu == menu_) {
        if (block) block_ = *block;
        next_ = menu;
        return;
    }
    if (slide_ != Slide::Out) PlayInterfaceSfx("SFX_InterfaceSlideOffScreen");
    slide_ = Slide::Out;
    next_ = menu;
    if (block) nextBlock_ = *block;
    hover_ = -1;
    pressed_ = false;
}

void IconPanel::Update(float elapsedAbs) {
    if (slide_ == Slide::Out) {
        if (panelOpen_) {
            // Leaving the priority list: its panel closes and the menu is already off screen.
            panelOpen_ = false;
            if (onPanelMenu) onPanelMenu(menu_, false);
            x_ = kMenuOffX;
        }
        x_ += elapsedAbs * kMenuSlideSpeed;
        if (x_ >= kMenuOffX) {
            x_ = kMenuOffX;
            PlayInterfaceSfx("SFX_InterfaceSlideOnScreen");
            menu_ = next_;
            block_ = nextBlock_;
            if (IsPanelMenu(menu_)) {
                slide_ = Slide::Away;
                panelOpen_ = true;
                if (onPanelMenu) onPanelMenu(menu_, true);
            } else {
                slide_ = Slide::In;
            }
        }
    } else if (slide_ == Slide::In) {
        x_ -= elapsedAbs * kMenuSlideSpeed;
        if (x_ <= originX_) {
            x_ = originX_;
            slide_ = Slide::Settled;
        }
    }
}

std::vector<IconPanel::Entry> IconPanel::Entries() const {
    std::vector<Entry> out;
    if (menu_ == MenuType::BuildBuilding || menu_ == MenuType::BuildSmallVehicle || menu_ == MenuType::BuildLargeVehicle) {
        // Interface_FUN_0041b2f0: buildings in type order; vehicles split by teleporter class.
        auto ordered = [&](ObjType t) {
            std::vector<const BuildDef*> v;
            for (const BuildDef& d : builds_)
                if (d.type->type == t) v.push_back(&d);
            std::sort(v.begin(), v.end(), [](const BuildDef* x, const BuildDef* y) { return x->type->id < y->type->id; });
            return v;
        };
        if (menu_ == MenuType::BuildBuilding) {
            for (const BuildDef* d : ordered(ObjType::Building)) out.push_back({MenuItem::Count, d});
        } else {
            for (const BuildDef* d : ordered(ObjType::Vehicle)) {
                const ObjectStats* st = GetStats().Find(d->type->name);
                const uint32_t f2 = st ? st->F2(0) : 0, f3 = st ? st->F3(0) : 0;
                const bool large = (f2 & StatsFlag2::UseBigTeleporter) || (f3 & StatsFlag3::ClassAsLarge);
                const bool small = ((f2 & StatsFlag2::UseSmallTeleporter) || (f2 & StatsFlag2::UseWaterTeleporter)) &&
                                   !(f3 & StatsFlag3::ClassAsLarge);
                if (menu_ == MenuType::BuildLargeVehicle ? large : small) out.push_back({MenuItem::Count, d});
            }
        }
        return out;
    }
    for (MenuItem item : Items(menu_)) out.push_back({item, nullptr});
    return out;
}

const Image* IconPanel::EntryImage(const Entry& e, bool enabled, bool pressed) const {
    const Image *n, *d, *p;
    if (e.build) {
        n = e.build->normal.get();
        d = e.build->disabled.get();
        p = e.build->pressed.get();
    } else {
        const ItemDef& def = items_[size_t(e.item)];
        n = def.normal.get();
        d = def.disabled.get();
        p = def.pressed.get();
    }
    const Image* img = !enabled ? d : (pressed ? p : n);
    return img ? img : n;
}

bool IconPanel::EntryEnabled(const Entry& e) const {
    if (e.build) return !isBuildEnabled || isBuildEnabled(*e.build->type);
    return !isEnabled || isEnabled(e.item);
}

void IconPanel::Activate(const Entry& e) {
    if (!EntryEnabled(e)) return;
    if (e.build) {
        if (onBuild) onBuild(*e.build->type);
    } else if (onItem) {
        onItem(e.item);
    }
}

RectF IconPanel::ItemRect(size_t index) const {
    const auto entries = Entries();
    float y = originY_;
    for (size_t i = 0; i < index && i < entries.size(); ++i) {
        const Image* img = EntryImage(entries[i], true, false);
        y += img ? float(img->height()) : 40.0f;
    }
    const Image* img = index < entries.size() ? EntryImage(entries[index], true, false) : nullptr;
    const float w = img ? float(img->width()) : 40.0f, h = img ? float(img->height()) : 40.0f;
    return {x_, y, w, h};
}

bool IconPanel::HandleInput(const Input& in) {
    const float mx = in.mouseX(), my = in.mouseY();
    const auto entries = Entries();
    hover_ = -1;
    hoverToolTip_.clear();
    if (slide_ != Slide::Settled) {
        pressed_ = false;
        return false;
    }
    bool over = false;
    for (size_t i = 0; i < entries.size(); ++i) {
        if (!ItemRect(i).Contains(mx, my)) continue;
        hover_ = int(i);
        over = true;
        const bool enabled = EntryEnabled(entries[i]);
        if (entries[i].build) {
            hoverToolTip_ = entries[i].build->toolTip;
        } else {
            const ItemDef& d = items_[size_t(entries[i].item)];
            hoverToolTip_ = enabled ? d.toolTip : d.disabledToolTip;
        }
    }
    if (HasBackButton(menu_)) {
        const Surround& s = surrounds_[std::min(entries.size(), surrounds_.size() - 1)];
        const RectF back{x_ + s.noBackX, originY_ + s.noBackY, backX_, backY_};
        if (back.Contains(mx, my)) {
            hover_ = -2;
            over = true;
            hoverToolTip_ = backToolTip_;
        }
        if (s.image && RectF{x_ + s.x, originY_ + s.y, float(s.image->width()), float(s.image->height())}
                           .Contains(mx, my))
            over = true;
    }
    if (in.leftClicked() && hover_ != -1) pressed_ = true;
    if (in.leftReleased()) {
        if (pressed_ && hover_ == -2) {
            BackToMain();
        } else if (pressed_ && hover_ >= 0) {
            Activate(entries[size_t(hover_)]);
        }
        pressed_ = false;
    }
    // Shortcut keys of the current menu's entries.
    for (const Entry& e : entries) {
        const SDL_Scancode key = e.build ? e.build->key : items_[size_t(e.item)].key;
        if (key != SDL_SCANCODE_UNKNOWN && in.KeyPressed(key) && EntryEnabled(e)) {
            Activate(e);
            break;
        }
    }
    return over;
}

void IconPanel::Draw(Renderer2D& r) const {
    if (x_ >= kMenuOffX) return;
    const auto entries = Entries();
    const Surround& s = surrounds_[std::min(entries.size(), surrounds_.size() - 1)];
    if (!HasBackButton(menu_)) {
        if (s.imageNoBack) s.imageNoBack->Draw(r, x_ + s.altX, originY_ + s.altY);
    } else {
        if (s.image) s.image->Draw(r, x_ + s.x, originY_ + s.y);
        const Image* b = hover_ == -2 ? (pressed_ ? backPressed_.get() : backHighlight_.get()) : nullptr;
        if (b) b->Draw(r, x_ + s.noBackX, originY_ + s.noBackY);
    }
    for (size_t i = 0; i < entries.size(); ++i) {
        const Image* img = EntryImage(entries[i], EntryEnabled(entries[i]), pressed_ && hover_ == int(i));
        const RectF rc = ItemRect(i);
        if (img) img->Draw(r, rc.x, rc.y);
    }
}

}  // namespace rr
