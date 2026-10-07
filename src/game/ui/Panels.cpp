#include "game/ui/Panels.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "core/Log.h"
#include "fs/Path.h"
#include "gfx/Font.h"
#include "platform/Input.h"

namespace rr {

namespace {

const char* const kPanelNames[int(PanelType::Count)] = {
    "Panel_Radar", "Panel_RadarFill", "Panel_RadarOverlay", "Panel_Messages", "Panel_MessagesSide",
    "Panel_CrystalSideBar", "Panel_TopPanel", "Panel_Information", "Panel_PriorityList", "Panel_CameraControl",
    "Panel_InfoDock", "Panel_Encyclopedia",
};

const char* const kButtonNames[int(PanelButton::Count)] = {
    "PanelButton_Radar_Toggle", "PanelButton_Radar_TaggedObjectView", "PanelButton_Radar_ZoomIn",
    "PanelButton_Radar_ZoomOut", "PanelButton_Radar_MapView", "PanelButton_InfoDock_Goto",
    "PanelButton_InfoDock_Close", "PanelButton_TopPanel_Options", "PanelButton_TopPanel_Priorities",
    "PanelButton_TopPanel_CallToArms", "PanelButton_Information_Toggle", "PanelButton_Information_Function",
    "PanelButton_PriorityList_Disable1", "PanelButton_PriorityList_Disable2", "PanelButton_PriorityList_Disable3",
    "PanelButton_PriorityList_Disable4", "PanelButton_PriorityList_Disable5", "PanelButton_PriorityList_Disable6",
    "PanelButton_PriorityList_Disable7", "PanelButton_PriorityList_Disable8", "PanelButton_PriorityList_Disable9",
    "PanelButton_PriorityList_UpOne1", "PanelButton_PriorityList_UpOne2", "PanelButton_PriorityList_UpOne3",
    "PanelButton_PriorityList_UpOne4", "PanelButton_PriorityList_UpOne5", "PanelButton_PriorityList_UpOne6",
    "PanelButton_PriorityList_UpOne7", "PanelButton_PriorityList_UpOne8", "PanelButton_PriorityList_Close",
    "PanelButton_PriorityList_Reset", "PanelButton_Encyclopedia_Close", "PanelButton_CameraControl_ZoomIn",
    "PanelButton_CameraControl_ZoomOut", "PanelButton_CameraControl_CycleBuildings",
    "PanelButton_CameraControl_Rotate", "PanelButton_CrystalSideBar_Ore", "PanelButton_CrystalSideBar_Crystals",
};

std::unique_ptr<Image> LoadKeyed(std::string_view path) {
    if (path.empty() || EqualsNoCase(path, "NULL")) return nullptr;
    auto img = Image::Load(path);
    if (img) img->SetupTrans({0, 0, 0}, {0, 0, 0});
    return img;
}

int ToInt(std::string_view s) { return std::atoi(std::string(s).c_str()); }

// Most frequent value of a short history, first seen wins ties (smooths flicker in the counts).
int Settled(std::array<int, 3>& history, int value) {
    history[0] = history[1];
    history[1] = history[2];
    history[2] = value;
    int best = history[0], bestCount = 0;
    for (size_t i = 0; i < history.size(); ++i) {
        const int n = int(std::count(history.begin(), history.end(), history[i]));
        if (n > bestCount) {
            best = history[i];
            bestCount = n;
        }
    }
    return best;
}

}  // namespace

void Panels::Load(const Config& cfg, std::string_view gameName) {
    // <panel> image,xOpen,yOpen,xClosed,yClosed
    for (Config::ItemRef it = cfg.FindArray(Config::Id(gameName, "Panels640x480")); it != Config::kNone;
         it = cfg.Next(it)) {
        for (int p = 0; p < int(PanelType::Count); ++p) {
            if (!EqualsNoCase(cfg.item(it).key, kPanelNames[p])) continue;
            const auto f = SplitTokens(cfg.item(it).value, ",");
            if (f.size() < 5) break;
            Panel& panel = panels_[size_t(p)];
            panel.image = LoadKeyed(f[0]);
            panel.openX = float(ToInt(f[1]));
            panel.openY = float(ToInt(f[2]));
            panel.closedX = float(ToInt(f[3]));
            panel.closedY = float(ToInt(f[4]));
            break;
        }
    }
    // <panel> button,normal,highlight,pressed,x,y,width,height,tooltip (relative to the panel)
    for (Config::ItemRef it = cfg.FindArray(Config::Id(gameName, "Buttons640x480")); it != Config::kNone;
         it = cfg.Next(it)) {
        int p = -1;
        for (int i = 0; i < int(PanelType::Count); ++i)
            if (EqualsNoCase(cfg.item(it).key, kPanelNames[i])) p = i;
        const auto f = SplitTokens(cfg.item(it).value, ",");
        if (p < 0 || f.size() < 9) continue;
        Button b;
        for (int i = 0; i < int(PanelButton::Count); ++i)
            if (EqualsNoCase(f[0], kButtonNames[i])) b.type = PanelButton(i);
        if (b.type == PanelButton::Count) continue;
        b.normal = LoadKeyed(f[1]);
        b.highlight = LoadKeyed(f[2]);
        b.pressed = LoadKeyed(f[3]);
        b.rect = {float(ToInt(f[4])), float(ToInt(f[5])), float(ToInt(f[6])), float(ToInt(f[7]))};
        if (!EqualsNoCase(f[8], "NULL")) {
            std::string tip = cfg.GetString(Config::Id(gameName, "ToolTips", f[8]), "");
            std::replace(tip.begin(), tip.end(), '_', ' ');
            b.toolTip = tip;
        }
        panels_[size_t(p)].buttons.push_back(std::move(b));
    }
    // Side bar pieces at fixed places (Lego_Initialise).
    smallCrystal_ = LoadKeyed("Interface\\RightPanel\\SmallCrystal.bmp");
    usedCrystal_ = LoadKeyed("Interface\\RightPanel\\UsedCrystal.bmp");
    noSmallCrystal_ = LoadKeyed("Interface\\RightPanel\\NoSmallCrystal.bmp");
    oreMeter_ = Image::Load("Interface\\RightPanel\\crystalsidebar_ore.bmp");
    // TODO(interface): air meter (Interface\AirMeter\...) for levels with oxygen.
    Reset();
}

void Panels::Reset() {
    for (Panel& p : panels_) {
        p.open = false;
        p.x = p.closedX;
        p.y = p.closedY;
    }
    crystalHistory_.fill(0);
    usedHistory_.fill(0);
    oreMeterFill_ = 0.0f;
    oreMeterCount_ = 0;
}

void Panels::Toggle(PanelType p) { panels_[size_t(p)].open = !panels_[size_t(p)].open; }

bool Panels::OverImage(const Panel& p, float mx, float my) const {
    if (!p.image) return false;
    const int px = int(mx - std::round(p.x) - p.anchorX), py = int(my - std::round(p.y));
    if (px < 0 || py < 0 || px >= p.image->width() || py >= p.image->height()) return false;
    // Only solid (non-black) pixels belong to the panel (Panel_TestPointInsideImage).
    const uint8_t* c = &p.image->bitmap().rgba[(size_t(py) * size_t(p.image->width()) + size_t(px)) * 4];
    return c[0] || c[1] || c[2];
}

bool Panels::HandleInput(const Input& in) {
    const float mx = in.mouseX(), my = in.mouseY();
    hoverPanel_ = hoverButton_ = -1;
    hoverToolTip_.clear();
    bool over = false;
    for (int p = 0; p < int(PanelType::Count); ++p) {
        const Panel& panel = panels_[size_t(p)];
        // Panels that are not drawn (messages, priorities, encyclopedia...) must not swallow clicks.
        if (!panel.image || !panel.shown) continue;
        for (size_t b = 0; b < panel.buttons.size(); ++b) {
            const Button& btn = panel.buttons[b];
            const RectF r{std::round(panel.x) + panel.anchorX + btn.rect.x, std::round(panel.y) + btn.rect.y,
                          btn.rect.w, btn.rect.h};
            if (r.w > 0 && r.Contains(mx, my)) {
                hoverPanel_ = p;
                hoverButton_ = int(b);
                hoverToolTip_ = btn.toolTip;
            }
        }
        if (OverImage(panel, mx, my)) over = true;
    }
    if (hoverButton_ >= 0) over = true;
    if (in.leftClicked() && hoverButton_ >= 0) pressed_ = true;
    if (in.leftReleased()) {
        if (pressed_ && hoverButton_ >= 0 && onButton)
            onButton(panels_[size_t(hoverPanel_)].buttons[size_t(hoverButton_)].type);
        pressed_ = false;
    }
    return over;
}

void Panels::DrawPanel(Renderer2D& r, PanelType type, float elapsed) {
    Panel& p = panels_[size_t(type)];
    if (!p.image) return;
    // Ease towards the target: each frame moves to (4/dt * pos + target) / (4/dt + 1).
    const float tx = p.open ? p.openX : p.closedX, ty = p.open ? p.openY : p.closedY;
    if (elapsed > 0.0f) {
        const float k = 4.0f / elapsed;
        p.x = std::abs(p.x - tx) <= 1.0f ? tx : (k * p.x + tx) / (k + 1.0f);
        p.y = std::abs(p.y - ty) <= 1.0f ? ty : (k * p.y + ty) / (k + 1.0f);
    }
    const float x = std::round(p.x), y = std::round(p.y);
    p.shown = true;
    p.image->Draw(r, x, y);
    for (size_t b = 0; b < p.buttons.size(); ++b) {
        const Button& btn = p.buttons[b];
        const bool hovered = hoverPanel_ == int(type) && hoverButton_ == int(b);
        const Image* img = hovered ? (pressed_ ? btn.pressed.get() : btn.highlight.get()) : btn.normal.get();
        if (img) img->Draw(r, x + btn.rect.x, y + btn.rect.y);
    }
}

void Panels::AnchorFor(Renderer2D& r, PanelType t) {
    // Panels designed for the right half follow the right screen edge in widescreen layouts.
    Panel& p = panels_[size_t(t)];
    const Anchor a = std::max(p.openX, p.closedX) >= 320.0f ? Anchor::Right : Anchor::Left;
    r.SetAnchor(a);
    p.anchorX = r.AnchorOffset(a);
}

void Panels::DrawPanels(Renderer2D& r, float elapsed) {
    // Lego_MainLoop drawing order; the radar map itself is drawn before this by the level.
    for (PanelType t : {PanelType::RadarFill, PanelType::Radar, PanelType::MessagesSide, PanelType::CameraControl,
                        PanelType::TopPanel}) {
        if (t == PanelType::RadarFill && radarMapView) continue;
        AnchorFor(r, t);
        DrawPanel(r, t, elapsed);
    }
    // TODO(interface): priority list, encyclopedia, information and info dock panels.
    r.SetAnchor(Anchor::Center);
}

void Panels::DrawSideBar(Renderer2D& r, const Font* font, const Resources& res, float elapsed) {
    AnchorFor(r, PanelType::CrystalSideBar);
    DrawPanel(r, PanelType::CrystalSideBar, elapsed);
    const Panel& bar = panels_[size_t(PanelType::CrystalSideBar)];
    // Crystal column: one slot per crystal of the quota from the bottom up, used ones first.
    const int quota = res.quota <= 0 || res.quota > 26 ? 26 : res.quota;
    const int crystals = std::min(Settled(crystalHistory_, res.crystals), quota);
    const int available = std::min(Settled(usedHistory_, res.crystals - res.usedCrystals), quota);
    if (noSmallCrystal_)
        for (int i = 0; i < quota; ++i) noSmallCrystal_->Draw(r, 624.0f, 429.0f - 17.0f * float(i));
    if (smallCrystal_) {
        int slot = 0;
        if (usedCrystal_)
            for (; slot < available; ++slot) smallCrystal_->Draw(r, 624.0f, 429.0f - 17.0f * float(slot));
        const Image* rest = usedCrystal_ ? usedCrystal_.get() : smallCrystal_.get();
        for (; slot < crystals; ++slot) rest->Draw(r, 624.0f, 429.0f - 17.0f * float(slot));
    }
    // Ore meter fills by 5% of what is left per ore (Panel_CryOreSideBar_ChangeOreMeter).
    const int oreTotal = res.ore + res.studs * 5;
    for (; oreMeterCount_ < oreTotal; ++oreMeterCount_) oreMeterFill_ += (1.0f - oreMeterFill_) * 0.05f;
    for (; oreMeterCount_ > oreTotal; --oreMeterCount_)
        oreMeterFill_ = std::max(0.0f, (oreMeterFill_ - 0.05f) * 20.0f / 19.0f);
    if (oreMeter_ && oreMeterFill_ > 0.0f) {
        const float h = oreMeterLength_ * oreMeterFill_;
        const RectF src{0, 0, float(oreMeter_->width()), h};
        const float w = float(oreMeter_->width());
        oreMeter_->Draw(r, oreMeterX_, oreMeterY_ - h, &src, &w, &h);
    }
    if (font) {
        auto printCentred = [&](float x, float y, int value) {
            const std::string s = std::to_string(value);
            font->Draw(r, std::round(bar.x) + x - float(font->StringWidth(s) / 2), std::round(bar.y) + y, s);
        };
        printCentred(41.0f, 469.0f, crystals);
        printCentred(16.0f, 469.0f, oreTotal);
    }
    r.SetAnchor(Anchor::Center);
}

}  // namespace rr
