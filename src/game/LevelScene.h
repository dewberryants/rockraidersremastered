#pragma once

#include <memory>
#include <string>

#include "app/App.h"

namespace rr {

// In-game scene: loads a level and runs it.
std::unique_ptr<Scene> CreateLevelScene(const std::string& levelId);

}  // namespace rr
