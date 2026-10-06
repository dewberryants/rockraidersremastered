#pragma once

#include <SDL.h>

#include <string>

namespace rr {

// The original game renders at a fixed 640x480. We keep that as the logical resolution for all
// 2D/UI coordinates and scale the final image to the window.
constexpr int kLogicalWidth = 640;
constexpr int kLogicalHeight = 480;

struct WindowConfig {
    std::string title = "LEGO Rock Raiders";
    int width = 1280;
    int height = 960;
    bool fullscreen = false;
    bool vsync = true;
};

class Window {
public:
    bool Create(const WindowConfig& config);
    void Destroy();
    void Swap();

    SDL_Window* Handle() const { return window_; }
    int DrawableWidth() const;
    int DrawableHeight() const;

private:
    SDL_Window* window_ = nullptr;
    SDL_GLContext context_ = nullptr;
};

}  // namespace rr
