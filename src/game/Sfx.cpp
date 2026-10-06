#include "game/Sfx.h"

#include <cstdlib>

#include "core/Log.h"
#include "core/Random.h"
#include "fs/Path.h"

namespace rr {

namespace {
const char* const kBuiltinNames[SFX_BuiltinCount] = {
    "SFX_NULL", "SFX_Stamp", "SFX_Drill", "SFX_DrillFade", "SFX_RockBreak", "SFX_Drip", "SFX_Ambient",
    "SFX_AmbientLoop", "SFX_Step", "SFX_RockMonster", "SFX_RockMonster2", "SFX_RockMonsterStep", "SFX_MFDeposit",
    "SFX_ButtonPressed", "SFX_MFLift", "SFX_MFThrow", "SFX_Walker", "SFX_YesSir", "SFX_Build", "SFX_Okay",
    "SFX_NotOkay", "SFX_InterfaceSlideOnScreen", "SFX_InterfaceSlideOffScreen", "SFX_PanelSlideOnScreen",
    "SFX_PanelSlideOffScreen", "SFX_Siren", "SFX_CrystalRecharge", "SFX_Laser", "SFX_LaserHit", "SFX_LazerRecharge",
    "SFX_TopPriority", "SFX_ImmovableRock", "SFX_Wall", "SFX_Floor", "SFX_BoulderHit", "SFX_Place", "SFX_PlaceOre",
    "SFX_PlaceCrystal", "SFX_Lava", "SFX_RockWipe", "SFX_FallIn", "SFX_MusicLoop", "SFX_CaptainSlide",
    "SFX_Dynamite", "SFX_AmbientMusicLoop",
};
}  // namespace

Sfx& GetSfx() {
    static Sfx instance;
    return instance;
}

Sfx::Sfx() {
    for (int i = 0; i < SFX_BuiltinCount; ++i) ids_[ToLower(kBuiltinNames[i])] = i;
    variants_.resize(SFX_BuiltinCount);
}

bool Sfx::GetType(std::string_view name, int& id) {
    if (name.empty()) return false;
    auto it = ids_.find(ToLower(name));
    if (it != ids_.end()) {
        id = it->second;
        return true;
    }
    if (!populate_) return false;
    id = int(variants_.size());
    ids_[ToLower(name)] = id;
    variants_.emplace_back();
    return true;
}

void Sfx::LoadSamples(const Config& cfg, std::string_view gameName, bool loadOptional) {
    populate_ = true;
    for (Config::ItemRef it = cfg.FindArray(Config::Id(gameName, "Samples")); it != Config::kNone; it = cfg.Next(it)) {
        std::string_view key = cfg.item(it).key;
        if (!key.empty() && key[0] == '!') {
            if (!loadOptional) continue;
            key.remove_prefix(1);
        }
        int id = 0;
        if (!GetType(key, id)) continue;
        std::vector<SoundBank::Handle>& list = variants_[size_t(id)];
        list.clear();
        for (std::string_view spec : SplitTokens(cfg.item(it).value, ",")) {
            const bool simultaneous = !spec.empty() && spec[0] == '*';
            if (simultaneous) spec.remove_prefix(1);
            int volume = 0;
            if (!spec.empty() && spec[0] == '#') {
                const size_t end = spec.find('#', 1);
                if (end == std::string_view::npos) break;
                volume = std::atoi(std::string(spec.substr(1, end - 1)).c_str());
                spec.remove_prefix(end + 1);
            }
            const bool streamed = !spec.empty() && spec[0] == '@';
            if (streamed) spec.remove_prefix(1);
            const SoundBank::Handle h = GetSoundBank().Load(std::string(spec), streamed, simultaneous, volume);
            if (h >= 0) list.push_back(h);
        }
    }
    populate_ = false;
    RR_INFO("Sound effects: %zu ids", variants_.size());
}

SoundBank::Handle Sfx::RandomVariant(int id) {
    if (id <= 0 || id >= int(variants_.size())) return -1;
    std::vector<SoundBank::Handle>& list = variants_[size_t(id)];
    if (list.empty()) return -1;
    if (list.size() > 1) {
        const size_t pick = size_t(random::Rand()) % list.size();
        std::swap(list[0], list[pick]);
    }
    return list[0];
}

int Sfx::PlayNormal(int id, bool loop) {
    if (queueMode_) {
        if (queue_.size() < 10) queue_.push_back({id, loop});
        return -1;
    }
    if (id == SFX_NULL || !soundOn_) return -1;
    const SoundBank::Handle h = RandomVariant(id);
    return h >= 0 ? GetSoundBank().Play(h, SoundPlay::Normal, loop) : -1;
}

int Sfx::PlayAt(int id, const Vec3& pos, bool loop) {
    if (id == SFX_NULL || !soundOn_) return -1;
    const SoundBank::Handle h = RandomVariant(id);
    return h >= 0 ? GetSoundBank().Play(h, SoundPlay::OnPos, loop, &pos) : -1;
}

float Sfx::SamplePlayTime(int id) {
    if (id <= 0 || id >= int(variants_.size()) || variants_[size_t(id)].empty()) return 0.0f;
    return GetSoundBank().PlayTimeSeconds(variants_[size_t(id)][0]);
}

void Sfx::SetSoundOn(bool on) {
    soundOn_ = on;
    if (!on) GetSoundBank().StopAll();
}

void Sfx::Update(float) {
    // Requests queued during the previous update play now (the original's one-update delay).
    const bool wasQueued = queueMode_;
    queueMode_ = false;
    for (const Queued& q : playNext_) PlayNormal(q.id, q.loop);
    queueMode_ = wasQueued;
    playNext_ = std::move(queue_);
    queue_.clear();
}

}  // namespace rr
