#include "gfx/model/ModelDraw.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

namespace rr {

Mat4 ObjectMatrix(const Vec3f& position, float heading, float scale) {
    const Vec3f dir{std::sin(heading), std::cos(heading), 0.0f};
    const Vec3f up{0, 0, -1};
    const Vec3f right = Cross(up, dir);
    Mat4 m;
    m.m[0] = right.x * scale; m.m[1] = right.y * scale; m.m[2] = right.z * scale;
    m.m[4] = up.x * scale; m.m[5] = up.y * scale; m.m[6] = up.z * scale;
    m.m[8] = dir.x * scale; m.m[9] = dir.y * scale; m.m[10] = dir.z * scale;
    m.m[12] = position.x; m.m[13] = position.y; m.m[14] = position.z;
    return m;
}

namespace {
uint32_t PackColour(const Vec3f& c, float alpha) {
    auto b = [](float v) { return uint32_t(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
    return b(c.x) | (b(c.y) << 8) | (b(c.z) << 16) | (b(alpha) << 24);
}
}  // namespace

void DrawMesh(Renderer3D& r, const Mesh& mesh, const Mat4& world, float dissolve) {
    static std::vector<Vertex3D> tris;
    for (const MeshGroup& g : mesh.groups) {
        const MeshMaterial& mat = g.material;
        const float alpha = mat.alpha * (1.0f - dissolve);
        if (alpha <= 0.0f) continue;
        // Diffuse plus self-illumination, as a vertex colour multiplier.
        const uint32_t colour = PackColour(mat.diffuse + mat.emissive, alpha);
        tris.clear();
        tris.reserve(g.indices.size());
        for (uint32_t idx : g.indices) {
            Vertex3D v = g.vertices[idx];
            v.pos = world.TransformPoint(v.pos);
            v.normal = Normalize(world.TransformDir(v.normal));
            v.colour = colour;
            tris.push_back(v);
        }
        static const bool debugNoTex = std::getenv("RR_DEBUG_NOTEX") != nullptr;
        // Additive groups always go to the alpha pass; others only while see-through
        // (Mesh_SetGroupMaterialValues).
        const Blend blend = mat.additive ? Blend::Additive : alpha < 0.999f ? Blend::Alpha : Blend::Opaque;
        r.DrawTriangles(debugNoTex ? nullptr : mat.texture, tris.data(), tris.size(), blend);
    }
}

void DrawPose(Renderer3D& r, const LwsScene& scene, const LwsPose& pose, const Mat4& object) {
    for (size_t i = 0; i < scene.nodes.size() && i < pose.world.size(); ++i) {
        const LwsNode& node = scene.nodes[i];
        if (!node.mesh || node.unseenByCamera) continue;
        DrawMesh(r, *node.mesh, object * pose.world[i], pose.dissolve[i]);
    }
}

}  // namespace rr
