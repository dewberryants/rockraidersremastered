#pragma once

#include <memory>
#include <string_view>
#include <vector>

#include "config/Config.h"
#include "gfx/Flic.h"
#include "gfx/Image.h"

namespace rr {

// Mouse pointer types; order matches the original table so ids line up with game code.
enum PointerType : int {
    Pointer_Standard = 0, Pointer_Blank, Pointer_Selected, Pointer_Drill, Pointer_CantDrill, Pointer_Clear,
    Pointer_Go, Pointer_CantGo, Pointer_Teleport, Pointer_CantTeleport, Pointer_Reinforce, Pointer_CantReinforce,
    Pointer_RadarPan, Pointer_TrackObject, Pointer_CantZoom, Pointer_Zoom, Pointer_Help, Pointer_CantHelp,
    Pointer_PutDown, Pointer_GetIn, Pointer_GetOut, Pointer_TutorialBlockInfo, Pointer_Okay, Pointer_NotOkay,
    Pointer_CanBuild, Pointer_CannotBuild, Pointer_Dynamite, Pointer_CantDynamite, Pointer_PickUp,
    Pointer_CantPickUp, Pointer_PickUpOre, Pointer_LegoManCantDig, Pointer_VehicleCantDig, Pointer_LegoManDig,
    Pointer_VehicleDig, Pointer_LegoManCantPickUp, Pointer_VehicleCantPickUp, Pointer_LegoManPickUp,
    Pointer_VehiclePickUp, Pointer_LegoManCantGo, Pointer_VehicleCantGo, Pointer_LegoManGo, Pointer_VehicleGo,
    Pointer_LegoManClear, Pointer_VehicleClear, Pointer_SurfaceType_Immovable, Pointer_SurfaceType_Hard,
    Pointer_SurfaceType_Medium, Pointer_SurfaceType_Loose, Pointer_SurfaceType_Soil, Pointer_SurfaceType_Lava,
    Pointer_SurfaceType_Water, Pointer_SurfaceType_OreSeam, Pointer_SurfaceType_Lake, Pointer_SurfaceType_CrystalSeam,
    Pointer_SurfaceType_RechargeSeam,
    Pointer_Count
};

// Mouse pointers from Lego*::Pointers. A pointer is either a bitmap (black transparent) drawn with
// its top-left at the mouse, or an animated .flh ("file,x,y") drawn at mouse+offset over the
// standard arrow.
class Pointers {
public:
    void Load(const Config& cfg, std::string_view gameName);
    bool GetType(std::string_view name, PointerType& out) const;

    // Pointer changes are held back while a timed pointer is showing (SetTimed).
    void SetCurrent(PointerType type) {
        if (timer_ <= 0.0f) current_ = type;
    }
    void SetTimed(PointerType type, float time) {
        current_ = type;
        timer_ = time;
    }
    PointerType current() const { return current_; }
    void Update(float elapsed);
    void Draw(Renderer2D& r, float mouseX, float mouseY);

private:
    struct Entry {
        std::unique_ptr<Image> image;
        std::unique_ptr<Flic> flic;
        int offsetX = 0, offsetY = 0;
    };
    std::vector<Entry> entries_ = std::vector<Entry>(Pointer_Count);
    PointerType current_ = Pointer_Standard;
    float timer_ = 0.0f;
    float animElapsed_ = 0.0f;
};

Pointers& GetPointers();

}  // namespace rr
