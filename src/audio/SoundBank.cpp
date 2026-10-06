#include "audio/SoundBank.h"

#include <algorithm>
#include <cmath>

#include "core/Log.h"

namespace rr {

SoundBank& GetSoundBank() {
    static SoundBank instance;
    return instance;
}

SoundBank::Handle SoundBank::Load(const std::string& name, bool streamed, bool simultaneous, int volume) {
    auto wave = LoadWave(name + ".wav");
    if (!wave) {
        RR_DEBUG("Sample %s.wav not found", name.c_str());
        return -1;
    }
    Sample s;
    s.name = name;
    s.wave = std::move(wave);
    s.simultaneous = simultaneous && !streamed;
    // Out-of-range volumes fall back to full volume, like Sound3D_CheckVolumeLimits.
    s.volume = (volume <= 0 && volume > -10001) ? volume : 0;
    samples_.push_back(std::move(s));
    return int(samples_.size()) - 1;
}

void SoundBank::Positional(const Vec3& p, float& gain, float& pan) const {
    const float dx = p.x - listener_.x, dy = p.y - listener_.y, dz = p.z - listener_.z;
    const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    // DirectSound3D inverse-distance model with roll-off, silent beyond the max distance.
    if (dist >= maxDist_) {
        gain = 0.0f;
    } else if (dist <= minDist_) {
        gain = 1.0f;
    } else {
        gain = minDist_ / (minDist_ + rollOff_ * (dist - minDist_));
    }
    if (dist > 0.001f) {
        const float side = (dx * listenerRight_.x + dy * listenerRight_.y + dz * listenerRight_.z) / dist;
        pan = std::clamp(side, -1.0f, 1.0f) * 0.8f;
    } else {
        pan = 0.0f;
    }
}

int SoundBank::Play(Handle handle, SoundPlay mode, bool loop, const Vec3* pos) {
    if (!enabled_ || handle < 0 || handle >= int(samples_.size())) return -1;
    Sample& s = samples_[size_t(handle)];
    Mixer& mixer = GetMixer();

    int dup = 0;
    if (s.simultaneous) {
        s.nextDuplicate = (s.nextDuplicate + 1) % kMaxDuplicates;
        dup = s.nextDuplicate;
    }
    // A DirectSound buffer can only play once at a time: restarting stops the previous play.
    if (s.voices[dup] != Mixer::kInvalidVoice) mixer.Stop(s.voices[dup]);

    int volume = s.volume;
    if (mode == SoundPlay::Normal) volume -= 200;
    float gain = DirectSoundVolumeToGain(volume) * DirectSoundVolumeToGain(globalVolume_);
    float pan = 0.0f;
    if (mode != SoundPlay::Normal && pos) {
        float g = 1.0f;
        Positional(*pos, g, pan);
        gain *= g;
    }
    s.voices[dup] = mixer.Play(s.wave, gain, pan, loop);
    return handle * kMaxDuplicates + dup;
}

void SoundBank::StopPlay(int playHandle) {
    if (playHandle < 0) return;
    const int idx = playHandle / kMaxDuplicates, dup = playHandle % kMaxDuplicates;
    if (idx >= int(samples_.size())) return;
    Mixer::VoiceId& v = samples_[size_t(idx)].voices[size_t(dup)];
    GetMixer().Stop(v);
    v = Mixer::kInvalidVoice;
}

void SoundBank::StopAll() {
    GetMixer().StopAll();
    for (auto& s : samples_) s.voices.fill(Mixer::kInvalidVoice);
}

float SoundBank::PlayTimeSeconds(Handle handle) const {
    if (handle < 0 || handle >= int(samples_.size())) return 0.0f;
    return samples_[size_t(handle)].wave->durationSeconds();
}

void SoundBank::SetGlobalVolume(int volume) {
    if (volume <= 0 && volume > -10001) globalVolume_ = volume;
}

}  // namespace rr
