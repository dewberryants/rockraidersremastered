#include "game/front/FrontEnd.h"

#include <algorithm>
#include <cstdio>
#include <functional>

#include "audio/SoundBank.h"
#include "core/Log.h"
#include "core/Random.h"
#include "fs/Path.h"
#include "game/Game.h"
#include "game/Pointer.h"
#include "game/Sfx.h"
#include "gfx/Image.h"
#include "game/LevelScene.h"

namespace rr::front {

int LevelSet::IndexOf(std::string_view id) const {
    for (size_t i = 0; i < ids.size(); ++i)
        if (EqualsNoCase(ids[i], id)) return int(i);
    return -1;
}

namespace {
RectF ParseRect(std::string_view s) {
    const auto p = SplitTokens(s, "|");
    RectF r{};
    if (p.size() >= 4) {
        r.x = float(std::atof(std::string(p[0]).c_str()));
        r.y = float(std::atof(std::string(p[1]).c_str()));
        r.w = float(std::atof(std::string(p[2]).c_str()));
        r.h = float(std::atof(std::string(p[3]).c_str()));
    }
    return r;
}
}  // namespace

void FrontEnd::Initialise(const Config& cfg, std::string_view gameName) {
    cfg_ = &cfg;
    game_ = std::string(gameName);
    Game& game = GetGame();

    // Level picker text window: "Menu::LevelText" { Window x|y|w|h, Panel file|x|y|w|h, Level, Tutorial }
    const std::string lt = "Menu::LevelText";
    levelText_ = TextWindow(game.textFont.get(), ParseRect(cfg.GetString(Config::Id(game_, lt, "Window"), "")));
    if (auto panel = cfg.GetString(Config::Id(game_, lt, "Panel"))) {
        const auto p = SplitTokens(*panel, "|");
        levelPanel_ = cache_.LoadImage(p[0]);
        if (levelPanel_) const_cast<Image*>(levelPanel_)->SetupTrans({0, 0, 0}, {0, 0, 0});
        if (p.size() >= 5) levelPanelRect_ = ParseRect(std::string(p[1]) + "|" + std::string(p[2]) + "|" +
                                                       std::string(p[3]) + "|" + std::string(p[4]));
    }
    levelPrompt_ = ReplaceUnderscores(cfg.GetString(Config::Id(game_, lt, "Level"), ""));
    tutorialPrompt_ = ReplaceUnderscores(cfg.GetString(Config::Id(game_, lt, "Tutorial"), ""));

    // Menu sets with their (value, callback) bindings in the order the original passed them.
    mainMenu_ = LoadMenuSet(cfg, game_, "MainMenuFull",
                            {Binding{&opts_.playCredits, nullptr, nullptr, nullptr,
                                     [] { RR_INFO("Credits requested (credits screen not implemented yet)"); }},
                             Binding{&opts_.quitGame}},
                            cache_);
    saveMenu_ = LoadMenuSet(cfg, game_, "SaveMenu", {Binding{&opts_.backFromSave}}, cache_);
    std::vector<Binding> options = {
        Binding{&opts_.gameSpeed},       Binding{&opts_.sfxVolume},  Binding{&opts_.musicVolume},
        Binding{&opts_.brightness},      Binding{&opts_.helpWindow}, Binding{&opts_.replayObjective},
    };
    std::vector<Binding> paused = {Binding{&opts_.continueGame}};
    paused.insert(paused.end(), options.begin(), options.end());
    paused.insert(paused.end(), {Binding{&opts_.quitMission}, Binding{&opts_.restartMission},
                                 Binding{&opts_.wallDetail}, Binding{&opts_.music}, Binding{&opts_.sound},
                                 Binding{&opts_.autoGameSpeed}});
    pausedMenu_ = LoadMenuSet(cfg, game_, "PausedMenu", paused, cache_);
    options.push_back(Binding{&opts_.continueGame});
    optionsMenu_ = LoadMenuSet(cfg, game_, "OptionsMenu", options, cache_);

    LoadLevelSet(missions_, "StartLevel");
    LoadLevelSet(tutorials_, "TutorialStartLevel");
    BuildGraph(missions_, missionGraph_);
    BuildGraph(tutorials_, tutorialGraph_);
    AddPicker(MissionsMenu(), missions_, missionGraph_, &opts_.missionSelected, true);
    AddPicker(TutorialsMenu(), tutorials_, tutorialGraph_, &opts_.tutorialSelected, false);
    RR_INFO("Front end: %zu missions, %zu tutorials", missions_.ids.size(), tutorials_.ids.size());
}

Menu* FrontEnd::MissionsMenu() const { return mainMenu_ ? mainMenu_->menu(mainMenu_->IndexOf("Levels")) : nullptr; }
Menu* FrontEnd::TutorialsMenu() const {
    return mainMenu_ ? mainMenu_->menu(mainMenu_->IndexOf("Tutorials")) : nullptr;
}

void FrontEnd::LoadLevelSet(LevelSet& set, std::string_view startKey) {
    std::string id = cfg_->GetString(Config::Id(game_, "Main", startKey), "");
    while (!id.empty() && set.IndexOf(id) == -1) {
        set.ids.push_back(id);
        const auto full = cfg_->GetString(Config::Id(game_, id, "FullName"));
        set.names.push_back(full ? ReplaceUnderscores(*full) : id);
        id = cfg_->GetString(Config::Id(game_, id, "NextLevel"), "");
    }
}

void FrontEnd::BuildGraph(const LevelSet& set, LevelGraph& graph) {
    graph.nodes.clear();
    graph.preorder.clear();
    if (set.ids.empty()) return;
    std::vector<int> nodeOf(set.ids.size(), -1);
    // Depth-first, a level's node is created on first visit; later links reuse it.
    std::function<int(int)> visit = [&](int setIndex) -> int {
        if (nodeOf[size_t(setIndex)] != -1) return nodeOf[size_t(setIndex)];
        const int node = int(graph.nodes.size());
        nodeOf[size_t(setIndex)] = node;
        graph.nodes.push_back({setIndex, {}});
        graph.preorder.push_back(node);
        const auto links = cfg_->GetString(Config::Id(game_, set.ids[size_t(setIndex)], "LevelLinks"));
        if (links) {
            for (std::string_view link : SplitTokens(*links, ",")) {
                const int child = set.IndexOf(link);
                if (child == -1) continue;
                const int childNode = visit(child);
                graph.nodes[size_t(node)].children.push_back(childNode);
            }
        }
        return node;
    };
    visit(0);
}

void FrontEnd::AddPicker(Menu* menu, const LevelSet& set, const LevelGraph& graph, int* value, bool missions) {
    if (!menu) return;
    auto item = std::make_unique<MenuItem>();
    item->type = ItemType::Select;
    SelectData& s = item->select;
    s.value = value;
    s.visibleCount = int(graph.preorder.size());
    s.callback = [this, missions](float elapsed, int entry) { OnLevelHover(missions, elapsed, entry); };
    const std::string defaults = cfg_->GetString(Config::Id(game_, "Menu", "DefaultLevelBMPS"), "");
    for (int node : graph.preorder) {
        const std::string& id = set.ids[size_t(graph.nodes[size_t(node)].setIndex)];
        SelectEntry e;
        e.isImage = true;
        e.x = cfg_->GetInt(Config::Id(game_, id, "FrontEndX")).value_or(0);
        e.y = cfg_->GetInt(Config::Id(game_, id, "FrontEndY")).value_or(0);
        e.frontEndOpen = cfg_->GetBool(Config::Id(game_, id, "FrontEndOpen")) == BoolTri::True;
        const std::string bmps = cfg_->GetString(Config::Id(game_, id, "MenuBMP"), defaults);
        const auto names = SplitTokens(bmps, ",");
        for (size_t k = 0; k < 3 && k < names.size(); ++k) {
            const Image* img = cache_.LoadImage(names[k]);
            if (img) const_cast<Image*>(img)->SetPenZeroTrans();
            e.images[k] = img;
            e.widths[k] = img ? img->width() : 0;
            e.heights[k] = img ? img->height() : 0;
        }
        s.entries.push_back(std::move(e));
    }
    menu->items.push_back(std::move(item));
}

// Front_Levels_UpdateAvailable: walk the link graph depth-first; a level is selectable if it is the
// start level, if the level linking to it was completed, or if it is marked FrontEndOpen.
// Without save data, "completed" is !keepLocked (so tutorials are all open, missions are not).
void FrontEnd::UpdateAvailability(const LevelSet&, const LevelGraph& graph, Menu* picker, bool keepLocked) {
    if (!picker || picker->items.empty()) return;
    MenuItem* item = nullptr;
    for (auto& it : picker->items)
        if (it->type == ItemType::Select) item = it.get();
    if (!item) return;
    std::vector<bool> visited(graph.nodes.size(), false);
    size_t entry = 0;
    std::function<void(int, bool)> walk = [&](int node, bool parentUnlocked) {
        if (visited[size_t(node)]) return;
        visited[size_t(node)] = true;
        const bool completed = !keepLocked;  // TODO(save data): use the save's per-level completion flag
        SelectEntry& e = item->select.entries[entry++];
        e.enabled = completed || parentUnlocked || e.frontEndOpen;
        for (int child : graph.nodes[size_t(node)].children) walk(child, completed);
    };
    if (!graph.nodes.empty()) walk(0, true);
}

void FrontEnd::OnLevelHover(bool missions, float elapsed, int entry) {
    const LevelSet& set = missions ? missions_ : tutorials_;
    const LevelGraph& graph = missions ? missionGraph_ : tutorialGraph_;
    if (entry < 0 || entry >= int(graph.preorder.size())) return;
    const int setIndex = graph.nodes[size_t(graph.preorder[size_t(entry)])].setIndex;
    lastHoverLevel_ = hoverLevel_;
    hoverLevel_ = setIndex + 1;
    hoverText_ = set.names[size_t(setIndex)];
    if (missions) {
        // TODO(save data): " <Level_Completed> (score)" for completed levels.
        hoverText_ += " " + ReplaceUnderscores(cfg_->GetString(Config::Id(game_, "Menu", "Level_Incomplete"), ""));
    }
    // After hovering the same level for 20 game units its name is spoken.
    if (hoverLevel_ != lastHoverLevel_) {
        nameSfxPending_ = true;
        nameSfxTimer_ = 0.0f;
    }
    if (nameSfxPending_) {
        nameSfxTimer_ += elapsed;
        if (nameSfxTimer_ * 25.0f > 500.0f) {
            char name[64];
            if (!missions || hoverLevel_ < 9) std::snprintf(name, sizeof(name), "Stream_LevelName_TLevel%d", hoverLevel_);
            else std::snprintf(name, sizeof(name), "Stream_LevelName_Level%d", hoverLevel_ - 8);
            int id = 0;
            if (GetSfx().GetType(name, id)) {
                GetSfx().PlayNormal(id);
                nameSfxPending_ = false;
            }
        }
    }
}

void FrontEnd::BeginScreen(ScreenType type) {
    opts_.missionSelected = opts_.tutorialSelected = -1;
    opts_.quitGame = 0;
    // TODO(save data): with a current save, missions unlock from its completion flags.
    UpdateAvailability(missions_, missionGraph_, MissionsMenu(), true);
    UpdateAvailability(tutorials_, tutorialGraph_, TutorialsMenu(), false);

    MenuSet* set = type == ScreenType::LoadSave ? saveMenu_.get() : mainMenu_.get();
    const int index = type == ScreenType::Missions ? 1 : type == ScreenType::Tutorials ? 2 : 0;
    for (auto& m : set->menus) m->closed = false;
    current_ = set->menu(index);
    pending_ = nullptr;
    ctx_ = MenuContext{};
    StopOverlay();
    GetSfx().SetQueueMode(false);
    GetSfx().PlayNormal(SFX_AmbientMusicLoop, true);
}

void FrontEnd::StopOverlay() { overlay_.reset(); }

void FrontEnd::UpdateOverlays(float elapsed) {
    if (!current_) return;
    if (overlay_) {
        if (!overlay_->Advance(elapsed)) StopOverlay();
        return;
    }
    if (current_->overlays.empty() || !current_->playRandom) return;
    // A random overlay starts on average once every 400 updates.
    if (random::Rand() % 400 != 0) return;
    const MenuOverlay& ov = current_->overlays[size_t(random::Rand()) % current_->overlays.size()];
    overlay_ = Flic::Load(ov.flicPath);
    if (!overlay_) return;
    overlayX_ = ov.x;
    overlayY_ = ov.y;
    if (ov.sfx != SFX_NULL) GetSfx().PlayNormal(ov.sfx);
}

void FrontEnd::Update(float elapsed) {
    if (!current_) return;
    App& app = GetApp();
    Input& in = app.input();
    ctx_.input = &in;
    ctx_.mouseX = in.mouseX();
    ctx_.mouseY = in.mouseY();
    ctx_.mouseReleased = in.leftReleased();
    ctx_.rightKey = in.KeyPressed(SDL_SCANCODE_RIGHT);
    ctx_.leftKey = in.KeyPressed(SDL_SCANCODE_LEFT);
    ctx_.playOkay = [] { GetSfx().PlayNormal(SFX_Okay); };

    hoverText_.clear();
    Menu* next = UpdateMenuInput(*current_, ctx_, elapsed);
    if (next && next != current_) {
        // TODO(3D): the original plays the rock-wipe animation and switches at its midpoint.
        if (overlay_ && next->image != current_->image) StopOverlay();
        current_ = next;
        current_->closed = false;
        ctx_.scrollX = ctx_.scrollY = 0;
    }
    UpdateMenuScroll(*current_, ctx_);
    UpdateOverlays(elapsed);
    GetPointers().Update(elapsed);
    GetSfx().Update(elapsed);

    if (current_->closed) {
        current_ = nullptr;
        StopOverlay();
        GetSoundBank().StopAll();
    }
}

void FrontEnd::Render(Renderer2D& r) {
    if (!current_) return;
    r.SetAnchor(Anchor::Center);
    DrawMenuBackground(*current_, ctx_, r);
    if (overlay_) overlay_->Draw(r, float(overlayX_), float(overlayY_));
    DrawMenuItems(*current_, ctx_, r, 0.0f, 640);

    // Level picker text panel (shows the hovered level from the item pass above).
    if (current_ == MissionsMenu() || current_ == TutorialsMenu()) {
        if (levelPanel_) levelPanel_->DrawStretched(r, levelPanelRect_);
        levelText_.Clear();
        levelText_.Print("\n");
        levelText_.Print(current_ == MissionsMenu() ? levelPrompt_ : tutorialPrompt_);
        levelText_.Print("\n");
        levelText_.Print(hoverText_);
        levelText_.Draw(r);
    }
    DrawMenuButtons(*current_, ctx_, r);
    GetPointers().Draw(r, ctx_.mouseX, ctx_.mouseY);
}

std::string FrontEnd::SelectedLevel() const {
    if (opts_.missionSelected >= 0 && opts_.missionSelected < int(missionGraph_.preorder.size()))
        return missions_.ids[size_t(missionGraph_.nodes[size_t(missionGraph_.preorder[size_t(opts_.missionSelected)])].setIndex)];
    if (opts_.tutorialSelected >= 0 && opts_.tutorialSelected < int(tutorialGraph_.preorder.size()))
        return tutorials_.ids[size_t(
            tutorialGraph_.nodes[size_t(tutorialGraph_.preorder[size_t(opts_.tutorialSelected)])].setIndex)];
    return {};
}

// --- Scenes -------------------------------------------------------------------------------

namespace {

// Placeholder until the 3D level is implemented: shows which level would start.
class LevelPlaceholderScene : public Scene {
public:
    explicit LevelPlaceholderScene(std::string level) : level_(std::move(level)) {}
    void OnEnter() override {
        const Config& cfg = GetApp().config();
        screen_ = Image::Load(cfg.GetString(GetGame().Id("Main", "LoadScreen"), ""));
        RR_INFO("Level selected: %s", level_.c_str());
    }
    void Update(float) override {
        Input& in = GetApp().input();
        if (in.KeyPressed(SDL_SCANCODE_ESCAPE) || in.leftReleased()) GetApp().SetScene(CreateFrontEndScene());
    }
    void Render(Renderer2D& r) override {
        r.SetAnchor(Anchor::Center);
        if (screen_) screen_->Draw(r, 0, 0);
        if (const Font* f = GetGame().textFont.get()) {
            const std::string text = "Level " + level_ + " (3D engine not implemented yet - click to return)";
            f->Draw(r, 320.0f - f->StringWidth(text) / 2.0f, 440, text);
        }
    }

private:
    std::string level_;
    std::unique_ptr<Image> screen_;
};

// Splash images before the front end (Front_PlayIntroSplash). Intro movies are AVI files with the
// Indeo codec; they are skipped until a movie decoder is added.
class FrontEndScene : public Scene {
public:
    void OnEnter() override {
        Game& game = GetGame();
        if (!game.introsDone) {
            const Config& cfg = GetApp().config();
            if (auto img = cfg.GetString(game.Id("Main", "DDILogo"))) {
                splash_ = Image::Load(*img);
                splashTime_ = cfg.GetFloat(game.Id("Main", "DDILogoTime")).value_or(0.0f) * 25.0f;
            }
            game.introsDone = true;
        }
        if (game.frontEnd) game.frontEnd->BeginScreen(ScreenType::MainMenu);
    }

    void Update(float elapsed) override {
        Input& in = GetApp().input();
        if (splash_) {
            splashTime_ -= elapsed;
            const bool skip = in.leftReleased() || in.rightReleased() || in.KeyPressed(SDL_SCANCODE_SPACE) ||
                              in.KeyPressed(SDL_SCANCODE_ESCAPE) || in.KeyPressed(SDL_SCANCODE_RETURN);
            if (splashTime_ <= 0.0f || skip) splash_.reset();
            return;
        }
        front::FrontEnd* fe = GetGame().frontEnd.get();
        if (!fe) return;
        fe->Update(elapsed);
        if (!fe->running()) {
            if (fe->quitRequested()) {
                GetApp().RequestQuit();
            } else if (!fe->SelectedLevel().empty()) {
                GetGame().selectedLevel = fe->SelectedLevel();
                GetApp().SetScene(CreateLevelScene(GetGame().selectedLevel));
            } else {
                fe->BeginScreen(ScreenType::MainMenu);
            }
        }
    }

    void Render(Renderer2D& r) override {
        r.SetAnchor(Anchor::Center);
        if (splash_) {
            splash_->Draw(r, 0, 0);
            return;
        }
        if (front::FrontEnd* fe = GetGame().frontEnd.get()) fe->Render(r);
    }

private:
    std::unique_ptr<Image> splash_;
    float splashTime_ = 0;
};

}  // namespace

std::unique_ptr<Scene> CreateFrontEndScene() { return std::make_unique<FrontEndScene>(); }

}  // namespace rr::front
