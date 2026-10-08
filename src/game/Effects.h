#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/Math3D.h"
#include "game/objects/Routing.h"

namespace rr {

class Config;
class Renderer3D;
struct LwsScene;

// Rock falls (Effect_*_RockFall): when a wall comes down a scene of tumbling rock and dust plays
// over the block. Lego.cfg RockFallStyles lists per style (Rock, Lava, Ice; chosen by a level's
// RockFallStyle) a tumble null name and one scene per fall type. Each scene has four instances,
// so at most four falls of a type play at once.
class RockFallEffects {
public:
    enum Type { ThreeSides = 0, OutsideCorner = 1, VTunnel = 2, kTypes = 3 };

    void Load(const Config& cfg, std::string_view gameName);
    // Effect_FindRockFallStyle + Effect_SetRockFallStyle; unknown names keep the first style.
    void SetStyle(std::string_view name);
    // Effect_Spawn_RockFall: false when all four instances of the type are playing.
    bool Spawn(Type type, BlockPos block, const Vec3f& pos, Vec2 dir);
    // Advances the playing falls; blocks whose fall has finished are appended to finished.
    void Update(float elapsed, std::vector<BlockPos>& finished);
    void Draw(Renderer3D& r) const;
    void StopAll();

private:
    struct Instance {
        bool playing = false;
        float time = 0.0f;
        Vec3f pos;
        float heading = 0.0f;
        BlockPos block;
    };
    struct Style {
        std::string name, tumbleNull;
        std::shared_ptr<LwsScene> scenes[kTypes];
        Instance instances[kTypes][4];
    };
    std::vector<Style> styles_;
    size_t current_ = 0;
};

// A one-shot scene played where something happened (Effect_Spawn_Explosion): at most four at
// once, each removed when its animation ends.
class SceneEffect {
public:
    void SetScene(std::shared_ptr<LwsScene> scene) { scene_ = std::move(scene); }
    void Spawn(const Vec3f& pos);
    void Update(float elapsed);
    void Draw(Renderer3D& r) const;
    void StopAll() { live_.clear(); }

private:
    struct Instance {
        Vec3f pos;
        float time = 0.0f;
    };
    std::shared_ptr<LwsScene> scene_;
    std::vector<Instance> live_;
};

}  // namespace rr
