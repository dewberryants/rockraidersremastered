#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "audio/SoundBank.h"
#include "config/Config.h"

namespace rr {

// Sound effect ids. The first 45 are fixed by the game code; names from the Lego.cfg "Samples"
// block that are not in this list (SND_*) get ids appended in file order.
enum SfxId : int {
    SFX_NULL = 0, SFX_Stamp, SFX_Drill, SFX_DrillFade, SFX_RockBreak, SFX_Drip, SFX_Ambient, SFX_AmbientLoop,
    SFX_Step, SFX_RockMonster, SFX_RockMonster2, SFX_RockMonsterStep, SFX_MFDeposit, SFX_ButtonPressed, SFX_MFLift,
    SFX_MFThrow, SFX_Walker, SFX_YesSir, SFX_Build, SFX_Okay, SFX_NotOkay, SFX_InterfaceSlideOnScreen,
    SFX_InterfaceSlideOffScreen, SFX_PanelSlideOnScreen, SFX_PanelSlideOffScreen, SFX_Siren, SFX_CrystalRecharge,
    SFX_Laser, SFX_LaserHit, SFX_LazerRecharge, SFX_TopPriority, SFX_ImmovableRock, SFX_Wall, SFX_Floor,
    SFX_BoulderHit, SFX_Place, SFX_PlaceOre, SFX_PlaceCrystal, SFX_Lava, SFX_RockWipe, SFX_FallIn, SFX_MusicLoop,
    SFX_CaptainSlide, SFX_Dynamite, SFX_AmbientMusicLoop,
    SFX_BuiltinCount
};

class Sfx {
public:
    Sfx();

    // Name -> id (case-insensitive). While samples are being loaded, unknown names are added.
    bool GetType(std::string_view name, int& id);
    // Lego*::Samples: "Name  [*][#vol#][@]path[,variant...]". Entries prefixed '!' are optional
    // (skipped only by the original's -reducesamples mode).
    void LoadSamples(const Config& cfg, std::string_view gameName, bool loadOptional);

    // 2D sound; returns a play handle or -1. In queue mode the request is deferred to the next Update.
    int PlayNormal(int id, bool loop = false);
    int PlayAt(int id, const Vec3& pos, bool loop = false);
    void Stop(int playHandle) { GetSoundBank().StopPlay(playHandle); }
    float SamplePlayTime(int id);

    void SetQueueMode(bool on) { queueMode_ = on; }
    bool queueMode() const { return queueMode_; }
    void SetSoundOn(bool on);
    bool soundOn() const { return soundOn_; }
    void Update(float elapsed);

private:
    // Picks a random variant (moving it to the front, as the original did) and returns its sample.
    SoundBank::Handle RandomVariant(int id);

    std::unordered_map<std::string, int> ids_;
    std::vector<std::vector<SoundBank::Handle>> variants_;
    bool populate_ = false;
    bool queueMode_ = false;
    bool soundOn_ = true;
    struct Queued {
        int id;
        bool loop;
    };
    std::vector<Queued> queue_, playNext_;
};

Sfx& GetSfx();

}  // namespace rr
