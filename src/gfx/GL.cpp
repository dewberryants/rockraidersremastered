#define RR_GL_NO_REDIRECT
#include "gfx/GL.h"

#include <SDL.h>

#include "core/Log.h"

#define RR_GL_DEFINE(ret, name, ...) PFN_rr_##name rr_##name = nullptr;
RR_GL_FUNCTIONS(RR_GL_DEFINE)
#undef RR_GL_DEFINE

namespace rr::gl {

bool LoadFunctions() {
    bool ok = true;
#define RR_GL_LOAD(ret, name, ...)                                                   \
    rr_##name = reinterpret_cast<PFN_rr_##name>(SDL_GL_GetProcAddress(#name));       \
    if (!rr_##name) {                                                                \
        RR_ERROR("OpenGL entry point %s is missing", #name);                         \
        ok = false;                                                                  \
    }
    RR_GL_FUNCTIONS(RR_GL_LOAD)
#undef RR_GL_LOAD
    return ok;
}

}  // namespace rr::gl
