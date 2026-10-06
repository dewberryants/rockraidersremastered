#include "app/App.h"

#include <SDL.h>

#include <algorithm>

#include "core/Log.h"
#include "fs/FileSystem.h"
#include "gfx/GL.h"
#include "gfx/PngWriter.h"

namespace fsys = std::filesystem;

namespace rr {

App& GetApp() {
    static App app;
    return app;
}

namespace {
// The game directory holds LegoRR0.wad/LegoRR1.wad and Data\. Look next to the executable, in the
// working directory, their parents, and a "Rock Raiders" subfolder of each.
fsys::path FindGameDir(const std::string& explicitDir, const fsys::path& exeDir) {
    if (!explicitDir.empty()) return explicitDir;
    for (const fsys::path& root : {fsys::current_path(), exeDir}) {
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

float App::tickElapsed() const { return options_.tickRate > 0 ? 25.0f / float(options_.tickRate) : 0.0f; }

bool App::Init(const LaunchOptions& launch) {
    launch_ = launch;
    if (char* base = SDL_GetBasePath()) {
        exeDir_ = base;
        SDL_free(base);
    } else {
        exeDir_ = fsys::current_path();
    }

    Settings& settings = GetSettings();
    const bool haveSettings = settings.Load(exeDir_ / "RockRaiders.ini");
    options_.Read(settings);
    if (!haveSettings) {
        options_.Write(settings);
        settings.Save();
    }
    if (launch_.forceWindowed) options_.fullscreen = false;

    const fsys::path gameDir = FindGameDir(!launch_.dataDir.empty() ? launch_.dataDir : options_.gameDir, exeDir_);
    if (gameDir.empty() || !Fs().Mount(gameDir)) {
        const char* msg =
            "Could not find the LEGO Rock Raiders game files (LegoRR0.wad).\n"
            "Run with --data <path to the game folder> or set [paths] gamedir in RockRaiders.ini.";
        RR_ERROR("%s", msg);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "LEGO Rock Raiders", msg, nullptr);
        return false;
    }
    RR_INFO("Game directory: %s", gameDir.string().c_str());

    // Override folders, highest priority first.
    size_t start = 0;
    const std::string& dirs = options_.overrideDirs;
    while (start <= dirs.size()) {
        size_t end = dirs.find(';', start);
        if (end == std::string::npos) end = dirs.size();
        if (end > start) {
            fsys::path p = dirs.substr(start, end - start);
            Fs().AddOverrideDir(p.is_absolute() ? p : exeDir_ / p);
        }
        start = end + 1;
    }

    if (!config_.Load("Lego.cfg")) return false;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
        RR_ERROR("SDL_Init failed: %s", SDL_GetError());
        return false;
    }
    WindowConfig wc;
    wc.width = options_.windowWidth;
    wc.height = options_.windowHeight;
    wc.fullscreen = options_.fullscreen;
    wc.vsync = options_.vsync;
    if (!window_.Create(wc)) return false;
    if (!r2d_.Init()) return false;
    r2d_.SetLayout(options_.widescreenUI, options_.smoothUI);
    SDL_StartTextInput();
    return true;
}

void App::SetScene(std::unique_ptr<Scene> scene) { pendingScene_ = std::move(scene); }

void App::Tick(float elapsed) {
    if (pendingScene_) {
        scene_ = std::move(pendingScene_);
        scene_->OnEnter();
    }
    input_.BeginTick();
    if (scene_) scene_->Update(elapsed);
    ++ticks_;
}

void App::RenderFrame() {
    const int dw = window_.DrawableWidth(), dh = window_.DrawableHeight();
    glViewport(0, 0, dw, dh);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    r2d_.BeginFrame(dw, dh);
    if (scene_) scene_->Render(r2d_);
    r2d_.Flush();
}

int App::Run() {
    const bool fixed = options_.tickRate > 0;
    const double tickSeconds = fixed ? 1.0 / options_.tickRate : 0.0;
    const Uint64 freq = SDL_GetPerformanceFrequency();
    Uint64 last = SDL_GetPerformanceCounter();
    double accumulator = tickSeconds;  // run one tick immediately

    while (!quit_) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) quit_ = true;
            input_.HandleEvent(ev);
        }
        // Mouse in logical coordinates (window points -> drawable pixels -> logical).
        int mx = 0, my = 0, ww = 1, wh = 1;
        SDL_GetMouseState(&mx, &my);
        SDL_GetWindowSize(window_.Handle(), &ww, &wh);
        float lx = 0, ly = 0;
        r2d_.WindowToLogical(mx * window_.DrawableWidth() / std::max(1, ww), my * window_.DrawableHeight() / std::max(1, wh),
                             lx, ly);
        input_.SetMousePos(lx, ly);

        const Uint64 now = SDL_GetPerformanceCounter();
        const double dt = double(now - last) / double(freq);
        last = now;

        if (!launch_.screenshotPath.empty()) {
            // Deterministic capture: one fixed tick per frame.
            Tick(fixed ? tickElapsed() : 1.0f);
        } else if (fixed) {
            accumulator += std::min(dt, 0.25);
            int steps = 0;
            while (accumulator >= tickSeconds && steps < 8) {
                Tick(tickElapsed());
                accumulator -= tickSeconds;
                ++steps;
            }
        } else {
            // Original behaviour: one update per frame, elapsed = ms * 0.025, at most 3.0.
            Tick(std::min(float(dt * 1000.0 * 0.025), 3.0f));
        }

        RenderFrame();
        if (!launch_.screenshotPath.empty() && ticks_ >= launch_.screenshotTicks) {
            SaveFramebufferPng(launch_.screenshotPath, window_.DrawableWidth(), window_.DrawableHeight());
            quit_ = true;
        }
        window_.Swap();
    }
    return 0;
}

void App::Shutdown() {
    scene_.reset();
    pendingScene_.reset();
    r2d_.Shutdown();
    window_.Destroy();
    SDL_Quit();
}

}  // namespace rr
