#include "audio/Mixer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/Log.h"

namespace rr {

Mixer& GetMixer() {
    static Mixer instance;
    return instance;
}

float DirectSoundVolumeToGain(int volume) {
    if (volume <= -10000) return 0.0f;
    return std::pow(10.0f, float(std::min(volume, 0)) / 2000.0f);
}

bool Mixer::Init() {
    SDL_AudioSpec want{}, have{};
    want.freq = 44100;
    want.format = AUDIO_F32SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = &Mixer::Callback;
    want.userdata = this;
    device_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (!device_) {
        RR_WARN("No audio device: %s", SDL_GetError());
        return false;
    }
    outputRate_ = have.freq;
    SDL_PauseAudioDevice(device_, 0);
    RR_INFO("Audio: %d Hz stereo", outputRate_);
    return true;
}

void Mixer::Shutdown() {
    if (device_) SDL_CloseAudioDevice(device_);
    device_ = 0;
    std::lock_guard<std::mutex> lock(mutex_);
    voices_.clear();
}

Mixer::Voice* Mixer::Find(VoiceId id) {
    for (auto& v : voices_)
        if (v.id == id) return &v;
    return nullptr;
}

const Mixer::Voice* Mixer::Find(VoiceId id) const {
    for (const auto& v : voices_)
        if (v.id == id) return &v;
    return nullptr;
}

Mixer::VoiceId Mixer::Play(std::shared_ptr<const WaveData> wave, float gain, float pan, bool loop) {
    if (!device_ || !wave || wave->frames() == 0) return kInvalidVoice;
    std::lock_guard<std::mutex> lock(mutex_);
    Voice v;
    v.id = nextId_++;
    if (nextId_ == kInvalidVoice) nextId_ = 1;
    v.step = double(wave->sampleRate) / double(outputRate_);
    v.wave = std::move(wave);
    v.gain = gain;
    v.pan = std::clamp(pan, -1.0f, 1.0f);
    v.loop = loop;
    voices_.push_back(std::move(v));
    return voices_.back().id;
}

void Mixer::Stop(VoiceId id) {
    std::lock_guard<std::mutex> lock(mutex_);
    voices_.erase(std::remove_if(voices_.begin(), voices_.end(), [id](const Voice& v) { return v.id == id; }),
                  voices_.end());
}

void Mixer::StopAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    voices_.clear();
}

bool Mixer::IsPlaying(VoiceId id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return Find(id) != nullptr;
}

void Mixer::SetGain(VoiceId id, float gain) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (Voice* v = Find(id)) v->gain = gain;
}

void Mixer::SetPan(VoiceId id, float pan) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (Voice* v = Find(id)) v->pan = std::clamp(pan, -1.0f, 1.0f);
}

void Mixer::SetMasterGain(float gain) {
    std::lock_guard<std::mutex> lock(mutex_);
    master_ = gain;
}

void Mixer::Callback(void* user, Uint8* stream, int len) {
    auto* self = static_cast<Mixer*>(user);
    float* out = reinterpret_cast<float*>(stream);
    const int frames = len / int(sizeof(float) * 2);
    std::memset(stream, 0, size_t(len));
    self->Mix(out, frames);
}

void Mixer::Mix(float* out, int frames) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& v : voices_) {
        const WaveData& w = *v.wave;
        const size_t total = w.frames();
        // Equal-power pan.
        const float angle = (v.pan + 1.0f) * 0.25f * 3.14159265f;
        const float gl = v.gain * master_ * std::cos(angle) * 1.41421356f;
        const float gr = v.gain * master_ * std::sin(angle) * 1.41421356f;
        for (int i = 0; i < frames; ++i) {
            if (v.position >= double(total)) {
                if (!v.loop) break;
                v.position = std::fmod(v.position, double(total));
            }
            const size_t i0 = size_t(v.position);
            const size_t i1 = (i0 + 1 < total) ? i0 + 1 : (v.loop ? 0 : i0);
            const float t = float(v.position - double(i0));
            float l, r;
            if (w.channels == 1) {
                l = r = w.samples[i0] + (w.samples[i1] - w.samples[i0]) * t;
            } else {
                l = w.samples[i0 * 2] + (w.samples[i1 * 2] - w.samples[i0 * 2]) * t;
                r = w.samples[i0 * 2 + 1] + (w.samples[i1 * 2 + 1] - w.samples[i0 * 2 + 1]) * t;
            }
            out[i * 2] += l * gl;
            out[i * 2 + 1] += r * gr;
            v.position += v.step;
        }
    }
    voices_.erase(std::remove_if(voices_.begin(), voices_.end(),
                                 [](const Voice& v) { return !v.loop && v.position >= double(v.wave->frames()); }),
                  voices_.end());
    for (int i = 0; i < frames * 2; ++i) out[i] = std::clamp(out[i], -1.0f, 1.0f);
}

}  // namespace rr
