#include "gfx/model/ActivityModel.h"

#include "config/Config.h"
#include "core/Log.h"
#include "fs/Path.h"

namespace rr {

const Activity* ActivityModel::Find(std::string_view activityId) const {
    auto it = activities.find(ToLower(activityId));
    return it == activities.end() ? nullptr : it->second.get();
}

std::shared_ptr<ActivityModel> LoadActivityModel(const std::string& folder, const std::string& sharedObjects) {
    static std::unordered_map<std::string, std::shared_ptr<ActivityModel>> cache;
    const std::string cacheKey = NormalizeGamePath(folder);
    if (auto it = cache.find(cacheKey); it != cache.end()) return it->second;

    const size_t slash = folder.find_last_of("\\/");
    const std::string leaf = slash == std::string::npos ? folder : folder.substr(slash + 1);
    Config ae;
    if (!ae.Load(folder + "\\" + leaf + ".ae")) {
        cache[cacheKey] = nullptr;
        return nullptr;
    }
    auto model = std::make_shared<ActivityModel>();
    model->folder = folder;
    model->scale = ae.GetFloat("Lego*::Scale").value_or(1.0f);
    model->cameraNull = ae.GetString("Lego*::CameraNullName", "");
    model->drillNull = ae.GetString("Lego*::DrillNullName", "");
    model->carryNull = ae.GetString("Lego*::CarryNullName", "");

    for (Config::ItemRef it = ae.FindArray("Lego*::Activities"); it != Config::kNone; it = ae.Next(it)) {
        std::string_view id = ae.item(it).key;
        if (!id.empty() && id[0] == '!') id.remove_prefix(1);  // optional (dropped by -reduceanimation)
        const std::string block = ae.item(it).value;
        const std::string base = Config::Id("Lego*", block);
        const auto file = ae.GetString(Config::Id(base, "FILE"));
        if (!file) continue;
        auto act = std::make_shared<Activity>();
        act->name = block;
        act->transCoef = ae.GetFloat(Config::Id(base, "TRANSCOEF")).value_or(0.0f);
        act->trigger = ae.GetInt(Config::Id(base, "TRIGGER")).value_or(0);
        act->sample = ae.GetString(Config::Id(base, "SAMPLE"), "");
        const BoolTri looping = ae.GetBool(Config::Id(base, "LOOPING"));
        act->looping = looping != BoolTri::False;
        act->scene = LoadLws(folder + "\\" + std::string(*file), sharedObjects);
        if (!act->scene) {
            RR_DEBUG("%s: activity %s scene %s missing", folder.c_str(), block.c_str(), std::string(*file).c_str());
            continue;
        }
        model->activities[ToLower(id)] = std::move(act);
    }
    cache[cacheKey] = model;
    return model;
}

}  // namespace rr
