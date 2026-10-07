#include "game/Effects.h"

#include <cmath>

#include "config/Config.h"
#include "core/Log.h"
#include "fs/Path.h"
#include "gfx/Renderer3D.h"
#include "gfx/model/LwsScene.h"
#include "gfx/model/ModelDraw.h"

namespace rr {

void RockFallEffects::Load(const Config& cfg, std::string_view gameName) {
    styles_.clear();
    current_ = 0;
    const std::string shared = cfg.GetString(Config::Id(gameName, "Main", "SharedObjects"), "");
    // "Rock  Item_Null,MiscAnims\RockFall\rock3sides,...OutsideCorner,...vtunnel"
    for (Config::ItemRef it = cfg.FindArray(Config::Id(gameName, "RockFallStyles")); it != Config::kNone;
         it = cfg.Next(it)) {
        Style s;
        s.name = cfg.item(it).key;
        const auto parts = SplitTokens(cfg.item(it).value, ",");
        if (!parts.empty()) s.tumbleNull = std::string(parts[0]);
        for (size_t i = 1; i < parts.size() && i <= kTypes; ++i) {
            s.scenes[i - 1] = LoadLws(std::string(parts[i]), shared);
            if (!s.scenes[i - 1]) RR_WARN("Rock fall scene %s not loaded", std::string(parts[i]).c_str());
        }
        styles_.push_back(std::move(s));
    }
    RR_INFO("Rock fall styles: %zu", styles_.size());
}

void RockFallEffects::SetStyle(std::string_view name) {
    StopAll();
    current_ = 0;
    for (size_t i = 0; i < styles_.size(); ++i)
        if (EqualsNoCase(styles_[i].name, name)) current_ = i;
}

bool RockFallEffects::Spawn(Type type, BlockPos block, const Vec3f& pos, Vec2 dir) {
    if (current_ >= styles_.size() || !styles_[current_].scenes[type]) return false;
    for (Instance& in : styles_[current_].instances[type]) {
        if (in.playing) continue;
        in.playing = true;
        in.time = 0.0f;
        in.pos = pos;
        in.heading = std::atan2(dir.x, dir.y);  // facing dir, up = -Z
        in.block = block;
        return true;
    }
    return false;
}

void RockFallEffects::Update(float elapsed, std::vector<BlockPos>& finished) {
    if (current_ >= styles_.size()) return;
    Style& s = styles_[current_];
    for (int t = 0; t < kTypes; ++t) {
        if (!s.scenes[t]) continue;
        const float frames = float(s.scenes[t]->frameCount());
        for (Instance& in : s.instances[t]) {
            if (!in.playing) continue;
            in.time += elapsed;
            if (in.time > frames) {  // Container_MoveAnimation ran past the end
                in.playing = false;
                finished.push_back(in.block);
            }
        }
    }
}

void RockFallEffects::Draw(Renderer3D& r) const {
    if (current_ >= styles_.size()) return;
    const Style& s = styles_[current_];
    static LwsPose pose;
    for (int t = 0; t < kTypes; ++t) {
        if (!s.scenes[t]) continue;
        for (const Instance& in : s.instances[t]) {
            if (!in.playing) continue;
            EvaluateLws(*s.scenes[t], in.time, false, pose);
            DrawPose(r, *s.scenes[t], pose, ObjectMatrix(in.pos, in.heading));
        }
    }
}

void RockFallEffects::StopAll() {
    for (Style& s : styles_)
        for (auto& type : s.instances)
            for (Instance& in : type) in.playing = false;
}

}  // namespace rr
