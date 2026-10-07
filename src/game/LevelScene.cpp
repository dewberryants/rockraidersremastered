#include "game/LevelScene.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "config/Config.h"
#include "core/Log.h"
#include "fs/Path.h"
#include "game/Game.h"
#include "game/Messages.h"
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

        LoadObjects(cfg);
        level_.TakeExposedBlocks();  // blocks opened while loading the level are not news
        level_.TakeCavernsDiscovered();
        messages_.LoadTranslation(cfg.GetString(Config::Id(game.gameName, levelId_, "PTLFile"), ""));
        tasks_.Reset();
        tasks_.LoadPriorities(cfg, Config::Id(game.gameName, levelId_));
        objects_.onActionDone = [this](GameObject& u, bool ok) { OnActionDone(u, ok); };
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
        // HUD panels and the icon menu take clicks over them (Panel_CheckCollision first).
        if (GetGame().panels->HandleInput(in)) return;
        if (panel.HandleInput(in)) return;
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
        case MenuItem::DeselectDig: return (blk.flags & world::BlockFlag::DigRequested) != 0;
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
        case MenuItem::TeleportMan:
            // Lego_TryTeleportObject: Rock Raiders wait for a teleporter (LegoObject_QueueTeleport).
            if (const ObjectTypeInfo* pilot = GetObjectRegistry().Get(ObjType::MiniFigure, 0))
                if (teleportQueue_.size() < kMaxTeleportQueue) teleportQueue_.push_back(pilot);
            break;
        case MenuItem::DeselectDig:
            level_.block(b.x, b.y).flags &= ~world::BlockFlag::DigRequested;
            // TODO(ai): AITask_RemoveWallReferences for the block's dig task.
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
                // Level_DestroyWall: the floor is left full of rubble with clearing queued, and
                // DigComplete starts the level's event chain (rock fall, crystals, ore).
                b.flags &= ~world::BlockFlag::DigRequested;
                messages_.Post(MessageType::DigComplete, nullptr, 0, &t->block);
                // TODO(ai): the game queues one clear request per rubble layer (four).
                tasks_.CreateClear(t->block);
            } else if (t->type == TaskType::Clear && (b.flags & world::BlockFlag::CleanFloor)) {
                messages_.Post(MessageType::ClearComplete, nullptr, 0, &t->block);
            }
        }
        tasks_.TaskFinished(unit, success);
    }

    void OnPanelButton(PanelButton b) {
        switch (b) {
        case PanelButton::RadarToggle: GetGame().panels->Toggle(PanelType::Radar); break;
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
            if (name == "GetPowered" + s + "Built") return 0;  // TODO(power)
        }
        if (name == "SetLevelCompleted" || name == "SetLevelFail") {
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
        if (m.type != MessageType::Select && m.type != MessageType::ClearSelection)
            RR_DEBUG("Event %d object %p block %d,%d", int(m.type), static_cast<void*>(m.object), m.block.x, m.block.y);
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
        // Events a level's PTL table chains together: drilling causes a rock fall, the fall
        // releases the block's crystals and ore, cleared rubble may hold ore.
        case MessageType::RockFall:
            // TODO(effects): the rock fall animation (Effect_Spawn_RockFall) runs before completing.
            messages_.Post(MessageType::RockFallComplete, m.object, m.arg, &m.block);
            break;
        case MessageType::GenerateFromCryOre: GenerateFromCryOre(m.block); break;
        case MessageType::GenerateCrystal: GenerateResource(m.block, ObjType::PowerCrystal, true); break;
        case MessageType::GenerateOre: GenerateResource(m.block, ObjType::Ore, true); break;
        case MessageType::CollectCrystal:
        case MessageType::CollectOre:
            if (m.object && !m.object->removed && !m.object->carrier) tasks_.CreateCollect(*m.object);
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
        ServiceTeleporters();
        tasks_.Update(objects_, level_, elapsed);
        for (const auto& [bx, by] : level_.TakeExposedBlocks())
            for (GameObject* o : objects_.ExposeBlock(bx, by)) {
                ++hiddenObjectsFound_;  // NERPsRuntime_IncHiddenObjectsFound
                if (o->type == ObjType::PowerCrystal || o->type == ObjType::Ore) tasks_.CreateCollect(*o);
            }
        if (int n = level_.TakeCavernsDiscovered()) RR_INFO("%d hidden cavern(s) discovered", n);
        for (const world::Level::SeamYield& y : level_.TakeSeamYields())
            GenerateResource({y.x, y.y}, y.crystal ? ObjType::PowerCrystal : ObjType::Ore, true);
        objects_.Update(elapsed);
        if (script_.loaded() && levelStatus_ == 0) {
            script_.UpdateTimers(elapsed);
            script_.Execute();
        }
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
        r2d.SetAnchor(Anchor::Right);
        game.iconPanel->Draw(r2d);
        r2d.SetAnchor(Anchor::Center);
        Panels::Resources res;
        res.crystals = crystals_;
        res.usedCrystals = 0;  // TODO(power): crystals in use by powered buildings
        res.quota = crystalQuota_;
        res.ore = ore_;
        game.panels->DrawSideBar(r2d, game.toolTipFont.get(), res, frameElapsed);
        // TODO(interface): the message panel's text window and speech; plain text for now.
        if (const Font* f = game.textFont.get()) {
            r2d.SetAnchor(Anchor::Left);
            if (!message_.empty() && script_.messageTimer > 0.0f) f->Draw(r2d, 90.0f, 440.0f, message_);
            r2d.SetAnchor(Anchor::Center);
        }
        objective_.Draw(r2d);
        // TODO(interface): proper tooltip boxes (ToolTip_*); plain text for now.
        std::string tip = game.panels->hoverToolTip();
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
    int crystalsPickedUp_ = 0, orePickedUp_ = 0, hiddenObjectsFound_ = 0;
    nerps::Runtime script_;
    std::string message_;
    int levelStatus_ = 0;  // 1 completed, 2 failed (Objective_SetStatus)
    ObjectiveWindow objective_;
    RadarMap radar_;
    uint64_t lastFrameMs_ = 0;
    Vec3f ambient_, topSpot_;
    float clipDist_ = 800.0f;
    float viewAspect_ = 4.0f / 3.0f, uiWidth_ = 640.0f;
    bool failed_ = false;
};

}  // namespace

std::unique_ptr<Scene> CreateLevelScene(const std::string& levelId) { return std::make_unique<LevelScene>(levelId); }

}  // namespace rr
