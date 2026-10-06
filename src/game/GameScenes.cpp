#include "game/GameScenes.h"

#include "fs/Path.h"
#include "game/Game.h"
#include "game/LevelScene.h"
#include "game/front/FrontEnd.h"
#include "gfx/Font.h"
#include "gfx/Image.h"

namespace rr {

namespace {

// Development scene: shows the loading screen with sample text in a game font.
class FontTestScene : public Scene {
public:
    void OnEnter() override {
        const Config& cfg = GetApp().config();
        background_ = Image::Load(cfg.GetString("Lego*::Main::LoadScreen", ""));
        font_ = Font::Load("Interface\\FrontEnd\\Menu_Font_Hi.bmp");
    }
    void Update(float) override {}
    void Render(Renderer2D& r) override {
        r.SetAnchor(Anchor::Center);
        if (background_) background_->Draw(r, 0, 0);
        if (font_) font_->Draw(r, 20, 20, "Font test 0123456789");
    }

private:
    std::unique_ptr<Image> background_;
    std::unique_ptr<Font> font_;
};

}  // namespace

std::unique_ptr<Scene> CreateStartScene() {
    App& app = GetApp();
    GetGame().Initialise(app.config());
    if (EqualsNoCase(app.launch().startScene, "fonttest")) return std::make_unique<FontTestScene>();
    if (EqualsNoCase(app.launch().startScene, "menu")) GetGame().introsDone = true;  // skip splash screens
    if (!app.launch().startLevel.empty()) {
        std::string id = app.launch().startLevel;
        if (!StartsWithNoCase(id, "Levels::")) id = "Levels::" + id;
        GetGame().introsDone = true;
        return CreateLevelScene(id);
    }
    return front::CreateFrontEndScene();
}

}  // namespace rr
