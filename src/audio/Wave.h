#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace rr {

// Decoded PCM sound: interleaved float samples in [-1, 1].
struct WaveData {
    int sampleRate = 22050;
    int channels = 1;
    std::vector<float> samples;  // frames * channels
    size_t frames() const { return channels ? samples.size() / size_t(channels) : 0; }
    float durationSeconds() const { return sampleRate ? float(frames()) / float(sampleRate) : 0.0f; }
};

// Parses a RIFF WAVE file with 8- or 16-bit PCM data (all retail sounds are in that form).
std::optional<WaveData> DecodeWave(const std::vector<uint8_t>& file);
std::shared_ptr<WaveData> LoadWave(std::string_view gamePath);

}  // namespace rr
