#pragma once

#include <memory>
#include <string>
#include <vector>

#include "config/Config.h"
#include "game/objects/Routing.h"

namespace rr {

struct GameObject;
struct ObjectTypeInfo;
class ObjectList;
namespace world {
class Level;
}

// Build requirements (Dependencies_*): "AlwaysCheck:<object> <type>:<level>,..." lines; an object
// can be built while every listed type is present at that upgrade level or above.
// "HitOnceStay:" entries stay available once they have been met.
class Dependencies {
public:
    void Load(const Config& cfg, std::string_view gameName);
    void ResetLevel();
    bool Met(const ObjectTypeInfo& type, const ObjectList& objects);

private:
    struct Requirement {
        const ObjectTypeInfo* type = nullptr;
        int level = 0;
    };
    struct Entry {
        const ObjectTypeInfo* object = nullptr;
        bool stay = false;    // HitOnceStay
        bool unlocked = false;
        std::vector<Requirement> requires;
    };
    std::vector<Entry> entries_;
};

// Footprint of a building at an origin block and direction (SelectPlace_TransformShapePoints).
std::vector<BlockPos> BuildingFootprint(const ObjectTypeInfo& type, BlockPos origin, int dir);
// Whether a building may be placed (SelectPlace_CheckAndUpdate / Level_CanBuildOnBlock).
bool CanPlaceBuilding(const world::Level& level, const std::vector<BlockPos>& footprint);

// A building site (Construction_Zone): foundation laid, waiting for its crystals and ore.
struct ConstructionZone {
    int handle = 0;
    const ObjectTypeInfo* type = nullptr;
    BlockPos origin;
    int dir = 0;
    std::vector<BlockPos> footprint;
    int crystalsNeeded = 0, oreNeeded = 0;
    bool studs = false;           // oreNeeded counts Building Studs (an Ore Refinery was active)
    int crystalsPlaced = 0, orePlaced = 0;
    std::vector<GameObject*> placed;  // resources dropped on the site
    bool path = false;            // a power path site (type is null)
    bool buildRequested = false;  // path site: the build-path job has been handed out
    bool complete() const { return crystalsPlaced >= crystalsNeeded && orePlaced >= oreNeeded; }
};

}  // namespace rr
