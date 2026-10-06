#include "gfx/Flic.h"

#include <cstring>

#include "core/Log.h"
#include "fs/FileSystem.h"

namespace rr {

namespace {
uint16_t U16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
uint32_t U32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

constexpr uint16_t kFrameMagic = 0xF1FA;
// Chunk types (Autodesk FLIC + Animator Studio "DTA" hi-colour extensions).
constexpr uint16_t kColor256 = 4, kDeltaFlc = 7, kColor64 = 11, kBlack = 13, kByteRun = 15, kCopy = 16,
                   kDtaBrun = 25, kDtaCopy = 26, kDtaLc = 27;
}  // namespace

std::unique_ptr<Flic> Flic::Load(std::string_view gamePath) {
    auto bytes = Fs().ReadAll(gamePath);
    if (!bytes || bytes->size() < 128) {
        RR_WARN("Flic %.*s not found", int(gamePath.size()), gamePath.data());
        return nullptr;
    }
    auto f = std::make_unique<Flic>();
    f->file_ = std::move(*bytes);
    const uint8_t* h = f->file_.data();
    f->magic_ = U16(h + 4);
    f->frameCount_ = U16(h + 6);
    f->width_ = U16(h + 8);
    f->height_ = U16(h + 10);
    f->depth_ = U16(h + 12);
    if (f->magic_ != 0xAF11 && f->magic_ != 0xAF12 && f->magic_ != 0xAF43 && f->magic_ != 0x9119 &&
        f->magic_ != 0x1234) {
        RR_WARN("Flic %.*s: unknown magic %04x", int(gamePath.size()), gamePath.data(), f->magic_);
        return nullptr;
    }
    if (f->width_ <= 0 || f->height_ <= 0) return nullptr;
    // FLC files store the offset of frame 1 at byte 80; FLI starts right after the 128-byte header.
    const uint32_t oframe1 = U32(h + 80);
    f->firstFrame_ = (f->magic_ != 0xAF11 && oframe1 >= 128 && oframe1 < f->file_.size()) ? oframe1 : 128;
    f->nextFrame_ = f->firstFrame_;
    f->pixels16_.assign(size_t(f->width_) * f->height_, 0);
    f->pixels8_.assign(size_t(f->width_) * f->height_, 0);
    f->rgba_.assign(size_t(f->width_) * f->height_ * 4, 255);
    f->StepFrame(false);  // show the first frame straight away
    return f;
}

void Flic::Restart() {
    nextFrame_ = firstFrame_;
    frame_ = -1;
    finished_ = false;
    clock_ = 0.0f;
    StepFrame(false);
}

bool Flic::StepFrame(bool loop) {
    if (frame_ + 1 >= frameCount_) {
        if (!loop) {
            finished_ = true;
            return false;
        }
        nextFrame_ = firstFrame_;
        frame_ = -1;
    }
    if (!DecodeFrame()) {
        finished_ = true;
        return false;
    }
    ++frame_;
    uploaded_ = false;
    return true;
}

bool Flic::Advance(float elapsed, bool loop) {
    if (finished_) return false;
    // Game units are 1/25 s and frames run at 25 per second: one frame per unit.
    const int before = int(clock_);
    clock_ += elapsed;
    for (int n = int(clock_) - before; n > 0; --n) {
        if (!StepFrame(loop)) return false;
    }
    return true;
}

bool Flic::DecodeFrame() {
    // Skip non-frame chunks (e.g. prefix chunks) until a frame chunk is found.
    while (nextFrame_ + 16 <= file_.size()) {
        const uint8_t* fh = &file_[nextFrame_];
        const uint32_t size = U32(fh);
        const uint16_t magic = U16(fh + 4);
        if (size < 16 || nextFrame_ + size > file_.size()) return false;
        const size_t frameStart = nextFrame_;
        nextFrame_ += size;
        if (magic != kFrameMagic) continue;
        const int chunks = U16(fh + 6);
        size_t pos = frameStart + 16;
        for (int c = 0; c < chunks && pos + 6 <= frameStart + size; ++c) {
            const uint32_t csize = U32(&file_[pos]);
            const uint16_t ctype = U16(&file_[pos + 4]);
            if (csize < 6 || pos + csize > frameStart + size) break;
            DoChunk(ctype, &file_[pos + 6], csize - 6);
            pos += csize;
        }
        return true;
    }
    return false;
}

void Flic::DoChunk(uint16_t type, const uint8_t* d, size_t size) {
    const int w = width_, h = height_;
    const uint8_t* end = d + size;
    switch (type) {
        case kColor256:
        case kColor64: {
            // packets: skip count, copy count (0 = 256), RGB triples
            if (size < 2) return;
            int packets = U16(d);
            const uint8_t* p = d + 2;
            int index = 0;
            while (packets-- > 0 && p + 2 <= end) {
                index += p[0];
                int count = p[1] ? p[1] : 256;
                p += 2;
                for (; count > 0 && index < 256 && p + 3 <= end; --count, ++index, p += 3) {
                    for (int k = 0; k < 3; ++k)
                        palette_[index][k] = type == kColor64 ? uint8_t((p[k] << 2) | (p[k] >> 4)) : p[k];
                }
            }
            break;
        }
        case kBlack:
            std::fill(pixels16_.begin(), pixels16_.end(), 0);
            std::fill(pixels8_.begin(), pixels8_.end(), 0);
            break;
        case kCopy:
        case kDtaCopy:
            if (depth_ == 16 && size >= size_t(w) * h * 2) {
                for (size_t i = 0; i < pixels16_.size(); ++i) pixels16_[i] = U16(d + i * 2);
            } else if (size >= size_t(w) * h) {
                std::memcpy(pixels8_.data(), d, size_t(w) * h);
            }
            break;
        case kByteRun:
        case kDtaBrun: {
            const uint8_t* p = d;
            const bool wide = (type == kDtaBrun) || depth_ == 16;
            for (int y = 0; y < h && p < end; ++y) {
                ++p;  // obsolete packet count
                int x = 0;
                while (x < w && p < end) {
                    const int8_t count = int8_t(*p++);
                    if (count >= 0) {  // run: one pixel repeated
                        if (wide) {
                            if (p + 2 > end) return;
                            const uint16_t v = U16(p);
                            p += 2;
                            for (int i = 0; i < count && x < w; ++i) pixels16_[size_t(y) * w + x++] = v;
                        } else {
                            if (p >= end) return;
                            const uint8_t v = *p++;
                            for (int i = 0; i < count && x < w; ++i) pixels8_[size_t(y) * w + x++] = v;
                        }
                    } else {  // literal pixels
                        const int n = -count;
                        for (int i = 0; i < n && x < w; ++i) {
                            if (wide) {
                                if (p + 2 > end) return;
                                pixels16_[size_t(y) * w + x++] = U16(p);
                                p += 2;
                            } else {
                                if (p >= end) return;
                                pixels8_[size_t(y) * w + x++] = *p++;
                            }
                        }
                    }
                }
            }
            break;
        }
        case kDeltaFlc:
        case kDtaLc: {
            if (size < 2) return;
            const bool wide = (type == kDtaLc) || depth_ == 16;
            int lines = U16(d);
            const uint8_t* p = d + 2;
            int y = 0;
            while (lines > 0 && p + 2 <= end && y < h) {
                uint16_t op = U16(p);
                p += 2;
                const uint16_t kind = op & 0xC000;
                if (kind == 0xC000) {  // skip lines
                    y += 0x10000 - op;
                    continue;
                }
                if (kind == 0x4000) return;  // undefined
                if (kind == 0x8000) {  // last pixel of the line (8-bit only meaningful)
                    if (!wide) pixels8_[size_t(y) * w + (w - 1)] = uint8_t(op & 0xFF);
                    if (p + 2 > end) return;
                    op = U16(p);
                    p += 2;
                }
                int packets = op;
                int x = 0;
                while (packets-- > 0 && p + 2 <= end) {
                    x += p[0];
                    const int8_t count = int8_t(p[1]);
                    p += 2;
                    if (count >= 0) {  // literal: count pixels (8-bit flics: count words)
                        const int n = wide ? count : count * 2;
                        for (int i = 0; i < n; ++i) {
                            if (wide) {
                                if (p + 2 > end) return;
                                if (x < w) pixels16_[size_t(y) * w + x] = U16(p);
                                p += 2;
                            } else {
                                if (p >= end) return;
                                if (x < w) pixels8_[size_t(y) * w + x] = *p;
                                ++p;
                            }
                            ++x;
                        }
                    } else {  // run: one value repeated
                        const int n = -count;
                        if (p + 2 > end) return;
                        const uint8_t b0 = p[0], b1 = p[1];
                        const uint16_t v = U16(p);
                        p += 2;
                        for (int i = 0; i < n; ++i) {
                            if (wide) {
                                if (x < w) pixels16_[size_t(y) * w + x] = v;
                                ++x;
                            } else {
                                if (x < w) pixels8_[size_t(y) * w + x] = b0;
                                ++x;
                                if (x < w) pixels8_[size_t(y) * w + x] = b1;
                                ++x;
                            }
                        }
                    }
                }
                ++y;
                --lines;
            }
            break;
        }
        default:
            break;  // stamps, labels etc. are ignored (as the original did)
    }
}

void Flic::Upload(bool transparent) {
    const size_t n = size_t(width_) * height_;
    for (size_t i = 0; i < n; ++i) {
        uint8_t* o = &rgba_[i * 4];
        if (depth_ == 16) {
            const uint16_t v = pixels16_[i];
            const uint8_t r = (v >> 10) & 31, g = (v >> 5) & 31, b = v & 31;
            o[0] = uint8_t((r << 3) | (r >> 2));
            o[1] = uint8_t((g << 3) | (g >> 2));
            o[2] = uint8_t((b << 3) | (b >> 2));
            o[3] = (transparent && v == 0) ? 0 : 255;
        } else {
            const uint8_t idx = pixels8_[i];
            o[0] = palette_[idx][0];
            o[1] = palette_[idx][1];
            o[2] = palette_[idx][2];
            o[3] = (transparent && idx == 0) ? 0 : 255;
        }
    }
    if (!texture_.valid()) texture_.CreateRgba(width_, height_, rgba_.data());
    else texture_.Update(rgba_.data());
    uploaded_ = true;
    uploadedTrans_ = transparent;
}

void Flic::Draw(Renderer2D& r, float x, float y, bool transparent) {
    DrawStretched(r, {x, y, float(width_), float(height_)}, transparent);
}

void Flic::DrawStretched(Renderer2D& r, const RectF& dst, bool transparent) {
    if (!uploaded_ || uploadedTrans_ != transparent) Upload(transparent);
    r.DrawTexture(texture_, dst, {0, 0, float(width_), float(height_)});
}

}  // namespace rr
