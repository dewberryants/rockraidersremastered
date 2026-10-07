#pragma once

#include <vector>

#include "game/objects/Routing.h"

namespace rr {

class ObjectList;
namespace world {
class Level;
}

// The power network (LegoObject_UpdatePowerConsumption, Construction_PowerGrid_*): power stations
// with crystals in store light up the power paths and building blocks connected to them; every
// other building draws its CrystalDrain from a station it is connected to, while crystals last.
class PowerGrid {
public:
    void Request() { dirty_ = true; }
    // Recomputes the network when something changed; returns the crystals in use.
    int Update(world::Level& level, ObjectList& objects, int storedCrystals);
    int used() const { return used_; }

private:
    bool dirty_ = true;
    int lastStored_ = -1;
    int used_ = 0;
    std::vector<BlockPos> powered_;
};

}  // namespace rr
