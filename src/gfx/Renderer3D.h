#pragma once

#include <cstdint>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core/Math3D.h"
#include "gfx/Bitmap.h"

namespace rr {

// A texture for 3D surfaces: mipmapped, repeating, optional colour-key transparency.
class Texture3D {
public:
    ~Texture3D();
    static std::unique_ptr<Texture3D> Load(std::string_view gamePath, bool colourKeyBlack = false);
    static std::unique_ptr<Texture3D> FromBitmap(const Bitmap& bmp, bool colourKeyBlack = false);
    unsigned id() const { return id_; }
    int width() const { return w_; }
    int height() const { return h_; }
    bool hasAlpha() const { return alpha_; }

private:
    unsigned id_ = 0;
    int w_ = 0, h_ = 0;
    bool alpha_ = false;
};

struct Vertex3D {
    Vec3f pos;
    Vec3f normal;
    Vec2 uv;
    uint32_t colour = 0xFFFFFFFF;  // RGBA8 diffuse multiplier (highlights, vertex colours) and alpha
    uint32_t emissive = 0;         // RGB8 self-illumination, added to the lighting
};

struct Light {
    enum class Type { Ambient, Directional, Point, Spot } type = Type::Directional;
    Vec3f colour{1, 1, 1};
    Vec3f position;     // point/spot, world space
    Vec3f direction{0, 0, 1};  // directional/spot, world space (towards where the light shines)
    float range = 1000.0f;
    float innerCos = 1.0f, outerCos = 0.0f;  // spot cone
};

struct FogSettings {
    bool enabled = false;
    Vec3f colour;
    float density = 0.0032f;  // exponential-squared fog (D3DRM fog mode 2)
};

// Forward renderer for world geometry. Draw calls are queued per frame and grouped by texture.
// World space is the game's left-handed space (X east, Y north, Z down).
// How a batch is blended (Mesh_SetRenderDesc). Opaque draws first; the alpha passes follow with
// depth writes and fog off: Alpha is (SRCALPHA, INVSRCALPHA), Additive is (SRCALPHA, ONE).
enum class Blend { Opaque, Alpha, Additive };

class Renderer3D {
public:
    bool Init();
    void Shutdown();

    void BeginFrame(int viewX, int viewY, int viewW, int viewH, const Mat4& view, const Mat4& proj,
                    const Vec3f& cameraPos);
    void SetLights(const std::vector<Light>& lights) { lights_ = lights; }
    void SetFog(const FogSettings& fog) { fog_ = fog; }
    void SetClearColour(const Vec3f& c) { clear_ = c; }

    // Queues triangles (3 vertices each). texture may be null (white).
    // cull: back faces are skipped (single-sided LightWave surfaces; clockwise is the front).
    void DrawTriangles(const Texture3D* texture, const Vertex3D* verts, size_t count, Blend blend = Blend::Opaque,
                       bool cull = false);
    void EndFrame();

private:
    struct Batch {
        const Texture3D* texture;
        Blend blend;
        bool cull;
        std::vector<Vertex3D> verts;
    };
    unsigned program_ = 0, vao_ = 0, vbo_ = 0, white_ = 0;
    std::vector<Batch> batches_;
    std::vector<Light> lights_;
    FogSettings fog_;
    Vec3f clear_{0, 0, 0};
    Mat4 view_, proj_;
    Vec3f cameraPos_;
    int vx_ = 0, vy_ = 0, vw_ = 1, vh_ = 1;
};

// Builds a left-handed view matrix (the original Direct3D convention) usable with OpenGL.
Mat4 LookDirLH(const Vec3f& eye, const Vec3f& forward, const Vec3f& up);

}  // namespace rr
