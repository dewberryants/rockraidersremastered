#include "gfx/model/ActivityModel.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "config/Config.h"
#include "core/Log.h"
#include "fs/Path.h"
#include "gfx/model/Mesh.h"

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
    model->carryNullFrames = ae.GetInt("Lego*::CarryNullFrames").value_or(0);
    model->depositNull = ae.GetString("Lego*::DepositNullName", "");
    model->toolNull = ae.GetString("Lego*::ToolNullName", "");
    model->wheelNull = ae.GetString("Lego*::WheelNullName", "");
    model->driverNull = ae.GetString("Lego*::DriverNullName", "");
    model->wheelRadius = ae.GetFloat("Lego*::WheelRadius").value_or(0.0f);
    if (const std::string wheel = ae.GetString("Lego*::WheelMesh", ""); !wheel.empty())
        model->wheelMesh = LoadLwo(folder + "\\" + wheel);
    for (uint32_t lv = 0; lv < 16; ++lv) {
        char id[64];
        std::snprintf(id, sizeof(id), "Lego*::Upgrades::Level%u%u%u%u", (lv >> 3) & 1, (lv >> 2) & 1, (lv >> 1) & 1,
                      lv & 1);
        for (Config::ItemRef it = ae.FindArray(id); it != Config::kNone; it = ae.Next(it)) {
            // "sbucket  normal_bucket_null,1[,weapon]"
            const auto t = SplitTokens(ae.item(it).value, ",");
            if (t.size() < 2) continue;
            ActivityModel::UpgradePart p;
            p.levels = lv;
            p.part = ae.item(it).key;
            p.nullName = std::string(t[0]);
            p.instance = std::max(0, std::atoi(std::string(t[1]).c_str()) - 1);
            if (t.size() > 2) p.weapon = std::string(t[2]);
            model->upgrades.push_back(std::move(p));
        }
    }
    model->shape.emplace_back(0, 0);
    if (auto shape = ae.GetString("Lego*::Shape")) {
        for (std::string_view pt : SplitTokens(*shape, ":")) {
            const auto xy = SplitTokens(pt, ",");
            if (xy.size() == 2)
                model->shape.emplace_back(std::atoi(std::string(xy[0]).c_str()), std::atoi(std::string(xy[1]).c_str()));
        }
    }

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
