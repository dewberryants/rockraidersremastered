#include <SDL.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>

#include "config/Config.h"
#include "core/Log.h"
#include "fs/FileSystem.h"
#include "gfx/Bitmap.h"
#include "gfx/GL.h"
#include "gfx/PngWriter.h"
#include "gfx/Renderer2D.h"
#include "gfx/Texture.h"
#include "platform/Window.h"

namespace fsys = std::filesystem;

namespace {

// The game directory holds LegoRR0.wad/LegoRR1.wad and Data\. Look for it next to the
// executable, in the working directory, and in a "Rock Raiders" subfolder of either.
fsys::path FindGameDir(const char* explicitDir) {
    if (explicitDir) return explicitDir;
    std::vector<fsys::path> roots = {fsys::current_path()};
    if (char* base = SDL_GetBasePath()) {
        roots.emplace_back(base);
        SDL_free(base);
    }
    for (const auto& root : roots) {
        for (fsys::path candidate = root; !candidate.empty(); candidate = candidate.parent_path()) {
            for (const fsys::path& dir : {candidate, candidate / "Rock Raiders"}) {
                std::error_code ec;
                if (fsys::exists(dir / "LegoRR0.wad", ec)) return dir;
            }
            if (candidate == candidate.parent_path()) break;
        }
    }
    return {};
}

}  // namespace

int main(int argc, char** argv) {
    const char* dataArg = nullptr;
    const char* screenshotPath = nullptr;
    int screenshotFrames = 5;
    bool windowed = true;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--data") && i + 1 < argc) dataArg = argv[++i];
        else if (!std::strcmp(argv[i], "--fullscreen")) windowed = false;
        else if (!std::strcmp(argv[i], "--debug")) rr::LogSetMinLevel(rr::LogLevel::Debug);
        // Automated visual checks: render N frames, save the window contents, exit.
        else if (!std::strcmp(argv[i], "--screenshot") && i + 1 < argc) screenshotPath = argv[++i];
        else if (!std::strcmp(argv[i], "--frames") && i + 1 < argc) screenshotFrames = std::atoi(argv[++i]);
    }

    const fsys::path gameDir = FindGameDir(dataArg);
    if (gameDir.empty() || !rr::Fs().Mount(gameDir)) {
        const char* msg = "Could not find the LEGO Rock Raiders game files (LegoRR0.wad).\n"
                          "Run with --data <path to the game folder>.";
        RR_ERROR("%s", msg);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "LEGO Rock Raiders", msg, nullptr);
        return 1;
    }
    RR_INFO("Game directory: %s", gameDir.string().c_str());

    rr::Config config;
    if (!config.Load("Lego.cfg")) return 1;

    rr::WindowConfig wc;
    wc.fullscreen = !windowed;
    rr::Window window;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0 || !window.Create(wc)) return 1;

    rr::Renderer2D r2d;
    if (!r2d.Init()) return 1;

    rr::Texture loadScreen;
    const std::string loadScreenPath = config.GetString("Lego*::Main::LoadScreen", "");
    if (auto bmp = rr::LoadBmp(loadScreenPath)) loadScreen.Create(*bmp);

    bool running = true;
    int frame = 0;
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = false;
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) running = false;
        }
        glViewport(0, 0, window.DrawableWidth(), window.DrawableHeight());
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        r2d.BeginFrame(window.DrawableWidth(), window.DrawableHeight());
        if (loadScreen.valid()) r2d.DrawTexture(loadScreen, 0, 0);
        r2d.Flush();
        if (screenshotPath && ++frame >= screenshotFrames) {
            rr::SaveFramebufferPng(screenshotPath, window.DrawableWidth(), window.DrawableHeight());
            running = false;
        }
        window.Swap();
    }

    r2d.Shutdown();
    window.Destroy();
    SDL_Quit();
    return 0;
}
