#include "gfx/Texture.h"

#include <vector>

#include "gfx/GL.h"

namespace rr {

Texture::~Texture() { Release(); }

Texture::Texture(Texture&& other) noexcept : id_(other.id_), width_(other.width_), height_(other.height_) {
    other.id_ = 0;
}

Texture& Texture::operator=(Texture&& other) noexcept {
    if (this != &other) {
        Release();
        id_ = other.id_;
        width_ = other.width_;
        height_ = other.height_;
        other.id_ = 0;
    }
    return *this;
}

bool Texture::Create(const Bitmap& bmp, std::optional<ColorKey> key, bool linearFilter) {
    if (!key) return CreateRgba(bmp.width, bmp.height, bmp.rgba.data(), linearFilter);
    std::vector<uint8_t> pixels = bmp.rgba;
    for (size_t i = 0; i < pixels.size(); i += 4) {
        if (pixels[i] == key->r && pixels[i + 1] == key->g && pixels[i + 2] == key->b) pixels[i + 3] = 0;
    }
    return CreateRgba(bmp.width, bmp.height, pixels.data(), linearFilter);
}

bool Texture::CreateRgba(int width, int height, const uint8_t* rgba, bool linearFilter) {
    Release();
    width_ = width;
    height_ = height;
    glGenTextures(1, &id_);
    glBindTexture(GL_TEXTURE_2D, id_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    const GLint filter = linearFilter ? GL_LINEAR : GL_NEAREST;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return id_ != 0;
}

void Texture::Update(const uint8_t* rgba) {
    glBindTexture(GL_TEXTURE_2D, id_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width_, height_, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
}

void Texture::Release() {
    if (id_) glDeleteTextures(1, &id_);
    id_ = 0;
}

}  // namespace rr
