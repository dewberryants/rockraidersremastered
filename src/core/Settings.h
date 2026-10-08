#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <string_view>

namespace rr {

// Remaster settings (RockRaiders.ini next to the executable). Everything that is not part of the
// original game lives here; Lego.cfg is never modified. Defaults reproduce the original.
//
// Format: "[Section]" headers, "Key = Value" lines, ';' or '#' comments. Keys are "Section.Key".
class Settings {
public:
    bool Load(const std::filesystem::path& path);
    bool Save() const;

    std::string GetString(std::string_view key, std::string_view fallback) const;
    int GetInt(std::string_view key, int fallback) const;
    float GetFloat(std::string_view key, float fallback) const;
    bool GetBool(std::string_view key, bool fallback) const;

    void Set(std::string_view key, std::string_view value);
    void SetInt(std::string_view key, int value) { Set(key, std::to_string(value)); }
    void SetBool(std::string_view key, bool value) { Set(key, value ? "1" : "0"); }

    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
    std::map<std::string, std::string> values_;  // "section.key" (lower case) -> value
};

Settings& GetSettings();

// Typed view of the settings the engine reads at start-up.
struct RemasterOptions {
    // Display
    int windowWidth = 1280;
    int windowHeight = 960;
    bool fullscreen = false;
    bool vsync = true;
    bool widescreenUI = false;   // false: 640x480 UI letterboxed at 4:3 (original layout)
    bool smoothUI = false;       // linear filtering when scaling 2D art
    // Simulation
    int tickRate = 30;           // fixed game-logic rate in Hz; 0 = original variable frame step
    // Paths
    std::string gameDir;         // empty: auto-detect
    std::string overrideDirs = "Override";  // ';'-separated, relative to the executable
    // Debug
    bool cheats = false;         // testing aids: all levels open, instant drilling, F-key cheats

    void Read(const Settings& s);
    void Write(Settings& s) const;
};

}  // namespace rr
