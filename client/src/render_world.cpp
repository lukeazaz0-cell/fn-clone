#include <cmath>
#include <cstring>

#include "raylib.h"
#include "raymath.h"
#include "models.h"
#include "render.h"
#include "rlgl.h"

namespace client {

extern unsigned int gAtlasTex;

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

static void vtx(const si::Vec3& p, float ao = 1.0f) {
    rlTexCoord2f((float)drawMaterial(), ao);
    rlVertex3f(p.x, p.y, p.z);
}
static void quad(const si::Vec3& a, const si::Vec3& b, const si::Vec3& c, const si::Vec3& d, const si::Vec3& n) {
    rlNormal3f(n.x, n.y, n.z);
    vtx(a); vtx(b); vtx(c); vtx(d);
}

void drawBox(const si::Vec3& c, const si::Vec3& h, const Basis& b0, Color col) {
    // Winding assumes a right-handed basis; yaw bases are left-handed, so mirror r (the box is symmetric).
    Basis b = b0;
    if (b0.r.cross(b0.u).dot(b0.f) < 0) b.r = b0.r * -1.0f;
    si::Vec3 R = b.r * h.x, U = b.u * h.y, F = b.f * h.z;
    si::Vec3 p[8] = {c - R - U - F, c + R - U - F, c + R + U - F, c - R + U - F,
                     c - R - U + F, c + R - U + F, c + R + U + F, c - R + U + F};
    rlSetTexture(gAtlasTex);
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

// Quad with its winding chosen so it faces `out` (works for any basis handedness).
static void quadOut(const si::Vec3& a, const si::Vec3& b, const si::Vec3& c, const si::Vec3& d, const si::Vec3& na,
                    const si::Vec3& nb, const si::Vec3& nc, const si::Vec3& nd, const si::Vec3& out) {
    bool flip = (b - a).cross(c - a).dot(out) < 0;
    const si::Vec3* P[4] = {&a, &b, &c, &d};
    const si::Vec3* N[4] = {&na, &nb, &nc, &nd};
    for (int k = 0; k < 4; k++) {
        int i = flip ? 3 - k : k;
        rlNormal3f(N[i]->x, N[i]->y, N[i]->z);
        vtx(*P[i]);
    }
}

void drawSphere(const si::Vec3& c, const si::Vec3& radii, const Basis& b, Color col, int rings, int segs) {
    rlSetTexture(gAtlasTex);
    rlBegin(RL_QUADS);
    rlColor4ub(col.r, col.g, col.b, col.a);
    auto dirAt = [&](int i, int j) {
        float lat = -kPi / 2 + kPi * i / rings, lon = 2 * kPi * j / segs;
        return si::Vec3{std::cos(lat) * std::cos(lon), std::sin(lat), std::cos(lat) * std::sin(lon)};
    };
    auto pt = [&](const si::Vec3& d) { return c + b.r * (d.x * radii.x) + b.u * (d.y * radii.y) + b.f * (d.z * radii.z); };
    auto nrm = [&](const si::Vec3& d) {
        return (b.r * (d.x / radii.x) + b.u * (d.y / radii.y) + b.f * (d.z / radii.z)).norm();
    };
    for (int i = 0; i < rings; i++)
        for (int j = 0; j < segs; j++) {
            si::Vec3 d0 = dirAt(i, j), d1 = dirAt(i, j + 1), d2 = dirAt(i + 1, j + 1), d3 = dirAt(i + 1, j);
            si::Vec3 mid = (pt(d0) + pt(d2)) * 0.5f;
            quadOut(pt(d0), pt(d1), pt(d2), pt(d3), nrm(d0), nrm(d1), nrm(d2), nrm(d3), mid - c);
        }
    rlEnd();
}

void drawCylinder(const si::Vec3& c, const si::Vec3& axis, float radius, float halfLen, Color col, int segs, float spin) {
    si::Vec3 a = axis.norm();
    si::Vec3 ref = std::fabs(a.y) < 0.9f ? si::Vec3{0, 1, 0} : si::Vec3{1, 0, 0};
    si::Vec3 u = a.cross(ref).norm(), v = a.cross(u).norm();
    auto ring = [&](int j) {
        float t = spin + 2 * kPi * j / segs;
        return u * std::cos(t) + v * std::sin(t);
    };
    rlSetTexture(gAtlasTex);
    rlBegin(RL_QUADS);
    rlColor4ub(col.r, col.g, col.b, col.a);
    si::Vec3 e0 = c - a * halfLen, e1 = c + a * halfLen;
    for (int j = 0; j < segs; j++) {
        si::Vec3 r0 = ring(j), r1 = ring(j + 1);
        quadOut(e0 + r0 * radius, e0 + r1 * radius, e1 + r1 * radius, e1 + r0 * radius, r0, r1, r1, r0, (r0 + r1) * 0.5f);
        // caps as thin quads to the centre
        si::Vec3 na = a * -1.0f;
        quadOut(e0, e0 + r0 * radius, e0 + r1 * radius, e0, na, na, na, na, na);
        quadOut(e1, e1 + r0 * radius, e1 + r1 * radius, e1, a, a, a, a, a);
    }
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
    rlSetTexture(gAtlasTex);
    rlBegin(RL_QUADS);
    rlColor4ub(col.r, col.g, col.b, col.a);
    quad(c[0], c[1], c[2], c[3], n);
    quad(c[3], c[2], c[1], c[0], n * -1.0f);
    rlEnd();
}

void drawPyramid(const AABB& b, Color col) {
    si::Vec3 apex{(b.min.x + b.max.x) / 2, b.max.y, (b.min.z + b.max.z) / 2};
    si::Vec3 c0{b.min.x, b.min.y, b.min.z}, c1{b.max.x, b.min.y, b.min.z}, c2{b.max.x, b.min.y, b.max.z}, c3{b.min.x, b.min.y, b.max.z};
    rlSetTexture(gAtlasTex);
    rlBegin(RL_TRIANGLES);
    rlColor4ub(col.r, col.g, col.b, col.a);
    auto tri = [&](const si::Vec3& a, const si::Vec3& b2, const si::Vec3& c) {
        si::Vec3 n = (b2 - a).cross(c - a).norm();
        if (n.y < 0) { n = n * -1.0f; }
        rlNormal3f(n.x, n.y, n.z);
        vtx(a); vtx(b2); vtx(c);
        vtx(c); vtx(b2); vtx(a);
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
    std::vector<float> pos, nrm, tc, tc2;
    std::vector<unsigned char> col;
    float curMat = 0;   // surface material id written to texcoord.x
    float curAO = 1.0f; // ambient occlusion written to texcoord.y
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
        m.texcoords = (float*)MemAlloc((unsigned)(tc.size() * sizeof(float)));
        m.texcoords2 = (float*)MemAlloc((unsigned)(tc2.size() * sizeof(float)));
        std::memcpy(m.texcoords, tc.data(), tc.size() * sizeof(float));
        std::memcpy(m.texcoords2, tc2.data(), tc2.size() * sizeof(float));
        m.indices = (unsigned short*)MemAlloc((unsigned)(idx.size() * sizeof(unsigned short)));
        std::memcpy(m.vertices, pos.data(), pos.size() * sizeof(float));
        std::memcpy(m.normals, nrm.data(), nrm.size() * sizeof(float));
        std::memcpy(m.colors, col.data(), col.size());
        std::memcpy(m.indices, idx.data(), idx.size() * sizeof(unsigned short));
        UploadMesh(&m, false);
        done.push_back(m);
        pos.clear(); nrm.clear(); col.clear(); idx.clear(); tc.clear(); tc2.clear();
    }
    unsigned short vert(const si::Vec3& p, const si::Vec3& n, Color c, float ao = -1.0f) {
        pos.push_back(p.x); pos.push_back(p.y); pos.push_back(p.z);
        nrm.push_back(n.x); nrm.push_back(n.y); nrm.push_back(n.z);
        col.push_back(c.r); col.push_back(c.g); col.push_back(c.b); col.push_back(255);
        tc.push_back(curMat); tc.push_back(ao < 0 ? curAO : ao);
        tc2.push_back(0); tc2.push_back(0);
        return (unsigned short)(verts() - 1);
    }
    // Terrain vertex: color alpha = baked AO, texcoords = blend weights.
    unsigned short vertT(const si::Vec3& p, const si::Vec3& n, Color c, float ao, float wg, float ws, float wsn, float wr) {
        pos.push_back(p.x); pos.push_back(p.y); pos.push_back(p.z);
        nrm.push_back(n.x); nrm.push_back(n.y); nrm.push_back(n.z);
        col.push_back(c.r); col.push_back(c.g); col.push_back(c.b); col.push_back((unsigned char)(clampf(ao, 0, 1) * 255));
        tc.push_back(wg); tc.push_back(ws);
        tc2.push_back(wsn); tc2.push_back(wr);
        return (unsigned short)(verts() - 1);
    }
    void quadAO(const si::Vec3& a, const si::Vec3& b, const si::Vec3& c, const si::Vec3& d, const si::Vec3& n, Color col, float aa, float ab, float ac, float ad) {
        flushIfNeeded(4);
        unsigned short i0 = vert(a, n, col, aa), i1 = vert(b, n, col, ab), i2 = vert(c, n, col, ac), i3 = vert(d, n, col, ad);
        idx.insert(idx.end(), {i0, i1, i2, i0, i2, i3});
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
    void box(const si::Vec3& c, const si::Vec3& h, const Basis& b0, Color col) {
        Basis b = b0;
        if (b0.r.cross(b0.u).dot(b0.f) < 0) b.r = b0.r * -1.0f;
        si::Vec3 R = b.r * h.x, U = b.u * h.y, F = b.f * h.z;
        si::Vec3 p[8] = {c - R - U - F, c + R - U - F, c + R + U - F, c - R + U - F,
                         c - R - U + F, c + R - U + F, c + R + U + F, c - R + U + F};
        Color top = col, side = col;
        // Upright boxes get baked contact occlusion toward their base.
        float lo = (b.u.y > 0.9f && h.y > 0.15f) ? clampf(0.45f + 0.12f / std::max(0.2f, h.y), 0.45f, 0.8f) : 1.0f;
        float hi = 1.0f;
        // Counter-clockwise when viewed from outside
        quadAO(p[4], p[5], p[6], p[7], b.f, side, lo, lo, hi, hi);
        quadAO(p[1], p[0], p[3], p[2], b.f * -1.0f, side, lo, lo, hi, hi);
        quad(p[3], p[7], p[6], p[2], b.u, top);
        quadAO(p[0], p[1], p[5], p[4], b.u * -1.0f, side, lo, lo, lo, lo);
        quadAO(p[1], p[2], p[6], p[5], b.r, side, lo, hi, hi, lo);
        quadAO(p[0], p[4], p[7], p[3], b.r * -1.0f, side, lo, lo, hi, hi);
    }
    void aabb(const AABB& bb, Color col) { box(bb.center(), bb.size() * 0.5f, Basis(), col); }
    // N-sided prism (barrels, poles)
    void prism(const si::Vec3& base, float radius, float h, int sides, Color col) {
        for (int i = 0; i < sides; i++) {
            float a0 = i * 2 * kPi / sides, a1 = (i + 1) * 2 * kPi / sides;
            si::Vec3 p0 = base + si::Vec3{std::cos(a0) * radius, 0, std::sin(a0) * radius};
            si::Vec3 p1 = base + si::Vec3{std::cos(a1) * radius, 0, std::sin(a1) * radius};
            si::Vec3 up{0, h, 0};
            si::Vec3 n = si::Vec3{std::cos((a0 + a1) / 2), 0, std::sin((a0 + a1) / 2)};
            quadAO(p1, p0, p0 + up, p1 + up, n, col, 0.6f, 0.6f, 1.0f, 1.0f);
            tri(p0 + up, base + up, p1 + up, col);
        }
    }
    // N-sided cone (used for pine tiers)
    void cone(const si::Vec3& base, float radius, float h, int sides, Color col, float rot = 0) {
        si::Vec3 apex = base + si::Vec3{0, h, 0};
        for (int i = 0; i < sides; i++) {
            float a0 = rot + i * 2 * kPi / sides, a1 = rot + (i + 1) * 2 * kPi / sides;
            si::Vec3 p0 = base + si::Vec3{std::cos(a0) * radius, 0, std::sin(a0) * radius};
            si::Vec3 p1 = base + si::Vec3{std::cos(a1) * radius, 0, std::sin(a1) * radius};
            Color c = shade4(col, 0.9f + 0.2f * (0.5f + 0.5f * std::cos(a0 + 0.8f)));
            tri(p1, p0, apex, c);
            tri(p0, p1, base, shade4(col, 0.6f));
        }
    }
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
    float rot = (float)(((int)(p.x * 7.3f + p.z * 3.1f)) % 628) / 100.0f; // stable per-object variation
    auto sh = MeshBuilder::shade4;
    switch (d.kind) {
        case DecorKind::PineTree:
        case DecorKind::SnowPine: {
            Color snow{235, 240, 245, 255};
            float y = -0.8f * s;
            float r = 2.8f * s;
            for (int i = 0; i < 4; i++) {
                Color tier = sh(c, 0.92f + i * 0.06f);
                mb.cone(p + si::Vec3{0, y, 0}, r, 2.7f * s, 8, tier, rot + i * 0.4f);
                if (d.kind == DecorKind::SnowPine) mb.cone(p + si::Vec3{0, y + 1.7f * s, 0}, r * 0.38f, 1.0f * s, 8, snow, rot);
                y += 1.45f * s;
                r *= 0.74f;
            }
            break;
        }
        case DecorKind::OakTree: {
            const float off[7][4] = {{0, 1.6f, 0, 2.0f}, {1.3f, 1.3f, 0.4f, 1.3f}, {-1.2f, 1.2f, -0.5f, 1.3f}, {0.3f, 1.2f, 1.3f, 1.2f},
                                     {-0.4f, 1.3f, -1.3f, 1.2f}, {0.2f, 2.9f, 0.1f, 1.3f}, {0.9f, 2.4f, -0.8f, 1.0f}};
            for (int i = 0; i < 7; i++) {
                float k = off[i][3] * s;
                mb.box(p + si::Vec3{off[i][0], off[i][1], off[i][2]} * s, {k, k * 0.8f, k}, yawBasis(rot + i), sh(c, 0.88f + (i % 3) * 0.08f));
            }
            // A couple of branches poking out of the trunk
            mb.box(p + si::Vec3{0.6f * s, 0.2f, 0}, si::Vec3{0.6f, 0.08f, 0.08f} * s, compose(yawBasis(rot), yawPitchBasis(0, 0.5f)), Color{95, 68, 44, 255});
            break;
        }
        case DecorKind::JungleTree:
            mb.box(p + si::Vec3{0, 0.8f * s, 0}, si::Vec3{3.6f, 0.9f, 3.6f} * s, yawBasis(rot), c);
            mb.box(p + si::Vec3{0.5f * s, 1.9f * s, -0.3f * s}, si::Vec3{2.6f, 0.8f, 2.6f} * s, yawBasis(rot + 0.7f), sh(c, 1.12f));
            mb.box(p + si::Vec3{-0.3f * s, 2.8f * s, 0.2f * s}, si::Vec3{1.5f, 0.6f, 1.5f} * s, yawBasis(rot + 1.3f), sh(c, 1.2f));
            for (int i = 0; i < 5; i++) { // hanging vines
                float a = rot + i * 1.26f;
                si::Vec3 v = p + si::Vec3{std::cos(a) * 3.0f * s, -0.9f * s, std::sin(a) * 3.0f * s};
                mb.box(v, si::Vec3{0.06f, 1.0f + (i % 3) * 0.4f, 0.06f} * s, Basis(), sh(c, 0.8f));
            }
            break;
        case DecorKind::PalmTree:
            for (int i = 0; i < 7; i++) {
                float a = i * 2 * kPi / 7 + rot;
                Basis b1 = compose(yawBasis(a), yawPitchBasis(0, -0.15f));
                Basis b2 = compose(yawBasis(a), yawPitchBasis(0, -0.75f));
                mb.box(p + b1.f * (1.0f * s), si::Vec3{0.45f, 0.06f, 1.05f} * s, b1, c);
                mb.box(p + b1.f * (2.0f * s) + b2.f * (0.8f * s), si::Vec3{0.35f, 0.05f, 0.85f} * s, b2, sh(c, 0.88f));
            }
            for (int i = 0; i < 3; i++) {
                float a = rot + i * 2.1f;
                mb.box(p + si::Vec3{std::cos(a) * 0.35f, -0.35f, std::sin(a) * 0.35f} * s, si::Vec3{0.18f, 0.18f, 0.18f} * s, Basis(), Color{110, 80, 45, 255});
            }
            break;
        case DecorKind::Bush:
            mb.box(p + si::Vec3{0, 0.45f * s, 0}, si::Vec3{0.75f, 0.5f, 0.75f} * s, yawBasis(rot), c);
            mb.box(p + si::Vec3{0.55f * s, 0.35f * s, 0.2f * s}, si::Vec3{0.5f, 0.38f, 0.5f} * s, yawBasis(rot + 0.8f), sh(c, 1.1f));
            mb.box(p + si::Vec3{-0.45f * s, 0.32f * s, -0.3f * s}, si::Vec3{0.45f, 0.35f, 0.45f} * s, yawBasis(rot + 1.7f), sh(c, 0.9f));
            mb.box(p + si::Vec3{0.1f * s, 0.85f * s, -0.1f * s}, si::Vec3{0.4f, 0.28f, 0.4f} * s, yawBasis(rot + 2.3f), sh(c, 1.18f));
            break;
        case DecorKind::Flower:
            mb.box(p + si::Vec3{0, 0.2f, 0}, {0.03f, 0.2f, 0.03f}, Basis(), Color{60, 130, 50, 255});
            mb.box(p + si::Vec3{0.06f, 0.12f, 0}, {0.07f, 0.015f, 0.03f}, yawBasis(rot), Color{70, 150, 60, 255});
            for (int i = 0; i < 4; i++) {
                float a = rot + i * kPi / 2;
                mb.box(p + si::Vec3{std::cos(a) * 0.09f, 0.42f, std::sin(a) * 0.09f}, {0.07f, 0.02f, 0.07f}, yawBasis(a), c);
            }
            mb.box(p + si::Vec3{0, 0.44f, 0}, {0.05f, 0.03f, 0.05f}, Basis(), Color{250, 220, 70, 255});
            break;
        case DecorKind::Crop:
            mb.box(p + si::Vec3{0, 0.6f * s, 0}, si::Vec3{0.08f, 0.6f, 0.08f} * s, Basis(), sh(c, 0.8f));
            mb.box(p + si::Vec3{0, 1.1f * s, 0}, si::Vec3{0.12f, 0.2f, 0.12f} * s, yawBasis(rot), c);
            mb.box(p + si::Vec3{0.1f, 0.6f * s, 0}, si::Vec3{0.15f, 0.03f, 0.05f} * s, yawBasis(rot), Color{90, 150, 60, 255});
            break;
        case DecorKind::Lamp:
            mb.box(p + si::Vec3{0, 0.3f, 0}, {0.3f, 0.35f, 0.3f}, Basis(), Color{255, 170, 80, 255});
            mb.box(p + si::Vec3{0, 0.7f, 0}, {0.38f, 0.05f, 0.38f}, Basis(), Color{60, 40, 30, 255});
            mb.box(p + si::Vec3{0, -0.08f, 0}, {0.34f, 0.04f, 0.34f}, Basis(), Color{60, 40, 30, 255});
            break;
        case DecorKind::Cactus:
            mb.box(p + si::Vec3{0.6f * s, -1.2f * s, 0}, si::Vec3{0.45f, 0.15f, 0.2f} * s, Basis(), c);
            mb.box(p + si::Vec3{0.95f * s, -0.7f * s, 0}, si::Vec3{0.18f, 0.55f, 0.18f} * s, Basis(), c);
            mb.box(p + si::Vec3{-0.55f * s, -1.8f * s, 0}, si::Vec3{0.4f, 0.14f, 0.2f} * s, Basis(), c);
            mb.box(p + si::Vec3{-0.85f * s, -1.4f * s, 0}, si::Vec3{0.16f, 0.45f, 0.16f} * s, Basis(), c);
            mb.box(p + si::Vec3{0, 0.08f * s, 0}, si::Vec3{0.2f, 0.1f, 0.2f} * s, Basis(), Color{240, 110, 150, 255}); // bloom
            break;
        case DecorKind::DeadTree:
            mb.box(p + si::Vec3{0.6f * s, -0.8f * s, 0}, si::Vec3{0.8f, 0.1f, 0.1f} * s, compose(yawBasis(0.5f + rot), yawPitchBasis(0, 0.4f)), c);
            mb.box(p + si::Vec3{-0.5f * s, -1.6f * s, 0.2f}, si::Vec3{0.7f, 0.1f, 0.1f} * s, compose(yawBasis(2.3f + rot), yawPitchBasis(0, 0.3f)), c);
            mb.box(p + si::Vec3{0.2f * s, 0.1f * s, -0.3f}, si::Vec3{0.5f, 0.07f, 0.07f} * s, compose(yawBasis(4.0f + rot), yawPitchBasis(0, 0.6f)), c);
            break;
        case DecorKind::Glass:
        case DecorKind::Trim:
            mb.box(p, d.size, Basis(), c);
            break;
        case DecorKind::RoadLine:
            mb.box(p, d.size, yawBasis(d.yaw), c);
            break;
        case DecorKind::GrassTuft: {
            for (int i = 0; i < 5; i++) {
                float a = d.yaw + i * 1.3f;
                Basis bb = compose(yawBasis(a), yawPitchBasis(0, 0.25f * ((i % 3) - 1)));
                float hgt = (0.25f + 0.08f * (i % 3)) * s;
                mb.box(p + si::Vec3{std::cos(a) * 0.12f, hgt, std::sin(a) * 0.12f}, {0.025f * s, hgt, 0.01f}, bb, sh(c, 0.85f + 0.07f * i));
            }
            break;
        }
        case DecorKind::StreetLamp: {
            Basis b = yawBasis(d.yaw);
            mb.prism(p - si::Vec3{0, 0.2f, 0}, 0.12f, 5.6f, 8, c);
            mb.box(p + si::Vec3{0, 5.5f, 0} + b.r * 0.7f, {0.75f, 0.06f, 0.06f}, b, c);
            mb.box(p + si::Vec3{0, 5.3f, 0} + b.r * 1.35f, {0.22f, 0.12f, 0.3f}, b, sh(c, 0.8f));
            mb.box(p + si::Vec3{0, 5.16f, 0} + b.r * 1.35f, {0.18f, 0.03f, 0.25f}, b, Color{255, 236, 170, 255});
            mb.prism(p - si::Vec3{0, 0.2f, 0}, 0.22f, 0.5f, 8, sh(c, 0.8f));
            break;
        }
        case DecorKind::Bench: {
            Basis b = yawBasis(d.yaw);
            for (int k = 0; k < 3; k++) mb.box(p + si::Vec3{0, 0.48f, 0} + b.f * (-0.15f + k * 0.15f), {0.9f, 0.03f, 0.06f}, b, c);
            for (int k = 0; k < 2; k++) mb.box(p + si::Vec3{0, 0.75f + k * 0.18f, 0} - b.f * 0.26f, {0.9f, 0.06f, 0.025f}, b, c);
            for (int sd = -1; sd <= 1; sd += 2) {
                mb.box(p + si::Vec3{0, 0.24f, 0} + b.r * (0.75f * sd), {0.04f, 0.24f, 0.25f}, b, Color{55, 55, 60, 255});
                mb.box(p + si::Vec3{0, 0.75f, 0} + b.r * (0.75f * sd) - b.f * 0.26f, {0.04f, 0.3f, 0.03f}, b, Color{55, 55, 60, 255});
            }
            break;
        }
        case DecorKind::Mailbox: {
            Basis b = yawBasis(d.yaw);
            mb.box(p + si::Vec3{0, 0.5f, 0}, {0.05f, 0.5f, 0.05f}, b, Color{110, 80, 50, 255});
            mb.box(p + si::Vec3{0, 1.12f, 0}, {0.16f, 0.14f, 0.26f}, b, c);
            mb.box(p + si::Vec3{0.18f, 1.2f, 0.1f}, {0.02f, 0.1f, 0.03f}, b, Color{220, 50, 40, 255});
            break;
        }
        case DecorKind::Hydrant:
            mb.prism(p - si::Vec3{0, 0.05f, 0}, 0.16f, 0.65f, 8, c);
            mb.prism(p + si::Vec3{0, 0.6f, 0}, 0.2f, 0.08f, 8, sh(c, 0.8f));
            mb.box(p + si::Vec3{0, 0.4f, 0}, {0.27f, 0.06f, 0.06f}, Basis(), sh(c, 0.85f));
            mb.box(p + si::Vec3{0, 0.74f, 0}, {0.08f, 0.06f, 0.08f}, Basis(), sh(c, 0.9f));
            break;
        case DecorKind::Barrel:
            mb.prism(p, 0.38f, 1.1f, 10, c);
            mb.prism(p + si::Vec3{0, 0.3f, 0}, 0.395f, 0.06f, 10, sh(c, 0.7f));
            mb.prism(p + si::Vec3{0, 0.8f, 0}, 0.395f, 0.06f, 10, sh(c, 0.7f));
            mb.box(p + si::Vec3{0.18f, 1.11f, 0}, {0.06f, 0.015f, 0.06f}, Basis(), sh(c, 0.6f));
            break;
        case DecorKind::Crate: {
            // Dark frame edges around the wooden crate shape
            si::Vec3 h = d.size;
            si::Vec3 ctr = p + si::Vec3{0, h.y, 0};
            float e = 0.06f;
            for (int sx = -1; sx <= 1; sx += 2)
                for (int sz = -1; sz <= 1; sz += 2) mb.box(ctr + si::Vec3{sx * (h.x - e + 0.01f), 0, sz * (h.z - e + 0.01f)}, {e, h.y + 0.01f, e}, Basis(), c);
            for (int sy = -1; sy <= 1; sy += 2) {
                mb.box(ctr + si::Vec3{0, sy * (h.y - e), h.z - e + 0.01f}, {h.x, e, e}, Basis(), c);
                mb.box(ctr + si::Vec3{0, sy * (h.y - e), -(h.z - e + 0.01f)}, {h.x, e, e}, Basis(), c);
                mb.box(ctr + si::Vec3{h.x - e + 0.01f, sy * (h.y - e), 0}, {e, e, h.z}, Basis(), c);
                mb.box(ctr + si::Vec3{-(h.x - e + 0.01f), sy * (h.y - e), 0}, {e, e, h.z}, Basis(), c);
            }
            break;
        }
        default: break;
    }
}

// Pick a surface texture for a static map shape from its material and render style.
int surfaceFor(const Shape& s) {
    switch (s.style) {
        case 1: return M_GLASS;
        case 3: case 4: return M_METAL;
        case 5: return M_FABRIC;
        case 6: return M_ROCK;
        case 7: return s.mat == si::Material::Wood ? M_BARK : M_PLAIN;
        case 8: return M_TILE;
        default: break;
    }
    si::Vec3 sz = s.box.size();
    bool slab = sz.y < 0.45f && sz.x > 1.5f && sz.z > 1.5f;
    if (s.kind != ShapeKind::Box && s.mat == si::Material::Wood) return M_ROOF;
    switch (s.mat) {
        case si::Material::Wood: return M_WOOD;
        case si::Material::Metal: return slab ? M_METAL : M_METAL;
        case si::Material::Brick: {
            if (!s.destructible) return sz.y > 3.0f ? M_ROCK : M_PLASTER; // foundations / temple blocks
            if (slab) return M_TILE;
            bool reddish = s.color.r > s.color.g + 25 && s.color.r > s.color.b + 25;
            return reddish ? M_BRICK : M_PLASTER;
        }
        default: return M_FABRIC;
    }
}

Color biomeColor(Biome b) {
    switch (b) {
        case Biome::Grass: return {100, 142, 70, 255};
        case Biome::Forest: return {78, 118, 60, 255};
        case Biome::Snow: return {232, 238, 245, 255};
        case Biome::Desert: return {222, 192, 132, 255};
        case Biome::Jungle: return {72, 132, 62, 255};
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
    if (slope > 0.9f && b != Biome::Snow) c = {138, 130, 118, 255};
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
                    // Texture blend weights from biome / road / water
                    Biome bio = m.biomeAt(wx, wz);
                    float wg = 0, ws = 0, wsn = 0, wr = 0;
                    switch (bio) {
                        case Biome::Snow: wsn = 1; break;
                        case Biome::Desert: case Biome::Beach: ws = 1; break;
                        case Biome::City: case Biome::Volcanic: wr = 1; break;
                        default: wg = 1; break;
                    }
                    int rx = std::min(HM_N - 1, ix), rz = std::min(HM_N - 1, iz);
                    if (m.roadMask[(size_t)rz * HM_N + rx]) { wg = ws = wsn = 0; wr = 1; }
                    if (h < WATER_LEVEL + 0.6f) { wg = wsn = wr = 0; ws = 1; }
                    // Baked ambient occlusion: valleys and creases are darker
                    float avg = 0;
                    int cnt = 0;
                    for (int oz = -3; oz <= 3; oz += 3)
                        for (int ox = -3; ox <= 3; ox += 3) {
                            avg += m.vertexHeight(clampf(ix + ox, 0, HM_N), clampf(iz + oz, 0, HM_N));
                            cnt++;
                        }
                    avg /= cnt;
                    float ao = clampf(1.0f - std::max(0.0f, avg - h) * 0.08f, 0.5f, 1.0f);
                    mb.vertT({wx, h, wz}, nrm, terrainColor(m, wx, wz, h, slope), ao, wg, ws, wsn, wr);
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
                model.materials[0].shader = light_->terrain;
                model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = light_->atlas;
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
    water_.materials[0].shader = light_->water;
    water_.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = light_->heightTex;
}

void WorldRenderer::buildChunk(Chunk& c, const CollisionWorld& world) {
    for (auto& m : c.models) UnloadModel(m);
    c.models.clear();
    MeshBuilder mb;
    for (uint32_t id : c.shapes) {
        const Shape* s = world.shape(id);
        if (!s || !s->alive || s->style == 9) continue; // style 9 = collision only, drawn by its decor
        // Style 11 = collision for a model-file prop; draw the box only when models are unavailable.
        if (s->style == 11 && models() && models()->has(PropModel::TruckGreen)) continue;
        Color col = C(s->color);
        col.a = 255;
        if (s->style == 1) col = {(unsigned char)std::min(255, col.r + 30), (unsigned char)std::min(255, col.g + 30), (unsigned char)std::min(255, col.b + 30), 255};
        col = jitterColor(col, id, 0.04f);
        mb.curMat = (float)surfaceFor(*s);
        if (s->kind == ShapeKind::Box) mb.aabb(s->box, col);
        else if (s->kind == ShapeKind::Ramp) mb.rampShape(s->box, s->rampDir, col);
        else {
            si::Vec3 base{(s->box.min.x + s->box.max.x) / 2, s->box.min.y, (s->box.min.z + s->box.max.z) / 2};
            mb.pyramid(base, (s->box.max.x - s->box.min.x) / 2, s->box.max.y - s->box.min.y, col);
        }
    }
    for (uint32_t di : c.decor) {
        if (decorDead_[di]) continue;
        Decor d = map_->decor[di];
        if (d.linkedShape != INVALID_ID) {
            const Shape* s = world.shape(d.linkedShape);
            if (!s || !s->alive) { decorDead_[di] = 1; continue; }
        }
        if (d.kind == DecorKind::Model) {
            // Only reached when the model file is missing: bake a procedural stand-in for trees.
            if (d.model != PropModel::PineModel && d.model != PropModel::TreeCluster) continue;
            d.kind = DecorKind::PineTree;
            d.pos.y += 2.0f;
            d.scale = 1.2f;
        }
        switch (d.kind) {
            case DecorKind::Glass: mb.curMat = M_GLASS; break;
            case DecorKind::Trim: mb.curMat = M_PLASTER; break;
            case DecorKind::Flower: case DecorKind::Lamp: mb.curMat = M_PLAIN; break;
            case DecorKind::Cactus: case DecorKind::DeadTree: mb.curMat = M_BARK; break;
            case DecorKind::Bench: case DecorKind::Crate: case DecorKind::Fence: mb.curMat = M_WOOD; break;
            case DecorKind::Barrel: case DecorKind::StreetLamp: case DecorKind::Mailbox: case DecorKind::Hydrant: mb.curMat = M_METAL; break;
            case DecorKind::RoadLine: mb.curMat = M_PLAIN; break;
            case DecorKind::GrassTuft: mb.curMat = M_LEAVES; break;
            default: mb.curMat = M_LEAVES; break;
        }
        addDecor(mb, d);
    }
    mb.flush();
    for (auto& mesh : mb.done) {
        Model model = LoadModelFromMesh(mesh);
        model.materials[0].shader = light_->shader;
        model.materials[0].maps[MATERIAL_MAP_DIFFUSE].texture = light_->atlas;
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
        if (area < 3 || s.style == 7 || s.style == 6 || s.style == 9) continue;
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
    world_ = &world;
    light_ = &light;
    light.setHeightmap(map);
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
        if (d.kind == DecorKind::Model && models() && models()->has(d.model)) chunks_[k].modelDecor.push_back((uint32_t)i);
        else chunks_[k].decor.push_back((uint32_t)i);
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
    drawModelDecor(cp, viewDist * 0.75f, nullptr);
}

void WorldRenderer::drawModelDecor(const si::Vec3& center, float radius, Shader* override) {
    ModelLibrary* lib = models();
    if (!lib || !world_) return;
    for (auto& [k, c] : chunks_) {
        if (c.modelDecor.empty()) continue;
        if (distXZ(S(c.center), center) > radius + 60) continue;
        for (uint32_t di : c.modelDecor) {
            const Decor& d = map_->decor[di];
            if (d.linkedShape != INVALID_ID) {
                const Shape* s = world_->shape(d.linkedShape);
                if (!s || !s->alive) continue;
            }
            if (distXZ(d.pos, center) > radius) continue;
            lib->drawProp(d.model, d.pos, d.yaw, d.scale, override);
        }
    }
}

void WorldRenderer::drawNear(const Vector3& center, float radius, Shader override) {
    auto near = [&](const Vector3& c, float r) {
        float dx = c.x - center.x, dz = c.z - center.z;
        return std::sqrt(dx * dx + dz * dz) < radius + r;
    };
    auto drawWith = [&](Model& m) {
        Shader keep = m.materials[0].shader;
        m.materials[0].shader = override;
        DrawModel(m, {0, 0, 0}, 1.0f, WHITE);
        m.materials[0].shader = keep;
    };
    for (size_t i = 0; i < terrain_.size(); i++)
        if (near(terrainCenters_[i], 100)) drawWith(terrain_[i]);
    for (auto& [k, c] : chunks_)
        if (near(c.center, 60))
            for (auto& m : c.models) drawWith(m);
    drawModelDecor(S(center), radius, &override);
}

void WorldRenderer::drawWater(const Camera3D&, float) {
    DrawModel(water_, {0, 0, 0}, 1.0f, WHITE);
}

} // namespace client
