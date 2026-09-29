#include <cmath>
#include <cstring>

#include "raylib.h"
#include "raymath.h"
#include "render.h"
#include "rlgl.h"

namespace client {

// ------------------------------------------------------------------ shader

static const char* kVS = R"(#version 330
in vec3 vertexPosition;
in vec3 vertexNormal;
in vec4 vertexColor;
uniform mat4 mvp;
uniform mat4 matModel;
uniform mat4 matNormal;
out vec3 fragPos;
out vec3 fragNormal;
out vec4 fragColor;
void main() {
    fragPos = vec3(matModel * vec4(vertexPosition, 1.0));
    fragNormal = normalize(vec3(matNormal * vec4(vertexNormal, 0.0)));
    fragColor = vertexColor;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";

static const char* kFS = R"(#version 330
in vec3 fragPos;
in vec3 fragNormal;
in vec4 fragColor;
uniform vec4 colDiffuse;
uniform vec3 viewPos;
uniform vec3 sunDir;
uniform vec3 fogColor;
uniform float fogDensity;
out vec4 finalColor;
void main() {
    vec3 n = normalize(fragNormal);
    if (!gl_FrontFacing) n = -n;
    float diff = max(dot(n, -sunDir), 0.0);
    float sky = 0.5 + 0.5 * n.y;
    vec3 base = fragColor.rgb * colDiffuse.rgb;
    vec3 c = base * (0.38 + 0.22 * sky + 0.62 * diff);
    float dist = length(viewPos - fragPos);
    float f = 1.0 - exp(-pow(dist * fogDensity, 2.0));
    c = mix(c, fogColor, clamp(f, 0.0, 1.0));
    finalColor = vec4(c, fragColor.a * colDiffuse.a);
}
)";

void Lighting::load() {
    shader = LoadShaderFromMemory(kVS, kFS);
    locViewPos = GetShaderLocation(shader, "viewPos");
    locSunDir = GetShaderLocation(shader, "sunDir");
    locFogColor = GetShaderLocation(shader, "fogColor");
    locFogDensity = GetShaderLocation(shader, "fogDensity");
    locModel = GetShaderLocation(shader, "matModel");
    locNormal = GetShaderLocation(shader, "matNormal");
    shader.locs[SHADER_LOC_MATRIX_MODEL] = locModel;
    shader.locs[SHADER_LOC_MATRIX_NORMAL] = locNormal;
    shader.locs[SHADER_LOC_VECTOR_VIEW] = locViewPos;
    Vector3 sun = Vector3Normalize({-0.45f, -0.8f, -0.35f});
    SetShaderValue(shader, locSunDir, &sun, SHADER_UNIFORM_VEC3);
}

void Lighting::unload() {
    if (shader.id) UnloadShader(shader);
    shader = {};
}

void Lighting::begin(const Vector3& camPos) {
    SetShaderValue(shader, locViewPos, &camPos, SHADER_UNIFORM_VEC3);
    float fc[3] = {fogColor.r / 255.0f, fogColor.g / 255.0f, fogColor.b / 255.0f};
    SetShaderValue(shader, locFogColor, fc, SHADER_UNIFORM_VEC3);
    SetShaderValue(shader, locFogDensity, &fogDensity, SHADER_UNIFORM_FLOAT);
    Matrix id = MatrixIdentity();
    SetShaderValueMatrix(shader, locModel, id);
    SetShaderValueMatrix(shader, locNormal, id);
}

void Lighting::setTint(Color) {}

// ------------------------------------------------------------------ primitives

Basis yawBasis(float yaw) {
    Basis b;
    b.f = {std::sin(yaw), 0, std::cos(yaw)};
    b.u = {0, 1, 0};
    b.r = {-std::cos(yaw), 0, std::sin(yaw)};
    return b;
}

Basis yawPitchBasis(float yaw, float pitch) {
    Basis b;
    b.f = dirFromAngles(yaw, pitch);
    b.r = {-std::cos(yaw), 0, std::sin(yaw)};
    b.u = b.r.cross(b.f).norm() * -1.0f;
    if (b.u.y < 0) b.u = b.u * -1.0f;
    return b;
}

Basis compose(const Basis& p, const Basis& l) {
    auto tr = [&](const si::Vec3& v) { return p.r * v.x + p.u * v.y + p.f * v.z; };
    Basis b;
    b.r = tr(l.r);
    b.u = tr(l.u);
    b.f = tr(l.f);
    return b;
}

static void quad(const si::Vec3& a, const si::Vec3& b, const si::Vec3& c, const si::Vec3& d, const si::Vec3& n) {
    rlNormal3f(n.x, n.y, n.z);
    rlVertex3f(a.x, a.y, a.z);
    rlVertex3f(b.x, b.y, b.z);
    rlVertex3f(c.x, c.y, c.z);
    rlVertex3f(d.x, d.y, d.z);
}

void drawBox(const si::Vec3& c, const si::Vec3& h, const Basis& b, Color col) {
    si::Vec3 R = b.r * h.x, U = b.u * h.y, F = b.f * h.z;
    si::Vec3 p[8] = {c - R - U - F, c + R - U - F, c + R + U - F, c - R + U - F,
                     c - R - U + F, c + R - U + F, c + R + U + F, c - R + U + F};
    rlBegin(RL_QUADS);
    rlColor4ub(col.r, col.g, col.b, col.a);
    quad(p[4], p[5], p[6], p[7], b.f);         // front (+f)
    quad(p[1], p[0], p[3], p[2], b.f * -1.0f);  // back
    quad(p[3], p[7], p[6], p[2], b.u);         // top
    quad(p[0], p[1], p[5], p[4], b.u * -1.0f);  // bottom
    quad(p[1], p[2], p[6], p[5], b.r);         // right (+r)
    quad(p[0], p[4], p[7], p[3], b.r * -1.0f);  // left
    rlEnd();
}

void drawAABB(const AABB& box, Color c) { drawBox(box.center(), box.size() * 0.5f, Basis(), c); }

void drawRampShape(const AABB& b, uint8_t dir, Color col) {
    si::Vec3 c[4];
    float y0 = b.min.y, y1 = b.max.y;
    switch (dir) {
        case RAMP_PX: c[0] = {b.min.x, y0, b.max.z}; c[1] = {b.min.x, y0, b.min.z}; c[2] = {b.max.x, y1, b.min.z}; c[3] = {b.max.x, y1, b.max.z}; break;
        case RAMP_NX: c[0] = {b.max.x, y0, b.min.z}; c[1] = {b.max.x, y0, b.max.z}; c[2] = {b.min.x, y1, b.max.z}; c[3] = {b.min.x, y1, b.min.z}; break;
        case RAMP_PZ: c[0] = {b.min.x, y0, b.min.z}; c[1] = {b.max.x, y0, b.min.z}; c[2] = {b.max.x, y1, b.max.z}; c[3] = {b.min.x, y1, b.max.z}; break;
        default: c[0] = {b.max.x, y0, b.max.z}; c[1] = {b.min.x, y0, b.max.z}; c[2] = {b.min.x, y1, b.min.z}; c[3] = {b.max.x, y1, b.min.z}; break;
    }
    si::Vec3 n = (c[1] - c[0]).cross(c[3] - c[0]).norm();
    if (n.y < 0) n = n * -1.0f;
    rlBegin(RL_QUADS);
    rlColor4ub(col.r, col.g, col.b, col.a);
    quad(c[0], c[1], c[2], c[3], n);
    quad(c[3], c[2], c[1], c[0], n * -1.0f);
    rlEnd();
}

void drawPyramid(const AABB& b, Color col) {
    si::Vec3 apex{(b.min.x + b.max.x) / 2, b.max.y, (b.min.z + b.max.z) / 2};
    si::Vec3 c0{b.min.x, b.min.y, b.min.z}, c1{b.max.x, b.min.y, b.min.z}, c2{b.max.x, b.min.y, b.max.z}, c3{b.min.x, b.min.y, b.max.z};
    rlBegin(RL_TRIANGLES);
    rlColor4ub(col.r, col.g, col.b, col.a);
    auto tri = [&](const si::Vec3& a, const si::Vec3& b2, const si::Vec3& c) {
        si::Vec3 n = (b2 - a).cross(c - a).norm();
        if (n.y < 0) { n = n * -1.0f; }
        rlNormal3f(n.x, n.y, n.z);
        rlVertex3f(a.x, a.y, a.z); rlVertex3f(b2.x, b2.y, b2.z); rlVertex3f(c.x, c.y, c.z);
        rlVertex3f(c.x, c.y, c.z); rlVertex3f(b2.x, b2.y, b2.z); rlVertex3f(a.x, a.y, a.z);
    };
    tri(c0, c1, apex); tri(c1, c2, apex); tri(c2, c3, apex); tri(c3, c0, apex);
    rlEnd();
}

Color materialColor(si::Material m) {
    switch (m) {
        case si::Material::Wood: return {176, 128, 78, 255};
        case si::Material::Brick: return {170, 90, 70, 255};
        case si::Material::Metal: return {150, 160, 170, 255};
        default: return {200, 200, 200, 255};
    }
}

// ------------------------------------------------------------------ mesh builder

namespace {

struct MeshBuilder {
    std::vector<float> pos, nrm;
    std::vector<unsigned char> col;
    std::vector<unsigned short> idx;
    std::vector<Mesh> done;

    size_t verts() const { return pos.size() / 3; }
    void flushIfNeeded(size_t add) {
        if (verts() + add > 60000) flush();
    }
    void flush() {
        if (idx.empty()) return;
        Mesh m{};
        m.vertexCount = (int)verts();
        m.triangleCount = (int)(idx.size() / 3);
        m.vertices = (float*)MemAlloc((unsigned)(pos.size() * sizeof(float)));
        m.normals = (float*)MemAlloc((unsigned)(nrm.size() * sizeof(float)));
        m.colors = (unsigned char*)MemAlloc((unsigned)col.size());
        m.indices = (unsigned short*)MemAlloc((unsigned)(idx.size() * sizeof(unsigned short)));
        std::memcpy(m.vertices, pos.data(), pos.size() * sizeof(float));
        std::memcpy(m.normals, nrm.data(), nrm.size() * sizeof(float));
        std::memcpy(m.colors, col.data(), col.size());
        std::memcpy(m.indices, idx.data(), idx.size() * sizeof(unsigned short));
        UploadMesh(&m, false);
        done.push_back(m);
        pos.clear(); nrm.clear(); col.clear(); idx.clear();
    }
    unsigned short vert(const si::Vec3& p, const si::Vec3& n, Color c) {
        pos.push_back(p.x); pos.push_back(p.y); pos.push_back(p.z);
        nrm.push_back(n.x); nrm.push_back(n.y); nrm.push_back(n.z);
        col.push_back(c.r); col.push_back(c.g); col.push_back(c.b); col.push_back(255);
        return (unsigned short)(verts() - 1);
    }
    void quad(const si::Vec3& a, const si::Vec3& b, const si::Vec3& c, const si::Vec3& d, const si::Vec3& n, Color col) {
        flushIfNeeded(4);
        unsigned short i0 = vert(a, n, col), i1 = vert(b, n, col), i2 = vert(c, n, col), i3 = vert(d, n, col);
        idx.insert(idx.end(), {i0, i1, i2, i0, i2, i3});
    }
    void tri(const si::Vec3& a, const si::Vec3& b, const si::Vec3& c, Color col, bool twoSided = false) {
        flushIfNeeded(6);
        si::Vec3 n = (b - a).cross(c - a).norm();
        unsigned short i0 = vert(a, n, col), i1 = vert(b, n, col), i2 = vert(c, n, col);
        idx.insert(idx.end(), {i0, i1, i2});
        if (twoSided) {
            unsigned short j0 = vert(a, n * -1.0f, col), j1 = vert(b, n * -1.0f, col), j2 = vert(c, n * -1.0f, col);
            idx.insert(idx.end(), {j0, j2, j1});
        }
    }
    void box(const si::Vec3& c, const si::Vec3& h, const Basis& b, Color col) {
        si::Vec3 R = b.r * h.x, U = b.u * h.y, F = b.f * h.z;
        si::Vec3 p[8] = {c - R - U - F, c + R - U - F, c + R + U - F, c - R + U - F,
                         c - R - U + F, c + R - U + F, c + R + U + F, c - R + U + F};
        Color top = col, side = col;
        // Counter-clockwise when viewed from outside
        quad(p[4], p[5], p[6], p[7], b.f, side);
        quad(p[1], p[0], p[3], p[2], b.f * -1.0f, side);
        quad(p[3], p[7], p[6], p[2], b.u, top);
        quad(p[0], p[1], p[5], p[4], b.u * -1.0f, side);
        quad(p[1], p[2], p[6], p[5], b.r, side);
        quad(p[0], p[4], p[7], p[3], b.r * -1.0f, side);
    }
    void aabb(const AABB& bb, Color col) { box(bb.center(), bb.size() * 0.5f, Basis(), col); }
    void pyramid(const si::Vec3& base, float halfW, float h, Color col) {
        si::Vec3 apex = base + si::Vec3{0, h, 0};
        si::Vec3 c0 = base + si::Vec3{-halfW, 0, -halfW}, c1 = base + si::Vec3{halfW, 0, -halfW};
        si::Vec3 c2 = base + si::Vec3{halfW, 0, halfW}, c3 = base + si::Vec3{-halfW, 0, halfW};
        tri(c1, c0, apex, col); tri(c2, c1, apex, col); tri(c3, c2, apex, col); tri(c0, c3, apex, col);
        quad(c0, c1, c2, c3, {0, -1, 0}, col);
    }
    void rampShape(const AABB& b, uint8_t dir, Color col) {
        si::Vec3 c[4];
        float y0 = b.min.y, y1 = b.max.y;
        switch (dir) {
            case RAMP_PX: c[0] = {b.min.x, y0, b.max.z}; c[1] = {b.min.x, y0, b.min.z}; c[2] = {b.max.x, y1, b.min.z}; c[3] = {b.max.x, y1, b.max.z}; break;
            case RAMP_NX: c[0] = {b.max.x, y0, b.min.z}; c[1] = {b.max.x, y0, b.max.z}; c[2] = {b.min.x, y1, b.max.z}; c[3] = {b.min.x, y1, b.min.z}; break;
            case RAMP_PZ: c[0] = {b.min.x, y0, b.min.z}; c[1] = {b.max.x, y0, b.min.z}; c[2] = {b.max.x, y1, b.max.z}; c[3] = {b.min.x, y1, b.max.z}; break;
            default: c[0] = {b.max.x, y0, b.max.z}; c[1] = {b.min.x, y0, b.max.z}; c[2] = {b.min.x, y1, b.min.z}; c[3] = {b.max.x, y1, b.min.z}; break;
        }
        si::Vec3 n = (c[1] - c[0]).cross(c[3] - c[0]).norm();
        if (n.y < 0) n = n * -1.0f;
        // Thin slab: top and bottom faces
        si::Vec3 d{0, -0.25f, 0};
        quad(c[0], c[1], c[2], c[3], n, col);
        quad(c[3] + d, c[2] + d, c[1] + d, c[0] + d, n * -1.0f, shade4(col, 0.7f));
        // Side triangles (make roofs look solid)
        si::Vec3 g2{c[2].x, y0, c[2].z}, g3{c[3].x, y0, c[3].z};
        tri(c[1], g2, c[2], shade4(col, 0.8f), true);
        tri(c[0], c[3], g3, shade4(col, 0.8f), true);
    }
    static Color shade4(Color c, float f) {
        auto ch = [f](unsigned char v) { return (unsigned char)clampf(v * f, 0.0f, 255.0f); };
        return {ch(c.r), ch(c.g), ch(c.b), c.a};
    }
};

Color jitterColor(Color c, uint32_t seed, float amt) {
    float f = 1.0f + (hashNoise((int)seed, 7, 3) - 0.5f) * 2 * amt;
    return {(unsigned char)clampf(c.r * f, 0, 255), (unsigned char)clampf(c.g * f, 0, 255), (unsigned char)clampf(c.b * f, 0, 255), 255};
}

void addDecor(MeshBuilder& mb, const Decor& d) {
    Color c = C(d.color);
    float s = d.scale;
    si::Vec3 p = d.pos;
    switch (d.kind) {
        case DecorKind::PineTree:
        case DecorKind::SnowPine:
            mb.pyramid(p + si::Vec3{0, -0.5f * s, 0}, 2.6f * s, 3.6f * s, c);
            mb.pyramid(p + si::Vec3{0, 1.8f * s, 0}, 2.0f * s, 3.0f * s, MeshBuilder::shade4(c, 1.08f));
            mb.pyramid(p + si::Vec3{0, 3.7f * s, 0}, 1.3f * s, 2.6f * s, MeshBuilder::shade4(c, 1.15f));
            break;
        case DecorKind::OakTree:
            mb.box(p + si::Vec3{0, 1.5f * s, 0}, si::Vec3{2.2f, 1.6f, 2.2f} * s, Basis(), c);
            mb.box(p + si::Vec3{0.8f * s, 2.8f * s, 0.4f * s}, si::Vec3{1.4f, 1.1f, 1.4f} * s, yawBasis(0.6f), MeshBuilder::shade4(c, 1.1f));
            mb.box(p + si::Vec3{-0.9f * s, 2.4f * s, -0.6f * s}, si::Vec3{1.2f, 1.0f, 1.2f} * s, yawBasis(1.1f), MeshBuilder::shade4(c, 0.92f));
            break;
        case DecorKind::JungleTree:
            mb.box(p + si::Vec3{0, 0.8f * s, 0}, si::Vec3{3.6f, 1.0f, 3.6f} * s, yawBasis(0.3f), c);
            mb.box(p + si::Vec3{0, 2.0f * s, 0}, si::Vec3{2.4f, 0.9f, 2.4f} * s, yawBasis(1.0f), MeshBuilder::shade4(c, 1.12f));
            break;
        case DecorKind::PalmTree:
            for (int i = 0; i < 5; i++) {
                float a = i * 2 * kPi / 5 + (d.pos.x * 0.1f);
                Basis b = compose(yawBasis(a), yawPitchBasis(0, -0.35f));
                si::Vec3 dir = b.f;
                mb.box(p + dir * (1.4f * s), si::Vec3{0.5f, 0.08f, 1.5f} * s, b, c);
            }
            mb.box(p, si::Vec3{0.35f, 0.3f, 0.35f} * s, Basis(), Color{110, 80, 50, 255});
            break;
        case DecorKind::Bush: mb.box(p + si::Vec3{0, 0.45f * s, 0}, si::Vec3{0.8f, 0.5f, 0.8f} * s, yawBasis(p.x), c); break;
        case DecorKind::Flower:
            mb.box(p + si::Vec3{0, 0.2f, 0}, {0.04f, 0.2f, 0.04f}, Basis(), Color{60, 130, 50, 255});
            mb.box(p + si::Vec3{0, 0.42f, 0}, {0.15f, 0.08f, 0.15f}, Basis(), c);
            break;
        case DecorKind::Crop: mb.box(p + si::Vec3{0, 0.6f * s, 0}, si::Vec3{0.12f, 0.6f, 0.12f} * s, Basis(), c); break;
        case DecorKind::Lamp: mb.box(p + si::Vec3{0, 0.3f, 0}, {0.35f, 0.4f, 0.35f}, Basis(), Color{255, 170, 80, 255}); break;
        case DecorKind::Cactus:
            mb.box(p + si::Vec3{0.6f * s, -1.2f * s, 0}, si::Vec3{0.45f, 0.15f, 0.2f} * s, Basis(), c);
            mb.box(p + si::Vec3{0.95f * s, -0.7f * s, 0}, si::Vec3{0.18f, 0.55f, 0.18f} * s, Basis(), c);
            mb.box(p + si::Vec3{-0.55f * s, -1.8f * s, 0}, si::Vec3{0.4f, 0.14f, 0.2f} * s, Basis(), c);
            mb.box(p + si::Vec3{-0.85f * s, -1.4f * s, 0}, si::Vec3{0.16f, 0.45f, 0.16f} * s, Basis(), c);
            break;
        case DecorKind::DeadTree:
            mb.box(p + si::Vec3{0.6f * s, -0.8f * s, 0}, si::Vec3{0.8f, 0.1f, 0.1f} * s, yawBasis(0.5f), c);
            mb.box(p + si::Vec3{-0.5f * s, -1.6f * s, 0.2f}, si::Vec3{0.7f, 0.1f, 0.1f} * s, yawBasis(2.3f), c);
            break;
        default: break;
    }
}

Color biomeColor(Biome b) {
    switch (b) {
        case Biome::Grass: return {104, 168, 72, 255};
        case Biome::Forest: return {76, 138, 64, 255};
        case Biome::Snow: return {232, 238, 245, 255};
        case Biome::Desert: return {222, 192, 132, 255};
        case Biome::Jungle: return {70, 150, 60, 255};
        case Biome::Volcanic: return {70, 62, 60, 255};
        case Biome::Beach: return {230, 214, 160, 255};
        case Biome::City: return {140, 150, 160, 255};
        case Biome::Farm: return {150, 175, 80, 255};
        default: return {120, 170, 80, 255};
    }
}

} // namespace

// ------------------------------------------------------------------ WorldRenderer

int WorldRenderer::chunkKey(float x, float z) const {
    int cx = (int)std::floor(x / CHUNK), cz = (int)std::floor(z / CHUNK);
    return (cz + 64) * 1024 + (cx + 64);
}

static Color terrainColor(const GameMap& map, float x, float z, float h, float slope) {
    Biome b = map.biomeAt(x, z);
    Color c = biomeColor(b);
    float n = fbm(x * 0.03f, z * 0.03f, 99, 3);
    float f = 0.85f + n * 0.3f;
    c = {(unsigned char)clampf(c.r * f, 0, 255), (unsigned char)clampf(c.g * f, 0, 255), (unsigned char)clampf(c.b * f, 0, 255), 255};
    if (slope > 0.9f && b != Biome::Snow) c = {125, 118, 108, 255};
    if (b == Biome::Snow && slope > 1.2f) c = {170, 175, 185, 255};
    if (h < WATER_LEVEL + 0.6f) c = {205, 190, 140, 255};
    if (h < WATER_LEVEL - 1.5f) c = {150, 140, 110, 255};
    int ix = (int)(x / CELL), iz = (int)(z / CELL);
    if (ix >= 0 && iz >= 0 && ix < HM_N && iz < HM_N && map.roadMask[(size_t)iz * HM_N + ix]) c = {92, 92, 96, 255};
    // Volcano crater glow
    if (b == Biome::Volcanic && h > 55) c = {140, 60, 40, 255};
    return c;
}

void WorldRenderer::buildTerrain() {
    const GameMap& m = *map_;
    const int CH = 32;
    for (int cz = 0; cz < HM_N; cz += CH) {
        for (int cx = 0; cx < HM_N; cx += CH) {
            MeshBuilder mb;
            int n = CH + 1;
            for (int z = 0; z < n; z++)
                for (int x = 0; x < n; x++) {
                    int ix = cx + x, iz = cz + z;
                    float wx = ix * CELL, wz = iz * CELL;
                    float h = m.vertexHeight(ix, iz);
                    float hl = m.vertexHeight(std::max(0, ix - 1), iz), hr = m.vertexHeight(std::min(HM_N, ix + 1), iz);
                    float hd = m.vertexHeight(ix, std::max(0, iz - 1)), hu = m.vertexHeight(ix, std::min(HM_N, iz + 1));
                    si::Vec3 nrm = si::Vec3{hl - hr, 2 * CELL, hd - hu}.norm();
                    float slope = std::sqrt((hr - hl) * (hr - hl) + (hu - hd) * (hu - hd)) / (2 * CELL);
                    mb.vert({wx, h, wz}, nrm, terrainColor(m, wx, wz, h, slope));
                }
            for (int z = 0; z < CH; z++)
                for (int x = 0; x < CH; x++) {
                    unsigned short i00 = (unsigned short)(z * n + x), i10 = (unsigned short)(z * n + x + 1);
                    unsigned short i01 = (unsigned short)((z + 1) * n + x), i11 = (unsigned short)((z + 1) * n + x + 1);
                    // Matches GameMap::heightAt triangulation (diagonal 00-11)
                    mb.idx.insert(mb.idx.end(), {i00, i11, i10, i00, i01, i11});
                }
            mb.flush();
            for (auto& mesh : mb.done) {
                Model model = LoadModelFromMesh(mesh);
                model.materials[0].shader = light_->shader;
                terrain_.push_back(model);
                terrainCenters_.push_back({(cx + CH / 2) * CELL, 10, (cz + CH / 2) * CELL});
            }
        }
    }
    // Ocean
    MeshBuilder wb;
    float E = 6000;
    wb.quad({-E, WATER_LEVEL, -E}, {-E, WATER_LEVEL, E}, {E, WATER_LEVEL, E}, {E, WATER_LEVEL, -E}, {0, 1, 0}, Color{50, 125, 185, 255});
    wb.flush();
    water_ = LoadModelFromMesh(wb.done[0]);
    water_.materials[0].shader = light_->shader;
}

void WorldRenderer::buildChunk(Chunk& c, const CollisionWorld& world) {
    for (auto& m : c.models) UnloadModel(m);
    c.models.clear();
    MeshBuilder mb;
    for (uint32_t id : c.shapes) {
        const Shape* s = world.shape(id);
        if (!s || !s->alive) continue;
        Color col = C(s->color);
        col.a = 255;
        if (s->style == 1) col = {(unsigned char)std::min(255, col.r + 30), (unsigned char)std::min(255, col.g + 30), (unsigned char)std::min(255, col.b + 30), 255};
        col = jitterColor(col, id, 0.04f);
        if (s->kind == ShapeKind::Box) mb.aabb(s->box, col);
        else if (s->kind == ShapeKind::Ramp) mb.rampShape(s->box, s->rampDir, col);
        else {
            si::Vec3 base{(s->box.min.x + s->box.max.x) / 2, s->box.min.y, (s->box.min.z + s->box.max.z) / 2};
            mb.pyramid(base, (s->box.max.x - s->box.min.x) / 2, s->box.max.y - s->box.min.y, col);
        }
    }
    for (uint32_t di : c.decor) {
        if (decorDead_[di]) continue;
        const Decor& d = map_->decor[di];
        if (d.linkedShape != INVALID_ID) {
            const Shape* s = world.shape(d.linkedShape);
            if (!s || !s->alive) { decorDead_[di] = 1; continue; }
        }
        addDecor(mb, d);
    }
    mb.flush();
    for (auto& mesh : mb.done) {
        Model model = LoadModelFromMesh(mesh);
        model.materials[0].shader = light_->shader;
        c.models.push_back(model);
    }
    c.dirty = false;
}

void WorldRenderer::buildMinimap() {
    const GameMap& m = *map_;
    int N = minimapSize;
    Image img = GenImageColor(N, N, Color{40, 110, 170, 255});
    Color* px = (Color*)img.data;
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) {
            float wx = (x + 0.5f) / N * WORLD_SIZE, wz = (y + 0.5f) / N * WORLD_SIZE;
            float h = m.heightAt(wx, wz);
            Color c;
            if (h < WATER_LEVEL) {
                float d = clampf(-h / 16.0f, 0, 1);
                c = {(unsigned char)(70 - 30 * d), (unsigned char)(150 - 40 * d), (unsigned char)(205 - 30 * d), 255};
            } else {
                float hx = m.heightAt(wx + 3, wz) - m.heightAt(wx - 3, wz);
                float hz = m.heightAt(wx, wz + 3) - m.heightAt(wx, wz - 3);
                float slope = std::sqrt(hx * hx + hz * hz) / 6.0f;
                c = terrainColor(m, wx, wz, h, slope);
                float shadeF = clampf(1.0f + (-hx - hz) * 0.04f, 0.7f, 1.25f);
                c = {(unsigned char)clampf(c.r * shadeF, 0, 255), (unsigned char)clampf(c.g * shadeF, 0, 255), (unsigned char)clampf(c.b * shadeF, 0, 255), 255};
            }
            px[y * N + x] = c;
        }
    // Buildings
    for (auto& s : m.shapes) {
        AABB b = s.box;
        float area = (b.max.x - b.min.x) * (b.max.z - b.min.z);
        if (area < 3 || s.style == 7 || s.style == 6) continue;
        int x0 = (int)(b.min.x / WORLD_SIZE * N), x1 = (int)(b.max.x / WORLD_SIZE * N);
        int y0 = (int)(b.min.z / WORLD_SIZE * N), y1 = (int)(b.max.z / WORLD_SIZE * N);
        for (int y = std::max(0, y0); y <= std::min(N - 1, y1); y++)
            for (int x = std::max(0, x0); x <= std::min(N - 1, x1); x++) {
                Color c = C(s.color);
                px[y * N + x] = {(unsigned char)(c.r * 0.75f), (unsigned char)(c.g * 0.75f), (unsigned char)(c.b * 0.75f), 255};
            }
    }
    minimap = LoadTextureFromImage(img);
    SetTextureFilter(minimap, TEXTURE_FILTER_BILINEAR);
    UnloadImage(img);
}

void WorldRenderer::build(const GameMap& map, const CollisionWorld& world, Lighting& light) {
    unload();
    map_ = &map;
    light_ = &light;
    buildTerrain();
    const auto& shapes = world.staticShapes();
    for (auto& s : shapes) {
        int k = chunkKey(s.box.center().x, s.box.center().z);
        Chunk& c = chunks_[k];
        c.shapes.push_back(s.id);
        shapeChunk_[s.id] = k;
    }
    decorDead_.assign(map.decor.size(), 0);
    for (size_t i = 0; i < map.decor.size(); i++) {
        const Decor& d = map.decor[i];
        int k = d.linkedShape != INVALID_ID ? shapeChunk_[d.linkedShape] : chunkKey(d.pos.x, d.pos.z);
        chunks_[k].decor.push_back((uint32_t)i);
    }
    for (auto& [k, c] : chunks_) {
        int cx = k % 1024 - 64, cz = k / 1024 - 64;
        c.center = {(cx + 0.5f) * CHUNK, 10, (cz + 0.5f) * CHUNK};
        buildChunk(c, world);
    }
    buildMinimap();
    loaded_ = true;
}

void WorldRenderer::unload() {
    if (!loaded_) return;
    for (auto& m : terrain_) UnloadModel(m);
    terrain_.clear();
    terrainCenters_.clear();
    for (auto& [k, c] : chunks_)
        for (auto& m : c.models) UnloadModel(m);
    chunks_.clear();
    shapeChunk_.clear();
    UnloadModel(water_);
    UnloadTexture(minimap);
    loaded_ = false;
}

void WorldRenderer::markShapeRemoved(uint32_t id) {
    auto it = shapeChunk_.find(id);
    if (it != shapeChunk_.end()) chunks_[it->second].dirty = true;
}

void WorldRenderer::markAllDirty() {
    std::fill(decorDead_.begin(), decorDead_.end(), 0);
    for (auto& [k, c] : chunks_) c.dirty = true;
}

void WorldRenderer::rebuildDirty(const CollisionWorld& world, int maxPerFrame) {
    int n = 0;
    for (auto& [k, c] : chunks_) {
        if (!c.dirty) continue;
        buildChunk(c, world);
        if (++n >= maxPerFrame) break;
    }
}

void WorldRenderer::draw(const Camera3D& cam, float viewDist, float) {
    si::Vec3 cp = S(cam.position);
    si::Vec3 fwd = (S(cam.target) - cp).norm();
    auto visible = [&](const Vector3& c, float radius) {
        si::Vec3 d = S(c) - cp;
        float dist = d.lenXZ();
        if (dist > viewDist + radius) return false;
        if (dist > radius * 1.5f && d.dot(fwd) < -radius) return false;
        return true;
    };
    for (size_t i = 0; i < terrain_.size(); i++)
        if (visible(terrainCenters_[i], 100)) DrawModel(terrain_[i], {0, 0, 0}, 1.0f, WHITE);
    for (auto& [k, c] : chunks_) {
        if (!visible(c.center, 70)) continue;
        for (auto& m : c.models) DrawModel(m, {0, 0, 0}, 1.0f, WHITE);
    }
}

void WorldRenderer::drawWater(const Camera3D&, float time) {
    float a = 0.78f + 0.04f * std::sin(time * 0.7f);
    DrawModel(water_, {0, 0, 0}, 1.0f, Color{255, 255, 255, (unsigned char)(a * 255)});
}

} // namespace client
