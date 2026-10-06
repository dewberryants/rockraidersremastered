#pragma once

#include "core/Math3D.h"

namespace rr::world {

class Map3D;

// The top-down game camera (LegoCamera type 1). A ground frame at the target position is rotated
// about Z by -rotation; a child frame tilts about X by -tilt; the eye sits `zoom` units behind
// the tilted frame looking along +Z. Angles are radians; tilt 0 looks straight down.
struct TopDownCamera {
    float x = 0, y = 0;     // world position of the view target
    float rotation = 0;     // heading
    float tilt = 0.8f;      // the level start value (Lego_LoadOLObjectList)
    float zoom = 200.0f;    // distance from the target (Camera_Create default)
    float minZoom = 150.0f, maxZoom = 250.0f;
    float minTilt = 0.0f, maxTilt = DegToRad(60.0f);

    struct View {
        Vec3f eye, forward, up, target;
    };
    View Compute(const Map3D* map) const;
};

}  // namespace rr::world
