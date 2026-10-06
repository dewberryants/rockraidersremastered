#pragma once

#include <cstdint>
#include <vector>

#include "core/Math3D.h"
#include "game/world/MapFile.h"

namespace rr::world {

// Terrain mesh of a level (the original Map3D). A height map of W x H points gives
// (W-1) x (H-1) quads; quad (bx, by) is the visible surface of block (bx, by). Its corners are
// c0 = (bx, by), c1 = (bx+1, by), c2 = (bx+1, by+1), c3 = (bx, by+1) in point coordinates.
//
// World placement matches the original: x = px * blockSize + offsetX, y = offsetY - py * blockSize,
// z = -height * roughLevel (+Z is down). Each quad has its own copy of its corner positions so that
// shared points are moved in every quad that uses them (MoveVertex).
class Map3D {
public:
    struct Quad {
        Vec3f pos[4];       // canonical corner order c0..c3
        Vec3f normal[4];    // per-corner normals for Gouraud lighting
        Vec2 uv[4];         // per-corner texture coordinates
        uint8_t texture = 0;
        uint8_t direction = 0;
        bool rotated = false;  // triangulation split along c0-c2 instead of c1-c3
        bool hidden = true;
        bool wobble = false;   // animated UVs (lava, water)
    };
    struct Point {
        uint8_t height = 0;
        Vec2 wobble;  // random UV drift direction (scaled 0.3)
    };

    // Consumes the game RNG exactly like Map3D_Create (two RandRange per point, row-major).
    bool Create(const MapFile& heights, float blockSize, float roughLevel);

    int pointsW() const { return pointsW_; }
    int pointsH() const { return pointsH_; }
    int blocksW() const { return pointsW_ - 1; }
    int blocksH() const { return pointsH_ - 1; }
    float blockSize() const { return blockSize_; }
    float minZ() const { return minZ_; }

    bool InBlocks(int bx, int by) const { return bx >= 0 && by >= 0 && bx < blocksW() && by < blocksH(); }
    Quad& quad(int bx, int by) { return quads_[size_t(by) * blocksW() + bx]; }
    const Quad& quad(int bx, int by) const { return quads_[size_t(by) * blocksW() + bx]; }
    const std::vector<Quad>& quads() const { return quads_; }

    Vec2 BlockCentre(int bx, int by) const;
    Vec3f PointWorld(int px, int py) const;  // current position of a shared point
    // Moves point (px, py) by dz along Z in every quad that uses it, then refreshes normals.
    void MoveVertex(int px, int py, float dz);
    void SetRotated(int bx, int by, bool on);
    void SetTexture(int bx, int by, uint8_t texture, uint8_t direction);
    void UpdateAllNormals();
    // Drifting texture coordinates for water and lava (Map3D_SetBlockUVWobbles / UpdateTextureUVs).
    // The list order is kept like the original's because re-targeting draws from the game RNG.
    void SetWobble(int bx, int by, bool on);
    void UpdateUVs(float elapsed);

    // Ground height (Z) at a world position, interpolated over the triangle under it.
    float WorldZ(float x, float y) const;
    bool WorldToBlock(float x, float y, int& bx, int& by) const;

private:
    void UpdateNormalsAround(int px, int py);
    void ComputeQuadNormals(int bx, int by);

    int pointsW_ = 0, pointsH_ = 0;
    float blockSize_ = 40.0f, roughLevel_ = 0.0f;
    float offsetX_ = 0.0f, offsetY_ = 0.0f;
    float minZ_ = 0.0f;
    std::vector<Point> points_;
    std::vector<Quad> quads_;
    std::vector<std::pair<int, int>> wobbleList_;
};

}  // namespace rr::world
