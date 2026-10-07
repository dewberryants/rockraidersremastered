#pragma once

#include <memory>
#include <string>
#include <vector>

#include "core/Math3D.h"
#include "gfx/model/Mesh.h"

namespace rr {

// A LightWave 5 scene (.lws, "LWSC") as used for every animated model in the game: a hierarchy of
// objects (meshes) and null objects with keyframed position / heading-pitch-bank / scale.
// The game interpolates keys linearly (Lws_InterpolateKeys) - spline settings are ignored.
struct LwsKey {
    float frame = 0;
    Vec3f pos, hpb, scale{1, 1, 1};  // hpb in radians
};

struct LwsDissolveKey {
    float frame = 0;
    float value = 0;
};

struct LwsNode {
    std::string name;       // file name for objects, given name for nulls
    std::shared_ptr<Mesh> mesh;
    bool isNull = false;
    int parent = -1;        // index into nodes
    Vec3f pivot;
    std::vector<LwsKey> keys;
    std::vector<LwsDissolveKey> dissolve;
    float staticDissolve = 0;
    bool unseenByCamera = false;
};

// A sound cue in a scene (Lws_SetupSoundTriggers): a null object named
// "SFX,<sample>,<frame>[,<frame>...]"; a frame may be a range "<start>-<end>".
struct LwsSound {
    std::string sample;
    int start = 0, end = 0;  // end > start: the sound plays over a range
};

struct LwsScene {
    std::string path;
    int firstFrame = 0, lastFrame = 0;
    float fps = 25.0f;
    std::vector<LwsNode> nodes;
    std::vector<LwsSound> sounds;

    int frameCount() const { return lastFrame; }
    int FindNode(std::string_view name) const;
};

// Loads "<path>.lws"; meshes are searched in the scene's directory, then sharedDir.
std::shared_ptr<LwsScene> LoadLws(const std::string& path, const std::string& sharedDir = {});

// Evaluated pose of a scene at a given time.
struct LwsPose {
    std::vector<Mat4> world;      // per node, scene space
    std::vector<float> dissolve;  // per node, 0 = opaque .. 1 = invisible
};
// time in frames; looping wraps (fmod), otherwise clamps to the last frame.
void EvaluateLws(const LwsScene& scene, float time, bool loop, LwsPose& out);

}  // namespace rr
