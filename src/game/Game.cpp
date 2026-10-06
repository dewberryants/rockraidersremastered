#include "game/Game.h"

#include "audio/Mixer.h"
#include "core/Log.h"
#include "game/Pointer.h"
#include "game/Sfx.h"
#include "game/front/FrontEnd.h"
#include "game/objects/ObjectRegistry.h"
#include "game/objects/Stats.h"
#include "gfx/model/Mesh.h"
#include <SDL.h>

namespace rr {

Game& GetGame() {
    static Game instance;
    return instance;
}

bool Game::Initialise(const Config& cfg) {
    config = &cfg;
    textFont = Font::Load("Interface\\Fonts\\FONT5_HI.bmp");
    toolTipFont = Font::Load("Interface\\Fonts\\ToolTipFont.bmp");
    deskTopFont = Font::Load("Interface\\Fonts\\DeskTopFont.bmp");
    font5Hi = Font::Load("Interface\\Fonts\\font5_HI.bmp");
    briefFont = Font::Load("Interface\\Fonts\\MBriefFONT.bmp");
    briefFont2 = Font::Load("Interface\\Fonts\\MBriefFONT2.bmp");

    GetMixer().Init();
    GetSfx().LoadSamples(cfg, gameName, true);
    GetPointers().Load(cfg, gameName);

    // Object types and their models (Lego_Load*Types, Lego_LoadMiscObjects, Stats_Initialise).
    GetMeshTextures().SetSharedDir(cfg.GetString(Id("Main", "SharedTextures"), ""));
    const uint64_t t0 = SDL_GetTicks64();
    GetStats().Load(cfg, gameName);
    GetObjectRegistry().Load(cfg, gameName);
    RR_INFO("Object models loaded in %llu ms", (unsigned long long)(SDL_GetTicks64() - t0));

    if (cfg.GetBool(Id("Main", "FrontEnd")) == BoolTri::True) {
        frontEnd = std::make_unique<front::FrontEnd>();
        frontEnd->Initialise(cfg, gameName);
    }
    return true;
}

}  // namespace rr
