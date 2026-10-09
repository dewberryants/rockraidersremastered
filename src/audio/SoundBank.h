#pragma once

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "audio/Mixer.h"
#include "audio/Wave.h"

namespace rr {

// How a sound is placed (Gods98 Sound3DPlay).
enum class SoundPlay { OnFrame = 0, OnPos = 1, Normal = 2 };

struct Vec3 {
    float x = 0, y = 0, z = 0;
};

// Loaded samples, like the original Sound3D table. A sample is "<name>.wav"; streamed samples
// were read from the loose Data folder at play time (we decode them on load, same result).
// Non-simultaneous samples own one playback buffer (playing again restarts it); simultaneous
// samples rotate through three.
class SoundBank {
public:
    static constexpr int kMaxDuplicates = 3;
    using Handle = int;  // -1 = none

    Handle Load(const std::string& name, bool streamed, bool simultaneous, int volume);
    // Returns a play handle (sample index * 3 + duplicate) or -1. pos is used for OnPos/OnFrame.
    int Play(Handle sample, SoundPlay mode, bool loop, const Vec3* pos = nullptr);
    void StopPlay(int playHandle);
    // Moves a positional sound that is still playing (Sound3D on a moving container).
    void SetPlayPosition(int playHandle, const Vec3& pos);
    // The voice behind a play handle; it changes when the buffer is restarted by another play.
    uint32_t VoiceOf(int playHandle) const;
    void StopAll();
    float PlayTimeSeconds(Handle sample) const;

    // Listener for positional sounds (updated by the camera).
    void SetListener(const Vec3& pos, const Vec3& right) {
        listener_ = pos;
        listenerRight_ = right;
    }
    void SetMinDistForAtten(float d) { minDist_ = d; }
    void SetMaxDist(float d) { maxDist_ = d; }
    void SetRollOff(float f) { rollOff_ = f; }
    // Global SFX volume in DirectSound units (-10000..0).
    void SetGlobalVolume(int volume);
    int globalVolume() const { return globalVolume_; }
    void SetEnabled(bool on) { enabled_ = on; }
    bool enabled() const { return enabled_; }

private:
    struct Sample {
        std::string name;
        std::shared_ptr<WaveData> wave;
        bool simultaneous = false;
        int volume = 0;
        int nextDuplicate = 0;
        std::array<Mixer::VoiceId, kMaxDuplicates> voices{};
    };
    void Positional(const Vec3& p, float& gain, float& pan) const;

    std::vector<Sample> samples_;
    Vec3 listener_{}, listenerRight_{1, 0, 0};
    float minDist_ = 40.0f, maxDist_ = 1000.0f, rollOff_ = 1.0f;
    int globalVolume_ = 0;
    bool enabled_ = true;
};

SoundBank& GetSoundBank();

}  // namespace rr
