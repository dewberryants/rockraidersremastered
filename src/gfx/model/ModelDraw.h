#pragma once

#include "gfx/Renderer3D.h"
#include "gfx/model/LwsScene.h"

namespace rr {

// World matrix of a game object: model +Z points along the heading, +Y up (world -Z).
// Heading 0 faces north (+Y); positive headings turn towards east.
Mat4 ObjectMatrix(const Vec3f& position, float heading, float scale = 1.0f);

// Submits a posed scene (or a single mesh) to the renderer. A colour, when given, replaces the
// diffuse and emissive colour of every group (Container_Mesh_SetColourAlpha + _SetEmissive).
void DrawPose(Renderer3D& r, const LwsScene& scene, const LwsPose& pose, const Mat4& object,
              const Vec3f* colour = nullptr);
void DrawMesh(Renderer3D& r, const Mesh& mesh, const Mat4& world, float dissolve = 0.0f,
              const Vec3f* colour = nullptr);

}  // namespace rr
