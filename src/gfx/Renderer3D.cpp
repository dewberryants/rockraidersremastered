#include "gfx/Renderer3D.h"

#include <algorithm>
#include <cstddef>
#include <string>

#include "core/Log.h"
#include "gfx/GL.h"
#include "gfx/Shader.h"

namespace rr {

Mat4 LookDirLH(const Vec3f& eye, const Vec3f& forward, const Vec3f& up) {
    const Vec3f f = Normalize(forward);
    const Vec3f s = Normalize(Cross(up, f));  // left-handed "right"
    const Vec3f u = Cross(f, s);
    Mat4 r;
    r.m[0] = s.x;
    r.m[4] = s.y;
    r.m[8] = s.z;
    r.m[1] = u.x;
    r.m[5] = u.y;
    r.m[9] = u.z;
    // OpenGL view space looks down -Z.
    r.m[2] = -f.x;
    r.m[6] = -f.y;
    r.m[10] = -f.z;
    r.m[12] = -Dot(s, eye);
    r.m[13] = -Dot(u, eye);
    r.m[14] = Dot(f, eye);
    return r;
}

// --- Texture3D ----------------------------------------------------------------------------

Texture3D::~Texture3D() {
    if (id_) glDeleteTextures(1, &id_);
}

std::unique_ptr<Texture3D> Texture3D::Load(std::string_view gamePath, bool colourKeyBlack) {
    auto bmp = LoadBmp(gamePath);
    if (!bmp) return nullptr;
    return FromBitmap(*bmp, colourKeyBlack);
}

std::unique_ptr<Texture3D> Texture3D::FromBitmap(const Bitmap& bmp, bool colourKeyBlack) {
    auto t = std::make_unique<Texture3D>();
    t->w_ = bmp.width;
    t->h_ = bmp.height;
    // The original copied BMP rows into texture surfaces in file order (bottom row first), so
    // texture v = 0 is the bottom of the image. Bitmap rows are top-down: reverse them here.
    std::vector<uint8_t> px(bmp.rgba.size());
    const size_t rowBytes = size_t(bmp.width) * 4;
    for (int y = 0; y < bmp.height; ++y)
        std::copy_n(&bmp.rgba[size_t(bmp.height - 1 - y) * rowBytes], rowBytes, &px[size_t(y) * rowBytes]);
    for (size_t i = 0; i < px.size(); i += 4) {
        if (colourKeyBlack && px[i] == 0 && px[i + 1] == 0 && px[i + 2] == 0) px[i + 3] = 0;
        if (px[i + 3] < 255) t->alpha_ = true;
    }
    glGenTextures(1, &t->id_);
    glBindTexture(GL_TEXTURE_2D, t->id_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, bmp.width, bmp.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    // Colour-keyed textures (often sparse atlases) are not mipmapped: averaging would fade them out
    // at a distance, whereas the original keyed each texel.
    if (!t->alpha_) glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, t->alpha_ ? GL_LINEAR : GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    return t;
}

// --- Renderer3D ---------------------------------------------------------------------------

namespace {
constexpr int kMaxLights = 8;

const char* kVertex = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;
layout(location = 3) in vec4 aColour;
uniform mat4 uViewProj;
out vec3 vWorld;
out vec3 vNormal;
out vec2 vUv;
out vec4 vColour;
void main() {
    vWorld = aPos;
    vNormal = aNormal;
    vUv = aUv;
    vColour = aColour;
    gl_Position = uViewProj * vec4(aPos, 1.0);
}
)";

// Lighting is evaluated per pixel from the interpolated Gouraud normal; light types follow
// Direct3D Retained Mode (ambient, directional, point, spot).
const char* kFragment = R"(#version 330 core
in vec3 vWorld;
in vec3 vNormal;
in vec2 vUv;
in vec4 vColour;
uniform sampler2D uTex;
uniform int uLightCount;
uniform int uLightType[8];
uniform vec3 uLightColour[8];
uniform vec3 uLightPos[8];
uniform vec3 uLightDir[8];
uniform vec3 uLightCone[8];  // innerCos, outerCos, range
uniform int uFog;
uniform vec3 uFogColour;
uniform float uFogDensity;
uniform vec3 uCameraPos;
out vec4 oColour;
void main() {
    vec4 tex = texture(uTex, vUv);
    if (tex.a < 0.5) discard;  // colour-keyed texel
    tex *= vColour;
    vec3 n = normalize(vNormal);
    vec3 light = vec3(0.0);
    for (int i = 0; i < uLightCount; ++i) {
        if (uLightType[i] == 0) { light += uLightColour[i]; continue; }
        vec3 L;
        float atten = 1.0;
        if (uLightType[i] == 1) {
            L = -normalize(uLightDir[i]);
        } else {
            vec3 d = uLightPos[i] - vWorld;
            float dist = length(d);
            L = d / max(dist, 0.0001);
            atten = clamp(1.0 - dist / uLightCone[i].z, 0.0, 1.0);
            if (uLightType[i] == 3) {
                float c = dot(-L, normalize(uLightDir[i]));
                atten *= smoothstep(uLightCone[i].y, uLightCone[i].x, c);
            }
        }
        light += uLightColour[i] * max(dot(n, L), 0.0) * atten;
    }
    vec3 colour = tex.rgb * min(light, vec3(1.0));
    if (uFog != 0) {
        float d = length(vWorld - uCameraPos) * uFogDensity;
        float f = exp(-d * d);
        colour = mix(uFogColour, colour, clamp(f, 0.0, 1.0));
    }
    oColour = vec4(colour, tex.a);
}
)";
}  // namespace

bool Renderer3D::Init() {
    program_ = gl::CompileProgram(kVertex, kFragment, "Renderer3D");
    if (!program_) return false;
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex3D), reinterpret_cast<void*>(offsetof(Vertex3D, pos)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex3D),
                          reinterpret_cast<void*>(offsetof(Vertex3D, normal)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex3D), reinterpret_cast<void*>(offsetof(Vertex3D, uv)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex3D),
                          reinterpret_cast<void*>(offsetof(Vertex3D, colour)));
    glBindVertexArray(0);
    const uint8_t white[4] = {255, 255, 255, 255};
    glGenTextures(1, &white_);
    glBindTexture(GL_TEXTURE_2D, white_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    return true;
}

void Renderer3D::Shutdown() {
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (program_) glDeleteProgram(program_);
    if (white_) glDeleteTextures(1, &white_);
    vbo_ = vao_ = program_ = white_ = 0;
}

void Renderer3D::BeginFrame(int viewX, int viewY, int viewW, int viewH, const Mat4& view, const Mat4& proj,
                            const Vec3f& cameraPos) {
    vx_ = viewX;
    vy_ = viewY;
    vw_ = viewW;
    vh_ = viewH;
    view_ = view;
    proj_ = proj;
    cameraPos_ = cameraPos;
    for (auto& b : batches_) b.verts.clear();
}

void Renderer3D::DrawTriangles(const Texture3D* texture, const Vertex3D* verts, size_t count, Blend blend) {
    // Reuse a batch with the same state (batches persist across frames to keep their allocations).
    for (auto& b : batches_) {
        if (b.texture == texture && b.blend == blend) {
            b.verts.insert(b.verts.end(), verts, verts + count);
            return;
        }
    }
    batches_.push_back({texture, blend, std::vector<Vertex3D>(verts, verts + count)});
}

void Renderer3D::EndFrame() {
    GLint previousViewport[4];
    glGetIntegerv(GL_VIEWPORT, previousViewport);
    glViewport(vx_, vy_, vw_, vh_);
    glEnable(GL_SCISSOR_TEST);
    glScissor(vx_, vy_, vw_, vh_);
    const Vec3f bg = fog_.enabled ? fog_.colour : clear_;
    glClearColor(bg.x, bg.y, bg.z, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);

    glUseProgram(program_);
    const Mat4 vp = proj_ * view_;
    glUniformMatrix4fv(glGetUniformLocation(program_, "uViewProj"), 1, GL_FALSE, vp.m);
    glUniform1i(glGetUniformLocation(program_, "uTex"), 0);
    const int count = std::min<int>(int(lights_.size()), kMaxLights);
    glUniform1i(glGetUniformLocation(program_, "uLightCount"), count);
    for (int i = 0; i < count; ++i) {
        const Light& l = lights_[size_t(i)];
        const std::string idx = "[" + std::to_string(i) + "]";
        glUniform1i(glGetUniformLocation(program_, ("uLightType" + idx).c_str()), int(l.type));
        glUniform3f(glGetUniformLocation(program_, ("uLightColour" + idx).c_str()), l.colour.x, l.colour.y, l.colour.z);
        glUniform3f(glGetUniformLocation(program_, ("uLightPos" + idx).c_str()), l.position.x, l.position.y,
                    l.position.z);
        glUniform3f(glGetUniformLocation(program_, ("uLightDir" + idx).c_str()), l.direction.x, l.direction.y,
                    l.direction.z);
        glUniform3f(glGetUniformLocation(program_, ("uLightCone" + idx).c_str()), l.innerCos, l.outerCos, l.range);
    }
    glUniform1i(glGetUniformLocation(program_, "uFog"), fog_.enabled ? 1 : 0);
    glUniform3f(glGetUniformLocation(program_, "uFogColour"), fog_.colour.x, fog_.colour.y, fog_.colour.z);
    glUniform1f(glGetUniformLocation(program_, "uFogDensity"), fog_.density);
    glUniform3f(glGetUniformLocation(program_, "uCameraPos"), cameraPos_.x, cameraPos_.y, cameraPos_.z);

    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    const Blend passes[] = {Blend::Opaque, Blend::Alpha, Blend::Additive};
    for (Blend pass : passes) {
        if (pass == Blend::Alpha) {
            glEnable(GL_BLEND);
            glDepthMask(GL_FALSE);
            glUniform1i(glGetUniformLocation(program_, "uFog"), 0);
        }
        if (pass != Blend::Opaque)
            glBlendFunc(GL_SRC_ALPHA, pass == Blend::Additive ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA);
        for (auto& b : batches_) {
            if (b.verts.empty() || b.blend != pass) continue;
            glBindTexture(GL_TEXTURE_2D, b.texture ? b.texture->id() : white_);
            glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(b.verts.size() * sizeof(Vertex3D)), b.verts.data(), GL_STREAM_DRAW);
            glDrawArrays(GL_TRIANGLES, 0, GLsizei(b.verts.size()));
        }
    }
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glBindVertexArray(0);
    glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
}

}  // namespace rr
