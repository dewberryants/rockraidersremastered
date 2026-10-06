#include <SDL.h>

#include <cstdlib>
#include <cstring>

#include "app/App.h"
#include "core/Log.h"
#include "game/GameScenes.h"

int main(int argc, char** argv) {
    rr::LaunchOptions launch;
    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        auto next = [&](const char* name) { return !std::strcmp(a, name) && i + 1 < argc; };
        if (next("--data")) launch.dataDir = argv[++i];
        else if (!std::strcmp(a, "--window") || !std::strcmp(a, "-window")) launch.forceWindowed = true;
        else if (!std::strcmp(a, "--debug")) rr::LogSetMinLevel(rr::LogLevel::Debug);
        // Automated visual checks: run N ticks, save the window contents, exit.
        else if (next("--screenshot")) launch.screenshotPath = argv[++i];
        else if (next("--ticks")) launch.screenshotTicks = std::atoi(argv[++i]);
        else if (next("--scene")) launch.startScene = argv[++i];
        else if (next("--startlevel") || next("-startlevel")) launch.startLevel = argv[++i];
    }

    rr::App& app = rr::GetApp();
    if (!app.Init(launch)) {
        app.Shutdown();
        return 1;
    }
    app.SetScene(rr::CreateStartScene());
    const int rc = app.Run();
    app.Shutdown();
    return rc;
}
