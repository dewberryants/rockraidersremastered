#include "gfx/model/LwsScene.h"

#include <cmath>
#include <cstdlib>
#include <functional>
#include <sstream>
#include <unordered_map>

#include "core/Log.h"
#include "fs/FileSystem.h"
#include "fs/Path.h"

namespace rr {

int LwsScene::FindNode(std::string_view name) const {
    for (size_t i = 0; i < nodes.size(); ++i)
        if (EqualsNoCase(nodes[i].name, name)) return int(i);
    return -1;
}

namespace {

std::string FileNamePart(const std::string& path) {
    const size_t slash = path.find_last_of("\\/");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string StripExtension(const std::string& name) {
    const size_t dot = name.find_last_of('.');
    return dot == std::string::npos ? name : name.substr(0, dot);
}

// Meshes are shared between scenes that use the same file.
std::shared_ptr<Mesh> LoadSceneMesh(const std::string& dir, const std::string& sharedDir, const std::string& file) {
    static std::unordered_map<std::string, std::weak_ptr<Mesh>> cache;
    for (const std::string& base : {dir, sharedDir}) {
        if (base.empty() && &base != &dir) continue;
        const std::string path = base.empty() ? file : base + "\\" + file;
        const std::string key = NormalizeGamePath(path);
        if (auto it = cache.find(key); it != cache.end())
            if (auto m = it->second.lock()) return m;
        if (auto m = LoadLwo(path)) {
            cache[key] = m;
            return m;
        }
    }
    return nullptr;
}

}  // namespace

std::shared_ptr<LwsScene> LoadLws(const std::string& pathIn, const std::string& sharedDir) {
    std::string path = pathIn;
    if (path.size() < 4 || !EqualsNoCase(std::string_view(path).substr(path.size() - 4), ".lws")) path += ".lws";
    auto text = Fs().ReadText(path);
    if (!text || text->compare(0, 4, "LWSC") != 0) {
        RR_DEBUG("LWS %s missing or invalid", path.c_str());
        return nullptr;
    }
    auto scene = std::make_shared<LwsScene>();
    scene->path = path;
    const size_t slash = path.find_last_of("\\/");
    const std::string dir = slash == std::string::npos ? std::string() : path.substr(0, slash);

    std::istringstream in(*text);
    std::string line;
    auto nextLine = [&](std::string& out) {
        while (std::getline(in, out)) {
            while (!out.empty() && (out.back() == '\r' || out.back() == ' ' || out.back() == '\t')) out.pop_back();
            size_t s = 0;
            while (s < out.size() && (out[s] == ' ' || out[s] == '\t')) ++s;
            out = out.substr(s);
            if (!out.empty()) return true;
        }
        return false;
    };
    LwsNode* current = nullptr;
    while (nextLine(line)) {
        std::istringstream ls(line);
        std::string key;
        ls >> key;
        if (key == "FirstFrame") {
            ls >> scene->firstFrame;
        } else if (key == "LastFrame") {
            ls >> scene->lastFrame;
        } else if (key == "FramesPerSecond") {
            ls >> scene->fps;
        } else if (key == "LoadObject" || key == "AddNullObject") {
            std::string rest;
            std::getline(ls, rest);
            size_t s = 0;
            while (s < rest.size() && rest[s] == ' ') ++s;
            rest = rest.substr(s);
            LwsNode node;
            node.isNull = key == "AddNullObject";
            if (node.isNull) {
                node.name = rest;
            } else {
                const std::string file = FileNamePart(rest);
                node.name = StripExtension(file);
                node.mesh = LoadSceneMesh(dir, sharedDir, file);
                if (!node.mesh) RR_DEBUG("%s: mesh %s not found", path.c_str(), file.c_str());
            }
            scene->nodes.push_back(std::move(node));
            current = &scene->nodes.back();
        } else if (key == "ObjectMotion" && current) {
            std::string l;
            nextLine(l);  // channel count (9)
            const int channels = std::atoi(l.c_str());
            nextLine(l);
            const int keys = std::atoi(l.c_str());
            for (int k = 0; k < keys; ++k) {
                if (!nextLine(l)) break;
                std::istringstream vs(l);
                float v[9] = {0, 0, 0, 0, 0, 0, 1, 1, 1};
                for (int c = 0; c < channels && c < 9; ++c) vs >> v[c];
                if (!nextLine(l)) break;  // frame linear tension continuity bias
                LwsKey key;
                key.frame = float(std::atof(l.c_str()));
                key.pos = {v[0], v[1], v[2]};
                key.hpb = {DegToRad(v[3]), DegToRad(v[4]), DegToRad(v[5])};
                key.scale = {v[6], v[7], v[8]};
                current->keys.push_back(key);
            }
        } else if (key == "ObjDissolve" && current) {
            std::string rest;
            ls >> rest;
            if (rest == "(envelope)") {
                std::string l;
                nextLine(l);  // channels (1)
                nextLine(l);
                const int keys = std::atoi(l.c_str());
                for (int k = 0; k < keys; ++k) {
                    if (!nextLine(l)) break;
                    const float value = float(std::atof(l.c_str()));
                    if (!nextLine(l)) break;
                    current->dissolve.push_back({float(std::atof(l.c_str())), value});
                }
            } else {
                current->staticDissolve = float(std::atof(rest.c_str()));
            }
        } else if (key == "ParentObject" && current) {
            int p = 0;
            ls >> p;
            current->parent = p - 1;
        } else if (key == "PivotPoint" && current) {
            ls >> current->pivot.x >> current->pivot.y >> current->pivot.z;
        } else if (key == "UnseenByCamera" && current) {
            current->unseenByCamera = true;
        } else if (key == "AddLight" || key == "CameraMotion" || key == "LightMotion") {
            current = nullptr;  // lights and cameras are not used by the game
        }
    }
    return scene;
}

namespace {

// LightWave heading/pitch/bank with scale, as rows of a row-vector matrix (Lws_SetupNodeTransform).
Mat4 NodeMatrix(const Vec3f& pos, const Vec3f& hpb, const Vec3f& scale, const Vec3f& pivot) {
    const float sh = std::sin(hpb.x), ch = std::cos(hpb.x);
    const float sp = std::sin(hpb.y), cp = std::cos(hpb.y);
    const float sb = std::sin(hpb.z), cb = std::cos(hpb.z);
    const Vec3f r0 = Vec3f{ch * cb + sh * sp * sb, cp * sb, ch * sp * sb - sh * cb} * scale.x;
    const Vec3f r1 = Vec3f{sh * sp * cb - ch * sb, cp * cb, sh * sb + ch * sp * cb} * scale.y;
    const Vec3f r2 = Vec3f{sh * cp, -sp, ch * cp} * scale.z;
    // Rotation happens about the pivot point.
    const Vec3f t = pos - (r0 * pivot.x + r1 * pivot.y + r2 * pivot.z);
    Mat4 m;
    m.m[0] = r0.x; m.m[1] = r0.y; m.m[2] = r0.z;
    m.m[4] = r1.x; m.m[5] = r1.y; m.m[6] = r1.z;
    m.m[8] = r2.x; m.m[9] = r2.y; m.m[10] = r2.z;
    m.m[12] = t.x; m.m[13] = t.y; m.m[14] = t.z;
    return m;
}

template <typename Key>
size_t FindPrev(const std::vector<Key>& keys, float time, float& delta) {
    size_t i = 0;
    while (i + 1 < keys.size() && keys[i + 1].frame <= time) ++i;
    if (i + 1 >= keys.size()) {
        delta = 0;
        return i;
    }
    const float span = keys[i + 1].frame - keys[i].frame;
    delta = span > 0 ? (time - keys[i].frame) / span : 0;
    return i;
}

}  // namespace

void EvaluateLws(const LwsScene& scene, float time, bool loop, LwsPose& out) {
    const float last = float(scene.lastFrame);
    if (loop && last > 0) time = std::fmod(time, last);
    else if (time > last) time = last;
    if (time < 0) time = 0;

    const size_t n = scene.nodes.size();
    out.world.resize(n);
    out.dissolve.resize(n);
    std::vector<Mat4> local(n);
    for (size_t i = 0; i < n; ++i) {
        const LwsNode& node = scene.nodes[i];
        if (!node.keys.empty()) {
            float d = 0;
            const size_t k = FindPrev(node.keys, time, d);
            const LwsKey& a = node.keys[k];
            const LwsKey& b = node.keys[std::min(k + 1, node.keys.size() - 1)];
            auto lerp = [d](const Vec3f& x, const Vec3f& y) { return x + (y - x) * d; };
            local[i] = NodeMatrix(lerp(a.pos, b.pos), lerp(a.hpb, b.hpb), lerp(a.scale, b.scale), node.pivot);
        } else {
            local[i] = NodeMatrix({}, {}, {1, 1, 1}, node.pivot);
        }
    }
    // Parents may appear after their children in the file: resolve the hierarchy recursively.
    std::vector<char> state(n, 0);  // 0 = pending, 1 = in progress, 2 = done
    std::function<void(size_t)> resolve = [&](size_t i) {
        if (state[i] == 2) return;
        const int parent = scene.nodes[i].parent;
        if (state[i] == 1 || parent < 0 || size_t(parent) >= n || size_t(parent) == i) {
            out.world[i] = local[i];  // root (or a broken cycle)
            state[i] = 2;
            return;
        }
        state[i] = 1;
        resolve(size_t(parent));
        out.world[i] = out.world[size_t(parent)] * local[i];
        state[i] = 2;
    };
    for (size_t i = 0; i < n; ++i) resolve(i);
    for (size_t i = 0; i < n; ++i) {
        const LwsNode& node = scene.nodes[i];
        float dissolve = node.staticDissolve;
        if (!node.dissolve.empty()) {
            float d = 0;
            const size_t k = FindPrev(node.dissolve, time, d);
            const auto& a = node.dissolve[k];
            const auto& b = node.dissolve[std::min(k + 1, node.dissolve.size() - 1)];
            dissolve = a.value + (b.value - a.value) * d;
        }
        out.dissolve[i] = dissolve;
    }
}

}  // namespace rr
