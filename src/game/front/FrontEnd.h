#pragma once

#include <memory>
#include <string>
#include <vector>

#include "app/App.h"
#include "game/front/Menu.h"
#include "ui/TextWindow.h"

namespace rr::front {

// Ordered chain of levels: StartLevel -> NextLevel -> ... (Front_LoadLevelSet).
struct LevelSet {
    std::vector<std::string> ids;    // config block ids, e.g. "Levels::Tutorial01"
    std::vector<std::string> names;  // display names (FullName, '_' -> ' '), or the id
    int IndexOf(std::string_view id) const;
};

// Level unlock graph built from each level's "LevelLinks" (first visit wins, depth-first).
struct LevelGraph {
    struct Node {
        int setIndex = -1;
        std::vector<int> children;
    };
    std::vector<Node> nodes;  // nodes[0] is the start level
    std::vector<int> preorder;  // depth-first order = order of the picker entries
};

// Values the menus read and write (the original's front-end option globals).
struct FrontOptions {
    int playCredits = 0;
    int quitGame = 0;
    int missionSelected = -1;
    int tutorialSelected = -1;
    int saveSelected = -1;
    int backFromSave = 0;
    // Options / paused menu
    int continueGame = 0;
    int gameSpeed = 0;
    int sfxVolume = 8;
    int musicVolume = 5;
    int brightness = 5;
    int helpWindow = 0;
    int replayObjective = 0;
    int quitMission = 0;
    int restartMission = 0;
    int wallDetail = 0;
    int music = 0;
    int sound = 0;
    int autoGameSpeed = 0;
};

enum class ScreenType { MainMenu = 0, Missions = 1, Tutorials = 2, LoadSave = 4 };

class FrontEnd {
public:
    void Initialise(const Config& cfg, std::string_view gameName);

    // Starts a front-end screen (Front_RunScreenMenuType); the screen runs until a menu closes.
    void BeginScreen(ScreenType type);
    bool running() const { return current_ != nullptr; }
    void Update(float elapsed);
    void Render(Renderer2D& r);

    const FrontOptions& options() const { return opts_; }
    bool quitRequested() const { return opts_.quitGame != 0; }
    // Config id of the chosen level, or empty.
    std::string SelectedLevel() const;
    bool missionSelected() const { return opts_.missionSelected != -1; }
    bool tutorialSelected() const { return opts_.tutorialSelected != -1; }

private:
    void LoadLevelSet(LevelSet& set, std::string_view startKey);
    void BuildGraph(const LevelSet& set, LevelGraph& graph);
    void AddPicker(Menu* menu, const LevelSet& set, const LevelGraph& graph, int* value, bool missions);
    void UpdateAvailability(const LevelSet& set, const LevelGraph& graph, Menu* picker, bool keepLocked);
    void OnLevelHover(bool missions, float elapsed, int entry);
    void UpdateOverlays(float elapsed);
    void StopOverlay();
    Menu* MissionsMenu() const;
    Menu* TutorialsMenu() const;

    const Config* cfg_ = nullptr;
    std::string game_;
    AssetCache cache_;
    FrontOptions opts_;
    std::unique_ptr<MenuSet> mainMenu_, saveMenu_, pausedMenu_, optionsMenu_;
    LevelSet missions_, tutorials_;
    LevelGraph missionGraph_, tutorialGraph_;

    // Level picker text window (Menu::LevelText)
    TextWindow levelText_;
    const Image* levelPanel_ = nullptr;
    RectF levelPanelRect_{};
    std::string levelPrompt_, tutorialPrompt_;
    std::string hoverText_;
    int hoverLevel_ = 0, lastHoverLevel_ = 0;
    bool nameSfxPending_ = false;
    float nameSfxTimer_ = 0;

    // Running screen
    Menu* current_ = nullptr;
    Menu* pending_ = nullptr;  // menu change waiting for the transition
    MenuContext ctx_;

    // Overlay animation
    std::unique_ptr<Flic> overlay_;
    int overlayX_ = 0, overlayY_ = 0;
};

// Scene that runs the intro splash screens then the front end, and starts levels from it.
std::unique_ptr<Scene> CreateFrontEndScene();

}  // namespace rr::front
