#include "game/Pointer.h"

#include <cstdlib>

#include "fs/Path.h"

namespace rr {

namespace {
const char* const kPointerNames[Pointer_Count] = {
    "Pointer_Standard", "Pointer_Blank", "Pointer_Selected", "Pointer_Drill", "Pointer_CantDrill", "Pointer_Clear",
    "Pointer_Go", "Pointer_CantGo", "Pointer_Teleport", "Pointer_CantTeleport", "Pointer_Reinforce",
    "Pointer_CantReinforce", "Pointer_RadarPan", "Pointer_TrackObject", "Pointer_CantZoom", "Pointer_Zoom",
    "Pointer_Help", "Pointer_CantHelp", "Pointer_PutDown", "Pointer_GetIn", "Pointer_GetOut",
    "Pointer_TutorialBlockInfo", "Pointer_Okay", "Pointer_NotOkay", "Pointer_CanBuild", "Pointer_CannotBuild",
    "Pointer_Dynamite", "Pointer_CantDynamite", "Pointer_PickUp", "Pointer_CantPickUp", "Pointer_PickUpOre",
    "Pointer_LegoManCantDig", "Pointer_VehicleCantDig", "Pointer_LegoManDig", "Pointer_VehicleDig",
    "Pointer_LegoManCantPickUp", "Pointer_VehicleCantPickUp", "Pointer_LegoManPickUp", "Pointer_VehiclePickUp",
    "Pointer_LegoManCantGo", "Pointer_VehicleCantGo", "Pointer_LegoManGo", "Pointer_VehicleGo",
    "Pointer_LegoManClear", "Pointer_VehicleClear", "Pointer_SurfaceType_Immovable", "Pointer_SurfaceType_Hard",
    "Pointer_SurfaceType_Medium", "Pointer_SurfaceType_Loose", "Pointer_SurfaceType_Soil", "Pointer_SurfaceType_Lava",
    "Pointer_SurfaceType_Water", "Pointer_SurfaceType_OreSeam", "Pointer_SurfaceType_Lake",
    "Pointer_SurfaceType_CrystalSeam", "Pointer_SurfaceType_RechargeSeam",
};
}  // namespace

Pointers& GetPointers() {
    static Pointers instance;
    return instance;
}

bool Pointers::GetType(std::string_view name, PointerType& out) const {
    for (int i = 0; i < Pointer_Count; ++i) {
        if (EqualsNoCase(name, kPointerNames[i])) {
            out = PointerType(i);
            return true;
        }
    }
    return false;
}

void Pointers::Load(const Config& cfg, std::string_view gameName) {
    for (Config::ItemRef it = cfg.FindArray(Config::Id(gameName, "Pointers")); it != Config::kNone; it = cfg.Next(it)) {
        std::string_view key = cfg.item(it).key;
        if (!key.empty() && key[0] == '!') key.remove_prefix(1);  // optional entry (skipped by -reduceimages)
        PointerType type;
        if (!GetType(key, type)) continue;
        const auto parts = SplitTokens(cfg.item(it).value, ",");
        Entry& e = entries_[type];
        if (parts.size() == 1) {
            e.image = Image::Load(parts[0]);
            if (e.image) e.image->SetupTrans({0, 0, 0}, {0, 0, 0});
        } else if (parts.size() >= 3) {
            e.flic = Flic::Load(parts[0]);
            e.offsetX = std::atoi(std::string(parts[1]).c_str());
            e.offsetY = std::atoi(std::string(parts[2]).c_str());
        }
    }
}

void Pointers::Update(float elapsed) {
    if (timer_ > 0.0f) timer_ -= elapsed;
    animElapsed_ = elapsed;
}

void Pointers::Draw(Renderer2D& r, float mouseX, float mouseY) {
    const float x = float(int(mouseX)), y = float(int(mouseY));
    Entry& e = entries_[current_];
    if (e.flic) {
        if (entries_[Pointer_Blank].image) entries_[Pointer_Blank].image->Draw(r, x, y);
        e.flic->Advance(animElapsed_, true);
        animElapsed_ = 0.0f;
        e.flic->Draw(r, x + float(e.offsetX), y + float(e.offsetY), true);
        return;
    }
    const Image* img = e.image ? e.image.get() : entries_[Pointer_Standard].image.get();
    if (img) img->Draw(r, x, y);
}

}  // namespace rr
