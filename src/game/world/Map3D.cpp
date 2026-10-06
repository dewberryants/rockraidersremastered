#include "game/world/Map3D.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "core/Random.h"

namespace rr::world {

namespace {
// Corner offsets in point units for c0..c3.
constexpr int kCornerX[4] = {0, 1, 1, 0};
constexpr int kCornerY[4] = {0, 0, 1, 1};
// Texture coordinates by rotated corner slot: (u, v) = (bit1, bit0) of {1, 3, 2, 0}.
constexpr uint8_t kUvBits[4] = {1, 3, 2, 0};
}  // namespace

bool Map3D::Create(const MapFile& heights, float blockSize, float roughLevel) {
    pointsW_ = heights.width;
    pointsH_ = heights.height;
    if (pointsW_ < 2 || pointsH_ < 2) return false;
    blockSize_ = blockSize;
    roughLevel_ = roughLevel;
    offsetX_ = -(float(pointsW_ - 1) * blockSize * 0.5f);
    offsetY_ = float(pointsH_ - 1) * blockSize * 0.5f;
    minZ_ = 0.0f;

    points_.assign(size_t(pointsW_) * pointsH_, Point{});
    for (int y = 0; y < pointsH_; ++y) {
        for (int x = 0; x < pointsW_; ++x) {
            Point& p = points_[size_t(y) * pointsW_ + x];
            p.height = uint8_t(heights.At(x, y));
            // Random unit vector scaled by 0.3; the draw order keeps the RNG in step with the original.
            float wx = random::RandRange(0.0f, 1.0f);
            const float wy = random::RandRange(0.0f, 1.0f);
            const float inv = 1.0f / std::sqrt(wx * wx + wy * wy);
            p.wobble = {wx * inv * 0.3f, wy * inv * 0.3f};
        }
    }

    quads_.assign(size_t(blocksW()) * blocksH(), Quad{});
    for (int by = 0; by < blocksH(); ++by) {
        for (int bx = 0; bx < blocksW(); ++bx) {
            Quad& q = quad(bx, by);
            for (int c = 0; c < 4; ++c) {
                const int px = bx + kCornerX[c], py = by + kCornerY[c];
                const float z = -float(points_[size_t(py) * pointsW_ + px].height) * roughLevel;
                q.pos[c] = {float(px) * blockSize + offsetX_, offsetY_ - float(py) * blockSize, z};
                minZ_ = std::min(minZ_, z);
            }
        }
    }
    // Split each quad along the flatter diagonal.
    for (int by = 0; by < blocksH(); ++by) {
        for (int bx = 0; bx < blocksW(); ++bx) {
            const int h0 = points_[size_t(by) * pointsW_ + bx].height;
            const int h2 = points_[size_t(by + 1) * pointsW_ + bx + 1].height;
            const int h1 = points_[size_t(by) * pointsW_ + bx + 1].height;
            const int h3 = points_[size_t(by + 1) * pointsW_ + bx].height;
            if (std::abs(h0 - h2) < std::abs(h1 - h3)) quad(bx, by).rotated = true;
        }
    }
    UpdateAllNormals();
    return true;
}

Vec2 Map3D::BlockCentre(int bx, int by) const {
    return {blockSize_ * 0.5f + float(bx) * blockSize_ + offsetX_, offsetY_ - (blockSize_ * 0.5f + float(by) * blockSize_)};
}

Vec3f Map3D::PointWorld(int px, int py) const {
    // Read the point from any quad that uses it.
    const int bx = std::min(px, blocksW() - 1), by = std::min(py, blocksH() - 1);
    for (int c = 0; c < 4; ++c) {
        if (bx + kCornerX[c] == px && by + kCornerY[c] == py) return quad(bx, by).pos[c];
    }
    return {};
}

void Map3D::MoveVertex(int px, int py, float dz) {
    for (int c = 0; c < 4; ++c) {
        const int bx = px - kCornerX[c], by = py - kCornerY[c];
        if (!InBlocks(bx, by)) continue;
        quad(bx, by).pos[c].z += dz;
    }
    UpdateNormalsAround(px, py);
}

void Map3D::SetRotated(int bx, int by, bool on) {
    if (!InBlocks(bx, by)) return;
    quad(bx, by).rotated = on;
    ComputeQuadNormals(bx, by);
}

void Map3D::SetTexture(int bx, int by, uint8_t texture, uint8_t direction) {
    if (!InBlocks(bx, by)) return;
    Quad& q = quad(bx, by);
    q.texture = texture;
    q.direction = direction;
    q.hidden = false;
    for (int c = 0; c < 4; ++c) {
        const uint8_t bits = kUvBits[(c - direction) & 3];
        q.uv[c] = {float(bits >> 1), float(bits & 1)};
    }
}

void Map3D::SetWobble(int bx, int by, bool on) {
    if (!InBlocks(bx, by)) return;
    Quad& q = quad(bx, by);
    if (on == q.wobble) return;
    q.wobble = on;
    if (on) {
        wobbleList_.emplace_back(bx, by);
        return;
    }
    for (size_t i = 0; i < wobbleList_.size(); ++i) {
        if (wobbleList_[i] == std::make_pair(bx, by)) {
            wobbleList_[i] = wobbleList_.back();  // swap-remove, as the original
            wobbleList_.pop_back();
            return;
        }
    }
}

void Map3D::UpdateUVs(float elapsed) {
    if (elapsed <= 0.001f) return;
    // Each corner eases towards a target (point wobble + corner base); when it gets close the
    // point picks a new random drift direction.
    static const Vec2 kBase[4] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
    const float p = 50.0f / elapsed;
    const float f = 1.0f / (p + 1.0f);
    for (const auto& [bx, by] : wobbleList_) {
        Quad& q = quad(bx, by);
        for (int c = 0; c < 4; ++c) {
            Point& pt = points_[size_t(by + kCornerY[c]) * pointsW_ + bx + kCornerX[c]];
            Vec2 target{pt.wobble.x + kBase[c].x, pt.wobble.y + kBase[c].y};
            Vec2& uv = q.uv[c];
            const float dist = std::sqrt((uv.x - target.x) * (uv.x - target.x) + (uv.y - target.y) * (uv.y - target.y));
            Vec2 from = target;
            if (dist <= 0.36f) {
                from = uv;
                if (dist < 0.05f) {
                    const float rx = random::RandRange(0.0f, 1.0f);
                    const float ry = random::RandRange(0.0f, 1.0f);
                    const float inv = 1.0f / std::sqrt(rx * rx + ry * ry);
                    pt.wobble = {(pt.wobble.x + rx * inv) * 0.3f, (pt.wobble.y + ry * inv) * 0.3f};
                    target = {pt.wobble.x + kBase[c].x, pt.wobble.y + kBase[c].y};
                }
            }
            uv = {f * (p * from.x + target.x), f * (p * from.y + target.y)};
        }
    }
}

void Map3D::ComputeQuadNormals(int bx, int by) {
    Quad& q = quad(bx, by);
    auto faceNormal = [&](int a, int b, int c) { return Normalize(Cross(q.pos[b] - q.pos[a], q.pos[c] - q.pos[a])); };
    for (auto& n : q.normal) n = {0, 0, 0};
    if (!q.rotated) {
        const Vec3f n0 = faceNormal(0, 1, 3), n1 = faceNormal(1, 2, 3);
        q.normal[0] += n0;
        q.normal[1] += n0 + n1;
        q.normal[3] += n0 + n1;
        q.normal[2] += n1;
    } else {
        const Vec3f n0 = faceNormal(3, 0, 2), n1 = faceNormal(0, 1, 2);
        q.normal[3] += n0;
        q.normal[0] += n0 + n1;
        q.normal[2] += n0 + n1;
        q.normal[1] += n1;
    }
}

void Map3D::UpdateNormalsAround(int px, int py) {
    for (int by = py - 2; by <= py + 1; ++by)
        for (int bx = px - 2; bx <= px + 1; ++bx)
            if (InBlocks(bx, by)) ComputeQuadNormals(bx, by);
}

void Map3D::UpdateAllNormals() {
    for (int by = 0; by < blocksH(); ++by)
        for (int bx = 0; bx < blocksW(); ++bx) ComputeQuadNormals(bx, by);
    // Smooth: a shared point's normal is the sum over every quad using it (Gouraud shading).
    std::vector<Vec3f> sum(points_.size(), Vec3f{});
    for (int by = 0; by < blocksH(); ++by)
        for (int bx = 0; bx < blocksW(); ++bx)
            for (int c = 0; c < 4; ++c)
                sum[size_t(by + kCornerY[c]) * pointsW_ + bx + kCornerX[c]] += quad(bx, by).normal[c];
    for (int by = 0; by < blocksH(); ++by)
        for (int bx = 0; bx < blocksW(); ++bx)
            for (int c = 0; c < 4; ++c)
                quad(bx, by).normal[c] = Normalize(sum[size_t(by + kCornerY[c]) * pointsW_ + bx + kCornerX[c]]);
}

bool Map3D::WorldToBlock(float x, float y, int& bx, int& by) const {
    bx = int(std::floor((x - offsetX_) / blockSize_));
    by = int(std::floor((offsetY_ - y) / blockSize_));
    return InBlocks(bx, by);
}

float Map3D::WorldZ(float x, float y) const {
    int bx, by;
    if (!WorldToBlock(x, y, bx, by)) return 0.0f;
    const Quad& q = quad(bx, by);
    // Local coordinates inside the quad: u east, v south, both 0..1.
    const float u = (x - (float(bx) * blockSize_ + offsetX_)) / blockSize_;
    const float v = ((offsetY_ - float(by) * blockSize_) - y) / blockSize_;
    const float z0 = q.pos[0].z, z1 = q.pos[1].z, z2 = q.pos[2].z, z3 = q.pos[3].z;
    if (!q.rotated) {  // triangles (c0,c1,c3) and (c1,c2,c3), split along u + v = 1
        if (u + v <= 1.0f) return z0 + (z1 - z0) * u + (z3 - z0) * v;
        return z2 + (z3 - z2) * (1.0f - u) + (z1 - z2) * (1.0f - v);
    }
    // rotated: triangles (c3,c0,c2) and (c0,c1,c2), split along u = v
    if (u >= v) return z0 + (z1 - z0) * u + (z2 - z1) * v;
    return z0 + (z2 - z3) * u + (z3 - z0) * v;
}

}  // namespace rr::world
