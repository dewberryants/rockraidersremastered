#include "game/LevelScene.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "config/Config.h"
#include "core/Log.h"
#include "fs/Path.h"
#include "game/Game.h"
#include "game/Construction.h"
#include "game/Messages.h"
#include "game/PowerGrid.h"
#include "game/Sfx.h"
#include "core/Random.h"
#include "game/ai/Tasks.h"
#include "game/nerps/Nerps.h"
#include "game/nerps/NerpsFunctions.h"
#include "game/ui/Objective.h"
#include "game/ui/Radar.h"
#include "game/objects/ObjectRegistry.h"
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

        game.rockFalls.SetStyle(cfg.GetString(Config::Id(game.gameName, levelId_, "RockFallStyle"), ""));
        cheats_ = GetApp().options().cheats;
        objects_.instantDrill = cheats_;
        infiniteStock_ = false;  // F7 turns it on
        LoadObjects(cfg);
        level_.TakeExposedBlocks();  // blocks opened while loading the level are not news
        level_.TakeCavernsDiscovered();
        messages_.LoadTranslation(cfg.GetString(Config::Id(game.gameName, levelId_, "PTLFile"), ""));
        tasks_.Reset();
        tasks_.LoadPriorities(cfg, Config::Id(game.gameName, levelId_));
        objects_.onActionDone = [this](GameObject& u, bool ok) { OnActionDone(u, ok); };
        objects_.onFetch = [this](GameObject& u) { return FetchFromStore(u); };
        objects_.onPlace = [this](GameObject& u, GameObject& item) { PlaceOnSite(u, item); };
        objects_.onBuildPath = [this](GameObject&, BlockPos b) { FinishPath(b); };
        objects_.onExplode = [this](GameObject& d) { ExplodeDynamite(d); };
        objects_.onBoard = [this](GameObject& u, GameObject& v) { OnBoard(u, v); };
        tasks_.hasStock = [this](ObjType t) { return t == ObjType::PowerCrystal ? crystals_ > 0 : ore_ > 0; };
        tasks_.zoneBlock = [this](int zone, BlockPos& b) {
            for (const auto& z : zones_)
                if (z->handle == zone) return SiteApproach(*z, b);
            return false;
        };
        objects_.onDeposit = [this](GameObject&, GameObject& item, GameObject&) {
            // TODO(resources): refinery and building-specific storage, info messages.
            if (item.type == ObjType::PowerCrystal) {
                ++crystals_;
                ++crystalsPickedUp_;
            } else if (item.type == ObjType::Ore) {
                ++ore_;
                ++orePickedUp_;
            }
            RR_INFO("Stored %s: crystals %d, ore %d", item.info->name.c_str(), crystals_, ore_);
        };
        // Lego_LoadOLObjectList: loose crystals, ore, dynamite and barriers are to be collected.
        for (const auto& o : objects_.objects())
            if (o->type == ObjType::PowerCrystal || o->type == ObjType::Ore || o->type == ObjType::Dynamite ||
                o->type == ObjType::Barrier)
                tasks_.CreateCollect(*o);
        // Debug set-up for scripted tests: RR_DEBUG_PATH="x,y;..." lays power paths,
        // RR_DEBUG_STOCK="crystals,ore" fills storage, RR_DEBUG_BUILDLEVEL=n upgrades buildings.
        if (const char* paths = std::getenv("RR_DEBUG_PATH")) {
            for (std::string_view entry : SplitTokens(paths, ";")) {
                const auto xy = SplitTokens(entry, ",");
                if (xy.size() != 2) continue;
                const int x = std::atoi(std::string(xy[0]).c_str()), y = std::atoi(std::string(xy[1]).c_str());
                if (!level_.InBounds(x, y)) continue;
                level_.block(x, y).flags |= world::BlockFlag::Path;
                level_.UpdateSurface(x, y);
            }
        }
        if (const char* stock = std::getenv("RR_DEBUG_STOCK")) {
            const auto v = SplitTokens(stock, ",");
            crystals_ = std::atoi(std::string(v[0]).c_str());
            if (v.size() > 1) ore_ = std::atoi(std::string(v[1]).c_str());
        }
        if (const char* lvl = std::getenv("RR_DEBUG_BUILDLEVEL"))
            for (const auto& o : objects_.objects())
                if (o->type == ObjType::Building) o->level = std::atoi(lvl);
        // RR_DEBUG_SPAWN="Type,x,y;..." places objects (vehicles...) on blocks.
        if (const char* sp = std::getenv("RR_DEBUG_SPAWN")) {
            for (std::string_view entry : SplitTokens(sp, ";")) {
                const auto f = SplitTokens(entry, ",");
                if (f.size() != 3) continue;
                const ObjectTypeInfo* info = GetObjectRegistry().Find(f[0]);
                const Vec2 c = level_.map().BlockCentre(std::atoi(std::string(f[1]).c_str()),
                                                        std::atoi(std::string(f[2]).c_str()));
                if (info) objects_.Spawn(*info, c.x, c.y);
            }
        }
        // RR_DEBUG_LAYPATH="x,y;..." lays power paths to be built.
        if (const char* laid = std::getenv("RR_DEBUG_LAYPATH")) {
            for (std::string_view entry : SplitTokens(laid, ";")) {
                const auto xy = SplitTokens(entry, ",");
                if (xy.size() == 2) StartPath({std::atoi(std::string(xy[0]).c_str()), std::atoi(std::string(xy[1]).c_str())});
            }
        }
        // RR_DEBUG_BUILD="Type,x,y,dir" starts a construction site without the placement checks.
        if (const char* build = std::getenv("RR_DEBUG_BUILD")) {
            const auto f = SplitTokens(build, ",");
            const ObjectTypeInfo* t = f.size() == 4 ? GetObjectRegistry().Find(f[0]) : nullptr;
            if (t && t->type == ObjType::Building)
                StartConstruction(*t, {std::atoi(std::string(f[1]).c_str()), std::atoi(std::string(f[2]).c_str())},
                                  std::atoi(std::string(f[3]).c_str()) & 3);
        }
        // Debug: RR_DEBUG_DIG="x,y;x,y;..." queues dig requests at the start (for scripted tests).
        if (const char* dig = std::getenv("RR_DEBUG_DIG")) {
            for (std::string_view entry : SplitTokens(dig, ";")) {
                const auto xy = SplitTokens(entry, ",");
                if (xy.size() != 2) continue;
                debugDigs_.push_back({std::atoi(std::string(xy[0]).c_str()), std::atoi(std::string(xy[1]).c_str())});
            }
        }
        LoadScript(cfg);
        objective_.Load(cfg, game.gameName, levelId_, game.briefFont.get());
        objective_.Show(ObjectiveWindow::State::Briefing);
        game.panels->Reset();
        crystalQuota_ = cfg.GetInt(Config::Id(game.gameName, levelId_, "Reward", "Quota", "Crystals")).value_or(0);
        game.panels->onButton = [this](PanelButton b) { OnPanelButton(b); };
        IconPanel& panel = *game.iconPanel;
        panel.BackToMain();
        panel.isEnabled = [this](MenuItem item) { return ItemEnabled(item); };
        panel.onBackToMain = [this] { messages_.Post(MessageType::ClearSelection); };
        panel.onPanelMenu = [](MenuType m, bool open) {
            if (m == MenuType::Priorities) GetGame().panels->SetOpen(PanelType::PriorityList, open);
            // TODO(interface): the encyclopedia panel.
        };
        panel.onItem = [this](MenuItem item) { OnMenuItem(item); };
        game.dependencies.ResetLevel();
        panel.isBuildEnabled = [this](const ObjectTypeInfo& t) { return GetGame().dependencies.Met(t, objects_); };
        panel.onBuild = [this](const ObjectTypeInfo& t) { BeginPlacement(t); };
        auto rgb = [&](std::string_view key, Vec3f fallback) {
            float r, g, b;
            return cfg.GetRGB(game.Id("Main", key), r, g, b) ? Vec3f{r, g, b} : fallback;
        };
        ambient_ = rgb("AmbientRGB", {0.04f, 0.04f, 0.04f});
        dynamiteDamageRadius_ = cfg.GetFloat(game.Id("Main", "DynamiteDamageRadius")).value_or(75.0f);
        dynamiteMaxDamage_ = cfg.GetFloat(game.Id("Main", "DynamiteMaxDamage")).value_or(150.0f);
        objects_.crystalColour = rgb("PowerCrystalRGB", {0.0f, 1.0f, 0.0f});
        objects_.drainedCrystalColour = rgb("UnpoweredCrystalRGB", {1.0f, 0.0f, 1.0f});
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

    // Direct3D Retained Mode default field of view: tan(half horizontal angle) = 0.5.
    Mat4 ViewProjection(const world::TopDownCamera::View& view, float aspect) const {
        const float fovY = 2.0f * std::atan(0.5f / aspect);
        return Mat4::Perspective(fovY, aspect, 1.0f, clipDist_) * LookDirLH(view.eye, view.forward, view.up);
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

    // Map3D_GetIntersections: the block under a screen position. A rough hit first (the ray
    // against a level plane, re-levelled 20 times to the terrain height under the last hit), then
    // the first of the 5x5 blocks around it, row by row, whose corners projected onto the screen
    // enclose the position.
    bool PickBlock(float lx, float ly, BlockPos& out) const {
        Vec3f origin, dir;
        MouseRay(lx, ly, origin, dir);
        const world::Map3D& map = level_.map();
        if (std::fabs(dir.z) < 1e-6f) return false;
        Vec3f p = origin;
        float planeZ = -50.0f;
        for (int i = 0; i < 20; ++i) {
            p = origin + dir * ((planeZ - origin.z) / dir.z);
            planeZ = map.WorldZ(p.x, p.y);
        }
        BlockPos rough;
        if (!map.WorldToBlock(p.x, p.y, rough.x, rough.y)) return false;

        const Mat4 vp = ViewProjection(camera_.Compute(&map), viewAspect_);
        auto project = [&](const Vec3f& v, float& sx, float& sy) {
            const float* m = vp.m;
            const float cx = m[0] * v.x + m[4] * v.y + m[8] * v.z + m[12];
            const float cy = m[1] * v.x + m[5] * v.y + m[9] * v.z + m[13];
            const float cw = m[3] * v.x + m[7] * v.y + m[11] * v.z + m[15];
            if (cw <= 1e-4f) return false;
            sx = (cx / cw + 1.0f) * 0.5f * uiWidth_;
            sy = (1.0f - cy / cw) * 0.5f * 480.0f;
            return true;
        };
        for (int dy = -2; dy <= 2; ++dy) {
            for (int dx = -2; dx <= 2; ++dx) {
                const int bx = rough.x + dx, by = rough.y + dy;
                if (!map.InBlocks(bx, by)) continue;
                const world::Map3D::Quad& q = map.quad(bx, by);
                float px[4], py[4];
                bool front = true;
                for (int i = 0; i < 4 && front; ++i) front = project(q.pos[i], px[i], py[i]);
                if (!front) continue;
                // Maths_PointInsidePoly: even-odd crossings.
                bool inside = false;
                for (int i = 0, j = 3; i < 4; j = i++) {
                    if ((py[i] > ly) != (py[j] > ly) &&
                        lx < (px[j] - px[i]) * (ly - py[i]) / (py[j] - py[i]) + px[i])
                        inside = !inside;
                }
                if (inside) {
                    out = {bx, by};
                    return true;
                }
            }
        }
        return false;
    }

    // World clicks (the part of Lego_HandleWorld implemented so far): a unit under the cursor is
    // selected; with units selected, clicking open floor sends them there.
    void HandleWorldClick(Input& in) {
        IconPanel& panel = *GetGame().iconPanel;
        // The priority list's images, then the HUD panels and the icon menu take clicks over them
        // (Priorities_HandleInput, Panel_CheckCollision).
        Panels& panels = *GetGame().panels;
        if (panels.IsFullyOpen(PanelType::PriorityList)) {
            const Vec2 p = panels.Position(PanelType::PriorityList);
            if (GetGame().priorities->HandleInput(in, p.x + panels.AnchorX(PanelType::PriorityList), p.y, tasks_))
                return;
        }
        if (panels.HandleInput(in)) return;
        if (panel.HandleInput(in)) return;
        if (placing_) {
            UpdatePlacement(in);
            return;
        }
        if (!in.leftReleased() && !in.rightReleased()) return;
        if (in.rightReleased()) {
            // TODO(interface): right-click handling of the original (menus, cancel).
            panel.BackToMain();
            return;
        }
        Vec3f origin, dir;
        MouseRay(in.mouseX(), in.mouseY(), origin, dir);
        // Objects that can't be selected (crystals, ore...) don't take the click from the block behind.
        if (GameObject* o = objects_.PickObject(origin, dir, &Selection::IsSelectable)) {
            // A driver stands for its vehicle.
            if (o->driving) o = o->driving;
            // Lego_HandleWorld: a selected Rock Raider with the skill is sent to get in.
            const GameObject* primary = selection_.Primary();
            if (o->type == ObjType::Vehicle && !o->driver && primary && primary->type == ObjType::MiniFigure &&
                (primary->abilities & ObjectList::DriverAbility(*o))) {
                GameObject& unit = *selection_.Primary();
                // TODO(vehicles): every selected unit may answer (AITask_UnkInitRouting_FUN_00402530),
                // and carried loads are dropped first.
                if (Task* t = tasks_.CreateFindDriver(*o); t && !unit.carried) tasks_.AssignTo(*t, unit, objects_, level_);
                panel.BackToMain();
                return;
            }
            // The unit's menu opens when the selection is made (after the clear).
            messages_.Post(MessageType::ClearSelection);
            messages_.Post(MessageType::Select, o);
            return;
        }
        BlockPos b;
        if (!PickBlock(in.mouseX(), in.mouseY(), b)) return;
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

    // Lego_PTL_RockFall: the collapse scene over a block (or a block corner point), facing the
    // wall's direction. A wall open on two or more sides comes down as an outside corner.
    void SpawnRockFall(BlockPos b, int direction, bool atPoint) {
        const world::Map3D& map = level_.map();
        static const int kNX[4] = {1, 0, -1, 0}, kNY[4] = {0, 1, 0, -1};
        int open = 0;
        for (int i = 0; i < 4; ++i)
            if (level_.InBounds(b.x + kNX[i], b.y + kNY[i]) &&
                (level_.block(b.x + kNX[i], b.y + kNY[i]).flags & world::BlockFlag::Ground))
                ++open;
        Vec2 c = map.BlockCentre(b.x, b.y);
        if (atPoint) {
            const Vec3f p = map.PointWorld(b.x, b.y);
            c = {p.x, p.y};
        }
        const Vec3f pos{c.x, c.y, map.WorldZ(c.x, c.y)};
        int sfx = 0;
        if (GetSfx().GetType("SFX_RockBreak", sfx)) GetSfx().PlayAt(sfx, Vec3{pos.x, pos.y, pos.z});
        static const Vec2 kDir[4] = {{0, 1}, {1, 0}, {0, -1}, {-1, 0}};
        const auto type = open > 1 ? RockFallEffects::OutsideCorner : RockFallEffects::ThreeSides;
        // As in the original, a fall that finds no free instance never completes (the block's
        // crystals and ore are not released).
        const bool spawned = GetGame().rockFalls.Spawn(type, b, pos, kDir[direction & 3]);
        if (spawned) level_.block(b.x, b.y).flags |= world::BlockFlag::RockFallFX;
        RR_INFO("Rock fall at %d,%d: %s, direction %d, %d open sides%s", b.x, b.y,
                type == RockFallEffects::OutsideCorner ? "outside corner" : "three sides", direction & 3, open,
                spawned ? "" : " (no free instance)");
    }

    // Level_GenerateCrystal / Level_GenerateOre: a resource at a random spot inside the block,
    // inset a quarter block from its edges.
    void GenerateResource(BlockPos b, ObjType type, bool count) {
        const ObjectTypeInfo* info = GetObjectRegistry().Get(type, 0);
        if (!info) return;
        const world::Map3D& map = level_.map();
        const float inset = map.blockSize() * 0.5f - map.blockSize() * 0.25f;
        const Vec3f p0 = map.PointWorld(b.x, b.y), p2 = map.PointWorld(b.x + 1, b.y + 1);
        const float x = random::RandRange(p0.x + inset, p2.x - inset);
        const float y = random::RandRange(p0.y - inset, p2.y + inset);
        GameObject* o = objects_.Spawn(*info, x, y);
        // TODO(interface): info message and "crystal found" text.
        messages_.Post(type == ObjType::PowerCrystal ? MessageType::GenerateCrystalComplete : MessageType::GenerateOreComplete, o);
        if (count) {
            world::Block& blk = level_.block(b.x, b.y);
            if (type == ObjType::PowerCrystal) ++blk.crystals;
            else ++blk.ore;
        }
    }

    // LegoObject_PTL_GenerateFromCryOre: the CryOre value v picks the kind ((v-1) & 3: crystals,
    // ore, then their second variants) and the amount ((v-1) / 4 into 1, 3, 5, 10, 25).
    void GenerateFromCryOre(BlockPos b) {
        const int v = level_.block(b.x, b.y).cryOre;
        RR_DEBUG("CryOre at %d,%d: %d", b.x, b.y, v);
        if (v == 0) return;
        static const int kAmounts[] = {1, 3, 5, 10, 25};
        const int amount = kAmounts[std::min((v - 1) / 4, 4)];
        // TODO(objects): the second crystal/ore variants (kinds 2 and 3) as distinct objects.
        const ObjType type = ((v - 1) & 1) ? ObjType::Ore : ObjType::PowerCrystal;
        for (int i = 0; i < amount; ++i) GenerateResource(b, type, true);
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
        case MenuItem::Dynamite: {
            // A blastable wall, a tool store to hand out dynamite and someone trained to use it.
            if (!level_.CanDynamite(b.x, b.y) || tasks_.HasBlockTask(b, TaskType::Dynamite) || !NearestToolStore(b))
                return false;
            if (!selection_.empty()) {
                for (GameObject* u : selection_.units())
                    if (u->abilities & Ability::Dynamite) return true;
                return false;
            }
            for (const auto& o : objects_.objects())
                if (!o->removed && o->type == ObjType::MiniFigure && (o->abilities & Ability::Dynamite)) return true;
            return false;
        }
        case MenuItem::Reinforce: {
            // Interface_GetItemStatus (Reinforce): a straight, unreinforced wall not beside a
            // building, and someone able to do it (with a hammer, or a tool store to get one).
            using namespace world;
            if (blk.flags & (BlockFlag::Reinforced | BlockFlag::OuterCorner | BlockFlag::InnerCorner)) return false;
            if (!level_.CanReinforce(b.x, b.y)) return false;
            static const int kNX[4] = {1, 0, -1, 0}, kNY[4] = {0, 1, 0, -1};
            for (int i = 0; i < 4; ++i)
                if (level_.InBounds(b.x + kNX[i], b.y + kNY[i]) &&
                    (level_.block(b.x + kNX[i], b.y + kNY[i]).flags & BlockFlag::Building))
                    return false;
            bool toolStore = false;
            for (const auto& o : objects_.objects())
                if (o->type == ObjType::Building && !o->removed && !o->hidden && o->stats &&
                    (o->stats->F1(o->level) & StatsFlag1::ToolStore))
                    toolStore = true;
            if (!selection_.empty()) {
                for (GameObject* u : selection_.units())
                    if (u->HasTool(ToolType::Hammer)) return true;
                return toolStore;
            }
            for (const auto& o : objects_.objects())
                if (!o->removed && !o->hidden && (o->flags3 & ObjFlag3::CanReinforce)) return true;
            return false;
        }
        case MenuItem::DeselectDig: return (blk.flags & world::BlockFlag::DigRequested) != 0;
        case MenuItem::BuildBuilding:
        case MenuItem::BuildSmallVehicle:
        case MenuItem::BuildLargeVehicle: return true;
        case MenuItem::GetTool: return selection_.Primary() && selection_.Primary()->type == ObjType::MiniFigure;
        case MenuItem::TrainSkill: {
            // Some building trains something the unit has not learnt yet.
            const GameObject* u = selection_.Primary();
            if (!u || u->type != ObjType::MiniFigure || (u->abilities & Ability::All) == Ability::All) return false;
            for (uint32_t a = 1; a <= Ability::Scanner; a <<= 1)
                if (NearestTrainer(*u, a)) return true;
            return false;
        }
        case MenuItem::TrainDriver:
        case MenuItem::TrainEngineer:
        case MenuItem::TrainGeologist:
        case MenuItem::TrainPilot:
        case MenuItem::TrainSailor:
        case MenuItem::TrainDynamite: {
            const GameObject* u = selection_.Primary();
            const uint32_t a = AbilityOfItem(item);
            if (!u || u->type != ObjType::MiniFigure || (u->abilities & a) || !NearestTrainer(*u, a)) return false;
            return u->trainTimer >= level_.settings().trainTime;
        }
        case MenuItem::GetDrill:
        case MenuItem::GetSpade:
        case MenuItem::GetHammer:
        case MenuItem::GetSpanner: {
            const GameObject* u = selection_.Primary();
            return u && u->type == ObjType::MiniFigure && !u->HasTool(ToolOfItem(item));
        }
        case MenuItem::LayPath: {
            // Clean free floor without a path, laid path or site already.
            // TODO(interface): Interface item checks (dependencies for Path, rubble rules).
            using namespace world;
            if (!(blk.flags & BlockFlag::Ground) || !(blk.flags & BlockFlag::CleanFloor)) return false;
            if (blk.flags & (BlockFlag::Path | BlockFlag::LaidPath | BlockFlag::Foundation | BlockFlag::Building))
                return false;
            return blk.terrain != Terrain::Lava && blk.terrain != Terrain::Lake;
        }
        case MenuItem::GetIn: {
            // Interface_HandleMenuItem (GetIn): nobody on the way yet, and a unit trained for it.
            // TODO(vehicles): boats only from a dock, or from land beside them (GetInAtLand).
            const GameObject* v = selection_.Primary();
            if (!v || v->type != ObjType::Vehicle || v->driver || tasks_.HasFindDriver(*v)) return false;
            const uint32_t a = ObjectList::DriverAbility(*v);
            if (a == Ability::Sailor) return false;
            for (const auto& o : objects_.objects())
                if (!o->removed && o->type == ObjType::MiniFigure && (o->abilities & a)) return true;
            return false;
        }
        case MenuItem::GetOut: {
            // Not while moving; boats need land beside them (TODO(water)).
            const GameObject* v = selection_.Primary();
            return v && v->driver && !v->routing && v->stats &&
                   (v->stats->F1(v->level) & StatsFlag1::CanBeDriven) && (v->stats->F1(v->level) & StatsFlag1::CrossLand);
        }
        case MenuItem::TeleportMan: {
            // A building that can beam Rock Raiders in, and room in the queue.
            if (teleportQueue_.size() >= kMaxTeleportQueue) return false;
            for (const auto& o : objects_.objects())
                if (o->type == ObjType::Building && !o->removed && !o->hidden && o->stats &&
                    (o->stats->F1(o->level) & StatsFlag1::ManTeleporter))
                    return true;
            return false;
        }
        default: return false;  // TODO(interface): the remaining items as their systems arrive
        }
    }

    void OnMenuItem(MenuItem item) {
        IconPanel& panel = *GetGame().iconPanel;
        const BlockPos b = panel.block();
        RR_INFO("Menu item %s at block %d,%d", MenuItemName(item), b.x, b.y);
        Task* t = nullptr;
        switch (item) {
        case MenuItem::Dig:
            t = tasks_.CreateDig(level_, b, true);
            if (t) level_.block(b.x, b.y).flags |= world::BlockFlag::DigRequested;
            break;
        case MenuItem::ClearRubble: t = tasks_.CreateClear(b); break;
        case MenuItem::Reinforce: t = tasks_.CreateReinforce(level_, b); break;
        case MenuItem::Dynamite:
            // AITask_PlaceRequestObject: the nearest tool store hands out a stick of dynamite.
            if (GameObject* store = NearestToolStore(b)) {
                const ObjectTypeInfo* info = GetObjectRegistry().Get(ObjType::Dynamite, 0);
                Vec3f at = store->position;
                Vec2 dir;
                if (store->info && store->info->model.activities)
                    objects_.NullPoint(*store, store->info->model.activities->toolNull, at, dir, 4);
                if (GameObject* d = info ? objects_.Spawn(*info, at.x, at.y) : nullptr) {
                    Task* dt = tasks_.CreateDynamite(b, *d);
                    GameObject* u = selection_.Primary();
                    if (u && (u->abilities & Ability::Dynamite)) tasks_.AssignTo(*dt, *u, objects_, level_);
                }
            }
            break;
        case MenuItem::LayPath: StartPath(b); break;
        case MenuItem::GetTool: panel.OpenMenu(MenuType::GetTool); return;
        case MenuItem::TrainSkill: panel.OpenMenu(MenuType::TrainSkill); return;
        case MenuItem::TrainDriver:
        case MenuItem::TrainEngineer:
        case MenuItem::TrainGeologist:
        case MenuItem::TrainPilot:
        case MenuItem::TrainSailor:
        case MenuItem::TrainDynamite:
            // Interface_DoAction: the nearest building that trains the skill.
            if (GameObject* u = selection_.Primary())
                if (GameObject* trainer = NearestTrainer(*u, AbilityOfItem(item)))
                    t = tasks_.CreateTrain(*trainer, AbilityOfItem(item), u);
            break;
        case MenuItem::GetDrill:
        case MenuItem::GetSpade:
        case MenuItem::GetHammer:
        case MenuItem::GetSpanner:
            if (GameObject* u = selection_.Primary()) {
                Task* gt = tasks_.CreateGetTool(ToolOfItem(item), u);
                tasks_.AssignTo(*gt, *u, objects_, level_);
            }
            break;
        // Interface_DoAction: the build icons open their lists.
        case MenuItem::BuildBuilding: panel.OpenMenu(MenuType::BuildBuilding); return;
        case MenuItem::BuildSmallVehicle: panel.OpenMenu(MenuType::BuildSmallVehicle); return;
        case MenuItem::BuildLargeVehicle: panel.OpenMenu(MenuType::BuildLargeVehicle); return;
        case MenuItem::TeleportMan:
            // Lego_TryTeleportObject: Rock Raiders wait for a teleporter (LegoObject_QueueTeleport).
            if (const ObjectTypeInfo* pilot = GetObjectRegistry().Get(ObjType::MiniFigure, 0))
                if (teleportQueue_.size() < kMaxTeleportQueue) teleportQueue_.push_back(pilot);
            break;
        case MenuItem::GetIn:
            // Interface_DoAction: the selection is dropped and any trained unit answers the call.
            if (GameObject* v = selection_.Primary()) tasks_.CreateFindDriver(*v);
            break;
        case MenuItem::GetOut:
            if (GameObject* v = selection_.Primary())
                if (objects_.GetOut(*v)) RR_DEBUG("Driver out of %s", v->info->name.c_str());
            break;
        case MenuItem::DeselectDig:
            // The request goes, and whoever was on their way or drilling stops.
            level_.block(b.x, b.y).flags &= ~world::BlockFlag::DigRequested;
            for (GameObject* u : tasks_.CancelBlockTasks(b, TaskType::Dig)) {
                if (u->routing) objects_.RouteEnd(*u, false);
                if (u->state != UnitState::Idle) objects_.FinishAction(*u, false);
            }
            break;
        default: break;
        }
        // With units selected the order goes to the primary unit straight away.
        if (t && selection_.Primary()) tasks_.AssignTo(*t, *selection_.Primary(), objects_, level_);
        panel.BackToMain();
    }

    void OnActionDone(GameObject& unit, bool success) {
        const Task* t = tasks_.TaskOf(unit);
        if (success && t) {
            world::Block& b = level_.block(t->block.x, t->block.y);
            if (t->type == TaskType::Dig && (b.flags & world::BlockFlag::Ground)) {
                WallBroken(t->block);
            } else if (t->type == TaskType::Dynamite && unit.carried && unit.carried == t->target) {
                // Collected the dynamite: the same task continues to the wall.
                if (tasks_.AssignTo(*const_cast<Task*>(t), unit, objects_, level_)) return;
            } else if (t->type == TaskType::Clear && (b.flags & world::BlockFlag::CleanFloor)) {
                messages_.Post(MessageType::ClearComplete, nullptr, 0, &t->block);
            } else if (t->type == TaskType::Train) {
                // LegoObject_TrainMiniFigure. TODO(interface): Info_Send / Text_DisplayMessage.
                unit.abilities |= t->ability;
                unit.trainTimer = 0.0f;
                RR_INFO("%s trained (abilities %02x)", unit.info->name.c_str(), unit.abilities);
            }
        }
        tasks_.TaskFinished(unit, success);
    }

    // Level_DestroyWall follow-up: the floor is left full of rubble with clearing queued, and
    // DigComplete starts the level's event chain (rock fall, crystals, ore).
    void WallBroken(BlockPos p) {
        world::Block& b = level_.block(p.x, p.y);
        b.flags &= ~world::BlockFlag::DigRequested;
        b.flags |= world::BlockFlag::RockFallFX;
        messages_.Post(MessageType::DigComplete, nullptr, b.brokenDirection, &p);
        // TODO(ai): the game queues one clear request per rubble layer (four).
        tasks_.CreateClear(p);
    }

    // LegoObject_SimpleObject_MoveAnimation (dynamite): the wall goes, everything near takes damage
    // falling off with distance, then the explosion.
    void ExplodeDynamite(GameObject& d) {
        const BlockPos w = d.dynamiteTarget;
        if (level_.InBounds(w.x, w.y) && !(level_.block(w.x, w.y).flags & world::BlockFlag::Ground)) {
            // Seams come down in one go.
            bool broken = false;
            for (int i = 0; i < 5 && !broken; ++i) broken = level_.DestroyWall(w.x, w.y, false);
            if (broken) {
                for (GameObject* u : tasks_.CancelBlockTasks(w, TaskType::Dig)) objects_.FinishAction(*u, false);
                WallBroken(w);
            }
        }
        // LegoObject_Callback_DynamiteExplosion. TODO(objects): units die at zero health.
        for (const auto& op : objects_.objects()) {
            GameObject& o = *op;
            if (&o == &d || o.removed || !(o.flags3 & 0x40000)) continue;
            const float dx = o.position.x - d.position.x, dy = o.position.y - d.position.y;
            const float dist = std::sqrt(dx * dx + dy * dy);
            if (dist < dynamiteDamageRadius_) o.health -= (1.0f - dist / dynamiteDamageRadius_) * dynamiteMaxDamage_;
        }
        int sfx = 0;
        if (GetSfx().GetType("SFX_Dynamite", sfx)) GetSfx().PlayNormal(sfx);
        // Effect_Spawn_Explosion. TODO(camera): Camera_Shake(5, 50).
        GetGame().explosions.Spawn({d.position.x, d.position.y, level_.map().WorldZ(d.position.x, d.position.y) - 1.0f});
        objects_.Remove(d);
    }

    // LegoObject_Callback_ScareTrainedMiniFiguresAwayFromTickingDynamite: explosives experts
    // within the damage radius of lit dynamite run away from it.
    void ScareFromDynamite() {
        const float bs = level_.map().blockSize();
        for (const auto& dp : objects_.objects()) {
            const GameObject& d = *dp;
            if (d.removed || d.type != ObjType::Dynamite || !d.ticking) continue;
            for (const auto& op : objects_.objects()) {
                GameObject& u = *op;
                if (u.removed || u.type != ObjType::MiniFigure || !(u.abilities & Ability::Dynamite) || u.routing ||
                    u.state != UnitState::Idle)
                    continue;
                const float dx = u.position.x - d.position.x, dy = u.position.y - d.position.y;
                const float dist = std::sqrt(dx * dx + dy * dy);
                if (dist >= dynamiteDamageRadius_ || dist <= 0.0f) continue;
                // TODO(ai): LegoObject_TryRunAway picks its escape route and plays RunPanic.
                BlockPos to;
                for (float reach = bs * 3.0f; reach >= bs; reach -= bs) {
                    const float x = u.position.x + dx / dist * reach, y = u.position.y + dy / dist * reach;
                    if (level_.map().WorldToBlock(x, y, to.x, to.y) && objects_.RouteToBlock(u, to)) break;
                }
            }
        }
    }

    void OnPanelButton(PanelButton b) {
        switch (b) {
        case PanelButton::RadarToggle: GetGame().panels->Toggle(PanelType::Radar); break;
        case PanelButton::TopPanelPriorities: {
            IconPanel& icons = *GetGame().iconPanel;
            if (icons.menu() == MenuType::Priorities)
                icons.BackToMain();
            else
                icons.OpenMenu(MenuType::Priorities);
            break;
        }
        case PanelButton::PriorityListClose: GetGame().iconPanel->BackToMain(); break;
        case PanelButton::PriorityListReset: tasks_.ResetPriorities(); break;
        case PanelButton::PriorityListDisable1:
        case PanelButton::PriorityListDisable2:
        case PanelButton::PriorityListDisable3:
        case PanelButton::PriorityListDisable4:
        case PanelButton::PriorityListDisable5:
        case PanelButton::PriorityListDisable6:
        case PanelButton::PriorityListDisable7:
        case PanelButton::PriorityListDisable8:
        case PanelButton::PriorityListDisable9:
            tasks_.TogglePriority(size_t(int(b) - int(PanelButton::PriorityListDisable1)));
            break;
        case PanelButton::PriorityListUpOne1:
        case PanelButton::PriorityListUpOne2:
        case PanelButton::PriorityListUpOne3:
        case PanelButton::PriorityListUpOne4:
        case PanelButton::PriorityListUpOne5:
        case PanelButton::PriorityListUpOne6:
        case PanelButton::PriorityListUpOne7:
        case PanelButton::PriorityListUpOne8:
            tasks_.MovePriorityUp(size_t(int(b) - int(PanelButton::PriorityListUpOne1)) + 1);
            break;
        case PanelButton::RadarMapView: GetGame().panels->radarMapView = true; break;
        case PanelButton::RadarTaggedObjectView: GetGame().panels->radarMapView = false; break;
        // TODO(radar): the game steps zoom by the click handler's amount; 1 per click here.
        case PanelButton::RadarZoomIn: radar_.ZoomIn(1.0f); break;
        case PanelButton::RadarZoomOut: radar_.ZoomOut(1.0f); break;
        // TODO(camera): the game's zoom steps for the camera control buttons.
        case PanelButton::CameraControlZoomIn: camera_.zoom = std::max(camera_.minZoom, camera_.zoom - 10.0f); break;
        case PanelButton::CameraControlZoomOut: camera_.zoom = std::min(camera_.maxZoom, camera_.zoom + 10.0f); break;
        default: RR_DEBUG("Panel button %d not implemented", int(b)); break;
        }
    }

    // Level script (NERPs): files from the level block, game functions answered here by name.
    void LoadScript(const Config& cfg) {
        if (std::getenv("RR_DEBUG_NOSCRIPT")) return;  // scripted tests that must not win or lose
        const std::string base = Config::Id(GetGame().gameName, levelId_);
        if (!script_.LoadScript(cfg.GetString(Config::Id(base, "NERPFile"), ""))) return;
        script_.LoadMessages(cfg.GetString(Config::Id(base, "NERPMessageFile"), ""));
        script_.call = [this](int index, const int* args) { return ScriptCall(index, args); };
    }

    int CountObjects(std::string_view typeName, int level = -1) const {
        int n = 0;
        for (const auto& o : objects_.objects())
            if (!o->removed && !o->hidden && o->info && EqualsNoCase(o->info->name, typeName) &&
                (level < 0 || o->level == level))
                ++n;
        return n;
    }

    int ScriptCall(int index, const int* args) {
        const std::string_view name = nerps::kFunctions[index].name;
        // Buildings counted by type name (NERPsRuntime_GetLevelObjectsBuilt).
        static const std::pair<const char*, const char*> kBuilt[] = {
            {"Barracks", "Barracks"}, {"OreRefineries", "OreRefinery"}, {"ToolStores", "Toolstation"},
            {"Teleports", "TeleportPad"}, {"Powerstations", "Powerstation"}, {"Docks", "Docks"},
            {"Geodome", "Geo-dome"}, {"Gunstations", "Gunstation"}, {"UpgradeStations", "Upgrade"},
            {"VehicleTeleports", "TeleportBIG"},
        };
        for (const auto& [suffix, type] : kBuilt) {
            const std::string s(suffix);
            if (name == "Get" + s + "Built") return CountObjects(type);
            if (name == "GetLevel1" + s + "Built") return CountObjects(type, 1);
            if (name == "GetLevel2" + s + "Built") return CountObjects(type, 2);
            if (name == "GetPowered" + s + "Built") {
                int n = 0;
                for (const auto& o : objects_.objects())
                    n += !o->removed && !o->hidden && o->powered && o->info && EqualsNoCase(o->info->name, type);
                return n;
            }
        }
        if (name == "SetLevelCompleted" || name == "SetLevelFail") {
            // Debug cheats: only F12 ends the level (cheat stock would meet goals at once).
            if (cheats_) {
                static bool logged = false;
                if (!logged) RR_INFO("Level script: %s ignored (cheats)", std::string(name).c_str());
                logged = true;
                return 0;
            }
            levelStatus_ = name == "SetLevelCompleted" ? 1 : 2;
            RR_INFO("Level script: %s", std::string(name).c_str());
            objective_.Show(levelStatus_ == 1 ? ObjectiveWindow::State::Achieved : ObjectiveWindow::State::Failed);
        }
        else if (name == "GetCrystalsCurrentlyStored") return crystals_;
        else if (name == "GetCrystalsPickedUp") return crystalsPickedUp_;
        else if (name == "GetOreCurrentlyStored") return ore_;
        else if (name == "GetOrePickedUp") return orePickedUp_;
        else if (name == "AddStoredOre") return ore_ += args[0];
        else if (name == "GetMiniFiguresOnLevel") return CountObjects("Pilot");
        else if (name == "GetMonstersOnLevel") {
            int n = 0;
            for (const auto& o : objects_.objects()) n += !o->removed && !o->hidden && o->type == ObjType::RockMonster;
            return n;
        } else if (name == "GetSlugsOnLevel") return CountObjects("Slug");
        else if (name == "GetHiddenObjectsFound") return hiddenObjectsFound_;
        else if (name == "SetHiddenObjectsFound") return hiddenObjectsFound_ = args[0];
        else if (name == "GetObjectiveShowing") return objective_.showing() ? 1 : 0;
        else if (name == "GetMessagesAreUpToDate") return 1;
        else if (name == "SetMessage") {
            // The "permit" flag blocks messages while non-zero (scripts clear it to talk).
            if (script_.messagePermit != 0) return 0;
            if (const std::string* line = script_.MessageLine(args[0])) {
                // "#name#" marks the speech sample that goes with the line; it is not shown.
                // TODO(audio): play the marked sample (Text_SetNERPsMessage).
                message_.clear();
                bool inTag = false;
                for (char c : *line) {
                    if (c == '#') inTag = !inTag;
                    else if (!inTag) message_ += c;
                }
                while (!message_.empty() && message_.back() == ' ') message_.pop_back();
                // TODO(nerps): message timing comes from the speech sample / timer values.
                script_.messageTimer = 3000.0f + 60.0f * float(message_.size());
                RR_INFO("Message %d shown", args[0]);
            }
            return args[0];
        } else {
            static std::vector<std::string> reported;
            if (std::find(reported.begin(), reported.end(), name) == reported.end()) {
                reported.emplace_back(name);
                RR_DEBUG("Script function %s not implemented", std::string(name).c_str());
            }
        }
        return 0;
    }

    // LegoObject_UpdateTeleporter: each free teleport building brings in the first queued unit
    // that uses its kind of teleporter and whose crystal cost is covered.
    void ServiceTeleporters() {
        if (teleportQueue_.empty()) return;
        for (const auto& bp : objects_.objects()) {
            GameObject& b = *bp;
            if (b.type != ObjType::Building || b.removed || b.hidden || !b.stats) continue;
            if (b.teleportService && b.teleportService->teleportingIn) continue;
            const uint32_t bf1 = b.stats->F1(b.level);
            for (size_t i = 0; i < teleportQueue_.size(); ++i) {
                const ObjectTypeInfo& info = *teleportQueue_[i];
                const ObjectStats* us = GetStats().Find(info.name);
                const uint32_t uf2 = us ? us->F2(0) : 0;
                const bool fits = ((uf2 & StatsFlag2::UseSmallTeleporter) && (bf1 & StatsFlag1::SmallTeleporter)) ||
                                  ((uf2 & StatsFlag2::UseBigTeleporter) && (bf1 & StatsFlag1::BigTeleporter)) ||
                                  ((uf2 & StatsFlag2::UseWaterTeleporter) && (bf1 & StatsFlag1::WaterTeleporter)) ||
                                  ((uf2 & StatsFlag2::UseLegoManTeleporter) && (bf1 & StatsFlag1::ManTeleporter));
                const int cost = us ? int(us->Num("CostCrystal", 0)) : 0;
                if (!fits || cost > crystals_) continue;
                // TODO(oxygen): only if the air supply supports another unit.
                // Arrives at the building's deposit point; TODO(models): use the real null, this
                // is the block in front (the building's path tile direction).
                const int dir = int(std::lround(b.heading / (kPi * 0.5f))) & 3;
                static const int kFrontX[4] = {0, 1, 0, -1}, kFrontY[4] = {-1, 0, 1, 0};
                BlockPos origin;
                if (!level_.map().WorldToBlock(b.position.x, b.position.y, origin.x, origin.y)) continue;
                const Vec2 at = level_.map().BlockCentre(origin.x + kFrontX[dir], origin.y + kFrontY[dir]);
                GameObject* u = objects_.Spawn(info, at.x, at.y, b.heading);
                crystals_ -= cost;
                u->teleportingIn = true;
                u->SetActivity(ActivityType::TeleportIn, 1);
                u->UpdateActivityChange();
                b.teleportService = u;
                teleportQueue_.erase(teleportQueue_.begin() + std::ptrdiff_t(i));
                RR_INFO("Teleported in %s at %s", info.name.c_str(), b.info->name.c_str());
                break;
            }
            if (teleportQueue_.empty()) return;
        }
    }

    // Building placement (SelectPlace): the footprint follows the block under the cursor, facing
    // the camera (Lego_TryTeleportObject picks the direction from the camera's orientation).
    void BeginPlacement(const ObjectTypeInfo& type) {
        if (type.type == ObjType::Vehicle) {
            // Lego_TryTeleportObject: the vehicle waits for a teleporter of its class.
            if (teleportQueue_.size() < kMaxTeleportQueue) teleportQueue_.push_back(&type);
            GetGame().iconPanel->BackToMain();
            return;
        }
        if (type.type != ObjType::Building) return;
        placing_ = &type;
        GetGame().iconPanel->BackToMain();
    }

    int PlacementDirection() const {
        const world::TopDownCamera::View view = camera_.Compute(&level_.map());
        const float fx = view.forward.x, fy = view.forward.y;
        if (std::abs(fx) <= std::abs(fy)) return fy > 0.0f ? 2 : 0;
        return fx > 0.0f ? 3 : 1;
    }

    void UpdatePlacement(Input& in) {
        placeValid_ = false;
        placeFootprint_.clear();
        BlockPos b;
        if (PickBlock(in.mouseX(), in.mouseY(), b)) {
            placeDir_ = PlacementDirection();
            placeOrigin_ = b;
            placeFootprint_ = BuildingFootprint(*placing_, b, placeDir_);
            placeValid_ = CanPlaceBuilding(level_, placeFootprint_);
        }
        if (in.rightReleased()) {
            placing_ = nullptr;
        } else if (in.leftReleased() && placeValid_) {
            StartConstruction(*placing_, placeOrigin_, placeDir_);
            placing_ = nullptr;
        }
    }

    // Construction_Zone_StartBuilding: the footprint becomes foundation and the site waits for
    // its crystals and ore.
    void StartConstruction(const ObjectTypeInfo& type, BlockPos origin, int dir) {
        auto z = std::make_unique<ConstructionZone>();
        z->handle = nextZoneHandle_++;
        z->type = &type;
        z->origin = origin;
        z->dir = dir;
        z->footprint = BuildingFootprint(type, origin, dir);
        if (const ObjectStats* st = GetStats().Find(type.name)) {
            z->crystalsNeeded = int(st->Num("CostCrystal", 0));
            z->oreNeeded = int(st->Num("CostOre", 0));
        }
        for (size_t i = 0; i < z->footprint.size(); ++i) {
            const BlockPos p = z->footprint[i];
            world::Block& blk = level_.block(p.x, p.y);
            blk.flags |= world::BlockFlag::Foundation;
            if (i + 1 < z->footprint.size() && z->footprint[i + 1] == p) {
                blk.flags |= world::BlockFlag::BuildingPath;
                ++i;
            }
            level_.UpdateSurface(p.x, p.y);
        }
        // TODO(construction): flatten the ground, barriers around the site.
        RR_INFO("Construction of %s started (needs %d crystals, %d ore)", type.name.c_str(), z->crystalsNeeded,
                z->oreNeeded);
        // Construction_Zone_RequestResource: one fetch per crystal and ore of the cost.
        for (int i = 0; i < z->crystalsNeeded; ++i) tasks_.CreateRequest(z->handle, ObjType::PowerCrystal);
        for (int i = 0; i < z->oreNeeded; ++i) tasks_.CreateRequest(z->handle, ObjType::Ore);
        zones_.push_back(std::move(z));
    }

    // Construction_Zone_StartPath: the block shows a laid path and waits for the path's cost.
    void StartPath(BlockPos b) {
        auto z = std::make_unique<ConstructionZone>();
        z->handle = nextZoneHandle_++;
        z->origin = b;
        z->footprint = {b};
        z->path = true;
        if (const ObjectStats* st = GetStats().Find("Path")) {
            z->crystalsNeeded = int(st->Num("CostCrystal", 0));
            // TODO(refinery): refined ore instead when an ore refinery exists.
            z->oreNeeded = int(st->Num("CostOre", 0));
        }
        level_.block(b.x, b.y).flags |= world::BlockFlag::LaidPath;
        level_.UpdateSurface(b.x, b.y);
        for (int i = 0; i < z->crystalsNeeded; ++i) tasks_.CreateRequest(z->handle, ObjType::PowerCrystal);
        for (int i = 0; i < z->oreNeeded; ++i) tasks_.CreateRequest(z->handle, ObjType::Ore);
        zones_.push_back(std::move(z));
    }

    // Construction_Zone_CompletePath: the laid path becomes a power path; its resources are used.
    void FinishPath(BlockPos b) {
        for (size_t i = 0; i < zones_.size(); ++i) {
            ConstructionZone& z = *zones_[i];
            if (!z.path || z.origin != b) continue;
            for (GameObject* r : z.placed) objects_.Remove(*r);
            world::Block& blk = level_.block(b.x, b.y);
            blk.flags = (blk.flags & ~world::BlockFlag::LaidPath) | world::BlockFlag::Path;
            // TODO(power): the power grid update (powered paths glow).
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) level_.UpdateSurface(b.x + dx, b.y + dy);
            tasks_.RemoveZone(z.handle);
            zones_.erase(zones_.begin() + std::ptrdiff_t(i));
            power_.Request();
            RR_INFO("Power path built at %d,%d", b.x, b.y);
            return;
        }
    }

    // Construction_UpdateAll: a site with everything delivered becomes the building once nobody
    // stands on it; a path site gets its build-path job.
    void UpdateConstruction() {
        for (size_t i = 0; i < zones_.size();) {
            ConstructionZone& z = *zones_[i];
            if (z.path) {
                if (z.complete() && !z.buildRequested) {
                    z.buildRequested = true;
                    tasks_.CreateBuildPath(z.origin);
                }
                ++i;
                continue;
            }
            if (!z.complete() || UnitsOnFootprint(z)) {
                ++i;
                continue;
            }
            for (GameObject* r : z.placed) objects_.Remove(*r);
            for (const BlockPos& p : z.footprint) {
                level_.block(p.x, p.y).flags &= ~(world::BlockFlag::Foundation | world::BlockFlag::BuildingPath);
            }
            GameObject* b = objects_.Create(*z.type);
            objects_.PlaceBuilding(*b, z.origin, z.dir);
            power_.Request();  // LegoObject_RequestPowerGridUpdate
            tasks_.RemoveZone(z.handle);
            // TODO(construction): the building teleports down (LegoObject_TeleportDownBuilding).
            RR_INFO("Construction of %s complete", z.type->name.c_str());
            zones_.erase(zones_.begin() + std::ptrdiff_t(i));
        }
    }

    // A unit at a storage building takes one resource out of stock and carries it away.
    GameObject* FetchFromStore(GameObject& unit) {
        int& stock = unit.fetchType == ObjType::PowerCrystal ? crystals_ : ore_;
        if (stock <= 0) return nullptr;
        const ObjectTypeInfo* info = GetObjectRegistry().Get(unit.fetchType, 0);
        if (!info) return nullptr;
        --stock;
        return objects_.Spawn(*info, unit.position.x, unit.position.y);
    }

    // Where loads are put down: the site's path tile if it has one, else its origin.
    static BlockPos SiteDropBlock(const ConstructionZone& z) {
        BlockPos b = z.origin;
        for (size_t i = 0; i + 1 < z.footprint.size(); ++i)
            if (z.footprint[i] == z.footprint[i + 1]) b = z.footprint[i];
        return b;
    }

    // Carriers stand beside the drop block, outside the site, so they never hold up completion.
    // TODO(construction): verify against the game's request routing (AITask_DoRequest_ObjectType).
    bool SiteApproach(const ConstructionZone& z, BlockPos& out) const {
        const BlockPos drop = SiteDropBlock(z);
        static const int kDx[4] = {0, 1, 0, -1}, kDy[4] = {-1, 0, 1, 0};
        for (int d = 0; d < 4; ++d) {
            const BlockPos n{drop.x + kDx[d], drop.y + kDy[d]};
            if (std::find(z.footprint.begin(), z.footprint.end(), n) != z.footprint.end()) continue;
            if (!level_.InBounds(n.x, n.y) || !(level_.block(n.x, n.y).flags & world::BlockFlag::Ground)) continue;
            if (level_.block(n.x, n.y).flags & world::BlockFlag::Building) continue;
            out = n;
            return true;
        }
        return false;
    }

    void PlaceOnSite(GameObject& unit, GameObject& item) {
        for (const auto& z : zones_) {
            if (z->handle != unit.deliverZone) continue;
            // The load lands on the site, scattered a little around the drop block's centre.
            const BlockPos drop = SiteDropBlock(*z);
            const Vec2 c = level_.map().BlockCentre(drop.x, drop.y);
            const float spread = level_.map().blockSize() * 0.3f;
            item.position.x = c.x + random::RandRange(-spread, spread);
            item.position.y = c.y + random::RandRange(-spread, spread);
            item.position.z = level_.map().WorldZ(item.position.x, item.position.y);
            z->placed.push_back(&item);
            if (item.type == ObjType::PowerCrystal) ++z->crystalsPlaced;
            else ++z->orePlaced;
            return;
        }
        // TODO(construction): resources for a cancelled site are collected again.
        tasks_.CreateCollect(item);
    }

    bool UnitsOnFootprint(const ConstructionZone& z) const {
        for (const auto& o : objects_.objects()) {
            if (o->removed || o->hidden || o->type == ObjType::Building || o->carrier) continue;
            if (o->type != ObjType::MiniFigure && o->type != ObjType::Vehicle && o->type != ObjType::RockMonster) continue;
            BlockPos b;
            if (!objects_.BlockOf(*o, b)) continue;
            for (const BlockPos& p : z.footprint)
                if (p == b) return true;
        }
        return false;
    }

    // Outline of the footprint under the cursor: green where it can go, red where not.
    void DrawPlacement(Renderer2D& r2d, const Mat4& viewProj) const {
        if (!placing_ || placeFootprint_.empty()) return;
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
        const ColorF c = placeValid_ ? ColorF{0, 1, 0, 1} : ColorF{1, 0, 0, 1};
        for (size_t i = 0; i < placeFootprint_.size(); ++i) {
            const BlockPos b = placeFootprint_[i];
            const bool pathTile = i + 1 < placeFootprint_.size() && placeFootprint_[i + 1] == b;
            const Vec3f corners[4] = {level_.map().PointWorld(b.x, b.y), level_.map().PointWorld(b.x + 1, b.y),
                                      level_.map().PointWorld(b.x + 1, b.y + 1), level_.map().PointWorld(b.x, b.y + 1)};
            float sx[4], sy[4];
            bool ok = true;
            for (int k = 0; k < 4; ++k) ok &= project(corners[k], sx[k], sy[k]);
            if (!ok) continue;
            const ColorF cc = pathTile && placeValid_ ? ColorF{1, 1, 0, 1} : c;
            for (int k = 0; k < 4; ++k) r2d.DrawLine(sx[k], sy[k], sx[(k + 1) & 3], sy[(k + 1) & 3], cc);
            if (pathTile) ++i;
        }
    }

    static uint32_t AbilityOfItem(MenuItem item) {
        switch (item) {
        case MenuItem::TrainDriver: return Ability::Driver;
        case MenuItem::TrainEngineer: return Ability::Repair;
        case MenuItem::TrainGeologist: return Ability::Scanner;
        case MenuItem::TrainPilot: return Ability::Pilot;
        case MenuItem::TrainSailor: return Ability::Sailor;
        case MenuItem::TrainDynamite: return Ability::Dynamite;
        default: return 0;
        }
    }

    // The tool store nearest to a block (dynamite is handed out there).
    GameObject* NearestToolStore(BlockPos b) const {
        const Vec2 c = level_.map().BlockCentre(b.x, b.y);
        GameObject* best = nullptr;
        float bestD = 0.0f;
        for (const auto& op : objects_.objects()) {
            GameObject& s = *op;
            if (s.type != ObjType::Building || s.removed || s.hidden || s.teleportingIn || !s.stats) continue;
            if (!(s.stats->F1(s.level) & StatsFlag1::ToolStore)) continue;
            const float dx = s.position.x - c.x, dy = s.position.y - c.y;
            if (!best || dx * dx + dy * dy < bestD) {
                best = &s;
                bestD = dx * dx + dy * dy;
            }
        }
        return best;
    }

    // LegoObject_Search (trainer): the nearest standing building whose stats train the skill.
    GameObject* NearestTrainer(const GameObject& unit, uint32_t ability) const {
        uint32_t flag = 0;
        switch (ability) {
        case Ability::Pilot: flag = StatsFlag2::TrainPilot; break;
        case Ability::Sailor: flag = StatsFlag2::TrainSailor; break;
        case Ability::Driver: flag = StatsFlag2::TrainDriver; break;
        case Ability::Dynamite: flag = StatsFlag2::TrainDynamite; break;
        case Ability::Repair: flag = StatsFlag2::TrainRepair; break;
        case Ability::Scanner: flag = StatsFlag2::TrainScanner; break;
        default: return nullptr;
        }
        GameObject* best = nullptr;
        float bestD = 0.0f;
        for (const auto& op : objects_.objects()) {
            GameObject& b = *op;
            if (b.type != ObjType::Building || b.removed || b.hidden || b.teleportingIn || !b.stats) continue;
            if (!(b.stats->F2(b.level) & flag)) continue;
            const float dx = b.position.x - unit.position.x, dy = b.position.y - unit.position.y;
            if (!best || dx * dx + dy * dy < bestD) {
                best = &b;
                bestD = dx * dx + dy * dy;
            }
        }
        return best;
    }

    static ToolType ToolOfItem(MenuItem item) {
        switch (item) {
        case MenuItem::GetDrill: return ToolType::Drill;
        case MenuItem::GetSpade: return ToolType::Spade;
        case MenuItem::GetHammer: return ToolType::Hammer;
        case MenuItem::GetSpanner: return ToolType::Spanner;
        default: return ToolType::Count;
        }
    }

    // A unit got into a vehicle. It leaves the selection (ours: the game keeps no selected unit
    // inside a vehicle), and a vehicle selected on its own switches to its driven menu
    // (Interface_ChangeMenu_IfVehicleMounted_IsLiveObject).
    void OnBoard(GameObject& unit, GameObject& vehicle) {
        if (!GetGame().iconPanel) return;
        IconPanel& panel = *GetGame().iconPanel;
        if (selection_.Deselect(unit) && selection_.empty() && panel.menu() == MenuType::LegoMan) panel.BackToMain();
        if (panel.menu() == MenuType::UnmannedVehicle && selection_.IsSelected(vehicle)) {
            if (selection_.units().size() == 1) panel.OpenMenu(UnitMenu(vehicle));
            else panel.BackToMain();
        }
    }

    static MenuType UnitMenu(const GameObject& o) {
        switch (o.type) {
        case ObjType::MiniFigure: return MenuType::LegoMan;
        case ObjType::Vehicle:
            if (!o.driver) return MenuType::UnmannedVehicle;
            return o.stats && !(o.stats->F1(o.level) & StatsFlag1::CrossLand) ? MenuType::WaterVehicle
                                                                              : MenuType::Vehicle;
        case ObjType::Building: return MenuType::Building;
        case ObjType::ElectricFence: return MenuType::ElectricFence;
        default: return MenuType::Main;
        }
    }

    void HandleMessage(const Message& m) {
        if (m.type != MessageType::Select && m.type != MessageType::ClearSelection)
            RR_DEBUG("Event %d object %p block %d,%d", int(m.type), static_cast<void*>(m.object), m.block.x, m.block.y);
        switch (m.type) {
        case MessageType::Select:
            if (m.object && selection_.Select(*m.object)) {
                GameObject& o = *m.object;
                // Message_SelectObject: units stop and wait for orders (route, work and task are
                // dropped), except while depositing (flags 0x24002000 / 0x100 in the game).
                if (o.type != ObjType::Building && o.state != UnitState::Depositing) {
                    objects_.RouteEnd(o, false);
                    if (o.state != UnitState::Idle) objects_.FinishAction(o, false);
                    tasks_.ReleaseUnit(o);
                }
                GetGame().iconPanel->OpenMenu(UnitMenu(o));
                // Message_SelectObject: units answer "yes sir" (vehicles only with a driver).
                int sfx = 0;
                if ((o.flags3 & ObjFlag3::CanYesSir) &&
                    (o.driver || !(o.stats && (o.stats->F1(o.level) & StatsFlag1::CanBeDriven))) &&
                    GetSfx().GetType("SFX_YesSir", sfx))
                    GetSfx().PlayAt(sfx, Vec3{o.position.x, o.position.y, o.position.z});
            }
            break;
        case MessageType::ClearSelection:
            // Message_ClearSelectedUnits; menus return to Main through Interface_BackToMain, which
            // posts this message.
            selection_.Clear();
            break;
        case MessageType::Deselect:
            if (m.object) selection_.Deselect(*m.object);
            break;
        // Events a level's PTL table chains together: drilling causes a rock fall, the fall
        // releases the block's crystals and ore, cleared rubble may hold ore.
        case MessageType::RockFall: SpawnRockFall(m.block, int(m.arg & 0xffff), (m.arg >> 16) != 0); break;
        case MessageType::GenerateFromCryOre: GenerateFromCryOre(m.block); break;
        case MessageType::GenerateCrystal: GenerateResource(m.block, ObjType::PowerCrystal, true); break;
        case MessageType::GenerateOre: GenerateResource(m.block, ObjType::Ore, true); break;
        case MessageType::CollectCrystal:
        case MessageType::CollectOre:
            if (m.object && !m.object->removed && !m.object->carrier) tasks_.CreateCollect(*m.object);
            break;
        case MessageType::UserGoto:
            // TODO(ai): AITask_QueueGotoBlock_Group; for now each unit routes directly.
            for (GameObject* o : selection_.units()) {
                if (o->type == ObjType::Vehicle && !o->driver) continue;  // nobody to drive it
                if (!objects_.RouteToBlock(*o, m.block)) RR_DEBUG("No route for %s", o->info->name.c_str());
            }
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
        // The briefing pauses the level; closing it lets the script start (objective switch).
        if (objective_.showing()) {
            switch (objective_.HandleInput(in)) {
            case ObjectiveWindow::State::Briefing: script_.objectiveSwitch = 1; break;
            case ObjectiveWindow::State::Achieved:
            case ObjectiveWindow::State::Failed:
                // TODO(rewards): the rewards screen comes between the level and the front end.
                GetApp().SetScene(front::CreateFrontEndScene());
                return;
            default: break;
            }
            return;
        }
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
        if (cheats_) HandleCheatKeys(in);
        // Debug cheat: the world runs four steps per frame.
        for (int step = 0; step < (fastForward_ ? 4 : 1); ++step) Simulate(elapsed);
    }

    // Debug cheats (RockRaiders.ini [debug] cheats=1 or --cheats).
    void HandleCheatKeys(Input& in) {
        if (in.KeyPressed(SDL_SCANCODE_F1)) {
            crystals_ += 10;
            ore_ += 10;
            power_.Request();
        }
        if (in.KeyPressed(SDL_SCANCODE_F2)) {
            // Selected buildings (or all of them) to their top level.
            bool any = false;
            for (GameObject* u : selection_.units())
                if (u->type == ObjType::Building && u->stats) {
                    u->level = u->stats->levels - 1;
                    any = true;
                }
            if (!any) {
                for (const auto& o : objects_.objects())
                    if (o->type == ObjType::Building && o->stats) o->level = o->stats->levels - 1;
                maxBuildingLevels_ = true;  // also for buildings still hidden in caverns
            }
            power_.Request();
        }
        if (in.KeyPressed(SDL_SCANCODE_F3))
            for (auto& z : zones_) {
                z->crystalsPlaced = std::max(z->crystalsPlaced, z->crystalsNeeded);
                z->orePlaced = std::max(z->orePlaced, z->oreNeeded);
            }
        if (in.KeyPressed(SDL_SCANCODE_F4)) {
            // Every skill for the selected raiders (or all of them).
            bool any = false;
            for (GameObject* u : selection_.units())
                if (u->type == ObjType::MiniFigure) {
                    u->abilities |= Ability::All;
                    any = true;
                }
            if (!any)
                for (const auto& o : objects_.objects())
                    if (o->type == ObjType::MiniFigure) o->abilities |= Ability::All;
        }
        if (in.KeyPressed(SDL_SCANCODE_F5)) fastForward_ = !fastForward_;
        if (in.KeyPressed(SDL_SCANCODE_F7)) infiniteStock_ = !infiniteStock_;
        if (infiniteStock_ && (crystals_ < 50 || ore_ < 50)) {
            // Storage never runs low.
            crystals_ = std::max(crystals_, 99);
            ore_ = std::max(ore_, 99);
            power_.Request();
        }
        if (in.KeyPressed(SDL_SCANCODE_F6)) {
            for (int y = 0; y < level_.height(); ++y)
                for (int x = 0; x < level_.width(); ++x)
                    if (level_.block(x, y).flags & world::BlockFlag::HiddenCavern) level_.UncoverHiddenCavern(x, y);
        }
        if (in.KeyPressed(SDL_SCANCODE_F12) && levelStatus_ == 0) {
            levelStatus_ = 1;
            objective_.Show(ObjectiveWindow::State::Achieved);
        }
    }

    void Simulate(float elapsed) {
        // RR_DEBUG_SKILLS=bits: every Rock Raider knows these skills (Ability bits, e.g. 7 =
        // Pilot, Sailor and Driver), including ones teleported in later.
        if (const char* sk = std::getenv("RR_DEBUG_SKILLS"))
            for (const auto& o : objects_.objects())
                if (o->type == ObjType::MiniFigure) o->abilities |= uint32_t(std::strtoul(sk, nullptr, 0));
        // RR_DEBUG_TRAIN="tick,ability": the first raider is sent to train at that tick.
        if (const char* tr = std::getenv("RR_DEBUG_TRAIN")) {
            const auto parts = SplitTokens(tr, ",");
            if (parts.size() == 2 && ++debugTrainTick_ == std::atoi(std::string(parts[0]).c_str())) {
                const uint32_t ab = uint32_t(std::atoi(std::string(parts[1]).c_str()));
                for (const auto& o : objects_.objects())
                    if (o->type == ObjType::MiniFigure && !o->removed) {
                        GameObject* trainer = NearestTrainer(*o, ab);
                        RR_INFO("Debug train: trainer %p", static_cast<void*>(trainer));
                        if (trainer) tasks_.CreateTrain(*trainer, ab, o.get());
                        break;
                    }
            }
        }
        // RR_DEBUG_DYNAMITE="tick,x,y": dynamite is ordered for that wall at that tick.
        if (const char* dy = std::getenv("RR_DEBUG_DYNAMITE")) {
            const auto parts = SplitTokens(dy, ",");
            if (parts.size() == 3 && ++debugDynamiteTick_ == std::atoi(std::string(parts[0]).c_str())) {
                const BlockPos w{std::atoi(std::string(parts[1]).c_str()), std::atoi(std::string(parts[2]).c_str())};
                GetGame().iconPanel->OpenMenu(MenuType::Wall, &w);
                RR_INFO("Debug dynamite at %d,%d", w.x, w.y);
                debugDynamite_ = w;
            }
            if (debugDynamite_.x >= 0 && GetGame().iconPanel->Settled() && GetGame().iconPanel->menu() == MenuType::Wall) {
                OnMenuItem(MenuItem::Dynamite);
                debugDynamite_ = {-1, -1};
            }
        }
        // Debug dig requests wait until their wall has been exposed.
        for (size_t i = 0; i < debugDigs_.size();) {
            const BlockPos b = debugDigs_[i];
            if (tasks_.CreateDig(level_, b, true)) {
                level_.block(b.x, b.y).flags |= world::BlockFlag::DigRequested;
                debugDigs_.erase(debugDigs_.begin() + std::ptrdiff_t(i));
            } else {
                ++i;
            }
        }
        if (std::getenv("RR_DEBUG_UNITS") && (++debugTick_ % 500) == 0) {
            for (const auto& o : objects_.objects()) {
                if (o->type != ObjType::MiniFigure || o->removed) continue;
                BlockPos b;
                objects_.BlockOf(*o, b);
                const Task* tk = tasks_.TaskOf(*o);
                std::string tools;
                for (ToolType t : o->tools) tools += std::to_string(int(t));
                RR_INFO("unit %p at %d,%d state %d routing %d act %d over %.1f carried %p zone %d task %d tools %s",
                        static_cast<void*>(o.get()), b.x, b.y, int(o->state), int(o->routing), int(o->activity),
                        o->animOverrun, static_cast<void*>(o->carried), o->deliverZone, tk ? int(tk->type) : -1,
                        tools.c_str());
            }
        }
        ServiceTeleporters();
        UpdateConstruction();
        power_.Update(level_, objects_, crystals_);
        tasks_.Update(objects_, level_, elapsed);
        for (const auto& [bx, by] : level_.TakeExposedBlocks())
            for (GameObject* o : objects_.ExposeBlock(bx, by)) {
                ++hiddenObjectsFound_;  // NERPsRuntime_IncHiddenObjectsFound
                if (o->type == ObjType::Building) {
                    if (maxBuildingLevels_ && o->stats) o->level = o->stats->levels - 1;
                    power_.Request();
                }
                if (o->type == ObjType::PowerCrystal || o->type == ObjType::Ore) tasks_.CreateCollect(*o);
            }
        if (int n = level_.TakeCavernsDiscovered()) RR_INFO("%d hidden cavern(s) discovered", n);
        for (const world::Level::SeamYield& y : level_.TakeSeamYields())
            GenerateResource({y.x, y.y}, y.crystal ? ObjType::PowerCrystal : ObjType::Ore, true);
        objects_.Update(elapsed);
        // Level_UpdateEffects: finished rock falls release the block (RockFallComplete).
        rockFallsDone_.clear();
        GetGame().rockFalls.Update(elapsed, rockFallsDone_);
        GetGame().explosions.Update(elapsed);
        ScareFromDynamite();
        for (BlockPos b : rockFallsDone_) {
            world::Block& blk = level_.block(b.x, b.y);
            if (!(blk.flags & 0x1000)) messages_.Post(MessageType::RockFallComplete, nullptr, 0, &b);
            blk.flags &= ~(world::BlockFlag::RockFallFX | 0x1000u | 0x8000000u);
        }
        if (script_.loaded() && levelStatus_ == 0) {
            script_.UpdateTimers(elapsed);
            script_.Execute();
        }
        level_.map().UpdateUVs(elapsed);
    }

    // Map3D_SetBlockHighlight tints (WallHighlightType): none, dig request, dynamite, reinforce,
    // the block whose menu is open, tutorial flashing.
    enum Highlight : uint8_t { kNone, kDig, kDynamite, kReinforce, kSelected, kTutorial };
    uint8_t BlockHighlight(int bx, int by) const {
        const IconPanel& panel = *GetGame().iconPanel;
        const BlockPos b{bx, by};
        switch (panel.menu()) {
        case MenuType::Ground:
        case MenuType::Erode:
        case MenuType::PlaceFence:
        case MenuType::Construction:
        case MenuType::Rubble:
        case MenuType::Wall:
            if (panel.block() == b) return kSelected;
            break;
        default: break;
        }
        if (tasks_.HasBlockTask(b, TaskType::Dynamite)) return kDynamite;
        if (tasks_.HasBlockTask(b, TaskType::Reinforce)) return kReinforce;
        if (level_.block(bx, by).flags & world::BlockFlag::DigRequested) return kDig;
        return kNone;
    }

    void DrawTerrain() {
        const world::Map3D& map = level_.map();
        static const uint32_t kHighlightColour[6] = {
            0xFFFFFFFFu,  // (1, 1, 1)
            0xFF808080u,  // (0.5, 0.5, 0.5)
            0xFF6680FFu,  // (1, 0.5, 0.4)
            0xFF80FF80u,  // (0.5, 1, 0.5)
            0xFFFF8066u,  // (0.4, 0.5, 1)
            0xFF009999u,  // (0.6, 0.6, 0)
        };
        tris_.clear();
        const int bw = map.blocksW();
        for (size_t qi = 0; qi < map.quads().size(); ++qi) {
            const world::Map3D::Quad& q = map.quads()[qi];
            if (q.hidden) continue;
            const uint32_t colour = kHighlightColour[BlockHighlight(int(qi) % bw, int(qi) / bw)];
            const Texture3D* tex = textures_.Get(q.texture);
            static const int kPlain[6] = {0, 1, 3, 1, 2, 3};
            static const int kRotated[6] = {3, 0, 2, 0, 1, 2};
            const int* order = q.rotated ? kRotated : kPlain;
            Vertex3D v[6];
            for (int i = 0; i < 6; ++i) v[i] = {q.pos[order[i]], q.normal[order[i]], q.uv[order[i]], colour};
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
        const Mat4 proj = Mat4::Perspective(fovY, aspect, 1.0f, clipDist_);  // as in ViewProjection
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
        GetGame().rockFalls.Draw(renderer_);
        GetGame().explosions.Draw(renderer_);
        renderer_.EndFrame();

        r2d.SetAnchor(Anchor::Center);
        // Primary unit green, the rest yellow (Lego_DrawAllSelectedUnitBoxes).
        const Mat4 viewProj = proj * viewMat;
        for (size_t i = 0; i < selection_.units().size(); ++i)
            DrawSelectionBox(r2d, viewProj, *selection_.units()[i], i == 0 ? ColorF{0, 1, 0, 1} : ColorF{1, 1, 0, 1});
        DrawPlacement(r2d, viewProj);
        // Panels slide in real time (Panel_Draw gets the unscaled frame time).
        const uint64_t nowMs = SDL_GetTicks64();
        const float frameElapsed = lastFrameMs_ ? float(nowMs - lastFrameMs_) * 0.025f : 0.0f;
        lastFrameMs_ = nowMs;
        Game& game = GetGame();
        // Lego_DrawRadarMap: centred on the tracked unit, else the camera's view point.
        if (game.panels->radarMapView && game.panels->IsFullyOpen(PanelType::Radar)) {
            r2d.SetAnchor(Anchor::Left);
            const GameObject* tracked = selection_.Primary();  // TODO(radar): tracked object, not selection
            radar_.Draw(r2d, level_, objects_,
                        tracked ? Vec2{tracked->position.x, tracked->position.y} : Vec2{camera_.x, camera_.y});
            r2d.SetAnchor(Anchor::Center);
        }
        game.panels->DrawPanels(r2d, frameElapsed);
        if (!game.panels->IsFullyClosed(PanelType::PriorityList)) {
            game.panels->SetAnchor(r2d, PanelType::PriorityList);
            const Vec2 p = game.panels->Position(PanelType::PriorityList);
            game.priorities->Draw(r2d, p.x, p.y, tasks_);
        }
        game.iconPanel->Update(frameElapsed);
        r2d.SetAnchor(Anchor::Right);
        game.iconPanel->Draw(r2d);
        r2d.SetAnchor(Anchor::Center);
        Panels::Resources res;
        res.crystals = crystals_;
        res.usedCrystals = power_.used();
        res.quota = crystalQuota_;
        res.ore = ore_;
        game.panels->DrawSideBar(r2d, game.toolTipFont.get(), res, frameElapsed);
        // TODO(interface): the message panel's text window and speech; plain text for now.
        if (const Font* f = game.textFont.get()) {
            r2d.SetAnchor(Anchor::Left);
            if (!message_.empty() && script_.messageTimer > 0.0f) f->Draw(r2d, 90.0f, 440.0f, message_);
            r2d.SetAnchor(Anchor::Center);
        }
        if (cheats_ && game.toolTipFont) {
            r2d.SetAnchor(Anchor::Left);
            game.toolTipFont->Draw(r2d, 4.0f, 200.0f,
                                   "CHEATS  F1 +10 crystals/ore  F2 max building level");
            game.toolTipFont->Draw(r2d, 4.0f, 212.0f, "F3 finish sites  F4 all skills  F5 speed x4");
            game.toolTipFont->Draw(r2d, 4.0f, 224.0f,
                                   "F6 open caverns  F7 infinite stock  F12 win level");
            r2d.SetAnchor(Anchor::Center);
        }
        objective_.Draw(r2d);
        // TODO(interface): proper tooltip boxes (ToolTip_*); plain text for now.
        std::string tip = game.panels->hoverToolTip();
        if (tip.empty() && game.panels->IsFullyOpen(PanelType::PriorityList)) tip = game.priorities->hoverToolTip();
        if (tip.empty()) tip = game.iconPanel->hoverToolTip();
        if (const Font* f = game.toolTipFont.get(); f && !tip.empty())
            f->Draw(r2d, GetApp().input().mouseX() + 16, GetApp().input().mouseY() + 16, tip);
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
    TaskSystem tasks_;
    std::vector<BlockPos> debugDigs_;
    int crystals_ = 0, ore_ = 0, crystalQuota_ = 0;
    static constexpr size_t kMaxTeleportQueue = 9;
    std::vector<const ObjectTypeInfo*> teleportQueue_;
    // Building placement and construction sites.
    const ObjectTypeInfo* placing_ = nullptr;
    BlockPos placeOrigin_;
    int placeDir_ = 0;
    bool placeValid_ = false;
    std::vector<BlockPos> placeFootprint_;
    std::vector<std::unique_ptr<ConstructionZone>> zones_;
    int nextZoneHandle_ = 1;
    int crystalsPickedUp_ = 0, orePickedUp_ = 0, hiddenObjectsFound_ = 0;
    nerps::Runtime script_;
    std::string message_;
    int levelStatus_ = 0;  // 1 completed, 2 failed (Objective_SetStatus)
    bool cheats_ = false, fastForward_ = false, infiniteStock_ = false;
    int debugTrainTick_ = 0, debugDynamiteTick_ = 0;
    BlockPos debugDynamite_{-1, -1};
    bool maxBuildingLevels_ = false;  // cheat F2 without a selection
    float dynamiteDamageRadius_ = 75.0f, dynamiteMaxDamage_ = 150.0f;
    ObjectiveWindow objective_;
    RadarMap radar_;
    PowerGrid power_;
    int debugTick_ = 0;
    uint64_t lastFrameMs_ = 0;
    Vec3f ambient_, topSpot_;
    float clipDist_ = 800.0f;
    float viewAspect_ = 4.0f / 3.0f, uiWidth_ = 640.0f;
    std::vector<BlockPos> rockFallsDone_;
    bool failed_ = false;
};

}  // namespace

std::unique_ptr<Scene> CreateLevelScene(const std::string& levelId) { return std::make_unique<LevelScene>(levelId); }

}  // namespace rr
