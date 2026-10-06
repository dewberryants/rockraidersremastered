#include "game/LevelScene.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "config/Config.h"
#include "core/Log.h"
#include "fs/Path.h"
#include "game/Game.h"
#include "game/Pointer.h"
#include "game/front/FrontEnd.h"
#include "game/world/Camera.h"
#include "game/world/Level.h"
#include "game/world/TextureSet.h"
#include "gfx/Renderer3D.h"
#include "gfx/model/ActivityModel.h"
#include "gfx/model/ModelDraw.h"

namespace rr {

namespace {

// Placeholder object until LegoObject is implemented: a model playing one activity.
struct PlacedObject {
    std::string type;
    std::shared_ptr<ActivityModel> model;
    const Activity* activity = nullptr;
    Vec3f position;
    float heading = 0;
    float time = 0;
};

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

        LoadCamera(cfg);
        LoadObjects(cfg);
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

    // Finds the model folder of an object type in the *Types blocks of Lego.cfg.
    std::string TypeFolder(const Config& cfg, std::string_view type) {
        for (const char* block : {"MiniFigureTypes", "VehicleTypes", "BuildingTypes", "RockMonsterTypes"}) {
            if (auto v = cfg.GetString(GetGame().Id(block, type))) {
                const auto parts = SplitTokens(*v, ",");
                return std::string(parts.back());
            }
        }
        return {};
    }

    void LoadObjects(const Config& cfg) {
        const std::string shared = cfg.GetString(GetGame().Id("Main", "SharedObjects"), "");
        GetMeshTextures().SetSharedDir(cfg.GetString(GetGame().Id("Main", "SharedTextures"), ""));
        Config ol;
        if (!ol.Load(cfg.GetString(Config::Id(GetGame().gameName, levelId_, "OListFile"), ""))) return;
        const world::Map3D& map = level_.map();
        const Vec2 origin = map.BlockCentre(0, 0);
        for (Config::ItemRef it = ol.FindArray("Lego*"); it != Config::kNone; it = ol.Next(it)) {
            const std::string obj = "Lego*::" + ol.item(it).key;
            PlacedObject po;
            po.type = ol.GetString(Config::Id(obj, "type"), "");
            const std::string folder = TypeFolder(cfg, po.type);
            if (folder.empty()) continue;
            po.model = LoadActivityModel(folder, shared);
            if (!po.model) continue;
            po.activity = po.model->Find("Activity_Stand");
            if (!po.activity && !po.model->activities.empty()) po.activity = po.model->activities.begin()->second.get();
            if (!po.activity) continue;
            const float bx = ol.GetFloat(Config::Id(obj, "xPos")).value_or(0) - 1.0f;
            const float by = ol.GetFloat(Config::Id(obj, "yPos")).value_or(0) - 1.0f;
            po.position.x = origin.x - map.blockSize() * 0.5f + bx * map.blockSize();
            po.position.y = origin.y + map.blockSize() * 0.5f - by * map.blockSize();
            po.position.z = map.WorldZ(po.position.x, po.position.y);
            po.heading = ol.GetFloat(Config::Id(obj, "heading")).value_or(0) * (kPi / 180.0f);
            objects_.push_back(std::move(po));
        }
        RR_INFO("Placed %zu objects", objects_.size());
    }

    void LoadCamera(const Config& cfg) {
        // The level's object list holds a TVCamera entry with the start view (1-based block coords).
        Config ol;
        const std::string olFile = cfg.GetString(Config::Id(GetGame().gameName, levelId_, "OListFile"), "");
        if (!ol.Load(olFile)) return;
        for (Config::ItemRef it = ol.FindArray("Lego*"); it != Config::kNone; it = ol.Next(it)) {
            const std::string obj = "Lego*::" + ol.item(it).key;
            if (!EqualsNoCase(ol.GetString(Config::Id(obj, "type"), ""), "TVCamera")) continue;
            const float bx = ol.GetFloat(Config::Id(obj, "xPos")).value_or(0) - 1.0f;
            const float by = ol.GetFloat(Config::Id(obj, "yPos")).value_or(0) - 1.0f;
            const float heading = ol.GetFloat(Config::Id(obj, "heading")).value_or(0);
            const world::Map3D& map = level_.map();
            const Vec2 origin = map.BlockCentre(0, 0);
            camera_.x = origin.x - map.blockSize() * 0.5f + bx * map.blockSize();
            camera_.y = origin.y + map.blockSize() * 0.5f - by * map.blockSize();
            camera_.rotation = heading * (1.0f / 180.0f) * kPi;
            camera_.tilt = 0.8f;
            return;
        }
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
        for (PlacedObject& o : objects_) o.time += elapsed;
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
        const float fovY = 2.0f * std::atan(0.5f / aspect);
        const Mat4 proj = Mat4::Perspective(fovY, aspect, 1.0f, clipDist_);
        renderer_.BeginFrame(vx, vy, vw, vh, LookDirLH(view.eye, view.forward, view.up), proj, view.eye);

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
        LwsPose pose;
        for (const PlacedObject& o : objects_) {
            EvaluateLws(*o.activity->scene, o.time, o.activity->looping, pose);
            static const bool hideObjects = std::getenv("RR_DEBUG_NOOBJ") != nullptr;  // pixel-diff checks
            if (hideObjects) continue;
            DrawPose(renderer_, *o.activity->scene, pose, ObjectMatrix(o.position, o.heading, o.model->scale));
        }
        renderer_.EndFrame();

        r2d.SetAnchor(Anchor::Center);
        if (const Font* f = GetGame().textFont.get()) f->Draw(r2d, 8, 8, ls.fullName);
        GetPointers().Draw(r2d, GetApp().input().mouseX(), GetApp().input().mouseY());
    }

private:
    std::string levelId_;
    world::Level level_;
    world::TextureSet textures_;
    world::TopDownCamera camera_;
    Renderer3D renderer_;
    std::vector<Vertex3D> tris_;
    std::vector<PlacedObject> objects_;
    Vec3f ambient_, topSpot_;
    float clipDist_ = 800.0f;
    bool failed_ = false;
};

}  // namespace

std::unique_ptr<Scene> CreateLevelScene(const std::string& levelId) { return std::make_unique<LevelScene>(levelId); }

}  // namespace rr
