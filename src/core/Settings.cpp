#include "core/Settings.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

#include "core/Log.h"
#include "fs/Path.h"

namespace rr {

namespace {
std::string Trim(std::string_view s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
    return std::string(s.substr(a, b - a));
}
}  // namespace

Settings& GetSettings() {
    static Settings instance;
    return instance;
}

bool Settings::Load(const std::filesystem::path& path) {
    path_ = path;
    values_.clear();
    std::ifstream in(path);
    if (!in) return false;
    std::string line, section;
    while (std::getline(in, line)) {
        const std::string t = Trim(line);
        if (t.empty() || t[0] == ';' || t[0] == '#') continue;
        if (t.front() == '[' && t.back() == ']') {
            section = ToLower(Trim(std::string_view(t).substr(1, t.size() - 2)));
            continue;
        }
        const size_t eq = t.find('=');
        if (eq == std::string::npos) continue;
        values_[section + "." + ToLower(Trim(std::string_view(t).substr(0, eq)))] =
            Trim(std::string_view(t).substr(eq + 1));
    }
    return true;
}

bool Settings::Save() const {
    std::ofstream out(path_);
    if (!out) {
        RR_WARN("Could not write settings to %s", path_.string().c_str());
        return false;
    }
    out << "; LEGO Rock Raiders remaster settings. Defaults reproduce the original game.\n";
    std::string current;
    for (const auto& [key, value] : values_) {
        const size_t dot = key.find('.');
        const std::string section = key.substr(0, dot);
        if (section != current) {
            out << "\n[" << section << "]\n";
            current = section;
        }
        out << key.substr(dot + 1) << " = " << value << "\n";
    }
    return true;
}

std::string Settings::GetString(std::string_view key, std::string_view fallback) const {
    auto it = values_.find(ToLower(key));
    return it == values_.end() ? std::string(fallback) : it->second;
}

int Settings::GetInt(std::string_view key, int fallback) const {
    auto it = values_.find(ToLower(key));
    return it == values_.end() ? fallback : std::atoi(it->second.c_str());
}

float Settings::GetFloat(std::string_view key, float fallback) const {
    auto it = values_.find(ToLower(key));
    return it == values_.end() ? fallback : float(std::atof(it->second.c_str()));
}

bool Settings::GetBool(std::string_view key, bool fallback) const {
    auto it = values_.find(ToLower(key));
    if (it == values_.end()) return fallback;
    const std::string v = ToLower(it->second);
    return v == "1" || v == "true" || v == "yes" || v == "on";
}

void Settings::Set(std::string_view key, std::string_view value) { values_[ToLower(key)] = std::string(value); }

void RemasterOptions::Read(const Settings& s) {
    windowWidth = s.GetInt("display.width", windowWidth);
    windowHeight = s.GetInt("display.height", windowHeight);
    fullscreen = s.GetBool("display.fullscreen", fullscreen);
    vsync = s.GetBool("display.vsync", vsync);
    widescreenUI = s.GetBool("display.widescreenui", widescreenUI);
    smoothUI = s.GetBool("display.smoothui", smoothUI);
    tickRate = s.GetInt("game.tickrate", tickRate);
    gameDir = s.GetString("paths.gamedir", gameDir);
    overrideDirs = s.GetString("paths.overridedirs", overrideDirs);
}

void RemasterOptions::Write(Settings& s) const {
    s.SetInt("display.width", windowWidth);
    s.SetInt("display.height", windowHeight);
    s.SetBool("display.fullscreen", fullscreen);
    s.SetBool("display.vsync", vsync);
    s.SetBool("display.widescreenui", widescreenUI);
    s.SetBool("display.smoothui", smoothUI);
    s.SetInt("game.tickrate", tickRate);
    s.Set("paths.gamedir", gameDir);
    s.Set("paths.overridedirs", overrideDirs);
}

}  // namespace rr
