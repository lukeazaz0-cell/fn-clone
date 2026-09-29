#include "building.h"

#include <queue>
#include <unordered_set>

namespace si {

float materialMaxHp(Material m) {
    switch (m) {
        case Material::Wood: return 150;
        case Material::Brick: return 300;
        case Material::Metal: return 450;
        default: return 100;
    }
}

float materialBuildTime(Material m) {
    switch (m) {
        case Material::Wood: return 2.5f;
        case Material::Brick: return 5.0f;
        case Material::Metal: return 8.0f;
        default: return 1.0f;
    }
}

AABB pieceBounds(PieceType p, int gx, int gy, int gz, uint8_t rot) {
    float x0 = gx * TILE, z0 = gz * TILE, y0 = gy * GRID_Y;
    switch (p) {
        case PieceType::Wall:
            if (rot == 0) return AABB({x0, y0, z0 - WALL_T / 2}, {x0 + TILE, y0 + TILE_H, z0 + WALL_T / 2});
            return AABB({x0 - WALL_T / 2, y0, z0}, {x0 + WALL_T / 2, y0 + TILE_H, z0 + TILE});
        case PieceType::Floor: return AABB({x0, y0 - WALL_T / 2, z0}, {x0 + TILE, y0 + WALL_T / 2, z0 + TILE});
        case PieceType::Ramp: return AABB({x0, y0, z0}, {x0 + TILE, y0 + TILE_H, z0 + TILE});
        case PieceType::Cone: return AABB({x0, y0, z0}, {x0 + TILE, y0 + TILE_H * 0.5f, z0 + TILE});
        default: return AABB({x0, y0, z0}, {x0 + TILE, y0 + TILE_H, z0 + TILE});
    }
}

static Color4 materialColor(Material m) {
    switch (m) {
        case Material::Wood: return rgb(176, 128, 78);
        case Material::Brick: return rgb(170, 90, 70);
        case Material::Metal: return rgb(150, 160, 170);
        default: return rgb(200, 200, 200);
    }
}

void pieceShapes(const Structure& s, std::vector<Shape>& out) {
    out.clear();
    AABB b = pieceBounds(s.piece, s.gx, s.gy, s.gz, s.rot);
    Shape base;
    base.kind = ShapeKind::Box;
    base.mat = s.mat;
    base.color = materialColor(s.mat);
    base.structure = s.id;
    base.destructible = true;
    base.hp = s.hp;
    base.maxHp = s.maxHp;
    base.style = 10; // player build
    auto push = [&](AABB bb) { Shape x = base; x.box = bb; out.push_back(x); };

    if (s.piece == PieceType::Wall) {
        bool alongX = s.rot == 0;
        // Wall local coords: u along the wall [0,TILE], v up [0,TILE_H]
        auto part = [&](float u0, float u1, float v0, float v1) {
            AABB bb = b;
            if (alongX) { bb.min.x = b.min.x + u0; bb.max.x = b.min.x + u1; }
            else { bb.min.z = b.min.z + u0; bb.max.z = b.min.z + u1; }
            bb.min.y = b.min.y + v0;
            bb.max.y = b.min.y + v1;
            push(bb);
        };
        switch ((WallEdit)s.edit) {
            case WallEdit::Door:
                part(0, 1.9f, 0, TILE_H); part(3.1f, TILE, 0, TILE_H); part(1.9f, 3.1f, 2.7f, TILE_H); break;
            case WallEdit::Window:
                part(0, 1.6f, 0, TILE_H); part(3.4f, TILE, 0, TILE_H); part(1.6f, 3.4f, 0, 1.5f); part(1.6f, 3.4f, 2.6f, TILE_H); break;
            case WallEdit::Arch:
                part(0, 0.6f, 0, TILE_H); part(TILE - 0.6f, TILE, 0, TILE_H); part(0.6f, TILE - 0.6f, 2.9f, TILE_H); break;
            default: push(b); break;
        }
    } else if (s.piece == PieceType::Floor) {
        if ((FloorEdit)s.edit == FloorEdit::Hole) {
            float h = 1.25f; // hole half size
            float cx = b.min.x + TILE / 2, cz = b.min.z + TILE / 2;
            push(AABB({b.min.x, b.min.y, b.min.z}, {b.max.x, b.max.y, cz - h}));
            push(AABB({b.min.x, b.min.y, cz + h}, {b.max.x, b.max.y, b.max.z}));
            push(AABB({b.min.x, b.min.y, cz - h}, {cx - h, b.max.y, cz + h}));
            push(AABB({cx + h, b.min.y, cz - h}, {b.max.x, b.max.y, cz + h}));
        } else {
            push(b);
        }
    } else if (s.piece == PieceType::Ramp) {
        Shape r = base;
        r.kind = ShapeKind::Ramp;
        r.rampDir = s.rot;
        r.box = b;
        out.push_back(r);
    } else {
        Shape r = base;
        r.kind = ShapeKind::Cone;
        r.box = b;
        out.push_back(r);
    }
}

uint8_t nextEdit(PieceType p, uint8_t edit) {
    switch (p) {
        case PieceType::Wall: return (uint8_t)((edit + 1) % (int)WallEdit::Count);
        case PieceType::Floor: return (uint8_t)((edit + 1) % (int)FloorEdit::Count);
        case PieceType::Ramp: return (uint8_t)((edit + 1) % 4); // rotate
        default: return edit;
    }
}

BuildTarget computeBuildTarget(const Vec3& feet, float yaw, float pitch, PieceType piece, uint8_t rotOffset) {
    BuildTarget t;
    t.piece = piece;
    t.valid = true;
    Vec3 fwd{std::sin(yaw), 0, std::cos(yaw)};
    bool xDominant = std::fabs(fwd.x) > std::fabs(fwd.z);
    // Vertical grid is half a tile so pieces line up with the player's feet on uneven terrain.
    int level = (int)std::floor((feet.y + 0.3f) / GRID_Y);
    int floorLevel = (int)std::floor(feet.y / GRID_Y + 0.5f);
    Vec3 ahead = feet + fwd * (TILE * 0.62f);
    int cgx = (int)std::floor(feet.x / TILE), cgz = (int)std::floor(feet.z / TILE);
    int agx = (int)std::floor(ahead.x / TILE), agz = (int)std::floor(ahead.z / TILE);

    switch (piece) {
        case PieceType::Wall: {
            // Wall on the edge of the current cell in front of the player.
            if (xDominant) {
                t.rot = 1;
                t.gx = fwd.x > 0 ? cgx + 1 : cgx;
                t.gz = agz;
            } else {
                t.rot = 0;
                t.gz = fwd.z > 0 ? cgz + 1 : cgz;
                t.gx = agx;
            }
            t.gy = level + (pitch > 0.55f ? 2 : 0);
            break;
        }
        case PieceType::Floor: {
            t.gx = agx; t.gz = agz;
            t.gy = floorLevel + (pitch > 0.35f ? 2 : 0);
            if (pitch < -0.9f) { t.gx = cgx; t.gz = cgz; }
            break;
        }
        case PieceType::Ramp: {
            t.gx = agx; t.gz = agz;
            t.gy = level;
            if (pitch < -0.8f) t.gy = level - 2;
            uint8_t dir;
            if (xDominant) dir = fwd.x > 0 ? RAMP_PX : RAMP_NX;
            else dir = fwd.z > 0 ? RAMP_PZ : RAMP_NZ;
            // rotOffset rotates clockwise through +Z, -X, -Z, +X
            static const uint8_t order[4] = {RAMP_PZ, RAMP_NX, RAMP_NZ, RAMP_PX};
            int idx = 0;
            for (int i = 0; i < 4; i++) if (order[i] == dir) idx = i;
            t.rot = order[(idx + rotOffset) % 4];
            break;
        }
        case PieceType::Cone: {
            t.gx = agx; t.gz = agz;
            t.gy = level + (pitch > -0.2f ? 2 : 0);
            break;
        }
        default: t.valid = false; break;
    }
    return t;
}

uint64_t StructureSet::slotKey(PieceType p, int gx, int gy, int gz, uint8_t rot) {
    uint64_t kind;
    switch (p) {
        case PieceType::Wall: kind = rot == 0 ? 3 : 4; break;
        case PieceType::Floor: kind = 0; break;
        case PieceType::Ramp: kind = 1; break;
        default: kind = 2; break;
    }
    return kind | ((uint64_t)(gx + 4096) << 4) | ((uint64_t)(gy + 4096) << 20) | ((uint64_t)(gz + 4096) << 36);
}

bool StructureSet::slotFree(const BuildTarget& t) const { return slots_.find(slotKey(t.piece, t.gx, t.gy, t.gz, t.rot)) == slots_.end(); }

uint32_t StructureSet::slotOwner(const BuildTarget& t) const {
    auto it = slots_.find(slotKey(t.piece, t.gx, t.gy, t.gz, t.rot));
    return it == slots_.end() ? INVALID_ID : it->second;
}

Structure* StructureSet::find(uint32_t id) {
    auto it = byId.find(id);
    return it == byId.end() ? nullptr : &it->second;
}

bool StructureSet::grounded(const Structure& s, const CollisionWorld& world) const {
    AABB b = pieceBounds(s.piece, s.gx, s.gy, s.gz, s.rot);
    float bottom = b.min.y;
    const float xs[3] = {b.min.x + 0.2f, (b.min.x + b.max.x) / 2, b.max.x - 0.2f};
    const float zs[3] = {b.min.z + 0.2f, (b.min.z + b.max.z) / 2, b.max.z - 0.2f};
    for (float x : xs)
        for (float z : zs)
            if (world.terrainHeight(x, z) >= bottom - 0.4f) return true;
    // Resting against map geometry counts as support.
    bool touching = false;
    world.query(b.expanded(0.15f), [&](const Shape& sh) {
        if (sh.structure == INVALID_ID && sh.solid) touching = true;
    });
    return touching;
}

void StructureSet::neighbors(const Structure& s, std::vector<uint32_t>& out) const {
    out.clear();
    AABB b = pieceBounds(s.piece, s.gx, s.gy, s.gz, s.rot).expanded(0.2f);
    // Candidate cells: neighbours on the grid within 1 tile
    for (auto& [id, o] : byId) {
        if (id == s.id) continue;
        if (std::abs(o.gx - s.gx) > 1 || std::abs(o.gz - s.gz) > 1 || std::abs(o.gy - s.gy) > 2) continue;
        if (pieceBounds(o.piece, o.gx, o.gy, o.gz, o.rot).overlaps(b)) out.push_back(id);
    }
}

bool StructureSet::canPlace(const BuildTarget& t, const CollisionWorld& world) const {
    if (!t.valid || !slotFree(t)) return false;
    AABB b = pieceBounds(t.piece, t.gx, t.gy, t.gz, t.rot);
    if (b.min.x < 0 || b.min.z < 0 || b.max.x > WORLD_SIZE || b.max.z > WORLD_SIZE) return false;
    if (b.max.y > 400) return false;
    // Fully buried pieces are pointless.
    float c = world.terrainHeight((b.min.x + b.max.x) / 2, (b.min.z + b.max.z) / 2);
    if (b.max.y < c - 0.5f) return false;
    Structure tmp;
    tmp.piece = t.piece; tmp.gx = t.gx; tmp.gy = t.gy; tmp.gz = t.gz; tmp.rot = t.rot;
    tmp.id = INVALID_ID;
    if (grounded(tmp, world)) return true;
    std::vector<uint32_t> n;
    neighbors(tmp, n);
    return !n.empty();
}

Structure& StructureSet::add(const Structure& s, CollisionWorld& world) {
    Structure& st = byId[s.id];
    st = s;
    std::vector<Shape> shapes;
    pieceShapes(st, shapes);
    st.shapes.clear();
    for (auto& sh : shapes) st.shapes.push_back(world.addDynamicShape(sh));
    slots_[slotKey(st.piece, st.gx, st.gy, st.gz, st.rot)] = st.id;
    return st;
}

void StructureSet::remove(uint32_t id, CollisionWorld& world) {
    auto it = byId.find(id);
    if (it == byId.end()) return;
    for (uint32_t sid : it->second.shapes) world.removeShape(sid);
    slots_.erase(slotKey(it->second.piece, it->second.gx, it->second.gy, it->second.gz, it->second.rot));
    byId.erase(it);
}

void StructureSet::setEdit(uint32_t id, uint8_t edit, CollisionWorld& world) {
    Structure* s = find(id);
    if (!s) return;
    for (uint32_t sid : s->shapes) world.removeShape(sid);
    s->shapes.clear();
    slots_.erase(slotKey(s->piece, s->gx, s->gy, s->gz, s->rot));
    if (s->piece == PieceType::Ramp) s->rot = edit % 4;
    else s->edit = edit;
    std::vector<Shape> shapes;
    pieceShapes(*s, shapes);
    for (auto& sh : shapes) s->shapes.push_back(world.addDynamicShape(sh));
    slots_[slotKey(s->piece, s->gx, s->gy, s->gz, s->rot)] = s->id;
}

std::vector<uint32_t> StructureSet::findUnsupported(const AABB& removedBounds, const CollisionWorld& world) const {
    std::vector<uint32_t> result;
    // Seeds: structures that touched the removed piece.
    std::vector<uint32_t> seeds;
    AABB rb = removedBounds.expanded(0.2f);
    for (auto& [id, o] : byId)
        if (pieceBounds(o.piece, o.gx, o.gy, o.gz, o.rot).overlaps(rb)) seeds.push_back(id);
    std::unordered_set<uint32_t> evaluated;
    std::vector<uint32_t> nb;
    for (uint32_t seed : seeds) {
        if (evaluated.count(seed)) continue;
        std::vector<uint32_t> comp;
        std::queue<uint32_t> q;
        std::unordered_set<uint32_t> seen{seed};
        q.push(seed);
        bool supported = false;
        while (!q.empty()) {
            uint32_t cur = q.front();
            q.pop();
            comp.push_back(cur);
            auto it = byId.find(cur);
            if (it == byId.end()) continue;
            if (grounded(it->second, world)) { supported = true; break; }
            if (comp.size() > 600) { supported = true; break; }
            neighbors(it->second, nb);
            for (uint32_t n : nb)
                if (seen.insert(n).second) q.push(n);
        }
        for (uint32_t c : seen) evaluated.insert(c);
        if (!supported)
            for (uint32_t c : seen) result.push_back(c);
    }
    return result;
}

void StructureSet::clear(CollisionWorld& world) {
    for (auto& [id, s] : byId)
        for (uint32_t sid : s.shapes) world.removeShape(sid);
    byId.clear();
    slots_.clear();
}

uint32_t StructureSet::structureOfShape(uint32_t shapeId, const CollisionWorld& world) const {
    const Shape* s = world.shape(shapeId);
    return s ? s->structure : INVALID_ID;
}

} // namespace si
