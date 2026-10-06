#pragma once

#include <SDL.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

#include "audio/Wave.h"

namespace rr {

// Software mixer on an SDL audio device (44.1 kHz stereo float). Voices are identified by a
// handle that stays valid (but inactive) after the voice stops.
class Mixer {
public:
    using VoiceId = uint32_t;
    static constexpr VoiceId kInvalidVoice = 0;

    bool Init();
    void Shutdown();

    VoiceId Play(std::shared_ptr<const WaveData> wave, float gain, float pan, bool loop);
    void Stop(VoiceId id);
    void StopAll();
    bool IsPlaying(VoiceId id) const;
    void SetGain(VoiceId id, float gain);
    void SetPan(VoiceId id, float pan);
    void SetMasterGain(float gain);

    bool initialised() const { return device_ != 0; }

private:
    struct Voice {
        VoiceId id = kInvalidVoice;
        std::shared_ptr<const WaveData> wave;
        double position = 0;  // in source frames
        double step = 1;      // source frames per output frame
        float gain = 1, pan = 0;
        bool loop = false;
    };
    static void Callback(void* user, Uint8* stream, int len);
    void Mix(float* out, int frames);
    Voice* Find(VoiceId id);
    const Voice* Find(VoiceId id) const;

    SDL_AudioDeviceID device_ = 0;
    int outputRate_ = 44100;
    mutable std::mutex mutex_;
    std::vector<Voice> voices_;
    VoiceId nextId_ = 1;
    float master_ = 1.0f;
};

Mixer& GetMixer();

// DirectSound attenuation (hundredths of a decibel, -10000..0) to linear gain.
float DirectSoundVolumeToGain(int volume);

}  // namespace rr
