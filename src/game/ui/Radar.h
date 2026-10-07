#pragma once

#include "core/Math3D.h"
#include "gfx/Renderer2D.h"

namespace rr {

class ObjectList;
namespace world {
class Level;
}

// The radar's map view (RadarMap_*): blocks as coloured squares around a centre point, objects as
// small squares on top. Screen rectangle and zoom follow Lego_Initialise (16, 13, 151 x 151;
// zoom 15 screen units per block, adjustable 10..20).
class RadarMap {
public:
    RectF rect{16.0f, 13.0f, 151.0f, 151.0f};
    float zoom = 15.0f;

    void ZoomIn(float step) { zoom = std::min(zoom + step, 20.0f); }
    void ZoomOut(float step) { zoom = std::max(zoom - step, 10.0f); }

    void Draw(Renderer2D& r, const world::Level& level, const ObjectList& objects, Vec2 centre) const;
};

}  // namespace rr
