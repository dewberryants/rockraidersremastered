#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "config/Config.h"
#include "game/world/Map3D.h"

namespace rr::world {

// Terrain types as stored in the terrain map (and named in Lego.cfg SurfaceTypeDescriptions).
enum class Terrain : uint8_t {
    Tunnel = 0, Immovable = 1, Hard = 2, Medium = 3, Loose = 4, Soil = 5, Lava = 6, Water = 7, OreSeam = 8,
    Lake = 9, CrystalSeam = 10, RechargeSeam = 11, Rubble = 12, Reinforcement = 13, Path = 14, SlugHole = 15,
    Undiscovered = 16, Cavern = 17,
};

// Block state flags (first flag word of the original block record).
namespace BlockFlag {
constexpr uint32_t RubbleMask = 0x3;        // rubble layers still on the floor
constexpr uint32_t Visible = 0x4;           // surface is floor/wall/corner (not a solid rock top)
constexpr uint32_t Ground = 0x8;            // all corners dug: walkable floor
constexpr uint32_t Wall = 0x10;             // wall shape (or wall facing newly opened ground)
constexpr uint32_t Reinforced = 0x20;
constexpr uint32_t Building = 0x400;        // solid part of a building: units route around it
constexpr uint32_t OuterCorner = 0x40;
constexpr uint32_t InnerCorner = 0x80;
constexpr uint32_t LavaSettled = 0x200;     // erosion finished: cooled lava texture
constexpr uint32_t RockFallFX = 0x800;      // a rock fall scene is playing over the block
constexpr uint32_t Diagonal = 0x2000;       // two opposite corners dug ("V" wall)
constexpr uint32_t HiddenCavern = 0x20000;  // predug cavern not yet discovered
constexpr uint32_t BuildingPath = 0x8000;   // walkable part of a building (path tile, tool store)
constexpr uint32_t Foundation = 0x100000;   // building foundation floor
constexpr uint32_t CleanFloor = 0x200000;   // floor without rubble (predug caverns)
constexpr uint32_t Path = 0x20000000;       // built power path
constexpr uint32_t LaidPath = 0x40000000;   // a power path is laid here, waiting to be built
constexpr uint32_t DigRequested = 0x800000; // marked for drilling by the player (highlighted)
constexpr uint32_t Eroding = 0x80000000;    // lava erosion has reached this block
}  // namespace BlockFlag

// Second flag word.
namespace BlockFlag2 {
constexpr uint32_t GeneratePower = 0x1;      // a power station stands here
constexpr uint32_t ToolStoreEntrance = 0x4;  // only units with EnterToolStore may route through
constexpr uint32_t SlugHole = 0x20;
constexpr uint32_t Powered = 0x100;
constexpr uint32_t HiddenSlugHole = 0x200;
constexpr uint32_t PathCounted = 0x400;
}  // namespace BlockFlag2

struct Block {
    // State of the terrain point at (x, y): 0 = raised (wall), 1 = dug; seams count down 5..2
    // while being drilled. (The original kept point state in the block grid.)
    uint8_t pointState = 0;
    uint8_t texture = 0;
    Terrain terrain = Terrain::Tunnel;
    uint8_t direction = 0;
    uint8_t brokenDirection = 0;  // direction of the wall as it came down (DigComplete argument)
    uint8_t erodeStage = 0;  // 4 = lava
    uint32_t flags = 0;
    uint32_t flags2 = 0;
    int16_t random = 0;      // per-block random value drawn at load
    int16_t seamDigs = 0;
    uint8_t cryOre = 0;       // CryOre map value: what drilling the wall releases
    int crystals = 0, ore = 0;  // crystals/ore generated on this block so far
    float damage = 0.0f;     // drilling progress, 1.0 = wall breaks (Level_Block_Damage)
};

struct LevelSettings {
    std::string id;  // config block, e.g. "Levels::Tutorial01"
    std::string fullName;
    float blockSize = 40.0f;
    float digDepth = 40.0f;
    float roofHeight = 40.0f;
    float roughLevel = 0.0f;
    float trainTime = 0.0f;  // TrainTime x 25: a unit can train again once this long has passed
    bool useRoof = false;
    std::string textureSet;
    bool fog = false;
    float fogR = 0, fogG = 0, fogB = 0;
    float fogHighR = 0, fogHighG = 0, fogHighB = 0;
};

class Level {
public:
    bool Load(const Config& cfg, std::string_view gameName, std::string_view levelId);

    int width() const { return width_; }
    int height() const { return height_; }
    const LevelSettings& settings() const { return settings_; }
    Map3D& map() { return map_; }
    const Map3D& map() const { return map_; }
    bool InBounds(int x, int y) const { return x >= 0 && y >= 0 && x < width_ && y < height_; }
    Block& block(int x, int y) { return blocks_[size_t(y) * width_ + x]; }
    const Block& block(int x, int y) const { return blocks_[size_t(y) * width_ + x]; }

    // Level_BlockUpdateSurface: chooses shape, texture and direction from the dug corner points.
    void UpdateSurface(int x, int y);
    // Level_DestroyWall: lowers the block's corner points and updates the surroundings.
    bool DestroyWall(int x, int y, bool hiddenCavern);
    // Building footprint blocks (Level_Block_SetSolidBuilding / SetPathBuilding / SetToolStoreBuilding).
    enum class BuildingBlock { Solid, Path, ToolStore };
    void SetBuildingBlock(int x, int y, BuildingBlock kind, bool generatesPower);

    // Removes one rubble layer; returns true once the floor is clean (Level_Block_ClearRubbleLayer).
    bool ClearRubbleLayer(int x, int y);
    // LiveObject_CanReinforceBlock (without a unit): an exposed wall inside the border that is
    // not reinforced yet and not a seam.
    bool CanReinforce(int x, int y) const;
    // LiveObject_CanDynamiteBlockPos: a wall inside the border, not an outer corner, immovable
    // rock, water or a recharge seam.
    bool CanDynamite(int x, int y) const;
    // Level_Block_Reinforce: straight walls take the reinforcement; corners never do.
    bool ReinforceBlock(int x, int y);
    // Opens a hidden cavern from block (x, y): a wave over the 8 neighbours reaches every
    // connected hidden block (Level_UncoverHiddenCavern).
    void UncoverHiddenCavern(int x, int y);

    // Resources released by drilling seams since the last call: block to drop on, crystal or ore.
    struct SeamYield {
        int x, y;
        bool crystal;
    };
    std::vector<SeamYield> TakeSeamYields() { return std::move(seamYields_); }

    // Blocks opened up since the last call (for hidden objects) and caverns found.
    std::vector<std::pair<int, int>> TakeExposedBlocks() { return std::move(exposed_); }
    int TakeCavernsDiscovered() {
        const int n = cavernsDiscovered_;
        cavernsDiscovered_ = 0;
        return n;
    }

private:
    std::vector<SeamYield> seamYields_;
    std::vector<std::pair<int, int>> exposed_;
    int cavernsDiscovered_ = 0;
    bool LoadPreDug(const std::string& path);
    bool LoadTerrain(const std::string& path);
    void LoadCryOre(const std::string& path);
    void LoadPaths(const std::string& path);
    uint8_t ChoosePathTexture(int x, int y, uint8_t& direction, bool powered) const;
    bool IsPathish(int x, int y) const;

    LevelSettings settings_;
    int width_ = 0, height_ = 0;
    std::vector<Block> blocks_;
    Map3D map_;
};

}  // namespace rr::world
