#include "game/world/Level.h"

#include <cmath>
#include <cstdlib>

#include "core/Log.h"
#include "core/Random.h"
#include "game/world/MapFile.h"

namespace rr::world {

namespace {
constexpr int kCornerX[4] = {0, 1, 1, 0};
constexpr int kCornerY[4] = {0, 0, 1, 1};

// Wall textures by terrain for straight walls (1), outer corners (0x30 + n) and inner corners (0x50 + n).
// Returns 0 for terrains that keep their previous texture.
int WallVariant(Terrain t) {
    switch (t) {
        case Terrain::Immovable:
        case Terrain::Water: return 5;
        case Terrain::Hard: return 4;
        case Terrain::Medium: return 3;
        case Terrain::Loose: return 2;
        case Terrain::Soil: return 1;
        default: return 0;
    }
}
}  // namespace

bool Level::Load(const Config& cfg, std::string_view gameName, std::string_view levelId) {
    settings_ = {};
    settings_.id = std::string(levelId);
    auto key = [&](std::string_view k) { return Config::Id(gameName, levelId, k); };
    auto num = [&](std::string_view k) { return cfg.GetFloat(key(k)).value_or(0.0f); };
    settings_.fullName = cfg.GetString(key("FullName"), std::string(levelId));
    settings_.blockSize = num("BlockSize");
    settings_.digDepth = num("DigDepth");
    settings_.roofHeight = num("RoofHeight");
    settings_.roughLevel = num("RoughLevel");
    settings_.useRoof = cfg.GetBool(key("UseRoof")) == BoolTri::True;
    settings_.textureSet = cfg.GetString(key("TextureSet"), "");
    settings_.fog = cfg.GetRGB(key("FogColourRGB"), settings_.fogR, settings_.fogG, settings_.fogB);
    cfg.GetRGB(key("HighFogColourRGB"), settings_.fogHighR, settings_.fogHighG, settings_.fogHighB);

    // Seed (if any) is applied before anything random happens in the level.
    if (const int seed = cfg.GetInt(key("Seed")).value_or(0)) random::Seed(uint32_t(seed));

    auto heights = MapFile::Load(cfg.GetString(key("SurfaceMap"), ""));
    if (!heights || !map_.Create(*heights, settings_.blockSize, settings_.roughLevel)) {
        RR_ERROR("Level %.*s: could not create terrain", int(levelId.size()), levelId.data());
        return false;
    }
    width_ = heights->width;
    height_ = heights->height;
    blocks_.assign(size_t(width_) * height_, Block{});
    for (Block& b : blocks_) b.random = random::Rand();

    if (!LoadPreDug(cfg.GetString(key("PredugMap"), ""))) return false;
    if (!LoadTerrain(cfg.GetString(key("TerrainMap"), ""))) return false;
    LoadCryOre(cfg.GetString(key("CryOreMap"), ""));
    LoadPaths(cfg.GetString(key("PathMap"), ""));
    for (int y = 0; y < height_; ++y)
        for (int x = 0; x < width_; ++x) UpdateSurface(x, y);
    map_.UpdateAllNormals();
    RR_INFO("Level %s: %dx%d blocks", settings_.id.c_str(), width_, height_);
    return true;
}

bool Level::LoadPreDug(const std::string& path) {
    auto m = MapFile::Load(path);
    if (!m || m->width != width_ || m->height != height_) return false;
    for (int y = 1; y < height_ - 1; ++y) {
        for (int x = 1; x < width_ - 1; ++x) {
            const int v = m->At(x, y);
            if (v == 1 || v == 3) {
                DestroyWall(x, y, true);
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) block(x + dx, y + dy).flags |= 0x4000000;  // initially exposed
            } else if (v == 2 || v == 4) {
                block(x, y).flags |= BlockFlag::HiddenCavern;
            }
            if (v == 3) block(x, y).flags2 |= BlockFlag2::SlugHole;
            if (v == 4) block(x, y).flags2 |= BlockFlag2::HiddenSlugHole;
        }
    }
    return true;
}

bool Level::LoadTerrain(const std::string& path) {
    auto m = MapFile::Load(path);
    if (!m || m->width != width_ || m->height != height_) return false;
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            int v = m->At(x, y);
            if (v == 5) v = 4;  // soil in the map file is loaded as loose rock
            Block& b = block(x, y);
            b.terrain = Terrain(v);
            if (b.terrain == Terrain::Lava) b.erodeStage = 4;
        }
    }
    return true;
}

void Level::LoadCryOre(const std::string& path) {
    auto m = MapFile::Load(path);
    if (!m || m->width != width_ || m->height != height_) return;
    for (int y = 0; y < height_; ++y)
        for (int x = 0; x < width_; ++x) block(x, y).cryOre = uint8_t(m->At(x, y));
}

void Level::LoadPaths(const std::string& path) {
    auto m = MapFile::Load(path);
    if (!m || m->width != width_ || m->height != height_) return;
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            Block& b = block(x, y);
            if (!(b.flags & BlockFlag::Ground)) continue;
            const int v = m->At(x, y);
            if (v == 2) {
                b.flags |= BlockFlag::Path;
            } else if (v == 1) {  // rubble
                b.flags &= ~(BlockFlag::CleanFloor | BlockFlag::Path | BlockFlag::LaidPath);
                b.flags |= BlockFlag::RubbleMask;
            }
        }
    }
}

bool Level::IsPathish(int x, int y) const {
    if (!InBounds(x, y)) return false;
    const Block& b = block(x, y);
    return (b.flags & BlockFlag::Path) || (b.flags & BlockFlag::Building);
}

uint8_t Level::ChoosePathTexture(int x, int y, uint8_t& direction, bool powered) const {
    // Neighbour mask: bit0 north (y-1), bit1 east, bit2 south, bit3 west.
    const int mask = (IsPathish(x, y - 1) ? 1 : 0) | (IsPathish(x + 1, y) ? 2 : 0) | (IsPathish(x, y + 1) ? 4 : 0) |
                     (IsPathish(x - 1, y) ? 8 : 0);
    // Piece: 1 cross, 2 straight, 3 corner, 4 T-junction, 5 end.
    static const uint8_t kPiece[16] = {5, 5, 5, 3, 5, 2, 3, 4, 5, 3, 2, 4, 3, 4, 4, 1};
    static const uint8_t kDir[16] = {0, 2, 3, 3, 0, 0, 0, 0, 1, 2, 1, 3, 1, 2, 1, 0};
    direction = kDir[mask];
    return uint8_t((powered ? 0x70 : 0x60) + kPiece[mask]);
}

void Level::UpdateSurface(int x, int y) {
    if (x >= width_ - 1 || y >= height_ - 1 || x < 0 || y < 0) return;
    Block& b = block(x, y);

    int dugCount = 0, lastDug = 0;
    int raised[4], raisedCount = 0;
    for (int c = 0; c < 4; ++c) {
        if (block(x + kCornerX[c], y + kCornerY[c]).pointState == 1) {
            ++dugCount;
            lastDug = c;
        } else {
            raised[raisedCount++] = c;
        }
    }
    const uint32_t before = b.flags;
    b.flags &= ~(BlockFlag::Ground | BlockFlag::Wall | BlockFlag::OuterCorner | BlockFlag::InnerCorner |
                 BlockFlag::Diagonal);
    constexpr uint32_t kShapeAndReinforce = BlockFlag::Ground | BlockFlag::Wall | BlockFlag::Reinforced |
                                            BlockFlag::OuterCorner | BlockFlag::InnerCorner | BlockFlag::Diagonal;
    Map3D& map = map_;
    const bool inMap = map.InBlocks(x, y);

    auto splitByHeights = [&]() {
        const float d02 = map.PointWorld(x, y).z - map.PointWorld(x + 1, y + 1).z;
        const float d13 = map.PointWorld(x + 1, y).z - map.PointWorld(x, y + 1).z;
        map.SetRotated(x, y, std::fabs(d13) < std::fabs(d02));
    };

    if (dugCount == 4) {
        b.flags = (before & ~kShapeAndReinforce) | BlockFlag::Visible | BlockFlag::Ground;
        b.direction = 0;
        if (b.terrain == Terrain::Lava || (b.flags & BlockFlag::Eroding)) {
            if (!(b.flags & BlockFlag::LavaSettled)) {
                if (b.erodeStage == 4) {
                    b.texture = 0x46;
                    map.SetWobble(x, y, true);
                    // TODO(effects): smoke on every third lava block.
                } else {
                    map.SetWobble(x, y, false);
                    static const uint8_t kStages[4] = {0x06, 0x16, 0x26, 0x36};
                    b.texture = kStages[b.erodeStage & 3];
                }
            } else {
                map.SetWobble(x, y, false);
                b.texture = 0x47;
            }
        } else if (b.terrain == Terrain::Lake) {
            b.texture = 0x45;
            map.SetWobble(x, y, true);
        } else if (!(b.flags & BlockFlag::Foundation)) {
            if (b.flags & BlockFlag::Path) {
                b.texture = ChoosePathTexture(x, y, b.direction, (b.flags2 & BlockFlag2::Powered) != 0);
            } else if (b.flags2 & BlockFlag2::SlugHole) {
                b.texture = 0x30;
            } else if (!(b.flags & BlockFlag::CleanFloor)) {
                static const uint8_t kRubble[4] = {0x13, 0x12, 0x11, 0x10};
                b.texture = kRubble[b.flags & BlockFlag::RubbleMask];
            } else {
                b.texture = (b.flags & BlockFlag::LaidPath) ? 0x61 : 0x00;
            }
        } else {
            b.texture = (b.flags2 & BlockFlag2::Powered) ? 0x66 : 0x76;
        }
    } else if (dugCount == 2) {
        b.flags |= BlockFlag::Visible | BlockFlag::Wall;
        // The two raised corners identify the wall: neighbours = straight wall, opposite = diagonal.
        static const int kPairs[6][2] = {{0, 1}, {1, 2}, {2, 3}, {0, 3}, {0, 2}, {1, 3}};
        int shape = 0;
        for (int i = 0; i < 6; ++i)
            if (kPairs[i][0] == raised[0] && kPairs[i][1] == raised[1]) shape = i;
        if (shape >= 4) {
            if (WallVariant(b.terrain) && b.terrain != Terrain::Water) b.texture = 0x77;
            else if (b.terrain == Terrain::Immovable) b.texture = 0x77;
            b.flags |= BlockFlag::Diagonal;
            b.direction = uint8_t(shape - 4);
            if (inMap) map.SetRotated(x, y, ((shape - 4) & 1) != 0);
        } else {
            b.direction = uint8_t(shape);
            if (b.flags & BlockFlag::Reinforced) {
                if (const int v = WallVariant(b.terrain)) b.texture = uint8_t(0x20 + v);
            } else if (const int v = WallVariant(b.terrain)) {
                b.texture = uint8_t(v);
            } else if (b.terrain == Terrain::OreSeam) {
                b.texture = 0x40;
            } else if (b.terrain == Terrain::CrystalSeam) {
                b.texture = 0x20;
            } else if (b.terrain == Terrain::RechargeSeam) {
                b.texture = 0x67;
            }
        }
    } else if (dugCount == 1 || dugCount == 3) {
        b.flags = (before & ~kShapeAndReinforce) | BlockFlag::Visible | BlockFlag::Wall;
        const int v = WallVariant(b.terrain);
        if (dugCount == 1) {
            if (v) b.texture = uint8_t(0x30 + v);
            b.flags |= BlockFlag::OuterCorner;
            b.direction = uint8_t((lastDug + 2) & 3);
        } else {
            if (v) b.texture = uint8_t(0x50 + v);
            b.flags |= BlockFlag::InnerCorner;
            b.direction = uint8_t(raised[0]);
        }
        if (inMap) splitByHeights();
    } else {
        // No corner dug: solid rock seen from above.
        const bool keep = (before & BlockFlag::Visible) && !WallVariant(b.terrain);
        b.flags = before & ~(BlockFlag::Ground | BlockFlag::Wall | BlockFlag::OuterCorner | BlockFlag::InnerCorner |
                             BlockFlag::Diagonal);
        if (!keep) b.texture = 0x70;
        b.direction = 0;
    }
    if (inMap) map.SetTexture(x, y, b.texture, b.direction);
}

bool Level::DestroyWall(int x, int y, bool hiddenCavern) {
    if (x <= 0 || y <= 0 || x >= width_ - 2 || y >= height_ - 2) return false;
    Block& b = block(x, y);
    if (b.flags & BlockFlag::Ground) return false;
    Map3D& map = map_;

    if (b.terrain == Terrain::OreSeam || b.terrain == Terrain::CrystalSeam) {
        // Seams are drilled in four stages, each lowering the corners by a quarter of the dig depth.
        for (int c = 0; c < 4; ++c) {
            Block& p = block(x + kCornerX[c], y + kCornerY[c]);
            if (p.pointState == 0) p.pointState = 5;
            if (p.pointState != 1) {
                --p.pointState;
                map.MoveVertex(x + kCornerX[c], y + kCornerY[c], settings_.digDepth * 0.25f);
            }
        }
        b.damage = 0.0f;  // each stage is drilled from scratch
        // Each stage drops one ore/crystal onto the first open floor beside the seam.
        // TODO(interface): "seam found" info messages when a seam is first touched.
        static const int kSideX[4] = {0, 1, 0, -1}, kSideY[4] = {-1, 0, 1, 0};
        for (int i = 0; i < 4; ++i) {
            const Block& n = block(x + kSideX[i], y + kSideY[i]);
            if (!(n.flags & BlockFlag::Ground) || n.terrain == Terrain::Lake || n.terrain == Terrain::Lava) continue;
            seamYields_.push_back({x + kSideX[i], y + kSideY[i], b.terrain == Terrain::CrystalSeam});
            break;
        }
        if (++b.seamDigs != 4) return false;
        b.terrain = Terrain::Soil;
        for (int c = 0; c < 4; ++c) block(x + kCornerX[c], y + kCornerY[c]).pointState = 1;
        return DestroyWall(x, y, hiddenCavern);
    }

    for (int c = 0; c < 4; ++c) {
        Block& p = block(x + kCornerX[c], y + kCornerY[c]);
        if (p.pointState == 0) {
            map.MoveVertex(x + kCornerX[c], y + kCornerY[c], settings_.digDepth);
            p.pointState = 1;
        }
    }
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (!dx && !dy) continue;
            Block& n = block(x + dx, y + dy);
            if (!(n.flags & BlockFlag::Ground)) {
                n.flags |= BlockFlag::Wall;
                n.flags &= ~0x40000u;
            }
        }
    }
    b.brokenDirection = b.direction;
    b.flags |= BlockFlag::Ground;
    b.flags &= ~BlockFlag::Reinforced;
    b.flags |= 0x400000;
    if (hiddenCavern) {
        b.flags |= BlockFlag::CleanFloor;
    } else {
        // TODO(game): uncover neighbouring hidden caverns, messages, rubble clearing tasks.
        b.flags &= ~(BlockFlag::CleanFloor | BlockFlag::Path | BlockFlag::LaidPath);
        b.flags |= BlockFlag::RubbleMask;
    }
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) UpdateSurface(x + dx, y + dy);
    if (b.flags2 & BlockFlag2::HiddenSlugHole) {
        b.flags2 = (b.flags2 & ~BlockFlag2::HiddenSlugHole) | BlockFlag2::SlugHole;
        UpdateSurface(x, y);
    }
    exposed_.emplace_back(x, y);
    if (!hiddenCavern) {
        // Breaking into a hidden cavern opens all of it.
        // TODO(interface): info message, "cavern discovered" text and reward counter.
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (!(block(x + dx, y + dy).flags & BlockFlag::HiddenCavern)) continue;
                UncoverHiddenCavern(x + dx, y + dy);
                ++cavernsDiscovered_;
            }
        }
    }
    return true;
}

void Level::UncoverHiddenCavern(int x, int y) {
    std::vector<std::pair<int, int>> wave{{x, y}}, next;
    while (!wave.empty()) {
        next.clear();
        for (const auto& [cx, cy] : wave) {
            for (int ny = cy - 1; ny <= cy + 1; ++ny) {
                for (int nx = cx - 1; nx <= cx + 1; ++nx) {
                    if (!InBounds(nx, ny) || !(block(nx, ny).flags & BlockFlag::HiddenCavern)) continue;
                    DestroyWall(nx, ny, true);
                    block(nx, ny).flags &= ~BlockFlag::HiddenCavern;
                    if (nx != cx || ny != cy) next.emplace_back(nx, ny);
                }
            }
        }
        wave.swap(next);
    }
}

bool Level::ClearRubbleLayer(int x, int y) {
    Block& b = block(x, y);
    if (b.flags & BlockFlag::CleanFloor) return true;
    const uint32_t layers = b.flags & BlockFlag::RubbleMask;
    if (layers != 0) {
        b.flags = (b.flags & ~BlockFlag::RubbleMask) | (layers - 1);
    } else {
        // Last layer gone. TODO(effects): dust particle; info messages and menus for the block.
        b.flags |= BlockFlag::CleanFloor;
    }
    UpdateSurface(x, y);
    return layers == 0;
}

void Level::SetBuildingBlock(int x, int y, BuildingBlock kind, bool generatesPower) {
    if (!InBounds(x, y)) return;
    Block& b = block(x, y);
    switch (kind) {
    case BuildingBlock::Solid:
        b.flags |= BlockFlag::Building | BlockFlag::Foundation;
        if (generatesPower) b.flags2 |= BlockFlag2::GeneratePower;
        break;
    case BuildingBlock::Path: b.flags |= BlockFlag::Path | BlockFlag::Foundation | BlockFlag::BuildingPath; break;
    case BuildingBlock::ToolStore:
        b.flags |= BlockFlag::Foundation | BlockFlag::BuildingPath;
        b.flags2 |= BlockFlag2::ToolStoreEntrance;
        break;
    }
    UpdateSurface(x, y);
}

}  // namespace rr::world
