#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "gfx/model/LwsScene.h"

namespace rr {

// One animation of an object (an entry of the "Activities" block in its .ae file).
struct Activity {
    std::string name;          // e.g. "Stand"
    std::shared_ptr<LwsScene> scene;
    float transCoef = 0.0f;    // movement per frame while the animation plays
    bool looping = true;
    int trigger = 0;           // frame that fires the activity's action
    std::string sample;        // sound played on the trigger
};

// An object's activity file (.ae): scale, special null names and its activities, keyed by the
// game's activity ids ("Activity_Stand" -> block "Stand" -> FILE VLPstand -> VLPstand.lws).
struct ActivityModel {
    std::string folder;        // e.g. "Mini-Figures\Pilot"
    float scale = 1.0f;
    std::string cameraNull, drillNull, carryNull;
    // Building footprint in blocks relative to the origin (always first); a point listed twice
    // in a row is the building's path tile.
    std::vector<std::pair<int, int>> shape;
    std::unordered_map<std::string, std::shared_ptr<Activity>> activities;  // lower-case "activity_stand"

    const Activity* Find(std::string_view activityId) const;
};

// Loads "<folder>\<last folder part>.ae" (cached by folder).
std::shared_ptr<ActivityModel> LoadActivityModel(const std::string& folder, const std::string& sharedObjects);

}  // namespace rr
