#include "platform/Window.h"

#include "core/Log.h"
#include "gfx/GL.h"

namespace rr {

bool Window::Create(const WindowConfig& config) {
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        RR_ERROR("SDL video init failed: %s", SDL_GetError());
        return false;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
    if (config.fullscreen) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    window_ = SDL_CreateWindow(config.title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               config.width, config.height, flags);
    if (!window_) {
        RR_ERROR("SDL_CreateWindow failed: %s", SDL_GetError());
        return false;
    }
    context_ = SDL_GL_CreateContext(window_);
    if (!context_) {
        RR_ERROR("OpenGL 3.3 context creation failed: %s", SDL_GetError());
        return false;
    }
    SDL_GL_SetSwapInterval(config.vsync ? 1 : 0);
    if (!gl::LoadFunctions()) return false;
    RR_INFO("OpenGL %s on %s", reinterpret_cast<const char*>(glGetString(GL_VERSION)),
            reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
    return true;
}

void Window::Destroy() {
    if (context_) SDL_GL_DeleteContext(context_);
    if (window_) SDL_DestroyWindow(window_);
    context_ = nullptr;
    window_ = nullptr;
}

void Window::Swap() { SDL_GL_SwapWindow(window_); }

int Window::DrawableWidth() const {
    int w = 0, h = 0;
    SDL_GL_GetDrawableSize(window_, &w, &h);
    return w;
}

int Window::DrawableHeight() const {
    int w = 0, h = 0;
    SDL_GL_GetDrawableSize(window_, &w, &h);
    return h;
}

}  // namespace rr
