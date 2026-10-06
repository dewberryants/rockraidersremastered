#include "game/Game.h"

#include "audio/Mixer.h"
#include "core/Log.h"
#include "game/Pointer.h"
#include "game/Sfx.h"
#include "game/front/FrontEnd.h"

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

    if (cfg.GetBool(Id("Main", "FrontEnd")) == BoolTri::True) {
        frontEnd = std::make_unique<front::FrontEnd>();
        frontEnd->Initialise(cfg, gameName);
    }
    return true;
}

}  // namespace rr
