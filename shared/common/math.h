// Small self-contained math library shared by client, game server and backend.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace si {

constexpr float kPi = 3.14159265358979f;
constexpr float DEG2RAD_F = kPi / 180.0f;

inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
inline float sign(float v) { return v < 0 ? -1.0f : 1.0f; }
inline float smoothstep(float a, float b, float x) {
    float t = clampf((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}
inline float wrapAngle(float a) {
    while (a > kPi) a -= 2 * kPi;
    while (a < -kPi) a += 2 * kPi;
    return a;
}

struct Vec2 {
    float x = 0, y = 0;
    Vec2() = default;
    Vec2(float x_, float y_) : x(x_), y(y_) {}
    Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(float s) const { return {x * s, y * s}; }
    float len() const { return std::sqrt(x * x + y * y); }
    Vec2 norm() const { float l = len(); return l > 1e-6f ? Vec2{x / l, y / l} : Vec2{0, 0}; }
};
inline float dist2d(Vec2 a, Vec2 b) { return (a - b).len(); }

struct Vec3 {
    float x = 0, y = 0, z = 0;
    Vec3() = default;
    Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    Vec3 operator/(float s) const { return {x / s, y / s, z / s}; }
    Vec3 operator-() const { return {-x, -y, -z}; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
    Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
    Vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
    float dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
    Vec3 cross(const Vec3& o) const { return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x}; }
    float len() const { return std::sqrt(x * x + y * y + z * z); }
    float len2() const { return x * x + y * y + z * z; }
    float lenXZ() const { return std::sqrt(x * x + z * z); }
    Vec3 norm() const { float l = len(); return l > 1e-6f ? Vec3{x / l, y / l, z / l} : Vec3{0, 0, 0}; }
    Vec2 xz() const { return {x, z}; }
};
inline Vec3 lerp3(const Vec3& a, const Vec3& b, float t) { return a + (b - a) * t; }
inline float distXZ(const Vec3& a, const Vec3& b) { float dx = a.x - b.x, dz = a.z - b.z; return std::sqrt(dx * dx + dz * dz); }

// Direction from yaw (radians, 0 = +Z/south, rotating toward +X) and pitch (positive = up).
inline Vec3 dirFromAngles(float yaw, float pitch) {
    float cp = std::cos(pitch);
    return {std::sin(yaw) * cp, std::sin(pitch), std::cos(yaw) * cp};
}
inline float yawFromDir(const Vec3& d) { return std::atan2(d.x, d.z); }

struct AABB {
    Vec3 min, max;
    AABB() = default;
    AABB(Vec3 a, Vec3 b) : min(a), max(b) {}
    static AABB fromCenter(Vec3 c, Vec3 half) { return {c - half, c + half}; }
    Vec3 center() const { return (min + max) * 0.5f; }
    Vec3 size() const { return max - min; }
    bool overlaps(const AABB& o) const {
        return min.x < o.max.x && max.x > o.min.x && min.y < o.max.y && max.y > o.min.y &&
               min.z < o.max.z && max.z > o.min.z;
    }
    bool contains(const Vec3& p) const {
        return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y && p.z >= min.z && p.z <= max.z;
    }
    AABB expanded(float e) const { return {min - Vec3{e, e, e}, max + Vec3{e, e, e}}; }
};

// Slab test. Returns true and entry distance t (>=0) if the ray hits within maxT.
inline bool rayAABB(const Vec3& o, const Vec3& d, const AABB& b, float maxT, float& tOut, Vec3* normalOut = nullptr) {
    float tmin = 0.0f, tmax = maxT;
    int axisHit = -1;
    float nsign = 0;
    const float* po = &o.x;
    const float* pd = &d.x;
    const float* bmin = &b.min.x;
    const float* bmax = &b.max.x;
    for (int i = 0; i < 3; i++) {
        if (std::fabs(pd[i]) < 1e-8f) {
            if (po[i] < bmin[i] || po[i] > bmax[i]) return false;
        } else {
            float inv = 1.0f / pd[i];
            float t1 = (bmin[i] - po[i]) * inv;
            float t2 = (bmax[i] - po[i]) * inv;
            float s = -1.0f;
            if (t1 > t2) { std::swap(t1, t2); s = 1.0f; }
            if (t1 > tmin) { tmin = t1; axisHit = i; nsign = s; }
            if (t2 < tmax) tmax = t2;
            if (tmin > tmax) return false;
        }
    }
    tOut = tmin;
    if (normalOut) {
        Vec3 n{0, 0, 0};
        if (axisHit >= 0) (&n.x)[axisHit] = nsign;
        *normalOut = n;
    }
    return true;
}

// Ray vs triangle (Moller-Trumbore), two-sided.
inline bool rayTri(const Vec3& o, const Vec3& d, const Vec3& a, const Vec3& b, const Vec3& c, float& t) {
    Vec3 e1 = b - a, e2 = c - a;
    Vec3 p = d.cross(e2);
    float det = e1.dot(p);
    if (std::fabs(det) < 1e-8f) return false;
    float inv = 1.0f / det;
    Vec3 s = o - a;
    float u = s.dot(p) * inv;
    if (u < 0 || u > 1) return false;
    Vec3 q = s.cross(e1);
    float v = d.dot(q) * inv;
    if (v < 0 || u + v > 1) return false;
    t = e2.dot(q) * inv;
    return t >= 0;
}

// Deterministic PCG32 random generator so client and server generate identical maps.
struct Rng {
    uint64_t state = 0x853c49e6748fea9bULL, inc = 0xda3e39cb94b95bdbULL;
    Rng() = default;
    explicit Rng(uint64_t seed, uint64_t seq = 1) { reseed(seed, seq); }
    void reseed(uint64_t seed, uint64_t seq = 1) {
        state = 0; inc = (seq << 1u) | 1u; next(); state += seed; next();
    }
    uint32_t next() {
        uint64_t old = state;
        state = old * 6364136223846793005ULL + inc;
        uint32_t xorshifted = (uint32_t)(((old >> 18u) ^ old) >> 27u);
        uint32_t rot = (uint32_t)(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((-rot) & 31));
    }
    float uniform() { return (next() >> 8) * (1.0f / 16777216.0f); }
    float range(float a, float b) { return a + (b - a) * uniform(); }
    int irange(int a, int b) { return a + (int)(next() % (uint32_t)(b - a + 1)); } // inclusive
    bool chance(float p) { return uniform() < p; }
};

// Hash based value noise (deterministic, no tables).
inline float hashNoise(int x, int y, uint32_t seed) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (h & 0xFFFFFF) / 16777215.0f;
}
inline float valueNoise(float x, float y, uint32_t seed) {
    int xi = (int)std::floor(x), yi = (int)std::floor(y);
    float xf = x - xi, yf = y - yi;
    float u = xf * xf * (3 - 2 * xf), v = yf * yf * (3 - 2 * yf);
    float a = hashNoise(xi, yi, seed), b = hashNoise(xi + 1, yi, seed);
    float c = hashNoise(xi, yi + 1, seed), d = hashNoise(xi + 1, yi + 1, seed);
    return lerpf(lerpf(a, b, u), lerpf(c, d, u), v);
}
inline float fbm(float x, float y, uint32_t seed, int octaves = 5) {
    float sum = 0, amp = 0.5f, freq = 1.0f, norm = 0;
    for (int i = 0; i < octaves; i++) {
        sum += amp * valueNoise(x * freq, y * freq, seed + i * 101);
        norm += amp;
        amp *= 0.5f;
        freq *= 2.0f;
    }
    return sum / norm;
}

// Packed RGBA color used by shared code (no raylib dependency).
struct Color4 {
    uint8_t r = 255, g = 255, b = 255, a = 255;
};
inline Color4 rgb(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) { return Color4{r, g, b, a}; }
inline Color4 shade(Color4 c, float f) {
    auto s = [f](uint8_t v) { return (uint8_t)clampf(v * f, 0, 255); };
    return Color4{s(c.r), s(c.g), s(c.b), c.a};
}
inline Color4 mix(Color4 a, Color4 b, float t) {
    return Color4{(uint8_t)lerpf(a.r, b.r, t), (uint8_t)lerpf(a.g, b.g, t), (uint8_t)lerpf(a.b, b.b, t),
                  (uint8_t)lerpf(a.a, b.a, t)};
}

} // namespace si
