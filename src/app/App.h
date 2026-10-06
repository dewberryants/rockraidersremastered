#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include "config/Config.h"
#include "core/Settings.h"
#include "gfx/Renderer2D.h"
#include "platform/Input.h"
#include "platform/Window.h"

namespace rr {

// A full-screen mode of the program (front end, level, reward screen, ...).
class Scene {
public:
    virtual ~Scene() = default;
    virtual void OnEnter() {}
    // elapsed is in game time units (1/25 s), as passed to the original Lego_MainLoop.
    virtual void Update(float elapsed) = 0;
    virtual void Render(Renderer2D& r2d) = 0;
};

struct LaunchOptions {
    std::string dataDir;
    std::string screenshotPath;
    int screenshotTicks = 5;
    std::string startScene;  // debugging: jump straight to a scene
    std::string startLevel;  // like the original -startlevel
    bool forceWindowed = false;
    // Scripted input for automated checks: "tick:move:x:y;tick:click;tick:key:NAME;..."
    // (positions in logical 640x480 coordinates).
    std::string inputScript;
    // Debug camera override "tiltDegrees,zoom" for overview screenshots.
    std::string debugCamera;
};

class App {
public:
    bool Init(const LaunchOptions& launch);
    int Run();
    void Shutdown();

    void SetScene(std::unique_ptr<Scene> scene);
    void RequestQuit() { quit_ = true; }

    Window& window() { return window_; }
    Renderer2D& r2d() { return r2d_; }
    Input& input() { return input_; }
    const Config& config() const { return config_; }
    RemasterOptions& options() { return options_; }
    const LaunchOptions& launch() const { return launch_; }
    // Game units per tick when running at a fixed rate (0.8333 at 30 Hz).
    float tickElapsed() const;
    std::filesystem::path exeDir() const { return exeDir_; }

private:
    void Tick(float elapsed);
    void RenderFrame();
    void ApplyScriptedInput();

    LaunchOptions launch_;
    RemasterOptions options_;
    Config config_;
    Window window_;
    Renderer2D r2d_;
    Input input_;
    std::unique_ptr<Scene> scene_;
    std::unique_ptr<Scene> pendingScene_;
    std::filesystem::path exeDir_;
    bool quit_ = false;
    int ticks_ = 0;
    bool scriptedMouse_ = false;
    bool automated_ = false;  // screenshot runs: no focus, real input ignored
    float scriptedX_ = 0, scriptedY_ = 0;
};

App& GetApp();

}  // namespace rr
