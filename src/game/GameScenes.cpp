#include "game/GameScenes.h"

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
        small_ = Font::Load("Interface\\Fonts\\MbriefFont.bmp");
    }
    void Update(float elapsed) override { time_ += elapsed; }
    void Render(Renderer2D& r) override {
        r.SetAnchor(Anchor::Center);
        if (background_) background_->Draw(r, 0, 0);
        if (font_) {
            const char* text = "Start Game";
            const int w = font_->StringWidth(text);
            font_->Draw(r, 320.0f - w / 2.0f, 200, text);
        }
        if (small_) small_->Draw(r, 20, 20, "Font test: The quick brown fox 0123456789\nSecond line\twith tab");
    }

private:
    std::unique_ptr<Image> background_;
    std::unique_ptr<Font> font_, small_;
    float time_ = 0;
};

}  // namespace

std::unique_ptr<Scene> CreateStartScene() { return std::make_unique<FontTestScene>(); }

}  // namespace rr
