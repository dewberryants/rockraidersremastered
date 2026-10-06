#pragma once

#include <cmath>

namespace rr {

// World space follows the original game: X east, Y north, Z down (heights are negative Z),
// 40 units per map block. Rendering converts this to OpenGL conventions in the view matrix.

struct Vec2 {
    float x = 0, y = 0;
};

struct Vec3f {
    float x = 0, y = 0, z = 0;
    Vec3f operator+(const Vec3f& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3f operator-(const Vec3f& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3f operator*(float s) const { return {x * s, y * s, z * s}; }
    Vec3f& operator+=(const Vec3f& o) {
        x += o.x;
        y += o.y;
        z += o.z;
        return *this;
    }
};

inline float Dot(const Vec3f& a, const Vec3f& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3f Cross(const Vec3f& a, const Vec3f& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float Length(const Vec3f& v) { return std::sqrt(Dot(v, v)); }
inline Vec3f Normalize(const Vec3f& v) {
    const float l = Length(v);
    return l > 1e-12f ? v * (1.0f / l) : Vec3f{0, 0, 0};
}

// Column-major 4x4 matrix (OpenGL layout): m[col * 4 + row].
struct Mat4 {
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

    static Mat4 Identity() { return {}; }
    static Mat4 Translate(const Vec3f& t) {
        Mat4 r;
        r.m[12] = t.x;
        r.m[13] = t.y;
        r.m[14] = t.z;
        return r;
    }
    static Mat4 Scale(const Vec3f& s) {
        Mat4 r;
        r.m[0] = s.x;
        r.m[5] = s.y;
        r.m[10] = s.z;
        return r;
    }
    // Rotation about a unit axis by angle (radians), right-hand rule.
    static Mat4 Rotate(const Vec3f& axis, float angle) {
        const Vec3f a = Normalize(axis);
        const float c = std::cos(angle), s = std::sin(angle), t = 1 - c;
        Mat4 r;
        r.m[0] = t * a.x * a.x + c;
        r.m[1] = t * a.x * a.y + s * a.z;
        r.m[2] = t * a.x * a.z - s * a.y;
        r.m[4] = t * a.x * a.y - s * a.z;
        r.m[5] = t * a.y * a.y + c;
        r.m[6] = t * a.y * a.z + s * a.x;
        r.m[8] = t * a.x * a.z + s * a.y;
        r.m[9] = t * a.y * a.z - s * a.x;
        r.m[10] = t * a.z * a.z + c;
        return r;
    }
    static Mat4 Perspective(float fovY, float aspect, float zNear, float zFar) {
        const float f = 1.0f / std::tan(fovY * 0.5f);
        Mat4 r;
        r.m[0] = f / aspect;
        r.m[5] = f;
        r.m[10] = (zFar + zNear) / (zNear - zFar);
        r.m[11] = -1;
        r.m[14] = 2 * zFar * zNear / (zNear - zFar);
        r.m[15] = 0;
        return r;
    }
    // View matrix for an eye looking along `forward` with `up`.
    static Mat4 LookDir(const Vec3f& eye, const Vec3f& forward, const Vec3f& up) {
        const Vec3f f = Normalize(forward);
        const Vec3f s = Normalize(Cross(f, up));
        const Vec3f u = Cross(s, f);
        Mat4 r;
        r.m[0] = s.x;
        r.m[4] = s.y;
        r.m[8] = s.z;
        r.m[1] = u.x;
        r.m[5] = u.y;
        r.m[9] = u.z;
        r.m[2] = -f.x;
        r.m[6] = -f.y;
        r.m[10] = -f.z;
        r.m[12] = -Dot(s, eye);
        r.m[13] = -Dot(u, eye);
        r.m[14] = Dot(f, eye);
        return r;
    }

    Mat4 operator*(const Mat4& o) const {
        Mat4 r;
        for (int c = 0; c < 4; ++c)
            for (int rr = 0; rr < 4; ++rr) {
                float v = 0;
                for (int k = 0; k < 4; ++k) v += m[k * 4 + rr] * o.m[c * 4 + k];
                r.m[c * 4 + rr] = v;
            }
        return r;
    }
    Vec3f TransformPoint(const Vec3f& p) const {
        return {m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12], m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13],
                m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14]};
    }
    Vec3f TransformDir(const Vec3f& d) const {
        return {m[0] * d.x + m[4] * d.y + m[8] * d.z, m[1] * d.x + m[5] * d.y + m[9] * d.z,
                m[2] * d.x + m[6] * d.y + m[10] * d.z};
    }
};

constexpr float kPi = 3.14159265358979f;
inline float DegToRad(float d) { return d * (kPi / 180.0f); }

}  // namespace rr
