#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "gfx/Renderer3D.h"

namespace rr {

// Material of a mesh group, derived from a LightWave surface the way Mesh_ParseLWO did it.
struct MeshMaterial {
    Vec3f diffuse{1, 1, 1};   // surface colour x DIFF (white when textured, unless FLAG 0x80)
    Vec3f emissive{0, 0, 0};  // colour x LUMI
    Vec3f specular{0, 0, 0};
    float power = 0;          // GLOS x 10
    float alpha = 1;          // 1 - TRAN
    const Texture3D* texture = nullptr;
    std::vector<const Texture3D*> sequence;  // animated texture frames ("(sequence)" surfaces)
    bool additive = false;     // LightWave FLAG 0x200
    bool doubleSided = false;  // FLAG 0x100
    bool pointFilter = false;  // texture without pixel blending (TFLG 0x20 clear)
};

struct MeshGroup {
    std::vector<Vertex3D> vertices;
    std::vector<uint32_t> indices;
    MeshMaterial material;
};

struct Mesh {
    std::string path;
    std::vector<MeshGroup> groups;
};

// Loads and caches textures for meshes. A texture whose file name starts with "A<nnn>_" uses
// palette entry <nnn> as its transparent colour (Container_GetDecalColour).
class MeshTextureCache {
public:
    const Texture3D* Load(const std::string& dir, const std::string& fileName);
    void SetSharedDir(std::string dir) { sharedDir_ = std::move(dir); }

private:
    const Texture3D* LoadExact(const std::string& path, const std::string& fileName);
    std::vector<std::pair<std::string, std::unique_ptr<Texture3D>>> cache_;
    std::string sharedDir_;
};

MeshTextureCache& GetMeshTextures();

// Loads "<path>.lwo" (path given with or without extension). Returns null if missing.
std::shared_ptr<Mesh> LoadLwo(const std::string& path);

}  // namespace rr
