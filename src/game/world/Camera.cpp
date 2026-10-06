#include "game/world/Camera.h"

#include "game/world/Map3D.h"

namespace rr::world {

TopDownCamera::View TopDownCamera::Compute(const Map3D* map) const {
    const float groundZ = map ? map->WorldZ(x, y) : 0.0f;
    const Mat4 frame = Mat4::Translate({x, y, groundZ}) * Mat4::Rotate({0, 0, 1}, -rotation) *
                       Mat4::Rotate({1, 0, 0}, -tilt);
    View v;
    v.target = {x, y, groundZ};
    v.eye = frame.TransformPoint({0, 0, -zoom});
    v.forward = frame.TransformDir({0, 0, 1});
    v.up = frame.TransformDir({0, 1, 0});
    return v;
}

}  // namespace rr::world
