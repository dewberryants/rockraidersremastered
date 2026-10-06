#pragma once

#include <memory>

#include "app/App.h"

namespace rr {

// First scene to run, honouring --scene for debugging.
std::unique_ptr<Scene> CreateStartScene();

}  // namespace rr
