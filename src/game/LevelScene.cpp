#include "game/LevelScene.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "config/Config.h"
#include "core/Log.h"
#include "fs/Path.h"
#include "game/Game.h"
#include "game/Messages.h"
#include "game/Pointer.h"
#include "game/front/FrontEnd.h"
#include "game/objects/GameObject.h"
#include "game/world/Camera.h"
#include "game/world/Level.h"
#include "game/world/TextureSet.h"
#include "gfx/Renderer3D.h"

namespace rr {

namespace {

class LevelScene : public Scene {
public:
    explicit LevelScene(std::string levelId) : levelId_(std::move(levelId)) {}

    void OnEnter() override {
        const Config& cfg = GetApp().config();
        Game& game = GetGame();
        if (!renderer_.Init()) RR_ERROR("3D renderer failed to initialise");
        if (!level_.Load(cfg, game.gameName, levelId_)) {
            RR_ERROR("Could not load level %s", levelId_.c_str());
            failed_ = true;
            return;
        }
        // Texture set ("Textures::Rock" etc.)
        const std::string& set = level_.settings().textureSet;
        const int tw = cfg.GetInt(Config::Id(game.gameName, set, "surftextwidth")).value_or(0);
        const int th = cfg.GetInt(Config::Id(game.gameName, set, "surftextheight")).value_or(0);
        if (!textures_.Load(cfg.GetString(Config::Id(game.gameName, set, "texturebasename"), ""), tw, th))
            RR_WARN("Texture set %s not loaded", set.c_str());

        LoadObjects(cfg);
        IconPanel& panel = *game.iconPanel;
        panel.BackToMain();
        panel.isEnabled = [this](MenuItem item) { return ItemEnabled(item); };
        panel.onItem = [this](MenuItem item) { OnMenuItem(item); };
        auto rgb = [&](std::string_view key, Vec3f fallback) {
            float r, g, b;
            return cfg.GetRGB(game.Id("Main", key), r, g, b) ? Vec3f{r, g, b} : fallback;
        };
        ambient_ = rgb("AmbientRGB", {0.04f, 0.04f, 0.04f});
        topSpot_ = rgb("TopSpotRGB", {0.5f, 0.5f, 0.5f});
        clipDist_ = cfg.GetFloat(game.Id("Main", "TVClipDist")).value_or(800.0f);
        if (auto v = cfg.GetFloat(game.Id("Main", "MinDist"))) camera_.minZoom = *v;
        if (auto v = cfg.GetFloat(game.Id("Main", "MaxDist"))) camera_.maxZoom = *v;
        if (auto v = cfg.GetFloat(game.Id("Main", "MinTilt"))) camera_.minTilt = DegToRad(*v);
        if (auto v = cfg.GetFloat(game.Id("Main", "MaxTilt"))) camera_.maxTilt = DegToRad(*v);
        const std::string& dbg = GetApp().launch().debugCamera;
        if (!dbg.empty()) {
            const auto parts = SplitTokens(dbg, ",");
            camera_.tilt = DegToRad(float(std::atof(std::string(parts[0]).c_str())));
            if (parts.size() > 1) camera_.zoom = float(std::atof(std::string(parts[1]).c_str()));
            if (parts.size() > 3) {
                camera_.x = float(std::atof(std::string(parts[2]).c_str()));
                camera_.y = float(std::atof(std::string(parts[3]).c_str()));
            }
            clipDist_ = 100000.0f;
        }
    }

    void LoadObjects(const Config& cfg) {
        ObjectList::CameraStart start;
        const std::string olFile = cfg.GetString(Config::Id(GetGame().gameName, levelId_, "OListFile"), "");
        if (!objects_.LoadObjectList(olFile, level_, start)) RR_WARN("Object list %s incomplete", olFile.c_str());
        if (start.found) {
            camera_.x = start.x;
            camera_.y = start.y;
            camera_.rotation = start.heading;
            camera_.tilt = 0.8f;
        }
    }

    // Ray from a logical screen position into the 3D view.
    void MouseRay(float lx, float ly, Vec3f& origin, Vec3f& dir) const {
        const world::TopDownCamera::View view = camera_.Compute(&level_.map());
        const float ndcX = lx / uiWidth_ * 2.0f - 1.0f, ndcY = 1.0f - ly / 480.0f * 2.0f;
        const float tanX = 0.5f, tanY = 0.5f / viewAspect_;
        const Vec3f f = Normalize(view.forward);
        const Vec3f s = Normalize(Cross(view.up, f));
        const Vec3f u = Cross(f, s);
        origin = view.eye;
        dir = Normalize(f + s * (ndcX * tanX) + u * (ndcY * tanY));
    }

    // Marches a ray until it passes below the terrain surface (+Z points down).
    bool PickGround(const Vec3f& origin, const Vec3f& dir, Vec3f& hit) const {
        const world::Map3D& map = level_.map();
        auto below = [&](float t) {
            const Vec3f p = origin + dir * t;
            return p.z >= map.WorldZ(p.x, p.y);
        };
        constexpr float kStep = 2.0f;
        for (float t = kStep; t < clipDist_; t += kStep) {
            if (!below(t)) continue;
            float lo = t - kStep, hi = t;
            for (int i = 0; i < 12; ++i) {
                const float mid = (lo + hi) * 0.5f;
                (below(mid) ? hi : lo) = mid;
            }
            hit = origin + dir * hi;
            return true;
        }
        return false;
    }

    // World clicks (the part of Lego_HandleWorld implemented so far): a unit under the cursor is
    // selected; with units selected, clicking open floor sends them there.
    void HandleWorldClick(Input& in) {
        IconPanel& panel = *GetGame().iconPanel;
        if (panel.HandleInput(in)) return;  // the panel takes clicks over it
        if (!in.leftReleased() && !in.rightReleased()) return;
        if (in.rightReleased()) {
            // TODO(interface): right-click handling of the original (menus, cancel).
            messages_.Post(MessageType::ClearSelection);
            return;
        }
        Vec3f origin, dir;
        MouseRay(in.mouseX(), in.mouseY(), origin, dir);
        if (GameObject* o = objects_.PickObject(origin, dir)) {
            if (Selection::IsSelectable(*o)) {
                messages_.Post(MessageType::ClearSelection);
                messages_.Post(MessageType::Select, o);
                panel.OpenMenu(UnitMenu(*o));
            }
            return;
        }
        Vec3f hit;
        BlockPos b;
        if (!PickGround(origin, dir, hit) || !level_.map().WorldToBlock(hit.x, hit.y, b.x, b.y)) return;
        const world::Block& blk = level_.block(b.x, b.y);
        if (!selection_.empty() && (blk.flags & world::BlockFlag::Ground)) {
            messages_.Post(MessageType::UserGoto, nullptr, 0, &b);
            panel.BackToMain();
            return;
        }
        // Block menus (TODO(interface): the remaining cases of Lego_HandleWorld).
        if ((blk.flags & world::BlockFlag::Wall) && !(blk.flags & world::BlockFlag::Ground)) {
            panel.OpenMenu(MenuType::Wall, &b);
            RR_DEBUG("Wall menu at %d,%d terrain %d flags %08x", b.x, b.y, int(blk.terrain), blk.flags);
        } else if (blk.flags & world::BlockFlag::Ground) {
            const bool rubble = !(blk.flags & world::BlockFlag::CleanFloor);
            panel.OpenMenu(rubble ? MenuType::Rubble : MenuType::Ground, &b);
        }
    }

    // Interface item availability (simplified until the matching game systems exist).
    bool ItemEnabled(MenuItem item) const {
        const BlockPos& b = GetGame().iconPanel->block();
        const world::Block& blk = level_.block(b.x, b.y);
        switch (item) {
        case MenuItem::Dig:
            // Hard rock needs a unit that can drill it; immovable rock never.
            return blk.terrain != world::Terrain::Immovable && !(blk.flags & world::BlockFlag::Ground);
        case MenuItem::ClearRubble: return (blk.flags & world::BlockFlag::Ground) && !(blk.flags & world::BlockFlag::CleanFloor);
        default: return false;  // TODO(interface): the remaining items as their systems arrive
        }
    }

    void OnMenuItem(MenuItem item) {
        IconPanel& panel = *GetGame().iconPanel;
        RR_INFO("Menu item %s at block %d,%d", MenuItemName(item), panel.block().x, panel.block().y);
        // TODO(ai): Dig/ClearRubble create AI task requests (AITask_DoDig_AtBlockPos, ...).
        panel.BackToMain();
    }

    static MenuType UnitMenu(const GameObject& o) {
        switch (o.type) {
        case ObjType::MiniFigure: return MenuType::LegoMan;
        case ObjType::Vehicle: return MenuType::Vehicle;  // TODO(vehicles): water / unmanned variants
        case ObjType::Building: return MenuType::Building;
        case ObjType::ElectricFence: return MenuType::ElectricFence;
        default: return MenuType::Main;
        }
    }

    void HandleMessage(const Message& m) {
        switch (m.type) {
        case MessageType::Select:
            if (m.object && selection_.Select(*m.object) && m.object->type != ObjType::Building)
                objects_.RouteEnd(*m.object, false);  // TODO(ai): also drops gather tasks
            break;
        case MessageType::ClearSelection:
            selection_.Clear();
            GetGame().iconPanel->BackToMain();
            break;
        case MessageType::Deselect:
            if (m.object) selection_.Deselect(*m.object);
            break;
        case MessageType::UserGoto:
            // TODO(ai): AITask_QueueGotoBlock_Group; for now each unit routes directly.
            for (GameObject* o : selection_.units())
                if (!objects_.RouteToBlock(*o, m.block)) RR_DEBUG("No route for %s", o->info->name.c_str());
            break;
        default: break;
        }
    }

    // Corner brackets around a selected unit's projected pick sphere (Lego_DrawObjectSelectionBox).
    void DrawSelectionBox(Renderer2D& r2d, const Mat4& viewProj, const GameObject& o, ColorF colour) const {
        auto project = [&](const Vec3f& p, float& x, float& y) {
            const float* m = viewProj.m;
            const float cx = m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12];
            const float cy = m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13];
            const float cw = m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15];
            if (cw <= 0.0f) return false;
            x = (cx / cw + 1.0f) * 0.5f * uiWidth_;
            y = (1.0f - cy / cw) * 0.5f * 480.0f;
            return true;
        };
        const Vec3f centre = objects_.PickCentre(o);
        float sx, sy;
        if (!project(centre, sx, sy) || sx < 0.0f || sy < 0.0f || sx > uiWidth_ || sy > 480.0f) return;
        // Size from how large the pick sphere appears: offset the centre sideways in view space.
        const world::TopDownCamera::View view = camera_.Compute(&level_.map());
        const Vec3f side = Normalize(Cross(view.up, Normalize(centre - view.eye)));
        float ex, ey;
        if (!project(centre + side * o.stats->Num("PickSphere", o.level), ex, ey)) return;
        const float dist = std::sqrt((ex - sx) * (ex - sx) + (ey - sy) * (ey - sy));
        const float h = dist / 2.5f;          // half size of the box
        const float arm = h / 5.0f + 3.0f;    // bracket length
        const float l = sx - h, r = sx + h, t = sy - h, btm = sy + h;
        r2d.DrawLine(l, t + arm, l, t, colour);
        r2d.DrawLine(l, t, l + arm, t, colour);
        r2d.DrawLine(r - arm, t, r, t, colour);
        r2d.DrawLine(r, t, r, t + arm, colour);
        r2d.DrawLine(r, btm - arm, r, btm, colour);
        r2d.DrawLine(r, btm, r - arm, btm, colour);
        r2d.DrawLine(l + arm, btm, l, btm, colour);
        r2d.DrawLine(l, btm, l, btm - arm, colour);
    }

    void Update(float elapsed) override {
        Input& in = GetApp().input();
        if (in.KeyPressed(SDL_SCANCODE_ESCAPE)) {
            GetApp().SetScene(front::CreateFrontEndScene());
            return;
        }
        if (failed_) return;
        // Temporary free camera until the interface/camera control module is implemented.
        const float speed = 7.0f * elapsed;
        const float c = std::cos(camera_.rotation), s = std::sin(camera_.rotation);
        auto move = [&](float fwd, float side) {
            camera_.x += (side * c + fwd * s) * speed;
            camera_.y += (-side * s + fwd * c) * speed;
        };
        if (in.KeyDown(SDL_SCANCODE_UP)) move(1, 0);
        if (in.KeyDown(SDL_SCANCODE_DOWN)) move(-1, 0);
        if (in.KeyDown(SDL_SCANCODE_LEFT)) move(0, -1);
        if (in.KeyDown(SDL_SCANCODE_RIGHT)) move(0, 1);
        if (in.KeyDown(SDL_SCANCODE_Q)) camera_.rotation -= 0.05f * elapsed;
        if (in.KeyDown(SDL_SCANCODE_E)) camera_.rotation += 0.05f * elapsed;
        if (in.KeyDown(SDL_SCANCODE_PAGEUP)) camera_.tilt = std::min(camera_.tilt + 0.02f * elapsed, camera_.maxTilt);
        if (in.KeyDown(SDL_SCANCODE_PAGEDOWN)) camera_.tilt = std::max(camera_.tilt - 0.02f * elapsed, camera_.minTilt);
        if (in.wheel()) camera_.zoom = std::clamp(camera_.zoom - float(in.wheel()) * 10.0f, camera_.minZoom, camera_.maxZoom);
        GetPointers().Update(elapsed);
        messages_.Update([this](const Message& m) { HandleMessage(m); });
        HandleWorldClick(in);
        objects_.Update(elapsed);
        level_.map().UpdateUVs(elapsed);
    }

    void DrawTerrain() {
        const world::Map3D& map = level_.map();
        tris_.clear();
        for (const world::Map3D::Quad& q : map.quads()) {
            if (q.hidden) continue;
            const Texture3D* tex = textures_.Get(q.texture);
            static const int kPlain[6] = {0, 1, 3, 1, 2, 3};
            static const int kRotated[6] = {3, 0, 2, 0, 1, 2};
            const int* order = q.rotated ? kRotated : kPlain;
            Vertex3D v[6];
            for (int i = 0; i < 6; ++i) v[i] = {q.pos[order[i]], q.normal[order[i]], q.uv[order[i]], 0xFFFFFFFFu};
            renderer_.DrawTriangles(tex, v, 6);
        }
    }

    void Render(Renderer2D& r2d) override {
        if (failed_) return;
        int vx, vy, vw, vh;
        r2d.ViewportPixels(vx, vy, vw, vh);
        const world::TopDownCamera::View view = camera_.Compute(&level_.map());
        // Direct3D Retained Mode default field of view: tan(half horizontal angle) = 0.5.
        const float aspect = float(vw) / float(std::max(1, vh));
        viewAspect_ = aspect;
        uiWidth_ = r2d.uiWidth();
        const float fovY = 2.0f * std::atan(0.5f / aspect);
        const Mat4 proj = Mat4::Perspective(fovY, aspect, 1.0f, clipDist_);
        const Mat4 viewMat = LookDirLH(view.eye, view.forward, view.up);
        renderer_.BeginFrame(vx, vy, vw, vh, viewMat, proj, view.eye);

        std::vector<Light> lights;
        lights.push_back({Light::Type::Ambient, ambient_});
        // TopSpot: placed at (200, 140, -130) from the view target, shining back towards it.
        Light spot;
        spot.type = Light::Type::Spot;
        spot.colour = topSpot_;
        spot.position = view.target + Vec3f{200.0f, 140.0f, -130.0f};
        spot.direction = Normalize({-1.0f, -0.8f, 0.75f});
        spot.innerCos = std::cos(0.4f * 0.5f);
        spot.outerCos = std::cos(0.85f * 0.5f);
        spot.range = 100000.0f;
        lights.push_back(spot);
        // Light-effects lamp above the view (refined when LightEffects is implemented).
        Light lamp = spot;
        lamp.colour = {0.8f, 0.8f, 0.8f};
        lamp.position = view.eye;
        lamp.direction = view.forward;
        lamp.innerCos = std::cos(0.0f);
        lamp.outerCos = std::cos(0.85f * 0.5f);
        lights.push_back(lamp);
        renderer_.SetLights(lights);

        FogSettings fog;
        const auto& ls = level_.settings();
        fog.enabled = ls.fog && GetApp().launch().debugCamera.empty();
        fog.colour = {ls.fogR, ls.fogG, ls.fogB};
        renderer_.SetFog(fog);

        DrawTerrain();
        static const bool hideObjects = std::getenv("RR_DEBUG_NOOBJ") != nullptr;  // pixel-diff checks
        if (!hideObjects) objects_.Draw(renderer_);
        renderer_.EndFrame();

        r2d.SetAnchor(Anchor::Center);
        // Primary unit green, the rest yellow (Lego_DrawAllSelectedUnitBoxes).
        const Mat4 viewProj = proj * viewMat;
        for (size_t i = 0; i < selection_.units().size(); ++i)
            DrawSelectionBox(r2d, viewProj, *selection_.units()[i], i == 0 ? ColorF{0, 1, 0, 1} : ColorF{1, 1, 0, 1});
        if (const Font* f = GetGame().textFont.get()) f->Draw(r2d, 8, 8, ls.fullName);
        r2d.SetAnchor(Anchor::Right);
        GetGame().iconPanel->Draw(r2d);
        r2d.SetAnchor(Anchor::Center);
        // TODO(interface): proper tooltip boxes (ToolTip_*); plain text for now.
        if (const Font* f = GetGame().toolTipFont.get(); f && !GetGame().iconPanel->hoverToolTip().empty())
            f->Draw(r2d, GetApp().input().mouseX() + 16, GetApp().input().mouseY() + 16, GetGame().iconPanel->hoverToolTip());
        GetPointers().Draw(r2d, GetApp().input().mouseX(), GetApp().input().mouseY());
    }

private:
    std::string levelId_;
    world::Level level_;
    world::TextureSet textures_;
    world::TopDownCamera camera_;
    Renderer3D renderer_;
    std::vector<Vertex3D> tris_;
    ObjectList objects_;
    MessageQueue messages_;
    Selection selection_;
    Vec3f ambient_, topSpot_;
    float clipDist_ = 800.0f;
    float viewAspect_ = 4.0f / 3.0f, uiWidth_ = 640.0f;
    bool failed_ = false;
};

}  // namespace

std::unique_ptr<Scene> CreateLevelScene(const std::string& levelId) { return std::make_unique<LevelScene>(levelId); }

}  // namespace rr
