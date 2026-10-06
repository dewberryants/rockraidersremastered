#pragma once

#include <cstdint>
#include <string>

namespace rr {

// Writes an RGBA8 image (top-down) as a PNG using stored (uncompressed) deflate blocks.
// Used for screenshots and automated visual checks; no compression library needed.
bool WritePng(const std::string& path, int width, int height, const uint8_t* rgba);

// Reads back the current GL framebuffer and writes it as PNG.
bool SaveFramebufferPng(const std::string& path, int width, int height);

}  // namespace rr
