#include "audio/Wave.h"

#include <algorithm>
#include <cstring>

#include "core/Log.h"
#include "fs/FileSystem.h"

namespace rr {

namespace {
uint16_t U16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
uint32_t U32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
}  // namespace

std::optional<WaveData> DecodeWave(const std::vector<uint8_t>& d) {
    if (d.size() < 12 || std::memcmp(d.data(), "RIFF", 4) != 0 || std::memcmp(d.data() + 8, "WAVE", 4) != 0)
        return std::nullopt;
    int format = 0, channels = 0, rate = 0, bits = 0;
    const uint8_t* data = nullptr;
    size_t dataSize = 0;
    size_t pos = 12;
    while (pos + 8 <= d.size()) {
        const uint32_t size = U32(&d[pos + 4]);
        const size_t body = pos + 8;
        const size_t avail = std::min<size_t>(size, d.size() - body);
        if (!std::memcmp(&d[pos], "fmt ", 4) && avail >= 16) {
            format = U16(&d[body]);
            channels = U16(&d[body + 2]);
            rate = int(U32(&d[body + 4]));
            bits = U16(&d[body + 14]);
        } else if (!std::memcmp(&d[pos], "data", 4)) {
            data = &d[body];
            dataSize = avail;
        }
        pos = body + size + (size & 1);
    }
    if (format != 1 || channels < 1 || channels > 2 || (bits != 8 && bits != 16) || !data || rate <= 0)
        return std::nullopt;

    WaveData w;
    w.sampleRate = rate;
    w.channels = channels;
    if (bits == 8) {
        w.samples.resize(dataSize);
        for (size_t i = 0; i < dataSize; ++i) w.samples[i] = (float(data[i]) - 128.0f) / 128.0f;
    } else {
        const size_t n = dataSize / 2;
        w.samples.resize(n);
        for (size_t i = 0; i < n; ++i) w.samples[i] = float(int16_t(U16(data + i * 2))) / 32768.0f;
    }
    // Drop a trailing partial frame.
    w.samples.resize(w.frames() * size_t(channels));
    return w;
}

std::shared_ptr<WaveData> LoadWave(std::string_view gamePath) {
    auto bytes = Fs().ReadAll(gamePath);
    if (!bytes) return nullptr;
    auto wave = DecodeWave(*bytes);
    if (!wave) {
        RR_WARN("Sound %.*s is not 8/16-bit PCM", int(gamePath.size()), gamePath.data());
        return nullptr;
    }
    return std::make_shared<WaveData>(std::move(*wave));
}

}  // namespace rr
