#pragma once

#include <memory>
#include <string>

#include "config/Config.h"
#include "gfx/Font.h"
#include "game/ui/Interface.h"

namespace rr {

namespace front {
class FrontEnd;
}

// Game-wide state that outlives individual scenes (the original's legoGlobs subset).
struct Game {
    const Config* config = nullptr;
    std::string gameName = "Lego*";  // root block of Lego.cfg ("Lego*")

    // Fonts loaded by Lego_Initialise from fixed paths.
    std::unique_ptr<Font> textFont;      // Interface\Fonts\FONT5_HI.bmp (text windows)
    std::unique_ptr<Font> toolTipFont;   // Interface\Fonts\ToolTipFont.bmp
    std::unique_ptr<Font> deskTopFont;   // Interface\Fonts\DeskTopFont.bmp
    std::unique_ptr<Font> font5Hi;       // Interface\Fonts\font5_HI.bmp
    std::unique_ptr<Font> briefFont;     // Interface\Fonts\MBriefFONT.bmp
    std::unique_ptr<Font> briefFont2;    // Interface\Fonts\MBriefFONT2.bmp

    std::unique_ptr<front::FrontEnd> frontEnd;
    std::unique_ptr<IconPanel> iconPanel;  // in-game icon menus (Interface_Initialise(565, 18))

    bool introsDone = false;
    std::string selectedLevel;  // "Levels::Tutorial01" when a level was chosen

    // One-time initialisation (Lego_Initialise subset implemented so far).
    bool Initialise(const Config& cfg);
    std::string Id(std::string_view a) const { return Config::Id(gameName, a); }
    std::string Id(std::string_view a, std::string_view b) const { return Config::Id(gameName, a, b); }
};

Game& GetGame();

}  // namespace rr
