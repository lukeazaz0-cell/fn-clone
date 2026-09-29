#include "world.h"

#include <limits>

#include "map.h"

namespace si {

float shapeSurfaceY(const Shape& s, float x, float z) {
    const AABB& b = s.box;
    if (s.kind == ShapeKind::Box) return b.max.y;
    float h = b.max.y - b.min.y;
    if (s.kind == ShapeKind::Ramp) {
        float t = 0;
        switch (s.rampDir) {
            case RAMP_PX: t = (x - b.min.x) / (b.max.x - b.min.x); break;
            case RAMP_NX: t = (b.max.x - x) / (b.max.x - b.min.x); break;
            case RAMP_PZ: t = (z - b.min.z) / (b.max.z - b.min.z); break;
            default: t = (b.max.z - z) / (b.max.z - b.min.z); break;
        }
        return b.min.y + clampf(t, 0, 1) * h;
    }
    // Cone / pyramid roof
    float cx = (b.min.x + b.max.x) * 0.5f, cz = (b.min.z + b.max.z) * 0.5f;
    float hx = (b.max.x - b.min.x) * 0.5f, hz = (b.max.z - b.min.z) * 0.5f;
    float d = std::max(std::fabs(x - cx) / hx, std::fabs(z - cz) / hz);
    return b.min.y + (1.0f - clampf(d, 0, 1)) * h;
}

static void rampCorners(const Shape& s, Vec3 out[4]) {
    const AABB& b = s.box;
    float y0 = b.min.y, y1 = b.max.y;
    switch (s.rampDir) {
        case RAMP_PX:
            out[0] = {b.min.x, y0, b.min.z}; out[1] = {b.min.x, y0, b.max.z};
            out[2] = {b.max.x, y1, b.max.z}; out[3] = {b.max.x, y1, b.min.z}; break;
        case RAMP_NX:
            out[0] = {b.max.x, y0, b.min.z}; out[1] = {b.max.x, y0, b.max.z};
            out[2] = {b.min.x, y1, b.max.z}; out[3] = {b.min.x, y1, b.min.z}; break;
        case RAMP_PZ:
            out[0] = {b.min.x, y0, b.min.z}; out[1] = {b.max.x, y0, b.min.z};
            out[2] = {b.max.x, y1, b.max.z}; out[3] = {b.min.x, y1, b.max.z}; break;
        default:
            out[0] = {b.min.x, y0, b.max.z}; out[1] = {b.max.x, y0, b.max.z};
            out[2] = {b.max.x, y1, b.min.z}; out[3] = {b.min.x, y1, b.min.z}; break;
    }
}

bool rayShape(const Shape& s, const Vec3& o, const Vec3& d, float maxT, float& t, Vec3* n) {
    if (s.kind == ShapeKind::Box) {
        if (!rayAABB(o, d, s.box, maxT, t, n)) return false;
        return t <= maxT;
    }
    float tb;
    if (!rayAABB(o, d, s.box, maxT, tb)) return false;
    float best = std::numeric_limits<float>::max();
    Vec3 bestN;
    auto tri = [&](const Vec3& a, const Vec3& b, const Vec3& c) {
        float tt;
        if (rayTri(o, d, a, b, c, tt) && tt < best && tt <= maxT) {
            best = tt;
            bestN = (b - a).cross(c - a).norm();
            if (bestN.dot(d) > 0) bestN = -bestN;
        }
    };
    if (s.kind == ShapeKind::Ramp) {
        Vec3 c[4];
        rampCorners(s, c);
        tri(c[0], c[1], c[2]);
        tri(c[0], c[2], c[3]);
    } else {
        const AABB& b = s.box;
        Vec3 apex{(b.min.x + b.max.x) * 0.5f, b.max.y, (b.min.z + b.max.z) * 0.5f};
        Vec3 c0{b.min.x, b.min.y, b.min.z}, c1{b.max.x, b.min.y, b.min.z}, c2{b.max.x, b.min.y, b.max.z}, c3{b.min.x, b.min.y, b.max.z};
        tri(c0, c1, apex); tri(c1, c2, apex); tri(c2, c3, apex); tri(c3, c0, apex);
    }
    if (best == std::numeric_limits<float>::max()) return false;
    t = best;
    if (n) *n = bestN;
    return true;
}

// ---------------------------------------------------------------- ShapeGrid

void ShapeGrid::clear() {
    for (auto& c : cells_) c.clear();
}

void ShapeGrid::insert(uint32_t idx, const AABB& b) {
    int x0 = std::max(0, toCell(b.min.x)), x1 = std::min(N - 1, toCell(b.max.x));
    int z0 = std::max(0, toCell(b.min.z)), z1 = std::min(N - 1, toCell(b.max.z));
    for (int z = z0; z <= z1; z++)
        for (int x = x0; x <= x1; x++) cells_[(size_t)z * N + x].push_back(idx);
}

void ShapeGrid::remove(uint32_t idx, const AABB& b) {
    int x0 = std::max(0, toCell(b.min.x)), x1 = std::min(N - 1, toCell(b.max.x));
    int z0 = std::max(0, toCell(b.min.z)), z1 = std::min(N - 1, toCell(b.max.z));
    for (int z = z0; z <= z1; z++)
        for (int x = x0; x <= x1; x++) {
            auto& v = cells_[(size_t)z * N + x];
            for (size_t i = 0; i < v.size(); i++)
                if (v[i] == idx) { v[i] = v.back(); v.pop_back(); break; }
        }
}

// ---------------------------------------------------------------- CollisionWorld

void CollisionWorld::setMap(const GameMap* m) {
    map = m;
    static_ = m->shapes;
    dynamic_.clear();
    freeDynamic_.clear();
    grid_.clear();
    for (auto& s : static_) {
        s.id = (uint32_t)(&s - &static_[0]);
        if (s.alive) grid_.insert(s.id, s.box);
    }
    stamp_.assign(static_.size(), 0);
}

Shape* CollisionWorld::shape(uint32_t id) {
    if (id >= STRUCTURE_SHAPE_BASE) {
        uint32_t i = id - STRUCTURE_SHAPE_BASE;
        return i < dynamic_.size() ? &dynamic_[i] : nullptr;
    }
    return id < static_.size() ? &static_[id] : nullptr;
}

const Shape* CollisionWorld::shape(uint32_t id) const {
    return const_cast<CollisionWorld*>(this)->shape(id);
}

uint32_t CollisionWorld::addDynamicShape(const Shape& s) {
    uint32_t idx;
    if (!freeDynamic_.empty()) {
        idx = freeDynamic_.back();
        freeDynamic_.pop_back();
    } else {
        idx = (uint32_t)dynamic_.size();
        dynamic_.push_back({});
        stampDyn_.push_back(0);
    }
    Shape& d = dynamic_[idx];
    d = s;
    d.id = STRUCTURE_SHAPE_BASE + idx;
    d.alive = true;
    grid_.insert(d.id, d.box);
    return d.id;
}

void CollisionWorld::removeShape(uint32_t id) {
    Shape* s = shape(id);
    if (!s || !s->alive) return;
    s->alive = false;
    grid_.remove(id, s->box);
    if (id >= STRUCTURE_SHAPE_BASE) freeDynamic_.push_back(id - STRUCTURE_SHAPE_BASE);
}

void CollisionWorld::clearDynamic() {
    for (auto& d : dynamic_)
        if (d.alive) { d.alive = false; grid_.remove(d.id, d.box); }
    dynamic_.clear();
    freeDynamic_.clear();
    stampDyn_.clear();
}

void CollisionWorld::newStamp() const {
    stampCounter_++;
    if (stampCounter_ == 0) {
        std::fill(stamp_.begin(), stamp_.end(), 0);
        std::fill(stampDyn_.begin(), stampDyn_.end(), 0);
        stampCounter_ = 1;
    }
}

bool CollisionWorld::visit(uint32_t id) const {
    uint32_t* st;
    if (id >= STRUCTURE_SHAPE_BASE) {
        uint32_t i = id - STRUCTURE_SHAPE_BASE;
        if (i >= stampDyn_.size()) return false;
        st = &stampDyn_[i];
    } else {
        if (id >= stamp_.size()) return false;
        st = &stamp_[id];
    }
    if (*st == stampCounter_) return false;
    *st = stampCounter_;
    return true;
}

void CollisionWorld::query(const AABB& b, const std::function<void(const Shape&)>& fn) const {
    newStamp();
    int x0 = std::max(0, ShapeGrid::toCell(b.min.x)), x1 = std::min(ShapeGrid::N - 1, ShapeGrid::toCell(b.max.x));
    int z0 = std::max(0, ShapeGrid::toCell(b.min.z)), z1 = std::min(ShapeGrid::N - 1, ShapeGrid::toCell(b.max.z));
    for (int z = z0; z <= z1; z++)
        for (int x = x0; x <= x1; x++) {
            const auto* c = grid_.cell(x, z);
            if (!c) continue;
            for (uint32_t id : *c) {
                if (!visit(id)) continue;
                const Shape* s = shape(id);
                if (s && s->alive && s->box.overlaps(b)) fn(*s);
            }
        }
}

float CollisionWorld::terrainHeight(float x, float z) const { return map ? map->heightAt(x, z) : 0.0f; }

float CollisionWorld::groundHeight(const Vec3& feet, float radius, float maxY, uint32_t* shapeOut) const {
    float best = terrainHeight(feet.x, feet.z);
    if (best > maxY) {
        // Terrain above us (e.g. inside a slope): still report it so the player pops up.
        best = terrainHeight(feet.x, feet.z);
    }
    if (shapeOut) *shapeOut = INVALID_ID;
    float r = radius * 0.8f;
    AABB q({feet.x - r, feet.y - 6.0f, feet.z - r}, {feet.x + r, maxY + 0.01f, feet.z + r});
    query(q, [&](const Shape& s) {
        if (!s.solid) return;
        float top;
        if (s.kind == ShapeKind::Box) {
            top = s.box.max.y;
        } else {
            // Sample ramp/cone surface at the player center clamped into bounds.
            float x = clampf(feet.x, s.box.min.x, s.box.max.x);
            float z = clampf(feet.z, s.box.min.z, s.box.max.z);
            if (feet.x < s.box.min.x - r || feet.x > s.box.max.x + r || feet.z < s.box.min.z - r || feet.z > s.box.max.z + r) return;
            top = shapeSurfaceY(s, x, z);
            // Floor-like thin surfaces only count when near the player center.
            if (std::fabs(x - feet.x) > 0.05f || std::fabs(z - feet.z) > 0.05f) top -= 0.35f;
        }
        if (top <= maxY && top > best) {
            best = top;
            if (shapeOut) *shapeOut = s.id;
        }
    });
    return best;
}

bool CollisionWorld::resolveHorizontal(Vec3& feet, float radius, float height, float stepHeight) const {
    bool any = false;
    for (int iter = 0; iter < 3; iter++) {
        bool moved = false;
        AABB pb({feet.x - radius, feet.y + stepHeight, feet.z - radius}, {feet.x + radius, feet.y + height, feet.z + radius});
        query(pb, [&](const Shape& s) {
            if (!s.solid || s.kind != ShapeKind::Box) return;
            // recompute against the current position
            float px0 = feet.x - radius, px1 = feet.x + radius, pz0 = feet.z - radius, pz1 = feet.z + radius;
            if (!(px0 < s.box.max.x && px1 > s.box.min.x && pz0 < s.box.max.z && pz1 > s.box.min.z)) return;
            if (!(feet.y + stepHeight < s.box.max.y && feet.y + height > s.box.min.y)) return;
            float pushXPos = s.box.max.x - px0; // move +x
            float pushXNeg = px1 - s.box.min.x; // move -x
            float pushZPos = s.box.max.z - pz0;
            float pushZNeg = pz1 - s.box.min.z;
            float mx = pushXPos < pushXNeg ? pushXPos : -pushXNeg;
            float mz = pushZPos < pushZNeg ? pushZPos : -pushZNeg;
            if (std::fabs(mx) < std::fabs(mz)) feet.x += mx;
            else feet.z += mz;
            moved = true;
        });
        if (!moved) break;
        any = true;
    }
    return any;
}

float CollisionWorld::ceilingHeight(const Vec3& feet, float radius, float height) const {
    float best = std::numeric_limits<float>::infinity();
    float r = radius * 0.8f;
    AABB q({feet.x - r, feet.y + 0.5f, feet.z - r}, {feet.x + r, feet.y + height + 1.0f, feet.z + r});
    query(q, [&](const Shape& s) {
        if (!s.solid || s.kind != ShapeKind::Box) return;
        if (s.box.min.y > feet.y + 0.5f && s.box.min.y < best) best = s.box.min.y;
    });
    return best;
}

bool CollisionWorld::overlapsSolid(const AABB& b, uint32_t ignoreStructure) const {
    bool hit = false;
    query(b, [&](const Shape& s) {
        if (s.solid && (ignoreStructure == INVALID_ID || s.structure != ignoreStructure)) hit = true;
    });
    return hit;
}

RayHit CollisionWorld::raycast(const Vec3& origin, const Vec3& dirIn, float maxDist, bool includeTerrain) const {
    RayHit res;
    Vec3 d = dirIn.norm();
    float best = maxDist;

    // Terrain: march then bisect.
    if (includeTerrain && map) {
        float step = 1.0f;
        float prevT = 0;
        bool prevAbove = origin.y > terrainHeight(origin.x, origin.z);
        for (float t = step; t <= maxDist + step; t += step) {
            float tt = std::min(t, maxDist);
            Vec3 p = origin + d * tt;
            bool above = p.y > terrainHeight(p.x, p.z);
            if (prevAbove && !above) {
                float lo = prevT, hi = tt;
                for (int i = 0; i < 12; i++) {
                    float mid = (lo + hi) * 0.5f;
                    Vec3 m = origin + d * mid;
                    if (m.y > terrainHeight(m.x, m.z)) lo = mid; else hi = mid;
                }
                best = hi;
                res.hit = true;
                res.terrain = true;
                res.t = hi;
                res.point = origin + d * hi;
                float e = 0.5f;
                Vec3 n{terrainHeight(res.point.x - e, res.point.z) - terrainHeight(res.point.x + e, res.point.z), 2 * e,
                       terrainHeight(res.point.x, res.point.z - e) - terrainHeight(res.point.x, res.point.z + e)};
                res.normal = n.norm();
                break;
            }
            prevAbove = above;
            prevT = tt;
            if (tt >= maxDist) break;
        }
    }

    // Shapes: 2D DDA over the grid.
    newStamp();
    float cs = ShapeGrid::CELL_SIZE;
    int cx = ShapeGrid::toCell(origin.x), cz = ShapeGrid::toCell(origin.z);
    int stepX = d.x > 0 ? 1 : -1, stepZ = d.z > 0 ? 1 : -1;
    // cell boundaries in world units: cell c covers [(c-1)*cs, c*cs)
    float nextX = d.x > 0 ? (cx) * cs : (cx - 1) * cs;
    float nextZ = d.z > 0 ? (cz) * cs : (cz - 1) * cs;
    float tMaxX = std::fabs(d.x) < 1e-8f ? 1e30f : (nextX - origin.x) / d.x;
    float tMaxZ = std::fabs(d.z) < 1e-8f ? 1e30f : (nextZ - origin.z) / d.z;
    float tDeltaX = std::fabs(d.x) < 1e-8f ? 1e30f : cs / std::fabs(d.x);
    float tDeltaZ = std::fabs(d.z) < 1e-8f ? 1e30f : cs / std::fabs(d.z);
    float tCell = 0;
    for (int guard = 0; guard < 4096; guard++) {
        const auto* c = grid_.cell(cx, cz);
        if (c) {
            for (uint32_t id : *c) {
                if (!visit(id)) continue;
                const Shape* s = shape(id);
                if (!s || !s->alive || !s->blocksShots) continue;
                float t;
                Vec3 n;
                if (rayShape(*s, origin, d, best, t, &n) && t < best) {
                    best = t;
                    res.hit = true;
                    res.terrain = false;
                    res.t = t;
                    res.point = origin + d * t;
                    res.normal = n;
                    res.shapeId = s->id;
                }
            }
        }
        tCell = std::min(tMaxX, tMaxZ);
        if (tCell > best) break;
        if (tMaxX < tMaxZ) { cx += stepX; tMaxX += tDeltaX; }
        else { cz += stepZ; tMaxZ += tDeltaZ; }
        if (cx < 0 || cz < 0 || cx >= ShapeGrid::N || cz >= ShapeGrid::N) break;
    }
    return res;
}

bool CollisionWorld::lineOfSight(const Vec3& a, const Vec3& b) const {
    Vec3 d = b - a;
    float len = d.len();
    if (len < 0.01f) return true;
    RayHit h = raycast(a, d / len, len);
    return !h.hit;
}

} // namespace si
