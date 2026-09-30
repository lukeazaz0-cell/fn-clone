#include "map.h"

#include <cstring>

namespace si {

namespace {

// Map layout in normalized [0,1] coordinates (x = east, y = south).
struct PoiSeed { const char* name; PoiType type; float u, v, radius; bool major; };
const PoiSeed kPois[] = {
    {"Chrome Crossing", PoiType::FutureCity, 0.50f, 0.47f, 70, true},
    {"Skyline Plaza", PoiType::Mall, 0.80f, 0.48f, 55, true},
    {"Forge Works", PoiType::Industrial, 0.25f, 0.22f, 55, true},
    {"Driftwood Cove", PoiType::PirateCove, 0.105f, 0.47f, 42, true},
    {"Frostpeak Lodge", PoiType::SnowLodge, 0.22f, 0.76f, 42, true},
    {"Icebreaker Airfield", PoiType::Airfield, 0.36f, 0.885f, 50, true},
    {"Sandstone Oasis", PoiType::DesertTown, 0.79f, 0.79f, 60, true},
    {"Lantern Harbor", PoiType::LanternVillage, 0.565f, 0.885f, 45, true},
    {"Harvest Farms", PoiType::Farm, 0.35f, 0.64f, 48, true},
    {"Maple Grove", PoiType::Suburb, 0.35f, 0.335f, 50, true},
    {"Crossroads", PoiType::SmallTown, 0.565f, 0.64f, 40, true},
    {"Pinewood Lodge", PoiType::Lodge, 0.82f, 0.19f, 32, true},
    {"Scrap Yard", PoiType::Junkyard, 0.26f, 0.085f, 34, true},
    {"Shadow Hills", PoiType::Mansion, 0.44f, 0.12f, 40, true},
    {"Sunset Steps", PoiType::Temple, 0.64f, 0.25f, 42, true},
    {"Seaside Estates", PoiType::Estates, 0.085f, 0.30f, 36, true},
    {"Canyon Mines", PoiType::Mines, 0.16f, 0.60f, 34, true},
    {"Hilltop Hamlet", PoiType::Hamlet, 0.67f, 0.745f, 30, true},
    {"Clearwater Lake", PoiType::LakeHouse, 0.43f, 0.505f, 18, false},
    {"Ember Peak", PoiType::Volcano, 0.12f, 0.16f, 20, false},
    {"Rustbucket Garage", PoiType::Landmark, 0.68f, 0.52f, 14, false},
    {"Quiet Pines", PoiType::Landmark, 0.60f, 0.11f, 14, false},
    {"Dustbowl Diner", PoiType::Landmark, 0.90f, 0.66f, 14, false},
    {"Old Mill", PoiType::Landmark, 0.24f, 0.46f, 14, false},
    {"Lookout Point", PoiType::Landmark, 0.72f, 0.39f, 14, false},
    {"Frozen Outpost", PoiType::Landmark, 0.10f, 0.72f, 12, false},
};

const float W = WORLD_SIZE;

float mountain(float u, float v, float cu, float cv, float rad, float height, uint32_t seed) {
    float d = std::sqrt((u - cu) * (u - cu) + (v - cv) * (v - cv));
    float t = 1.0f - d / rad;
    if (t <= 0) return 0;
    return height * t * t * (0.75f + 0.5f * fbm(u * 20, v * 20, seed));
}

Color4 jitter(Rng& r, Color4 c, float amt = 0.12f) { return shade(c, 1.0f + r.range(-amt, amt)); }

} // namespace

// =====================================================================================
// Terrain

float GameMap::heightAt(float x, float z) const {
    float fx = clampf(x / CELL, 0, (float)HM_N - 0.001f);
    float fz = clampf(z / CELL, 0, (float)HM_N - 0.001f);
    int ix = (int)fx, iz = (int)fz;
    float tx = fx - ix, tz = fz - iz;
    float h00 = vertexHeight(ix, iz), h10 = vertexHeight(ix + 1, iz);
    float h01 = vertexHeight(ix, iz + 1), h11 = vertexHeight(ix + 1, iz + 1);
    // Match the triangulation used by the renderer (split along the 00-11 diagonal).
    if (tx > tz) return h00 + (h10 - h00) * tx + (h11 - h10) * tz;
    return h00 + (h11 - h01) * tx + (h01 - h00) * tz;
}

Biome GameMap::biomeAt(float x, float z) const {
    int ix = (int)clampf(x / CELL, 0, HM_N - 1), iz = (int)clampf(z / CELL, 0, HM_N - 1);
    return (Biome)biomes[(size_t)iz * HM_N + ix];
}

const POI* GameMap::nearestPoi(float x, float z, float maxDist) const {
    const POI* best = nullptr;
    float bd = maxDist;
    for (auto& p : pois) {
        float d = dist2d(p.center, {x, z});
        if (d < bd) { bd = d; best = &p; }
    }
    return best;
}

Vec3 GameMap::randomLandPoint(Rng& rng, float margin) const {
    for (int i = 0; i < 500; i++) {
        float x = rng.range(margin, W - margin), z = rng.range(margin, W - margin);
        float h = heightAt(x, z);
        if (h > WATER_LEVEL + 1.0f && h < 45.0f) return {x, h, z};
    }
    return {W * 0.5f, heightAt(W * 0.5f, W * 0.5f), W * 0.5f};
}

void GameMap::genTerrain() {
    heights.assign((size_t)(HM_N + 1) * (HM_N + 1), 0.0f);
    for (int iz = 0; iz <= HM_N; iz++) {
        for (int ix = 0; ix <= HM_N; ix++) {
            float u = (float)ix / HM_N, v = (float)iz / HM_N;
            float dx = u - 0.5f, dz = v - 0.5f;
            float r = std::sqrt(dx * dx + dz * dz);
            float coast = (fbm(u * 5, v * 5, seed + 1) - 0.5f) * 0.10f;
            float land = 1.0f - smoothstep(0.40f, 0.47f, r + coast);
            float base = 3.0f + fbm(u * 7, v * 7, seed + 7) * 16.0f;
            base += (fbm(u * 25, v * 25, seed + 9) - 0.5f) * 3.0f;
            // Big south-west snowy mountain
            base += mountain(u, v, 0.18f, 0.80f, 0.14f, 70, seed + 11);
            // Northern hills
            base += mountain(u, v, 0.45f, 0.10f, 0.10f, 26, seed + 12);
            base += mountain(u, v, 0.72f, 0.14f, 0.08f, 20, seed + 13);
            // Eastern ridge between mall and temple
            base += mountain(u, v, 0.72f, 0.38f, 0.07f, 24, seed + 14);
            // South-east desert canyon mesas
            base += mountain(u, v, 0.88f, 0.72f, 0.06f, 22, seed + 15);
            base += mountain(u, v, 0.70f, 0.88f, 0.05f, 16, seed + 16);
            // Volcano in the far north-west with a crater
            {
                float d = std::sqrt((u - 0.12f) * (u - 0.12f) + (v - 0.16f) * (v - 0.16f)) / 0.085f;
                if (d < 1.0f) {
                    float hv = 78.0f * std::pow(1.0f - d, 1.2f);
                    if (d < 0.22f) hv = 78.0f * std::pow(1.0f - 0.22f, 1.2f) - (0.22f - d) * 150.0f;
                    base = std::max(base, hv + 3.0f);
                }
            }
            float h = lerpf(-16.0f, base, land);
            // Beaches: flatten near the coastline
            if (land > 0.0f && land < 0.6f) h = lerpf(h, 1.2f, 0.5f * (1.0f - land / 0.6f));
            heights[(size_t)iz * (HM_N + 1) + ix] = h;
        }
    }
    // Clearwater lake depression
    Vec2 lake{0.43f * W, 0.505f * W};
    for (int iz = 0; iz <= HM_N; iz++)
        for (int ix = 0; ix <= HM_N; ix++) {
            float d = dist2d({ix * CELL, iz * CELL}, lake);
            float t = 1.0f - smoothstep(38.0f, 62.0f, d);
            float& h = heights[(size_t)iz * (HM_N + 1) + ix];
            if (t > 0) h = lerpf(h, -4.0f, t);
        }
    // Island in the lake
    for (int iz = 0; iz <= HM_N; iz++)
        for (int ix = 0; ix <= HM_N; ix++) {
            float d = dist2d({ix * CELL, iz * CELL}, lake);
            float t = 1.0f - smoothstep(10.0f, 18.0f, d);
            float& h = heights[(size_t)iz * (HM_N + 1) + ix];
            if (t > 0) h = lerpf(h, 2.0f, t);
        }
}

void GameMap::flatten(Vec2 c, float halfX, float halfZ, float target, float blend) {
    int x0 = std::max(0, (int)((c.x - halfX - blend) / CELL)), x1 = std::min(HM_N, (int)((c.x + halfX + blend) / CELL) + 1);
    int z0 = std::max(0, (int)((c.y - halfZ - blend) / CELL)), z1 = std::min(HM_N, (int)((c.y + halfZ + blend) / CELL) + 1);
    for (int iz = z0; iz <= z1; iz++)
        for (int ix = x0; ix <= x1; ix++) {
            float dx = std::max(0.0f, std::fabs(ix * CELL - c.x) - halfX);
            float dz = std::max(0.0f, std::fabs(iz * CELL - c.y) - halfZ);
            float d = std::sqrt(dx * dx + dz * dz);
            float w = 1.0f - smoothstep(0.0f, blend, d);
            float& h = heights[(size_t)iz * (HM_N + 1) + ix];
            h = lerpf(h, target, w);
        }
}

float GameMap::footprintMax(float x0, float z0, float x1, float z1) const {
    float m = -1e9f;
    for (float z = z0; z <= z1 + 0.01f; z += std::max(1.0f, (z1 - z0) / 6))
        for (float x = x0; x <= x1 + 0.01f; x += std::max(1.0f, (x1 - x0) / 6)) m = std::max(m, heightAt(x, z));
    return m;
}

float GameMap::footprintMin(float x0, float z0, float x1, float z1) const {
    float m = 1e9f;
    for (float z = z0; z <= z1 + 0.01f; z += std::max(1.0f, (z1 - z0) / 6))
        for (float x = x0; x <= x1 + 0.01f; x += std::max(1.0f, (x1 - x0) / 6)) m = std::min(m, heightAt(x, z));
    return m;
}

void GameMap::genBiomes() {
    biomes.assign((size_t)HM_N * HM_N, (uint8_t)Biome::Grass);
    for (int iz = 0; iz < HM_N; iz++)
        for (int ix = 0; ix < HM_N; ix++) {
            float u = (ix + 0.5f) / HM_N, v = (iz + 0.5f) / HM_N;
            float h = heightAt((ix + 0.5f) * CELL, (iz + 0.5f) * CELL);
            float n = (fbm(u * 12, v * 12, seed + 40) - 0.5f) * 0.06f;
            Biome b = Biome::Grass;
            if (u + n < 0.40f && v + n > 0.66f) b = Biome::Snow;
            else if (u + n > 0.64f && v + n > 0.62f) b = Biome::Desert;
            else if (u + n > 0.56f && v + n < 0.33f) b = Biome::Jungle;
            else if (std::sqrt((u - 0.12f) * (u - 0.12f) + (v - 0.16f) * (v - 0.16f)) + n < 0.11f) b = Biome::Volcanic;
            else if (fbm(u * 6, v * 6, seed + 41) > 0.58f) b = Biome::Forest;
            if (std::sqrt((u - 0.5f) * (u - 0.5f) + (v - 0.47f) * (v - 0.47f)) < 0.045f) b = Biome::City;
            if (std::sqrt((u - 0.35f) * (u - 0.35f) + (v - 0.64f) * (v - 0.64f)) < 0.04f) b = Biome::Farm;
            if (h < 2.2f && b != Biome::Snow) b = Biome::Beach;
            biomes[(size_t)iz * HM_N + ix] = (uint8_t)b;
        }
}

void GameMap::genRoads() {
    roadMask.assign((size_t)HM_N * HM_N, 0);
    auto P = [&](const char* name) -> Vec2 {
        for (auto& p : kPois)
            if (std::strcmp(p.name, name) == 0) return {p.u * W, p.v * W};
        return {W / 2, W / 2};
    };
    const char* links[][2] = {
        {"Chrome Crossing", "Skyline Plaza"}, {"Chrome Crossing", "Maple Grove"}, {"Chrome Crossing", "Crossroads"},
        {"Chrome Crossing", "Sunset Steps"}, {"Maple Grove", "Forge Works"}, {"Maple Grove", "Shadow Hills"},
        {"Shadow Hills", "Scrap Yard"}, {"Scrap Yard", "Forge Works"}, {"Forge Works", "Seaside Estates"},
        {"Seaside Estates", "Driftwood Cove"}, {"Driftwood Cove", "Canyon Mines"}, {"Canyon Mines", "Harvest Farms"},
        {"Harvest Farms", "Crossroads"}, {"Harvest Farms", "Frostpeak Lodge"}, {"Frostpeak Lodge", "Icebreaker Airfield"},
        {"Icebreaker Airfield", "Lantern Harbor"}, {"Lantern Harbor", "Crossroads"}, {"Crossroads", "Hilltop Hamlet"},
        {"Hilltop Hamlet", "Sandstone Oasis"}, {"Sandstone Oasis", "Skyline Plaza"}, {"Skyline Plaza", "Pinewood Lodge"},
        {"Pinewood Lodge", "Sunset Steps"}, {"Sunset Steps", "Shadow Hills"}, {"Harvest Farms", "Maple Grove"},
    };
    for (auto& l : links) {
        Vec2 a = P(l[0]), b = P(l[1]);
        // Slight bend through a jittered midpoint for a natural look.
        Vec2 mid = (a + b) * 0.5f + Vec2{rng_.range(-25, 25), rng_.range(-25, 25)};
        roads.push_back({a, mid, 7.0f});
        roads.push_back({mid, b, 7.0f});
    }
    for (auto& r : roads) {
        Vec2 d = r.b - r.a;
        float len = d.len();
        for (float t = 0; t <= len; t += 2.0f) {
            Vec2 p = r.a + d * (t / len);
            for (float oz = -r.width / 2; oz <= r.width / 2; oz += 2.0f)
                for (float ox = -r.width / 2; ox <= r.width / 2; ox += 2.0f) {
                    int ix = (int)((p.x + ox) / CELL), iz = (int)((p.y + oz) / CELL);
                    if (ix < 0 || iz < 0 || ix >= HM_N || iz >= HM_N) continue;
                    if (heightAt(p.x + ox, p.y + oz) > WATER_LEVEL + 0.5f) roadMask[(size_t)iz * HM_N + ix] = 1;
                }
        }
    }
}

// =====================================================================================
// Building toolkit

uint32_t GameMap::box(const Vec3& mn, const Vec3& mx, Material m, Color4 c, float hp, bool destructible, uint8_t style) {
    Shape s;
    s.kind = ShapeKind::Box;
    s.box = AABB(mn, mx);
    s.mat = m;
    s.color = c;
    if (hp <= 0) hp = 200;
    s.hp = s.maxHp = hp;
    s.destructible = destructible;
    s.style = style;
    s.id = (uint32_t)shapes.size();
    shapes.push_back(s);
    return s.id;
}

uint32_t GameMap::ramp(const Vec3& mn, const Vec3& mx, uint8_t dir, Material m, Color4 c, float hp) {
    Shape s;
    s.kind = ShapeKind::Ramp;
    s.rampDir = dir;
    s.box = AABB(mn, mx);
    s.mat = m;
    s.color = c;
    s.hp = s.maxHp = hp;
    s.id = (uint32_t)shapes.size();
    shapes.push_back(s);
    return s.id;
}

void GameMap::foundation(float x0, float z0, float x1, float z1, float top, Color4 c) {
    float bottom = footprintMin(x0, z0, x1, z1) - 1.0f;
    box({x0, bottom, z0}, {x1, top, z1}, Material::Brick, c, 0, false);
}

void GameMap::addChest(const Vec3& p, float yaw) { chests.push_back({p, yaw}); }
void GameMap::addLoot(const Vec3& p) { floorLoot.push_back(p); }

void GameMap::wallRun(bool alongX, float fixed, float from, float to, float y0, float h, Material m, Color4 c,
                      float doorAt, int windowEvery, float thick, uint8_t style) {
    if (to < from) std::swap(from, to);
    float len = to - from;
    int n = std::max(1, (int)std::round(len / 2.5f));
    float seg = len / n;
    float hp = m == Material::Wood ? 150 : m == Material::Brick ? 250 : 350;
    auto panel = [&](float s0, float s1, float ya, float yb) -> uint32_t {
        if (yb - ya < 0.05f) return INVALID_ID;
        if (alongX) return box({s0, ya, fixed - thick / 2}, {s1, yb, fixed + thick / 2}, m, c, hp, true, style);
        return box({fixed - thick / 2, ya, s0}, {fixed + thick / 2, yb, s1}, m, c, hp, true, style);
    };
    // Non-colliding detail (glass, frames) aligned with the wall
    auto detail = [&](DecorKind k, float s0, float s1, float ya, float yb, float depth, Color4 col, uint32_t link) {
        AABB b = alongX ? AABB({s0, ya, fixed - depth}, {s1, yb, fixed + depth}) : AABB({fixed - depth, ya, s0}, {fixed + depth, yb, s1});
        addBoxDecor(k, b, col, link);
    };
    Color4 trim = style == 1 ? rgb(220, 225, 235) : shade(c, 0.62f);
    for (int i = 0; i < n; i++) {
        float s0 = from + i * seg, s1 = s0 + seg;
        bool door = doorAt >= 0 && doorAt >= s0 && doorAt < s1;
        bool window = !door && windowEvery > 0 && i % windowEvery == 1 && i != n - 1;
        if (door) {
            uint32_t top = panel(s0, s1, y0 + 2.6f, y0 + h);
            detail(DecorKind::Trim, s0, s0 + 0.12f, y0, y0 + 2.6f, thick * 0.5f + 0.04f, trim, top);
            detail(DecorKind::Trim, s1 - 0.12f, s1, y0, y0 + 2.6f, thick * 0.5f + 0.04f, trim, top);
            detail(DecorKind::Trim, s0, s1, y0 + 2.5f, y0 + 2.65f, thick * 0.5f + 0.04f, trim, top);
        } else if (window) {
            uint32_t low = panel(s0, s1, y0, y0 + 1.1f);
            panel(s0, s1, y0 + 2.4f, y0 + h);
            float in = 0.12f;
            detail(DecorKind::Glass, s0 + in, s1 - in, y0 + 1.1f, y0 + 2.4f, 0.03f, style == 1 ? rgb(120, 205, 235) : rgb(160, 205, 230), low);
            detail(DecorKind::Trim, s0, s1, y0 + 1.02f, y0 + 1.14f, thick * 0.5f + 0.08f, trim, low); // sill
            detail(DecorKind::Trim, s0, s0 + in, y0 + 1.1f, y0 + 2.4f, thick * 0.5f + 0.02f, trim, low);
            detail(DecorKind::Trim, s1 - in, s1, y0 + 1.1f, y0 + 2.4f, thick * 0.5f + 0.02f, trim, low);
            detail(DecorKind::Trim, (s0 + s1) / 2 - 0.04f, (s0 + s1) / 2 + 0.04f, y0 + 1.1f, y0 + 2.4f, 0.05f, trim, low); // mullion
            detail(DecorKind::Trim, s0, s1, y0 + 2.34f, y0 + 2.42f, thick * 0.5f + 0.03f, trim, low);
        } else {
            panel(s0, s1, y0, y0 + h);
        }
    }
}

void GameMap::addProp(DecorKind k, float x, float z, float yaw, Color4 c, uint32_t link) {
    Decor d;
    d.kind = k;
    d.pos = {x, heightAt(x, z), z};
    d.yaw = yaw;
    d.color = c;
    d.linkedShape = link;
    decor.push_back(d);
}

// Barrels collide as a box (style 9 = not drawn as a box) and render as a decor prism.
void GameMap::barrel(float x, float z, Color4 c) {
    float y = heightAt(x, z);
    uint32_t id = box({x - 0.35f, y, z - 0.35f}, {x + 0.35f, y + 1.1f, z + 0.35f}, Material::Metal, c, 120, true, 9);
    addProp(DecorKind::Barrel, x, z, 0, c, id);
}

void GameMap::crate(float x, float z, float sz) {
    float y = heightAt(x, z);
    uint32_t id = box({x - sz / 2, y - 0.05f, z - sz / 2}, {x + sz / 2, y + sz, z + sz / 2}, Material::Wood, rgb(170, 125, 75), 90, true, 0);
    Decor d;
    d.kind = DecorKind::Crate;
    d.pos = {x, y, z};
    d.size = {sz / 2, sz / 2, sz / 2};
    d.color = rgb(120, 85, 50);
    d.linkedShape = id;
    decor.push_back(d);
}

void GameMap::propCluster(float cx, float cz, float radius, int count) {
    Color4 barrelColors[] = {rgb(190, 60, 40), rgb(50, 110, 170), rgb(70, 130, 70), rgb(200, 160, 50)};
    for (int i = 0; i < count; i++) {
        float a = rng_.range(0, 2 * kPi), d = rng_.range(0, radius);
        float x = cx + std::cos(a) * d, z = cz + std::sin(a) * d;
        if (!isLand(x, z)) continue;
        if (rng_.chance(0.5f)) barrel(x, z, barrelColors[rng_.irange(0, 3)]);
        else crate(x, z, rng_.range(0.8f, 1.3f));
    }
}

void GameMap::genStreetProps() {
    // Street lamps and dashed centre lines along every road
    for (auto& rd : roads) {
        Vec2 d = rd.b - rd.a;
        float len = d.len();
        if (len < 1) continue;
        Vec2 dir = d * (1.0f / len);
        Vec2 side{-dir.y, dir.x};
        float yaw = std::atan2(dir.x, dir.y);
        for (float t = 3; t < len; t += 7.0f) {
            Vec2 p = rd.a + dir * t;
            if (!isLand(p.x, p.y)) continue;
            Decor l;
            l.kind = DecorKind::RoadLine;
            l.pos = {p.x, heightAt(p.x, p.y) + 0.03f, p.y};
            l.yaw = yaw;
            l.size = {0.08f, 0.02f, 1.3f};
            l.color = rgb(235, 225, 170);
            decor.push_back(l);
        }
        for (float t = 25; t < len - 10; t += 55.0f) {
            Vec2 p = rd.a + dir * t + side * 5.2f;
            if (!isLand(p.x, p.y)) continue;
            addProp(DecorKind::StreetLamp, p.x, p.y, yaw, rgb(70, 72, 80));
        }
    }
    // Town furniture near every named location
    for (auto& p : pois) {
        if (!p.major) continue;
        int n = (int)(p.radius / 8);
        for (int i = 0; i < n; i++) {
            float a = rng_.range(0, 2 * kPi), d = rng_.range(0.3f, 0.95f) * p.radius;
            float x = p.center.x + std::cos(a) * d, z = p.center.y + std::sin(a) * d;
            if (!isLand(x, z)) continue;
            int kind = rng_.irange(0, 2);
            if (kind == 0) addProp(DecorKind::Bench, x, z, a + kPi / 2, rgb(130, 90, 55));
            else if (kind == 1) addProp(DecorKind::Hydrant, x, z, a, rgb(210, 50, 40));
            else addProp(DecorKind::Mailbox, x, z, a, rgb(60, 90, 170));
        }
        if (p.type == PoiType::Industrial || p.type == PoiType::Junkyard || p.type == PoiType::Farm || p.type == PoiType::PirateCove ||
            p.type == PoiType::Mines || p.type == PoiType::Airfield)
            propCluster(p.center.x + rng_.range(-15, 15), p.center.y + rng_.range(-15, 15), 14, 10);
    }
}

void GameMap::addBoxDecor(DecorKind k, const AABB& b, Color4 c, uint32_t link) {
    Decor d;
    d.kind = k;
    d.pos = b.center();
    d.size = b.size() * 0.5f;
    d.color = c;
    d.linkedShape = link;
    decor.push_back(d);
}

float GameMap::house(float cx, float cz, float w, float d, int floors, Material m, Color4 wallC, Color4 roofC, bool loot, int roofStyle) {
    float x0 = cx - w / 2, x1 = cx + w / 2, z0 = cz - d / 2, z1 = cz + d / 2;
    float base = footprintMax(x0, z0, x1, z1) + 0.3f;
    foundation(x0 - 0.3f, z0 - 0.3f, x1 + 0.3f, z1 + 0.3f, base, rgb(120, 115, 110));
    const float fh = 3.6f;
    Color4 floorC = shade(wallC, 0.7f);
    float rampW = 2.2f, rampLen = fh * 1.25f;
    for (int f = 0; f < floors; f++) {
        float y = base + f * fh;
        bool ground = f == 0;
        // Walls: front (z0) has the door on the ground floor.
        wallRun(true, z0, x0, x1, y, fh, m, wallC, ground ? cx : -1, 2);
        wallRun(true, z1, x0, x1, y, fh, m, wallC, ground && w > 9 ? cx + w * 0.25f : -1, 2);
        wallRun(false, x0, z0 + 0.15f, z1 - 0.15f, y, fh, m, wallC, -1, 2);
        wallRun(false, x1, z0 + 0.15f, z1 - 0.15f, y, fh, m, wallC, -1, 2);
        // Stairs to the next floor
        if (f < floors - 1) {
            float rx0 = x1 - 0.2f - rampW, rx1 = x1 - 0.2f;
            float rz0 = z0 + 0.3f, rz1 = std::min(z1 - 0.3f, rz0 + rampLen);
            ramp({rx0, y, rz0}, {rx1, y + fh, rz1}, RAMP_PZ, m, floorC);
            // Next floor slab with a stairwell hole
            float yf = y + fh;
            box({x0 + 0.15f, yf - 0.25f, z0 + 0.15f}, {rx0, yf, z1 - 0.15f}, m, floorC, 200);
            box({rx0, yf - 0.25f, rz1}, {x1 - 0.15f, yf, z1 - 0.15f}, m, floorC, 200);
        }
        if (loot) {
            float lx = rng_.range(x0 + 1.2f, x1 - 3.0f), lz = rng_.range(z0 + 1.2f, z1 - 1.2f);
            if (rng_.chance(0.8f)) addLoot({lx, y, lz});
            if (w * d > 90 && rng_.chance(0.6f)) addLoot({rng_.range(x0 + 1, x1 - 3), y, rng_.range(z0 + 1, z1 - 1)});
            if (rng_.chance(0.5f)) addLoot({rng_.range(x0 + 1, x1 - 3), y, rng_.range(z0 + 1, z1 - 1)});
            if (ground && rng_.chance(0.35f)) ammoBoxes.push_back({x0 + 0.8f, y, z1 - 0.8f});
            if (!ground && rng_.chance(0.3f)) addChest({x0 + 1.2f, y, z1 - 1.2f}, 0);
        }
        // Furniture on the ground floor
        if (ground && w > 7 && rng_.chance(0.8f)) {
            float fx = x0 + 1.0f, fz = cz + rng_.range(-1, 1);
            box({fx, y, fz - 1.0f}, {fx + 0.9f, y + 0.8f, fz + 1.0f}, Material::Wood, rgb(140, 60, 60), 60);
        }
        if (!ground && w > 7 && rng_.chance(0.8f)) {
            // Bed with pillow and a wardrobe
            float bx = x0 + 1.4f, bz = z1 - 1.6f;
            box({bx - 0.9f, y, bz - 1.1f}, {bx + 0.9f, y + 0.55f, bz + 1.1f}, Material::Wood, rgb(90, 110, 170), 60);
            box({bx - 0.8f, y + 0.55f, bz + 0.5f}, {bx + 0.8f, y + 0.7f, bz + 1.0f}, Material::Wood, rgb(235, 235, 230), 30);
            box({x1 - 3.2f, y, z1 - 0.8f}, {x1 - 2.0f, y + 2.0f, z1 - 0.2f}, Material::Wood, rgb(110, 75, 45), 60);
        }
        if (ground && rng_.chance(0.5f)) {
            // Kitchen counter along the back wall
            box({cx - 1.5f, y, z1 - 0.8f}, {cx + 0.5f, y + 0.95f, z1 - 0.2f}, Material::Wood, rgb(200, 195, 185), 60);
        }
        if (ground && d > 7 && rng_.chance(0.6f)) {
            float tx = cx - 1.0f, tz = cz + d * 0.15f;
            box({tx - 0.6f, y + 0.7f, tz - 0.6f}, {tx + 0.6f, y + 0.8f, tz + 0.6f}, Material::Wood, rgb(120, 85, 50), 40);
            box({tx - 0.1f, y, tz - 0.1f}, {tx + 0.1f, y + 0.7f, tz + 0.1f}, Material::Wood, rgb(100, 70, 40), 40);
        }
    }
    float top = base + floors * fh;
    // Mailbox by the front door
    if (loot && rng_.chance(0.5f)) addProp(DecorKind::Mailbox, cx + 2.5f, z0 - 1.6f, 0, rgb(rng_.irange(40, 200), 60, 60));
    // Roof / attic
    if (roofStyle == 1) {
        float rh = std::min(3.2f, d * 0.35f);
        ramp({x0 - 0.5f, top, z0 - 0.5f}, {x1 + 0.5f, top + rh, cz}, RAMP_PZ, Material::Wood, roofC, 180);
        ramp({x0 - 0.5f, top, cz}, {x1 + 0.5f, top + rh, z1 + 0.5f}, RAMP_NZ, Material::Wood, shade(roofC, 0.9f), 180);
        box({x0, top - 0.25f, z0}, {x1, top, z1}, m, floorC, 200);
        // Chimney
        if (rng_.chance(0.6f)) {
            float chx = x0 + w * 0.25f, chz = cz - d * 0.2f;
            box({chx - 0.45f, top, chz - 0.45f}, {chx + 0.45f, top + rh + 1.4f, chz + 0.45f}, Material::Brick, rgb(150, 80, 65), 200);
            box({chx - 0.55f, top + rh + 1.4f, chz - 0.55f}, {chx + 0.55f, top + rh + 1.6f, chz + 0.55f}, Material::Brick, rgb(90, 85, 85), 100);
        }
        // Gable ends (stepped) so the attic is enclosed
        for (int i = 0; i < 3; i++) {
            float hh = rh * (i + 1) / 3.5f;
            float inset = d * 0.5f * i / 3.0f;
            box({x0, top, z0 + inset}, {x0 + 0.25f, top + hh, z1 - inset}, m, wallC, 120);
            box({x1 - 0.25f, top, z0 + inset}, {x1, top + hh, z1 - inset}, m, wallC, 120);
        }
        // Attic access ramp
        if (floors >= 1) {
            float yb = top - fh;
            (void)yb;
        }
        if (loot && rng_.chance(0.7f)) addChest({cx - w * 0.2f, top, cz}, 0);
        top += rh;
    } else if (roofStyle == 2) {
        // Tiled pagoda-ish overhang
        box({x0 - 1.2f, top, z0 - 1.2f}, {x1 + 1.2f, top + 0.4f, z1 + 1.2f}, Material::Wood, roofC, 200);
        Shape s;
        uint32_t id = (uint32_t)shapes.size();
        s.id = id; s.kind = ShapeKind::Cone; s.box = AABB({x0 - 0.8f, top + 0.4f, z0 - 0.8f}, {x1 + 0.8f, top + 2.6f, z1 + 0.8f});
        s.mat = Material::Wood; s.color = shade(roofC, 0.9f); s.hp = s.maxHp = 200;
        shapes.push_back(s);
        if (loot && rng_.chance(0.6f)) addChest({cx, top - fh, cz + d * 0.3f}, 0);
        top += 2.6f;
    } else {
        box({x0 - 0.2f, top - 0.25f, z0 - 0.2f}, {x1 + 0.2f, top + 0.1f, z1 + 0.2f}, m, roofC, 200);
        // Parapet
        box({x0 - 0.2f, top + 0.1f, z0 - 0.2f}, {x1 + 0.2f, top + 0.8f, z0 + 0.1f}, m, wallC, 100);
        box({x0 - 0.2f, top + 0.1f, z1 - 0.1f}, {x1 + 0.2f, top + 0.8f, z1 + 0.2f}, m, wallC, 100);
        if (loot && rng_.chance(0.55f)) addChest({cx, top - fh, cz + d * 0.25f}, 0);
    }
    return top;
}

void GameMap::tower(float cx, float cz, float w, float d, int floors, Color4 c, Color4 glass) {
    float x0 = cx - w / 2, x1 = cx + w / 2, z0 = cz - d / 2, z1 = cz + d / 2;
    float base = footprintMax(x0, z0, x1, z1) + 0.3f;
    foundation(x0 - 1, z0 - 1, x1 + 1, z1 + 1, base, rgb(90, 95, 105));
    const float fh = 4.0f;
    float rampW = 2.4f, rampLen = 5.0f;
    for (int f = 0; f < floors; f++) {
        float y = base + f * fh;
        // Corner pillars (indestructible core) + glass/metal panels
        for (int k = 0; k < 4; k++) {
            float px = (k & 1) ? x1 - 0.5f : x0, pz = (k & 2) ? z1 - 0.5f : z0;
            box({px, y, pz}, {px + 0.5f, y + fh, pz + 0.5f}, Material::Metal, shade(c, 0.8f), 0, false);
        }
        wallRun(true, z0, x0 + 0.5f, x1 - 0.5f, y, fh, Material::Metal, f == 0 ? c : glass, f == 0 ? cx : -1, f == 0 ? 2 : 3, 0.25f, f == 0 ? 0 : 1);
        wallRun(true, z1, x0 + 0.5f, x1 - 0.5f, y, fh, Material::Metal, f == 0 ? c : glass, f == 0 ? cx : -1, f == 0 ? 2 : 3, 0.25f, f == 0 ? 0 : 1);
        wallRun(false, x0, z0 + 0.5f, z1 - 0.5f, y, fh, Material::Metal, f == 0 ? c : glass, -1, 2, 0.25f, f == 0 ? 0 : 1);
        wallRun(false, x1, z0 + 0.5f, z1 - 0.5f, y, fh, Material::Metal, f == 0 ? c : glass, f == 0 ? cz : -1, 2, 0.25f, f == 0 ? 0 : 1);
        float yf = y + fh;
        // Alternate the stairwell side each floor
        bool east = f % 2 == 0;
        float rx0 = east ? x1 - 0.5f - rampW : x0 + 0.5f, rx1 = rx0 + rampW;
        float rz0 = z0 + 0.5f, rz1 = rz0 + rampLen;
        if (f < floors - 1) {
            ramp({rx0, y, rz0}, {rx1, yf, rz1}, RAMP_PZ, Material::Metal, shade(c, 0.9f), 200);
            // Slab with hole
            if (east) {
                box({x0 + 0.1f, yf - 0.3f, z0 + 0.1f}, {rx0, yf, z1 - 0.1f}, Material::Metal, shade(c, 0.7f), 300);
                box({rx0, yf - 0.3f, rz1}, {x1 - 0.1f, yf, z1 - 0.1f}, Material::Metal, shade(c, 0.7f), 300);
            } else {
                box({rx1, yf - 0.3f, z0 + 0.1f}, {x1 - 0.1f, yf, z1 - 0.1f}, Material::Metal, shade(c, 0.7f), 300);
                box({x0 + 0.1f, yf - 0.3f, rz1}, {rx1, yf, z1 - 0.1f}, Material::Metal, shade(c, 0.7f), 300);
            }
        } else {
            box({x0, yf - 0.3f, z0}, {x1, yf, z1}, Material::Metal, shade(c, 0.7f), 300);
            // Roof access: small hut with a ramp is skipped; roofs are reachable by building.
        }
        if (rng_.chance(0.7f)) addLoot({cx + rng_.range(-w * 0.25f, w * 0.25f), y, cz + d * 0.2f});
        if (f == floors - 1 || rng_.chance(0.3f)) addChest({cx - w * 0.2f, y, z1 - 1.2f}, 0);
    }
    float top = base + floors * fh;
    // Rooftop antenna / sign
    box({cx - 0.3f, top, cz - 0.3f}, {cx + 0.3f, top + 6, cz + 0.3f}, Material::Metal, rgb(200, 200, 210), 150);
    box({x0 + 1, top, z0 + 1}, {x0 + 3, top + 1.2f, z0 + 3}, Material::Metal, rgb(160, 160, 170), 150);
}

void GameMap::mall(float cx, float cz) {
    float w = 64, d = 40;
    float x0 = cx - w / 2, x1 = cx + w / 2, z0 = cz - d / 2, z1 = cz + d / 2;
    float base = footprintMax(x0, z0, x1, z1) + 0.3f;
    foundation(x0 - 1, z0 - 1, x1 + 1, z1 + 1, base, rgb(150, 150, 150));
    Color4 wallC = rgb(225, 215, 190), accent = rgb(60, 140, 200), floorC = rgb(190, 185, 175);
    const float fh = 5.0f;
    for (int f = 0; f < 2; f++) {
        float y = base + f * fh;
        // Front has a wide entrance on the ground floor (two doors)
        wallRun(true, z0, x0, x1, y, fh, Material::Brick, f == 0 ? wallC : accent, f == 0 ? cx : -1, 2, 0.4f, f == 0 ? 0 : 1);
        wallRun(true, z1, x0, x1, y, fh, Material::Brick, wallC, f == 0 ? cx - 16 : -1, 3);
        wallRun(false, x0, z0, z1, y, fh, Material::Brick, wallC, f == 0 ? cz : -1, 3);
        wallRun(false, x1, z0, z1, y, fh, Material::Brick, wallC, f == 0 ? cz : -1, 3);
        // Interior shop partitions
        for (int s = -2; s <= 2; s++) {
            if (s == 0) continue;
            float sx = cx + s * 11.0f;
            wallRun(false, sx, z1 - 12, z1 - 0.2f, y, fh, Material::Wood, rgb(200, 190, 170), z1 - 6, 0, 0.25f);
            if (rng_.chance(0.8f)) addLoot({sx + 3, y, z1 - 6});
        }
        wallRun(true, z1 - 12, x0 + 0.2f, x1 - 0.2f, y, fh, Material::Wood, rgb(200, 190, 170), cx + 5, 0, 0.25f);
        for (int s = -2; s <= 2; s++) addLoot({cx + s * 11.0f + 5.5f, y, z1 - 4});
        addChest({cx - 22, y, z1 - 3}, 0);
        addChest({cx + 22, y, z1 - 3}, 0);
        addLoot({cx - 8, y, cz - 4});
        addLoot({cx + 8, y, cz - 4});
        ammoBoxes.push_back({cx, y, z1 - 2});
    }
    // Upper floor with a big atrium and escalators
    float yf = base + fh;
    box({x0, yf - 0.3f, z0}, {cx - 7, yf, z1}, Material::Brick, floorC, 400);
    box({cx + 7, yf - 0.3f, z0}, {x1, yf, z1}, Material::Brick, floorC, 400);
    box({cx - 7, yf - 0.3f, z1 - 12}, {cx + 7, yf, z1}, Material::Brick, floorC, 400);
    ramp({cx - 7, base, z0 + 4}, {cx - 4, yf, z0 + 11}, RAMP_PZ, Material::Metal, rgb(160, 160, 170), 250);
    ramp({cx + 4, base, z0 + 4}, {cx + 7, yf, z0 + 11}, RAMP_PZ, Material::Metal, rgb(160, 160, 170), 250);
    // Fountain in the atrium
    box({cx - 2.5f, base, cz - 2.5f}, {cx + 2.5f, base + 0.8f, cz + 2.5f}, Material::Brick, rgb(120, 170, 220), 300);
    addChest({cx, base + 0.8f, cz}, 0);
    // Roof + sign
    float top = base + 2 * fh;
    box({x0, top - 0.3f, z0}, {x1, top, z1}, Material::Brick, rgb(170, 170, 170), 400);
    box({cx - 12, top, z0 - 0.5f}, {cx + 12, top + 3, z0 + 0.2f}, Material::Metal, accent, 200, true, 2);
    addChest({cx + 20, top, cz}, 0);
    // Parking lot with cars in front
    for (int i = 0; i < 10; i++) {
        float x = x0 + 5 + i * 6.0f;
        if (rng_.chance(0.6f)) car(x, z0 - 12, false, jitter(rng_, rgb(rng_.irange(40, 220), rng_.irange(40, 220), rng_.irange(40, 220))));
    }
    // Side shops
    house(x1 + 14, cz - 8, 10, 12, 1, Material::Brick, rgb(210, 180, 160), rgb(90, 90, 100), true, 0);
    house(x1 + 14, cz + 10, 10, 12, 1, Material::Brick, rgb(180, 200, 210), rgb(90, 90, 100), true, 0);
}

void GameMap::barn(float cx, float cz, float w, float d) {
    float x0 = cx - w / 2, x1 = cx + w / 2, z0 = cz - d / 2, z1 = cz + d / 2;
    float base = footprintMax(x0, z0, x1, z1) + 0.2f;
    foundation(x0 - 0.2f, z0 - 0.2f, x1 + 0.2f, z1 + 0.2f, base, rgb(110, 100, 90));
    Color4 red = rgb(160, 45, 35);
    float h = 7.0f;
    // Big barn door front and back
    auto doorWall = [&](float z) {
        box({x0, base, z - 0.15f}, {cx - 2.5f, base + h, z + 0.15f}, Material::Wood, red, 200);
        box({cx + 2.5f, base, z - 0.15f}, {x1, base + h, z + 0.15f}, Material::Wood, red, 200);
        box({cx - 2.5f, base + 4.5f, z - 0.15f}, {cx + 2.5f, base + h, z + 0.15f}, Material::Wood, red, 200);
    };
    doorWall(z0);
    doorWall(z1);
    wallRun(false, x0, z0, z1, base, h, Material::Wood, red, -1, 3);
    wallRun(false, x1, z0, z1, base, h, Material::Wood, red, -1, 3);
    // Loft
    float ly = base + 3.6f;
    box({x0 + 0.15f, ly - 0.25f, cz}, {x1 - 0.15f, ly, z1 - 0.15f}, Material::Wood, rgb(140, 100, 60), 200);
    ramp({x0 + 0.3f, base, cz - 4.5f}, {x0 + 2.5f, ly, cz}, RAMP_PZ, Material::Wood, rgb(140, 100, 60));
    addChest({cx + 2, ly, z1 - 2}, 0);
    addLoot({cx - 2, base, cz - 3});
    addLoot({cx, ly, cz + 3});
    // Hay bales
    box({x1 - 3, base, z0 + 1}, {x1 - 1, base + 1.2f, z0 + 3}, Material::Wood, rgb(220, 190, 90), 60);
    box({x1 - 3, base + 1.2f, z0 + 1.5f}, {x1 - 1.5f, base + 2.2f, z0 + 2.8f}, Material::Wood, rgb(220, 190, 90), 60);
    // Pitched roof
    float top = base + h;
    ramp({x0 - 0.6f, top, z0 - 0.4f}, {cx, top + 3, z1 + 0.4f}, RAMP_PX, Material::Wood, rgb(90, 60, 50), 200);
    ramp({cx, top, z0 - 0.4f}, {x1 + 0.6f, top + 3, z1 + 0.4f}, RAMP_NX, Material::Wood, rgb(80, 55, 45), 200);
}

void GameMap::silo(float cx, float cz, float r, float h, Color4 c) {
    float base = footprintMin(cx - r, cz - r, cx + r, cz + r) - 0.5f;
    box({cx - r * 0.7f, base, cz - r}, {cx + r * 0.7f, base + h, cz + r}, Material::Metal, c, 400);
    box({cx - r, base, cz - r * 0.7f}, {cx + r, base + h, cz + r * 0.7f}, Material::Metal, shade(c, 0.95f), 400);
    Shape s;
    s.id = (uint32_t)shapes.size();
    s.kind = ShapeKind::Cone;
    s.box = AABB({cx - r, base + h, cz - r}, {cx + r, base + h + r * 0.8f, cz + r});
    s.mat = Material::Metal;
    s.color = shade(c, 0.8f);
    s.hp = s.maxHp = 300;
    shapes.push_back(s);
}

void GameMap::container(float x, float z, bool alongX, Color4 c) {
    float lx = alongX ? 6.0f : 2.4f, lz = alongX ? 2.4f : 6.0f;
    float base = footprintMin(x - lx / 2, z - lz / 2, x + lx / 2, z + lz / 2);
    box({x - lx / 2, base - 0.3f, z - lz / 2}, {x + lx / 2, base + 2.6f, z + lz / 2}, Material::Metal, c, 400, true, 3);
}

void GameMap::car(float x, float z, bool alongX, Color4 c) {
    float lx = alongX ? 4.2f : 1.9f, lz = alongX ? 1.9f : 4.2f;
    float base = heightAt(x, z);
    // Style 11: collision boxes that the client replaces with a truck model when available.
    uint32_t body = box({x - lx / 2, base + 0.3f, z - lz / 2}, {x + lx / 2, base + 1.15f, z + lz / 2}, Material::Metal, c, 300, true, 11);
    float cx2 = alongX ? 2.2f : 1.7f, cz2 = alongX ? 1.7f : 2.2f;
    box({x - cx2 / 2, base + 1.15f, z - cz2 / 2}, {x + cx2 / 2, base + 1.8f, z + cz2 / 2}, Material::Metal, shade(c, 0.8f), 200, true, 11);
    Decor d;
    d.kind = DecorKind::Model;
    d.model = (PropModel)((int)PropModel::TruckGreen + rng_.irange(0, 3));
    d.pos = {x, base, z};
    d.yaw = (alongX ? kPi / 2 : 0.0f) + (rng_.chance(0.5f) ? kPi : 0.0f);
    d.scale = 1.5f;
    d.color = c;
    d.linkedShape = body;
    decor.push_back(d);
}

void GameMap::modelProp(PropModel m, float x, float z, float yaw, float scale, Vec3 colHalf, float hp, bool linkCollision) {
    float y = heightAt(x, z);
    uint32_t id = INVALID_ID;
    if (colHalf.x > 0) id = box({x - colHalf.x, y - 0.2f, z - colHalf.z}, {x + colHalf.x, y + colHalf.y * 2, z + colHalf.z}, Material::Brick,
                               rgb(180, 180, 180), hp > 0 ? hp : 400, hp > 0, 11);
    Decor d;
    d.kind = DecorKind::Model;
    d.model = m;
    d.pos = {x, y, z};
    d.yaw = yaw;
    d.scale = scale;
    d.color = rgb(200, 200, 200);
    d.linkedShape = linkCollision ? id : INVALID_ID;
    decor.push_back(d);
}

void GameMap::genLandmarkProps() {
    for (auto& p : pois) {
        float cx = p.center.x, cz = p.center.y;
        switch (p.type) {
            case PoiType::FutureCity:
                for (int i = 0; i < 4; i++) {
                    float a = i * kPi / 2;
                    modelProp(PropModel::Statue, cx + std::cos(a) * 11, cz + std::sin(a) * 11, a + kPi, 2.4f, {0.8f, 1.6f, 0.8f});
                }
                break;
            case PoiType::Suburb:
                modelProp(PropModel::Fountain, cx + 9, cz - 9, 0, 7.0f, {3.2f, 0.9f, 3.2f});
                break;
            case PoiType::SmallTown:
            case PoiType::Hamlet:
                modelProp(PropModel::Fountain, cx - 6, cz - 6, 0, 6.0f, {2.8f, 0.8f, 2.8f});
                break;
            case PoiType::Temple:
                for (int i = 0; i < 8; i++) {
                    float a = i * kPi / 4;
                    modelProp(PropModel::Column, cx + std::cos(a) * 24, cz + std::sin(a) * 24, 0, 4.0f, {1.1f, 2.0f, 1.1f}, 600);
                }
                break;
            case PoiType::Mansion:
                modelProp(PropModel::Statue, cx - 8, cz - 12, 0, 3.0f, {1.0f, 2.0f, 1.0f});
                modelProp(PropModel::Statue, cx + 8, cz - 12, 0, 3.0f, {1.0f, 2.0f, 1.0f});
                modelProp(PropModel::Banner, cx - 4, cz - 7, 0, 3.0f, {0, 0, 0});
                modelProp(PropModel::Banner, cx + 4, cz - 7, 0, 3.0f, {0, 0, 0});
                break;
            case PoiType::LanternVillage:
                for (int i = 0; i < 6; i++) {
                    float a = i * kPi / 3 + 0.3f;
                    modelProp(PropModel::Banner, cx + std::cos(a) * 16, cz + std::sin(a) * 16, a + kPi / 2, 3.0f, {0, 0, 0});
                }
                break;
            case PoiType::Industrial:
            case PoiType::Airfield:
                for (int i = 0; i < 3; i++) modelProp(PropModel::WeaponRack, cx - 12 + i * 3.5f, cz - 2, 0, 2.2f, {0.8f, 0.5f, 0.35f}, 150);
                break;
            default: break;
        }
        // A flag marks the centre of every named location
        if (p.major) modelProp(PropModel::Flag, cx + p.radius * 0.35f, cz + p.radius * 0.35f, 0.4f, 4.0f, {0.25f, 2.4f, 0.25f}, 150);
    }
    // Motorcycles parked around the island
    for (int i = 0; i < 18; i++) {
        Vec3 pt = randomLandPoint(rng_, 150);
        if (pt.y > 30) continue;
        modelProp(PropModel::Motorcycle, pt.x, pt.z, rng_.range(0, 2 * kPi), 1.5f, {0.5f, 0.9f, 1.0f}, 200);
    }
}

void GameMap::pagoda(float cx, float cz, int levels) {
    float w = 11;
    float base = footprintMax(cx - w / 2, cz - w / 2, cx + w / 2, cz + w / 2) + 0.4f;
    foundation(cx - w / 2 - 1, cz - w / 2 - 1, cx + w / 2 + 1, cz + w / 2 + 1, base, rgb(140, 130, 120));
    Color4 wallC = rgb(180, 50, 40), roofC = rgb(50, 70, 60), floorC = rgb(120, 80, 50);
    float y = base;
    for (int l = 0; l < levels; l++) {
        float hw = w / 2 - l * 1.2f;
        float fh = 3.6f;
        float x0 = cx - hw, x1 = cx + hw, z0 = cz - hw, z1 = cz + hw;
        wallRun(true, z0, x0, x1, y, fh, Material::Wood, wallC, cx, 0);
        wallRun(true, z1, x0, x1, y, fh, Material::Wood, wallC, -1, 2);
        wallRun(false, x0, z0 + 0.15f, z1 - 0.15f, y, fh, Material::Wood, wallC, -1, 2);
        wallRun(false, x1, z0 + 0.15f, z1 - 0.15f, y, fh, Material::Wood, wallC, -1, 2);
        float yf = y + fh;
        // Overhanging roof tier
        box({x0 - 1.5f, yf, z0 - 1.5f}, {x1 + 1.5f, yf + 0.4f, z1 + 1.5f}, Material::Wood, roofC, 200);
        if (l < levels - 1) {
            ramp({x1 - 2.4f, y, z0 + 0.3f}, {x1 - 0.2f, yf, z0 + 0.3f + 4.4f}, RAMP_PZ, Material::Wood, floorC);
            // Floor slab (the overhang doubles as the next floor), cut a stairwell by leaving the ramp column open
            box({x0 + 0.15f, yf - 0.25f, z0 + 4.8f}, {x1 - 0.15f, yf, z1 - 0.15f}, Material::Wood, floorC, 200);
            box({x0 + 0.15f, yf - 0.25f, z0 + 0.15f}, {x1 - 2.5f, yf, z0 + 4.8f}, Material::Wood, floorC, 200);
        }
        addLoot({cx - 1, y, cz + 1});
        y = yf + 0.4f;
    }
    addChest({cx, y - 4.0f, cz + 1}, 0);
    Shape s;
    s.id = (uint32_t)shapes.size();
    s.kind = ShapeKind::Cone;
    float hw = w / 2 - (levels - 1) * 1.2f;
    s.box = AABB({cx - hw - 1, y, cz - hw - 1}, {cx + hw + 1, y + 3.5f, cz + hw + 1});
    s.mat = Material::Wood; s.color = roofC; s.hp = s.maxHp = 200;
    shapes.push_back(s);
}

void GameMap::templePyramid(float cx, float cz) {
    float base = footprintMin(cx - 20, cz - 20, cx + 20, cz + 20) - 0.5f;
    Color4 stone = rgb(150, 140, 110);
    float y = base;
    int steps = 6;
    for (int i = 0; i < steps; i++) {
        float hw = 18 - i * 2.8f;
        float h = 2.8f;
        box({cx - hw, y, cz - hw}, {cx + hw, y + h, cz + hw}, Material::Brick, jitter(rng_, stone, 0.06f), 0, false);
        // Stairs up the south face
        ramp({cx - 2.5f, y, cz + hw}, {cx + 2.5f, y + h, cz + hw + 2.8f}, RAMP_NZ, Material::Brick, shade(stone, 0.9f), 400);
        if (i % 2 == 1) addLoot({cx + hw - 1.5f, y + h, cz});
        y += h;
    }
    // Shrine on top
    float hw = 3.5f;
    wallRun(true, cz - hw, cx - hw, cx + hw, y, 3.2f, Material::Brick, stone, -1, 0);
    wallRun(false, cx - hw, cz - hw, cz + hw, y, 3.2f, Material::Brick, stone, -1, 2);
    wallRun(false, cx + hw, cz - hw, cz + hw, y, 3.2f, Material::Brick, stone, -1, 2);
    box({cx - hw - 0.5f, y + 3.2f, cz - hw - 0.5f}, {cx + hw + 0.5f, y + 3.7f, cz + hw + 0.5f}, Material::Brick, shade(stone, 0.8f), 300);
    addChest({cx, y, cz - 1}, 0);
    addChest({cx - 12, base + 2.8f, cz + 12}, 0);
    // Jungle huts around
    for (int i = 0; i < 4; i++) {
        float a = i * kPi / 2 + 0.6f;
        house(cx + std::cos(a) * 32, cz + std::sin(a) * 32, 6, 6, 1, Material::Wood, rgb(130, 100, 60), rgb(90, 120, 50), true, 2);
    }
}

void GameMap::hangar(float cx, float cz, bool alongX) {
    float w = alongX ? 30 : 22, d = alongX ? 22 : 30;
    float x0 = cx - w / 2, x1 = cx + w / 2, z0 = cz - d / 2, z1 = cz + d / 2;
    float base = footprintMax(x0, z0, x1, z1) + 0.2f;
    foundation(x0, z0, x1, z1, base, rgb(130, 130, 135));
    Color4 c = rgb(150, 160, 170);
    float h = 10;
    // Open front on z0 (or x0)
    if (alongX) {
        wallRun(true, z1, x0, x1, base, h, Material::Metal, c, cx, 4);
        wallRun(false, x0, z0, z1, base, h, Material::Metal, c, -1, 3);
        wallRun(false, x1, z0, z1, base, h, Material::Metal, c, -1, 3);
        box({x0, base + 7, z0 - 0.2f}, {x1, base + h, z0 + 0.2f}, Material::Metal, c, 400);
    } else {
        wallRun(false, x1, z0, z1, base, h, Material::Metal, c, cz, 4);
        wallRun(true, z0, x0, x1, base, h, Material::Metal, c, -1, 3);
        wallRun(true, z1, x0, x1, base, h, Material::Metal, c, -1, 3);
        box({x0 - 0.2f, base + 7, z0}, {x0 + 0.2f, base + h, z1}, Material::Metal, c, 400);
    }
    box({x0, base + h, z0}, {x1, base + h + 0.4f, z1}, Material::Metal, shade(c, 0.8f), 500);
    // Small plane inside
    box({cx - 1, base + 0.8f, cz - 5}, {cx + 1, base + 2.4f, cz + 5}, Material::Metal, rgb(220, 220, 230), 300);
    box({cx - 6, base + 1.6f, cz - 1}, {cx + 6, base + 1.9f, cz + 1}, Material::Metal, rgb(200, 60, 60), 200);
    // Catwalk
    box({x0 + 0.3f, base + 5, z1 - 3}, {x1 - 0.3f, base + 5.3f, z1 - 0.3f}, Material::Metal, rgb(100, 100, 110), 250);
    ramp({x0 + 0.3f, base, z1 - 9.5f}, {x0 + 2.8f, base + 5, z1 - 3}, RAMP_PZ, Material::Metal, rgb(100, 100, 110), 250);
    addChest({cx + 4, base + 5.3f, z1 - 1.5f}, 0);
    addLoot({cx - 4, base, cz + 3});
    addLoot({cx + 5, base, cz - 4});
    ammoBoxes.push_back({x1 - 1.5f, base, z0 + 2});
}

void GameMap::factory(float cx, float cz, float w, float d) {
    float x0 = cx - w / 2, x1 = cx + w / 2, z0 = cz - d / 2, z1 = cz + d / 2;
    float base = footprintMax(x0, z0, x1, z1) + 0.3f;
    foundation(x0 - 1, z0 - 1, x1 + 1, z1 + 1, base, rgb(110, 110, 110));
    Color4 c = rgb(130, 120, 110);
    float h = 9;
    wallRun(true, z0, x0, x1, base, h, Material::Metal, c, cx, 3);
    wallRun(true, z1, x0, x1, base, h, Material::Metal, c, cx - w / 4, 3);
    wallRun(false, x0, z0, z1, base, h, Material::Brick, rgb(150, 80, 60), cz, 3);
    wallRun(false, x1, z0, z1, base, h, Material::Brick, rgb(150, 80, 60), -1, 3);
    box({x0, base + h, z0}, {x1, base + h + 0.4f, z1}, Material::Metal, rgb(90, 90, 95), 500);
    // Catwalk & machines
    box({x0 + 0.3f, base + 4.5f, z0 + 0.3f}, {x0 + 3.5f, base + 4.8f, z1 - 0.3f}, Material::Metal, rgb(90, 90, 100), 250);
    ramp({x0 + 3.5f, base, z1 - 7}, {x0 + 6, base + 4.8f, z1 - 1}, RAMP_NX, Material::Metal, rgb(90, 90, 100), 250);
    for (int i = 0; i < 3; i++) {
        float mx = cx - w / 4 + i * w / 4;
        box({mx - 1.5f, base, cz - 2}, {mx + 1.5f, base + 3, cz + 2}, Material::Metal, rgb(200, 150, 40), 400, true, 3);
    }
    addChest({x0 + 1.5f, base + 4.8f, cz}, 0);
    addLoot({cx, base, z0 + 3});
    addLoot({cx + w / 4, base, z1 - 3});
    addLoot({cx - w / 4, base, z1 - 3});
    ammoBoxes.push_back({x1 - 2, base, z1 - 2});
    // Chimney
    box({x1 - 3, base + h, z0 + 2}, {x1 - 1.5f, base + h + 12, z0 + 3.5f}, Material::Brick, rgb(140, 70, 50), 400);
}

void GameMap::watchtower(float cx, float cz, float h) {
    float base = footprintMin(cx - 3, cz - 3, cx + 3, cz + 3) - 0.3f;
    Color4 wood = rgb(120, 85, 55);
    for (int k = 0; k < 4; k++) {
        float px = (k & 1) ? cx + 2.2f : cx - 2.7f, pz = (k & 2) ? cz + 2.2f : cz - 2.7f;
        box({px, base, pz}, {px + 0.5f, base + h, pz + 0.5f}, Material::Wood, wood, 300);
    }
    // Zig-zag ramps up the tower
    float y = base;
    int seg = 0;
    while (y + 4 <= base + h) {
        uint8_t dir = seg % 2 == 0 ? RAMP_PZ : RAMP_NZ;
        float x0 = seg % 2 == 0 ? cx - 2.2f : cx + 0.1f;
        ramp({x0, y, cz - 2.2f}, {x0 + 2.1f, y + 4, cz + 2.2f}, dir, Material::Wood, wood);
        y += 4;
        seg++;
    }
    float top = base + h;
    box({cx - 3.5f, top, cz - 3.5f}, {cx + 3.5f, top + 0.3f, cz + 3.5f}, Material::Wood, wood, 250);
    box({cx - 3.5f, top + 0.3f, cz - 3.5f}, {cx + 3.5f, top + 1.2f, cz - 3.3f}, Material::Wood, wood, 100);
    box({cx - 3.5f, top + 0.3f, cz + 3.3f}, {cx + 3.5f, top + 1.2f, cz + 3.5f}, Material::Wood, wood, 100);
    box({cx - 3.8f, top + 3.4f, cz - 3.8f}, {cx + 3.8f, top + 3.7f, cz + 3.8f}, Material::Wood, rgb(70, 50, 35), 200);
    addChest({cx, top + 0.3f, cz}, 0);
}

void GameMap::ship(float cx, float cz) {
    float base = -1.0f;
    Color4 hull = rgb(90, 60, 40), deck = rgb(150, 110, 70);
    box({cx - 4, base, cz - 14}, {cx + 4, base + 4, cz + 14}, Material::Wood, hull, 400);
    box({cx - 3, base + 4, cz - 16}, {cx + 3, base + 5, cz - 14}, Material::Wood, hull, 200);
    box({cx - 4, base + 4, cz - 14}, {cx - 3.7f, base + 5, cz + 14}, Material::Wood, hull, 150);
    box({cx + 3.7f, base + 4, cz - 14}, {cx + 4, base + 5, cz + 14}, Material::Wood, hull, 150);
    // Captain cabin
    house(cx, cz + 10, 7, 6, 1, Material::Wood, deck, rgb(60, 40, 30), true, 0);
    box({cx - 0.4f, base + 4, cz - 3}, {cx + 0.4f, base + 22, cz - 2.2f}, Material::Wood, rgb(80, 55, 35), 300);
    box({cx - 5, base + 10, cz - 2.8f}, {cx + 5, base + 17, cz - 2.6f}, Material::None, rgb(240, 235, 220), 100, true, 5);
    addChest({cx, base + 4, cz - 8}, 0);
    addLoot({cx + 1, base + 4, cz});
}

void GameMap::rock(float x, float z, float size, Color4 c) {
    float base = heightAt(x, z) - 0.5f;
    box({x - size * 0.6f, base, z - size * 0.5f}, {x + size * 0.6f, base + size * 0.9f, z + size * 0.5f}, Material::Brick, c, 250, true, 6);
    if (size > 2) box({x - size * 0.3f, base + size * 0.9f, z - size * 0.3f}, {x + size * 0.4f, base + size * 1.3f, z + size * 0.25f}, Material::Brick, shade(c, 1.1f), 150, true, 6);
}

void GameMap::fenceRing(float x0, float z0, float x1, float z1, Color4 c) {
    auto run = [&](bool alongX, float fixed, float a, float b) {
        for (float s = a; s < b; s += 4.0f) {
            if (rng_.chance(0.15f)) continue;
            float e = std::min(b, s + 3.6f);
            float y = alongX ? heightAt((s + e) / 2, fixed) : heightAt(fixed, (s + e) / 2);
            if (alongX) box({s, y, fixed - 0.08f}, {e, y + 1.1f, fixed + 0.08f}, Material::Wood, c, 60);
            else box({fixed - 0.08f, y, s}, {fixed + 0.08f, y + 1.1f, e}, Material::Wood, c, 60);
        }
    };
    run(true, z0, x0, x1);
    run(true, z1, x0, x1);
    run(false, x0, z0, z1);
    run(false, x1, z0, z1);
}

void GameMap::gasStation(float cx, float cz) {
    float base = footprintMax(cx - 10, cz - 8, cx + 10, cz + 8) + 0.1f;
    flatten({cx, cz}, 11, 9, base - 0.1f, 6);
    for (int k = 0; k < 4; k++) {
        float px = (k & 1) ? cx + 5 : cx - 5.4f, pz = (k & 2) ? cz + 3 : cz - 3.4f;
        box({px, base, pz}, {px + 0.4f, base + 4.5f, pz + 0.4f}, Material::Metal, rgb(220, 220, 220), 200);
    }
    box({cx - 7, base + 4.5f, cz - 5}, {cx + 7, base + 5.2f, cz + 5}, Material::Metal, rgb(220, 60, 50), 300);
    box({cx - 1.8f, base, cz - 0.5f}, {cx - 1.0f, base + 1.6f, cz + 0.5f}, Material::Metal, rgb(230, 230, 230), 150);
    box({cx + 1.0f, base, cz - 0.5f}, {cx + 1.8f, base + 1.6f, cz + 0.5f}, Material::Metal, rgb(230, 230, 230), 150);
    house(cx, cz + 11, 9, 7, 1, Material::Brick, rgb(230, 230, 220), rgb(200, 60, 50), true, 0);
}

void GameMap::mansion(float cx, float cz) {
    Color4 wall = rgb(90, 85, 100), roof = rgb(45, 40, 55);
    house(cx, cz, 16, 12, 3, Material::Brick, wall, roof, true, 1);
    house(cx - 13, cz + 2, 10, 9, 2, Material::Brick, wall, roof, true, 1);
    house(cx + 13, cz + 2, 10, 9, 2, Material::Brick, wall, roof, true, 1);
    addChest({cx, footprintMax(cx - 8, cz - 6, cx + 8, cz + 6) + 0.3f, cz - 3}, 0);
    // Crypts
    for (int i = 0; i < 5; i++) {
        float x = cx - 20 + i * 10, z = cz + 22;
        float y = heightAt(x, z);
        box({x - 1.5f, y - 0.5f, z - 2}, {x + 1.5f, y + 2.5f, z + 2}, Material::Brick, rgb(120, 120, 125), 300);
        if (i % 2 == 0) addLoot({x, y, z + 3});
    }
}

void GameMap::crossroadsSign(float x, float z) {
    float y = heightAt(x, z);
    box({x - 0.15f, y, z - 0.15f}, {x + 0.15f, y + 3.5f, z + 0.15f}, Material::Wood, rgb(110, 80, 50), 60);
    box({x - 1.2f, y + 2.6f, z - 0.05f}, {x + 1.2f, y + 3.1f, z + 0.05f}, Material::Wood, rgb(200, 180, 120), 40);
}

void GameMap::addTree(DecorKind k, float x, float z, float scale) {
    float y = heightAt(x, z);
    if (y < WATER_LEVEL + 0.5f) return;
    float trunkH = 3.0f * scale, trunkW = 0.55f * scale;
    Color4 bark = rgb(100, 70, 45);
    Color4 leaf = rgb(60, 130, 50);
    switch (k) {
        case DecorKind::PineTree: trunkH = 2.5f * scale; leaf = rgb(40, 100, 55); break;
        case DecorKind::SnowPine: trunkH = 2.5f * scale; leaf = rgb(210, 230, 235); break;
        case DecorKind::PalmTree: trunkH = 6.0f * scale; trunkW = 0.4f * scale; bark = rgb(150, 120, 80); leaf = rgb(70, 150, 60); break;
        case DecorKind::JungleTree: trunkH = 6.0f * scale; trunkW = 0.9f * scale; leaf = rgb(40, 120, 40); break;
        case DecorKind::Cactus: trunkH = 3.2f * scale; trunkW = 0.7f * scale; bark = rgb(70, 140, 70); break;
        case DecorKind::DeadTree: trunkH = 4.0f * scale; trunkW = 0.45f * scale; bark = rgb(60, 50, 45); break;
        default: break;
    }
    uint32_t trunk = box({x - trunkW / 2, y - 0.3f, z - trunkW / 2}, {x + trunkW / 2, y + trunkH, z + trunkW / 2}, Material::Wood,
                         bark, 150 + 50 * scale, true, 7);
    if (k != DecorKind::Cactus && k != DecorKind::DeadTree) {
        Decor d;
        d.kind = k;
        d.pos = {x, y + trunkH, z};
        d.scale = scale;
        d.linkedShape = trunk;
        d.color = jitter(rng_, leaf, 0.12f);
        decor.push_back(d);
    } else {
        Decor d;
        d.kind = k;
        d.pos = {x, y + trunkH, z};
        d.scale = scale;
        d.linkedShape = trunk;
        d.color = bark;
        decor.push_back(d);
    }
}

// =====================================================================================
// POIs

void GameMap::buildPoi(POI& p) {
    float cx = p.center.x, cz = p.center.y;
    Rng& r = rng_;
    auto ring = [&](int count, float rad, auto fn) {
        float start = r.range(0, 2 * kPi);
        for (int i = 0; i < count; i++) {
            float a = start + i * 2 * kPi / count + r.range(-0.15f, 0.15f);
            float rr = rad * r.range(0.8f, 1.05f);
            fn(cx + std::cos(a) * rr, cz + std::sin(a) * rr, i);
        }
    };
    Color4 houseColors[] = {rgb(230, 220, 200), rgb(200, 170, 140), rgb(170, 190, 210), rgb(210, 200, 150), rgb(190, 150, 150), rgb(160, 180, 150)};
    Color4 roofColors[] = {rgb(130, 60, 50), rgb(70, 70, 80), rgb(60, 90, 120), rgb(100, 80, 60)};
    auto hc = [&]() { return jitter(r, houseColors[r.irange(0, 5)]); };
    auto rc = [&]() { return roofColors[r.irange(0, 3)]; };

    switch (p.type) {
        case PoiType::FutureCity: {
            // Grid of glass towers around a plaza with a central spire.
            Color4 metal = rgb(170, 180, 195), glass = rgb(110, 200, 230, 170);
            for (int gz = -2; gz <= 2; gz++)
                for (int gx = -2; gx <= 2; gx++) {
                    if (gx == 0 && gz == 0) continue;
                    if (std::abs(gx) + std::abs(gz) > 3) continue;
                    float x = cx + gx * 24 + r.range(-2, 2), z = cz + gz * 24 + r.range(-2, 2);
                    int floors = r.irange(2, 7);
                    tower(x, z, r.range(11, 15), r.range(11, 15), floors, jitter(r, metal, 0.06f), glass);
                }
            // Central spire
            tower(cx, cz, 10, 10, 9, rgb(200, 210, 225), rgb(160, 110, 240, 170));
            // Hover platforms
            for (int i = 0; i < 4; i++) {
                float a = i * kPi / 2 + kPi / 4;
                float x = cx + std::cos(a) * 18, z = cz + std::sin(a) * 18;
                float y = heightAt(x, z) + 9 + i * 2;
                box({x - 4, y, z - 4}, {x + 4, y + 0.5f, z + 4}, Material::Metal, rgb(90, 220, 240), 300, true, 2);
                addLoot({x, y + 0.5f, z});
            }
            // Street cars
            for (int i = 0; i < 8; i++) car(cx + r.range(-55, 55), cz + (i % 2 ? 12 : -12), true, jitter(r, rgb(220, 220, 230)));
            break;
        }
        case PoiType::Mall: mall(cx, cz); break;
        case PoiType::Industrial: {
            factory(cx - 10, cz - 6, 30, 20);
            factory(cx + 20, cz + 18, 22, 16);
            silo(cx + 24, cz - 14, 4, 16, rgb(180, 180, 185));
            silo(cx + 34, cz - 14, 4, 16, rgb(170, 170, 175));
            silo(cx + 29, cz - 24, 4, 20, rgb(190, 190, 195));
            for (int i = 0; i < 10; i++) {
                Color4 cc[] = {rgb(200, 60, 40), rgb(40, 110, 170), rgb(50, 140, 70), rgb(210, 150, 40)};
                container(cx - 30 + r.range(0, 22), cz + 14 + r.range(0, 22), r.chance(0.5f), cc[r.irange(0, 3)]);
            }
            house(cx - 32, cz - 26, 8, 8, 2, Material::Brick, rgb(150, 150, 150), rgb(80, 80, 80), true, 0);
            break;
        }
        case PoiType::PirateCove: {
            ship(cx - 30, cz);
            // Docks
            float y = 0.6f;
            box({cx - 26, y - 0.3f, cz - 2}, {cx - 4, y, cz + 2}, Material::Wood, rgb(130, 95, 60), 200);
            for (int i = 0; i < 5; i++) {
                float x = cx - 4 + std::cos(i * 1.3f) * 16, z = cz + std::sin(i * 1.3f) * 20;
                house(x, z, 7, 6, 1, Material::Wood, rgb(150, 110, 70), rgb(80, 60, 40), true, 1);
            }
            watchtower(cx + 10, cz - 22, 12);
            break;
        }
        case PoiType::SnowLodge: {
            house(cx, cz, 14, 10, 2, Material::Wood, rgb(140, 95, 60), rgb(230, 235, 240), true, 1);
            ring(5, 24, [&](float x, float z, int) { house(x, z, 7, 7, 1 + (r.chance(0.4f) ? 1 : 0), Material::Wood, rgb(130, 90, 60), rgb(230, 235, 240), true, 1); });
            // Ski lift poles
            for (int i = 0; i < 4; i++) {
                float x = cx + 30 + i * 12, z = cz - 10 - i * 6;
                float y = heightAt(x, z);
                box({x - 0.3f, y, z - 0.3f}, {x + 0.3f, y + 10, z + 0.3f}, Material::Metal, rgb(80, 80, 90), 200);
            }
            break;
        }
        case PoiType::Airfield: {
            // Runway
            flatten({cx, cz}, 60, 10, p.baseHeight, 10);
            hangar(cx - 20, cz - 22, true);
            hangar(cx + 18, cz - 22, true);
            tower(cx + 42, cz - 10, 6, 6, 3, rgb(220, 220, 230), rgb(140, 200, 230, 180));
            house(cx - 42, cz - 16, 8, 8, 1, Material::Metal, rgb(200, 200, 210), rgb(90, 90, 100), true, 0);
            for (int i = 0; i < 6; i++) container(cx - 40 + i * 6, cz + 20, false, rgb(90, 110, 140));
            break;
        }
        case PoiType::DesertTown: {
            Color4 adobe = rgb(215, 170, 120);
            // Hotel
            house(cx, cz, 18, 12, 3, Material::Brick, rgb(230, 200, 170), rgb(200, 120, 80), true, 0);
            ring(8, 38, [&](float x, float z, int i) {
                if (i % 3 == 0) gasStation(x, z);
                else house(x, z, r.range(8, 11), r.range(8, 10), r.irange(1, 2), Material::Brick, jitter(r, adobe), rgb(180, 110, 70), true, 0);
            });
            // Car dealership lot
            for (int i = 0; i < 6; i++) car(cx - 15 + i * 6, cz + 22, false, jitter(r, rgb(r.irange(60, 240), r.irange(60, 240), r.irange(60, 240))));
            break;
        }
        case PoiType::LanternVillage: {
            pagoda(cx, cz, 3);
            ring(6, 26, [&](float x, float z, int) { house(x, z, 8, 7, 1, Material::Wood, rgb(230, 220, 200), rgb(160, 50, 40), true, 2); });
            // Lantern posts
            ring(10, 15, [&](float x, float z, int) {
                float y = heightAt(x, z);
                box({x - 0.1f, y, z - 0.1f}, {x + 0.1f, y + 3, z + 0.1f}, Material::Wood, rgb(60, 40, 30), 40);
                Decor d; d.kind = DecorKind::Lamp; d.pos = {x, y + 3, z}; d.color = rgb(255, 120, 60); decor.push_back(d);
            });
            break;
        }
        case PoiType::Farm: {
            barn(cx - 12, cz, 12, 18);
            barn(cx + 16, cz - 16, 10, 14);
            house(cx + 14, cz + 14, 10, 9, 2, Material::Wood, rgb(230, 225, 210), rgb(120, 50, 40), true, 1);
            silo(cx - 26, cz - 14, 3.5f, 14, rgb(200, 200, 205));
            fenceRing(cx - 40, cz - 36, cx + 40, cz + 36, rgb(160, 130, 90));
            // Crop rows (decor)
            for (float z = cz + 20; z < cz + 34; z += 2.5f)
                for (float x = cx - 38; x < cx - 5; x += 2.0f) {
                    Decor d; d.kind = DecorKind::Crop; d.pos = {x, heightAt(x, z), z}; d.scale = r.range(0.8f, 1.2f); d.color = rgb(210, 190, 70);
                    decor.push_back(d);
                }
            break;
        }
        case PoiType::Suburb:
        case PoiType::SmallTown:
        case PoiType::Hamlet: {
            int count = p.type == PoiType::Suburb ? 10 : p.type == PoiType::SmallTown ? 8 : 5;
            float rad = p.radius * 0.7f;
            // Central feature
            if (p.type == PoiType::Suburb) {
                // Park with a gazebo
                box({cx - 3, heightAt(cx, cz), cz - 3}, {cx + 3, heightAt(cx, cz) + 0.3f, cz + 3}, Material::Wood, rgb(200, 190, 170), 150);
                box({cx - 3.2f, heightAt(cx, cz) + 3, cz - 3.2f}, {cx + 3.2f, heightAt(cx, cz) + 3.3f, cz + 3.2f}, Material::Wood, rgb(150, 60, 50), 150);
                for (int k = 0; k < 4; k++) {
                    float px = (k & 1) ? cx + 2.7f : cx - 3.0f, pz = (k & 2) ? cz + 2.7f : cz - 3.0f;
                    box({px, heightAt(cx, cz), pz}, {px + 0.3f, heightAt(cx, cz) + 3, pz + 0.3f}, Material::Wood, rgb(230, 230, 230), 80);
                }
                addChest({cx, heightAt(cx, cz) + 0.3f, cz}, 0);
            } else if (p.type == PoiType::SmallTown) {
                crossroadsSign(cx + 3, cz + 3);
                gasStation(cx - 16, cz + 20);
            }
            ring(count, rad, [&](float x, float z, int) {
                int floors = r.chance(0.5f) ? 2 : 1;
                house(x, z, r.range(8, 11), r.range(8, 10), floors, r.chance(0.3f) ? Material::Brick : Material::Wood, hc(), rc(), true, r.chance(0.7f) ? 1 : 0);
            });
            if (p.type != PoiType::Hamlet) ring(count / 2, rad * 1.5f, [&](float x, float z, int) {
                house(x, z, r.range(7, 9), r.range(7, 9), 1, Material::Wood, hc(), rc(), true, 1);
            });
            break;
        }
        case PoiType::Lodge: {
            watchtower(cx, cz, 24);
            ring(4, 14, [&](float x, float z, int) { house(x, z, 7, 6, 1, Material::Wood, rgb(120, 80, 50), rgb(70, 90, 60), true, 1); });
            break;
        }
        case PoiType::Junkyard: {
            // Stacks of crushed cars
            for (int i = 0; i < 26; i++) {
                float x = cx + r.range(-26, 26), z = cz + r.range(-26, 26);
                float y = heightAt(x, z);
                int stack = r.irange(1, 4);
                for (int s = 0; s < stack; s++) {
                    Color4 c = jitter(r, rgb(r.irange(80, 200), r.irange(60, 160), r.irange(50, 140)), 0.1f);
                    box({x - 2, y + s * 1.2f, z - 1}, {x + 2, y + s * 1.2f + 1.2f, z + 1}, Material::Metal, c, 250, true, 4);
                }
                if (i % 4 == 0) addLoot({x + 2.5f, y, z});
            }
            house(cx + 30, cz, 8, 6, 1, Material::Metal, rgb(160, 160, 150), rgb(100, 90, 80), true, 0);
            addChest({cx, heightAt(cx, cz) + 0.1f, cz}, 0);
            break;
        }
        case PoiType::Mansion: mansion(cx, cz); break;
        case PoiType::Temple: templePyramid(cx, cz); break;
        case PoiType::Estates: {
            house(cx, cz, 18, 12, 2, Material::Brick, rgb(245, 245, 240), rgb(60, 60, 70), true, 0);
            house(cx + 4, cz + 26, 14, 10, 2, Material::Brick, rgb(240, 230, 210), rgb(70, 90, 110), true, 0);
            house(cx + 2, cz - 26, 14, 10, 2, Material::Metal, rgb(220, 230, 240), rgb(60, 60, 70), true, 0);
            // Pools
            float y = heightAt(cx + 16, cz);
            box({cx + 12, y - 1.5f, cz - 4}, {cx + 20, y + 0.1f, cz + 4}, Material::Brick, rgb(90, 190, 230), 0, false, 8);
            break;
        }
        case PoiType::Mines: {
            for (int i = 0; i < 5; i++) {
                float x = cx + r.range(-22, 22), z = cz + r.range(-22, 22);
                house(x, z, 6, 6, 1, Material::Wood, rgb(130, 100, 70), rgb(90, 70, 50), true, 0);
            }
            // Mine tunnel frame
            float y = heightAt(cx, cz);
            box({cx - 3, y, cz - 0.3f}, {cx - 2.5f, y + 4, cz + 0.3f}, Material::Wood, rgb(100, 70, 40), 150);
            box({cx + 2.5f, y, cz - 0.3f}, {cx + 3, y + 4, cz + 0.3f}, Material::Wood, rgb(100, 70, 40), 150);
            box({cx - 3, y + 4, cz - 0.3f}, {cx + 3, y + 4.5f, cz + 0.3f}, Material::Wood, rgb(100, 70, 40), 150);
            for (int i = 0; i < 4; i++) container(cx + 20 + r.range(-4, 4), cz - 15 + i * 7, true, rgb(160, 120, 60));
            addChest({cx, y, cz + 2}, 0);
            break;
        }
        case PoiType::LakeHouse: {
            house(cx, cz, 9, 8, 2, Material::Wood, rgb(220, 220, 210), rgb(60, 80, 110), true, 1);
            break;
        }
        case PoiType::Volcano: {
            // Vents around the volcano launch players into the air.
            for (int i = 0; i < 6; i++) {
                float a = i * kPi / 3;
                float x = cx + std::cos(a) * 55, z = cz + std::sin(a) * 55;
                vents.push_back({{x, heightAt(x, z), z}, 3.5f});
            }
            addChest({cx + 30, heightAt(cx + 30, cz + 20), cz + 20}, 0);
            break;
        }
        case PoiType::Landmark: {
            int kind = (int)(r.next() % 4);
            if (kind == 0) gasStation(cx, cz);
            else if (kind == 1) watchtower(cx, cz, 14);
            else if (kind == 2) { barn(cx, cz, 10, 12); }
            else { house(cx, cz, 9, 8, 2, Material::Wood, hc(), rc(), true, 1); }
            break;
        }
        case PoiType::StuntPark: break; // built by genDetails
    }
}

void GameMap::genPois() {
    for (auto& ps : kPois) {
        POI p;
        p.name = ps.name;
        p.type = ps.type;
        p.center = {ps.u * W, ps.v * W};
        p.radius = ps.radius;
        p.major = ps.major;
        p.baseHeight = std::max(2.0f, heightAt(p.center.x, p.center.y));
        pois.push_back(p);
    }
    // Flatten town areas so buildings sit nicely (not the volcano or the lake).
    for (auto& p : pois) {
        if (p.type == PoiType::Volcano || p.type == PoiType::LakeHouse) continue;
        float flatR = p.radius * (p.type == PoiType::Temple ? 0.6f : 0.85f);
        flatten(p.center, flatR, flatR, p.baseHeight, p.radius * 0.6f);
    }
    for (auto& p : pois) buildPoi(p);
    // Outdoor loot around every named location.
    for (auto& p : pois) {
        int n = p.major ? 14 : 5;
        for (int i = 0; i < n; i++) {
            float a = rng_.range(0, 2 * kPi), d = rng_.range(0.2f, 1.0f) * p.radius;
            float x = p.center.x + std::cos(a) * d, z = p.center.y + std::sin(a) * d;
            if (!isLand(x, z)) continue;
            if (i < (p.major ? 2 : 1)) addChest({x, heightAt(x, z), z}, 0);
            else addLoot({x, heightAt(x, z), z});
        }
    }

    // Scattered lone houses and landmarks outside POIs.
    for (int i = 0; i < 45; i++) {
        Vec3 pt = randomLandPoint(rng_, 120);
        if (nearestPoi(pt.x, pt.z, 90)) continue;
        if (pt.y > 35) continue;
        Biome b = biomeAt(pt.x, pt.z);
        Color4 wall = b == Biome::Desert ? rgb(215, 175, 125) : b == Biome::Snow ? rgb(140, 100, 70) : rgb(220, 215, 200);
        Color4 roof = b == Biome::Snow ? rgb(230, 235, 240) : rgb(120, 60, 50);
        flatten({pt.x, pt.z}, 6, 6, pt.y, 8);
        if (i % 7 == 0) gasStation(pt.x, pt.z);
        else if (i % 5 == 0) watchtower(pt.x, pt.z, 12);
        else house(pt.x, pt.z, rng_.range(7, 10), rng_.range(7, 9), rng_.irange(1, 2), Material::Wood, wall, roof, true, 1);
    }
    // Cars along roads
    for (auto& rd : roads) {
        Vec2 d = rd.b - rd.a;
        float len = d.len();
        for (float t = 20; t < len - 20; t += rng_.range(35, 80)) {
            Vec2 p = rd.a + d * (t / len);
            if (!isLand(p.x, p.y) || nearestPoi(p.x, p.y, 40)) continue;
            bool alongX = std::fabs(d.x) > std::fabs(d.y);
            Vec2 side = alongX ? Vec2{0, 4.5f} : Vec2{4.5f, 0};
            car(p.x + side.x, p.y + side.y, alongX, jitter(rng_, rgb(rng_.irange(50, 220), rng_.irange(50, 220), rng_.irange(50, 220))));
        }
    }
}

void GameMap::genNature() {
    // Occupancy bitmap (2m resolution) to keep trees out of buildings.
    const int ON = (int)(W / 2);
    std::vector<uint8_t> occ((size_t)ON * ON, 0);
    for (auto& s : shapes) {
        int x0 = std::max(0, (int)(s.box.min.x / 2) - 1), x1 = std::min(ON - 1, (int)(s.box.max.x / 2) + 1);
        int z0 = std::max(0, (int)(s.box.min.z / 2) - 1), z1 = std::min(ON - 1, (int)(s.box.max.z / 2) + 1);
        for (int z = z0; z <= z1; z++)
            for (int x = x0; x <= x1; x++) occ[(size_t)z * ON + x] = 1;
    }
    auto freeAt = [&](float x, float z) {
        int ix = (int)(x / 2), iz = (int)(z / 2);
        if (ix < 0 || iz < 0 || ix >= ON || iz >= ON) return false;
        if (occ[(size_t)iz * ON + ix]) return false;
        int cx = (int)(x / CELL), cz = (int)(z / CELL);
        if (roadMask[(size_t)std::min(HM_N - 1, cz) * HM_N + std::min(HM_N - 1, cx)]) return false;
        return true;
    };
    for (float z = 6; z < W - 6; z += 9.0f) {
        for (float x = 6; x < W - 6; x += 9.0f) {
            float px = x + rng_.range(-4, 4), pz = z + rng_.range(-4, 4);
            float h = heightAt(px, pz);
            if (h < WATER_LEVEL + 0.8f) continue;
            if (!freeAt(px, pz)) continue;
            Biome b = biomeAt(px, pz);
            float roll = rng_.uniform();
            const POI* nearP = nearestPoi(px, pz);
            bool inTown = nearP && dist2d(nearP->center, {px, pz}) < nearP->radius * 0.9f;
            float density = 0.0f;
            DecorKind kind = DecorKind::OakTree;
            switch (b) {
                case Biome::Grass: density = 0.14f; kind = rng_.chance(0.5f) ? DecorKind::OakTree : DecorKind::PineTree; break;
                case Biome::Forest: density = 0.55f; kind = rng_.chance(0.7f) ? DecorKind::PineTree : DecorKind::OakTree; break;
                case Biome::Snow: density = 0.25f; kind = DecorKind::SnowPine; break;
                case Biome::Desert: density = 0.06f; kind = rng_.chance(0.7f) ? DecorKind::Cactus : DecorKind::PalmTree; break;
                case Biome::Jungle: density = 0.5f; kind = rng_.chance(0.6f) ? DecorKind::JungleTree : DecorKind::PalmTree; break;
                case Biome::Volcanic: density = 0.08f; kind = DecorKind::DeadTree; break;
                case Biome::Beach: density = 0.05f; kind = DecorKind::PalmTree; break;
                case Biome::City: density = 0.0f; break;
                case Biome::Farm: density = 0.02f; break;
                default: break;
            }
            if (inTown) density *= 0.25f;
            if (roll < density && (b == Biome::Forest || b == Biome::Grass || b == Biome::Snow) && rng_.chance(0.12f)) {
                // Model-based tree variety (trunk collision stays a harvestable shape)
                float sc = rng_.range(3.2f, 4.4f);
                uint32_t trunk = box({px - 0.35f, h - 0.3f, pz - 0.35f}, {px + 0.35f, h + 2.2f, pz + 0.35f}, Material::Wood, rgb(100, 70, 45), 200, true, 11);
                Decor d;
                d.kind = DecorKind::Model;
                d.model = b == Biome::Grass && rng_.chance(0.4f) ? PropModel::TreeCluster : PropModel::PineModel;
                d.pos = {px, h, pz};
                d.yaw = rng_.range(0, 2 * kPi);
                d.scale = d.model == PropModel::TreeCluster ? sc * 1.8f : sc;
                d.color = rgb(60, 130, 70);
                d.linkedShape = trunk;
                decor.push_back(d);
                int ix = (int)(px / 2), iz = (int)(pz / 2);
                occ[(size_t)iz * ON + ix] = 1;
            } else if (roll < density) {
                addTree(kind, px, pz, rng_.range(0.8f, 1.4f));
                // mark occupied
                int ix = (int)(px / 2), iz = (int)(pz / 2);
                occ[(size_t)iz * ON + ix] = 1;
            } else if (roll < density + 0.03f && !inTown) {
                Color4 rc = b == Biome::Snow ? rgb(200, 205, 215) : b == Biome::Desert ? rgb(190, 140, 100) : b == Biome::Volcanic ? rgb(60, 55, 55) : rgb(130, 130, 125);
                rock(px, pz, rng_.range(1.2f, 3.5f), jitter(rng_, rc, 0.08f));
            } else if (roll < density + 0.45f && (b == Biome::Grass || b == Biome::Forest || b == Biome::Jungle || b == Biome::Farm) && !inTown) {
                Decor d;
                d.kind = DecorKind::GrassTuft;
                d.pos = {px, h, pz};
                d.scale = rng_.range(0.7f, 1.4f);
                d.yaw = rng_.range(0, 2 * kPi);
                d.color = b == Biome::Jungle ? rgb(60, 140, 55) : rgb(95, 155, 65);
                decor.push_back(d);
            } else if (roll < density + 0.57f && b != Biome::Desert && b != Biome::Volcanic && b != Biome::Beach) {
                Decor d;
                d.kind = rng_.chance(0.7f) ? DecorKind::Bush : DecorKind::Flower;
                d.pos = {px, h, pz};
                d.scale = rng_.range(0.7f, 1.3f);
                d.color = b == Biome::Snow ? rgb(220, 230, 235) : d.kind == DecorKind::Flower ? rgb(240, 120, 160) : rgb(70, 140, 60);
                decor.push_back(d);
            }
        }
    }
}

void GameMap::genSlipstreams() {
    auto mk = [&](std::initializer_list<Vec2> pts, float height) {
        Slipstream s;
        std::vector<Vec2> ctrl(pts);
        for (size_t i = 0; i + 1 < ctrl.size(); i++) {
            Vec2 a = ctrl[i] * W, b = ctrl[i + 1] * W;
            int steps = (int)(dist2d(a, b) / 20) + 1;
            for (int k = 0; k < steps; k++) {
                Vec2 p = a + (b - a) * ((float)k / steps);
                float y = 0;
                for (float o = -20; o <= 20; o += 10) y = std::max(y, heightAt(p.x + o, p.y + o));
                s.points.push_back({p.x, y + height, p.y});
            }
        }
        Vec2 last = ctrl.back() * W;
        s.points.push_back({last.x, heightAt(last.x, last.y) + height, last.y});
        // Smooth heights
        for (int it = 0; it < 3; it++)
            for (size_t i = 1; i + 1 < s.points.size(); i++)
                s.points[i].y = std::max(s.points[i].y, (s.points[i - 1].y + s.points[i + 1].y) * 0.5f);
        slipstreams.push_back(s);
    };
    // Loop from the city up to the jungle temple, over to the mall and back.
    mk({{0.53f, 0.44f}, {0.60f, 0.33f}, {0.64f, 0.26f}, {0.74f, 0.33f}, {0.80f, 0.43f}, {0.70f, 0.56f}, {0.56f, 0.52f}}, 14);
    // South route: farm -> crossroads -> hamlet -> desert
    mk({{0.37f, 0.62f}, {0.50f, 0.66f}, {0.62f, 0.72f}, {0.77f, 0.76f}}, 12);
    // North-west: industrial -> suburb -> city
    mk({{0.27f, 0.24f}, {0.33f, 0.31f}, {0.42f, 0.38f}, {0.48f, 0.44f}}, 12);
}

// ------------------------------------------------------------------ extra detail

void GameMap::stuntRamp(float x, float z, int dir, float len, float width, float h, float baseY, Color4 c, bool backPlate) {
    AABB b;
    uint8_t rd = RAMP_PX;
    switch (dir) {
        case 0: b = AABB({x, baseY, z - width / 2}, {x + len, baseY + h, z + width / 2}); rd = RAMP_PX; break;
        case 1: b = AABB({x - len, baseY, z - width / 2}, {x, baseY + h, z + width / 2}); rd = RAMP_NX; break;
        case 2: b = AABB({x - width / 2, baseY, z}, {x + width / 2, baseY + h, z + len}); rd = RAMP_PZ; break;
        default: b = AABB({x - width / 2, baseY, z - len}, {x + width / 2, baseY + h, z}); rd = RAMP_NZ; break;
    }
    ramp(b.min, b.max, rd, Material::Wood, c, 600);
    if (!backPlate) return;
    const float t = 0.3f;
    Color4 back = shade(c, 0.7f);
    float bottom = baseY - 1.0f;
    switch (dir) {
        case 0: box({b.max.x - t, bottom, b.min.z}, {b.max.x, b.max.y, b.max.z}, Material::Wood, back, 600, true, 0); break;
        case 1: box({b.min.x, bottom, b.min.z}, {b.min.x + t, b.max.y, b.max.z}, Material::Wood, back, 600, true, 0); break;
        case 2: box({b.min.x, bottom, b.max.z - t}, {b.max.x, b.max.y, b.max.z}, Material::Wood, back, 600, true, 0); break;
        default: box({b.min.x, bottom, b.min.z}, {b.max.x, b.max.y, b.min.z + t}, Material::Wood, back, 600, true, 0); break;
    }
}

// A concrete pad full of jumps: kickers, a table-top, a gap jump, quarter pipes, floodlights,
// banners and a loot chest on the table.
void GameMap::stuntPark(float cx, float cz, const std::string& name) {
    const float half = 26;
    float top = footprintMax(cx - half, cz - half, cx + half, cz + half) + 0.15f;
    float bottom = footprintMin(cx - half, cz - half, cx + half, cz + half) - 1.0f;
    box({cx - half, bottom, cz - half}, {cx + half, top, cz + half}, Material::Brick, rgb(150, 152, 158), 0, false);
    // Access ramps where the pad stands proud of the terrain
    {
        struct Side { float x, z; int dir; } sides[] = {{cx - half, cz + 14, 0}, {cx + half, cz - 14, 1}, {cx + 10, cz + half, 3}, {cx - 2, cz - half, 2}};
        for (auto& sd : sides) {
            float lx = sd.dir == 0 ? sd.x - 8 : sd.dir == 1 ? sd.x + 8 : sd.x;
            float lz = sd.dir == 2 ? sd.z - 8 : sd.dir == 3 ? sd.z + 8 : sd.z;
            float g = heightAt(lx, lz);
            if (top - g < 0.3f) continue;
            stuntRamp(lx, lz, sd.dir, 8, 9, top - g + 0.02f, g - 0.05f, rgb(140, 142, 148), false);
        }
    }
    // Painted lanes
    for (int i = -2; i <= 2; i++)
        addBoxDecor(DecorKind::Trim, AABB({cx - half + 1, top, cz + i * 10.0f - 0.12f}, {cx + half - 1, top + 0.02f, cz + i * 10.0f + 0.12f}),
                    rgb(235, 200, 60), INVALID_ID);
    Color4 ply = rgb(196, 156, 104), steel = rgb(120, 140, 170), red = rgb(210, 70, 60);
    // Gap jump along X (south side)
    stuntRamp(cx - 22, cz - 14, 0, 7, 6, 2.6f, top, ply, true);
    stuntRamp(cx + 12, cz - 14, 1, 9, 6, 2.6f, top, ply, false); // landing ramp: high edge faces the kicker
    box({cx + 3, top - 0.2f, cz - 17}, {cx + 3.3f, top + 2.6f, cz - 11}, Material::Wood, shade(ply, 0.7f), 600, true, 0);
    // Table-top along Z (east side)
    stuntRamp(cx + 16, cz - 6, 2, 8, 7, 3.2f, top, steel, false);
    box({cx + 12.5f, top, cz + 2}, {cx + 19.5f, top + 3.2f, cz + 9}, Material::Metal, shade(steel, 0.85f), 800, true, 0);
    stuntRamp(cx + 16, cz + 17, 3, 8, 7, 3.2f, top, steel, false);
    addChest({cx + 16, top + 3.2f, cz + 5.5f}, 0);
    // Kickers in the middle
    stuntRamp(cx - 6, cz + 2, 0, 6, 5, 2.0f, top, red, true);
    stuntRamp(cx + 2, cz + 12, 1, 6, 5, 2.4f, top, ply, true);
    // Quarter pipes on the north and west edges
    stuntRamp(cx - 8, cz + 20, 2, 5, 12, 5.0f, top, steel, true);
    stuntRamp(cx - 20, cz - 2, 1, 5, 12, 5.0f, top, steel, true);
    // Floodlights, banners, loot, barrels
    for (int k = 0; k < 4; k++) {
        float sx = k % 2 ? 1.0f : -1.0f, sz = k < 2 ? 1.0f : -1.0f;
        addProp(DecorKind::StreetLamp, cx + sx * (half - 1.5f), cz + sz * (half - 1.5f), kPi / 4 + k * kPi / 2, rgb(70, 72, 80));
        decor.back().pos.y = top;
    }
    for (int k = 0; k < 3; k++) {
        Decor d;
        d.kind = DecorKind::Model;
        d.model = k == 1 ? PropModel::Flag : PropModel::Banner;
        d.pos = {cx - 12.0f + k * 12.0f, top, cz - half + 1.0f};
        d.yaw = 0;
        d.scale = 2.5f;
        d.color = rgb(200, 200, 200);
        decor.push_back(d);
    }
    addLoot({cx - 14, top + 0.05f, cz + 8});
    addLoot({cx + 4, top + 0.05f, cz - 4});
    addLoot({cx - 2, top + 0.05f, cz - 22});
    POI p;
    p.name = name;
    p.type = PoiType::StuntPark;
    p.center = {cx, cz};
    p.radius = half + 6;
    p.major = false;
    p.baseHeight = top;
    pois.push_back(p);
}

// Fills the space between locations: stunt parks and jumps, campsites, ruins, radio towers,
// hay fields, container yards, parked wrecks, billboards, fallen logs, rock arches and
// denser ground cover. Own random stream so the rest of the map is unchanged.
void GameMap::genDetails() {
    Rng dr(seed ^ 0xDE7A11, 17);
    // Occupancy (2 m cells) from everything placed so far
    const int ON = (int)(W / 2);
    std::vector<uint8_t> occ((size_t)ON * ON, 0);
    auto mark = [&](float x0, float z0, float x1, float z1) {
        int ix0 = std::max(0, (int)(x0 / 2) - 1), ix1 = std::min(ON - 1, (int)(x1 / 2) + 1);
        int iz0 = std::max(0, (int)(z0 / 2) - 1), iz1 = std::min(ON - 1, (int)(z1 / 2) + 1);
        for (int z = iz0; z <= iz1; z++)
            for (int x = ix0; x <= ix1; x++) occ[(size_t)z * ON + x] = 1;
    };
    // Trees (style 7) and boulders (style 6) don't block placement: they get cleared instead.
    auto isVegetation = [](const Shape& sh) { return sh.style == 7 || sh.style == 6; };
    for (auto& s : shapes)
        if (s.alive && !isVegetation(s)) mark(s.box.min.x, s.box.min.z, s.box.max.x, s.box.max.z);
    size_t shapesBefore = shapes.size();
    auto clearVeg = [&](float x0, float z0, float x1, float z1) {
        AABB area({x0 - 1, -1000, z0 - 1}, {x1 + 1, 1000, z1 + 1});
        for (size_t i = 0; i < shapesBefore; i++)
            if (shapes[i].alive && isVegetation(shapes[i]) && shapes[i].box.overlaps(area)) { shapes[i].alive = false; shapes[i].solid = false; }
        decor.erase(std::remove_if(decor.begin(), decor.end(), [&](const Decor& d) {
                        bool ground = d.kind == DecorKind::GrassTuft || d.kind == DecorKind::Bush || d.kind == DecorKind::Flower || d.kind == DecorKind::Crop;
                        return ground && d.linkedShape == INVALID_ID && d.pos.x > x0 - 1 && d.pos.x < x1 + 1 && d.pos.z > z0 - 1 && d.pos.z < z1 + 1;
                    }),
                    decor.end());
    };
    auto claim = [&](float x0, float z0, float x1, float z1) { mark(x0, z0, x1, z1); clearVeg(x0, z0, x1, z1); };
    auto onRoad = [&](float x, float z) {
        int cx = std::min(HM_N - 1, std::max(0, (int)(x / CELL))), cz = std::min(HM_N - 1, std::max(0, (int)(z / CELL)));
        return roadMask[(size_t)cz * HM_N + cx] != 0;
    };
    auto areaFree = [&](float x0, float z0, float x1, float z1, bool allowRoad) {
        if (x0 < 30 || z0 < 30 || x1 > W - 30 || z1 > W - 30) return false;
        for (float z = z0; z <= z1; z += 2)
            for (float x = x0; x <= x1; x += 2) {
                int ix = (int)(x / 2), iz = (int)(z / 2);
                if (occ[(size_t)iz * ON + ix]) return false;
                if (heightAt(x, z) < WATER_LEVEL + 0.8f) return false;
                if (!allowRoad && onRoad(x, z)) return false;
            }
        return true;
    };
    auto flatness = [&](float x0, float z0, float x1, float z1) {
        float lo = 1e9f, hi = -1e9f;
        for (float z = z0; z <= z1; z += 3)
            for (float x = x0; x <= x1; x += 3) { float h = heightAt(x, z); lo = std::min(lo, h); hi = std::max(hi, h); }
        return hi - lo;
    };
    auto inPoi = [&](float x, float z, float margin) {
        for (auto& p : pois)
            if (dist2d(p.center, {x, z}) < p.radius + margin) return true;
        return false;
    };
    // Random spot in the wild with a free, reasonably flat footprint
    auto findSpot = [&](float halfX, float halfZ, float maxSlope, float poiMargin, Vec2& out, int tries = 60) {
        for (int i = 0; i < tries; i++) {
            float x = dr.range(60, W - 60), z = dr.range(60, W - 60);
            if (inPoi(x, z, poiMargin)) continue;
            if (!areaFree(x - halfX, z - halfZ, x + halfX, z + halfZ, false)) continue;
            if (flatness(x - halfX, z - halfZ, x + halfX, z + halfZ) > maxSlope) continue;
            out = {x, z};
            return true;
        }
        return false;
    };

    // --- Stunt parks
    const char* parkNames[] = {"Airtime Park", "Kickflip Yard", "Big Air Field"};
    std::vector<Vec2> parks;
    for (int n = 0; n < 3; n++) {
        for (int attempt = 0; attempt < 400; attempt++) {
            Vec2 c;
            if (!findSpot(28, 28, 2.0f, 45, c, 1)) continue;
            bool far = true;
            for (auto& o : parks) if (dist2d(o, c) < 350) far = false;
            if (!far) continue;
            stuntPark(c.x, c.y, parkNames[n]);
            parks.push_back(c);
            claim(c.x - 28, c.y - 28, c.x + 28, c.y + 28);
            break;
        }
    }

    // --- Stand-alone jumps: kickers, table-tops and gap jumps in open fields
    Color4 jumpCols[] = {rgb(196, 156, 104), rgb(210, 90, 60), rgb(90, 140, 200), rgb(230, 190, 70)};
    for (int n = 0; n < 34; n++) {
        int kind = n % 3; // 0 kicker, 1 table-top, 2 gap jump
        int dir = dr.irange(0, 3);
        bool alongX = dir < 2;
        float lenTotal = kind == 0 ? 10 : kind == 1 ? 26 : 32;
        float halfL = lenTotal / 2 + 4, halfW = 6;
        Vec2 c;
        if (!findSpot(alongX ? halfL : halfW, alongX ? halfW : halfL, 1.6f, 25, c, 20)) continue;
        float base = footprintMin(c.x - (alongX ? halfL : halfW), c.y - (alongX ? halfW : halfL), c.x + (alongX ? halfL : halfW), c.y + (alongX ? halfW : halfL)) - 0.05f;
        Color4 col = jumpCols[n % 4];
        float sgn = (dir == 0 || dir == 2) ? 1.0f : -1.0f;
        auto at = [&](float along) { return alongX ? Vec2{c.x + sgn * along, c.y} : Vec2{c.x, c.y + sgn * along}; };
        int back = dir ^ 1; // opposite direction
        if (kind == 0) {
            Vec2 p = at(-lenTotal / 2);
            stuntRamp(p.x, p.y, dir, dr.range(6, 9), dr.range(4.5f, 6), dr.range(2.2f, 3.6f), base, col, true);
        } else if (kind == 1) {
            float h = dr.range(2.6f, 3.6f);
            Vec2 a = at(-13), b = at(13);
            stuntRamp(a.x, a.y, dir, 8, 6, h, base, col, false);
            Vec2 t0 = at(-5), t1 = at(5);
            box({std::min(t0.x, t1.x) - (alongX ? 0 : 3), base, std::min(t0.y, t1.y) - (alongX ? 3 : 0)},
                {std::max(t0.x, t1.x) + (alongX ? 0 : 3), base + h, std::max(t0.y, t1.y) + (alongX ? 3 : 0)}, Material::Wood, shade(col, 0.85f), 800, true, 0);
            stuntRamp(b.x, b.y, back, 8, 6, h, base, col, false);
            if (dr.chance(0.5f)) addLoot({c.x, base + h + 0.05f, c.y});
        } else {
            float h = dr.range(2.4f, 3.2f), gap = dr.range(10, 16);
            Vec2 a = at(-lenTotal / 2);
            stuntRamp(a.x, a.y, dir, 7, 6, h, base, col, true);
            Vec2 l = at(-lenTotal / 2 + 7 + gap);
            // landing ramp: high edge at the start, sloping down away from the kicker
            Vec2 le = at(-lenTotal / 2 + 7 + gap + 9);
            stuntRamp(le.x, le.y, back, 9, 6, h, base, col, false);
            (void)l;
        }
        claim(c.x - (alongX ? halfL : halfW), c.y - (alongX ? halfW : halfL), c.x + (alongX ? halfL : halfW), c.y + (alongX ? halfW : halfL));
    }

    // --- Campsites: tents, a fire ring with log benches, supplies and loot
    for (int n = 0; n < 14; n++) {
        Vec2 c;
        if (!findSpot(7, 7, 1.5f, 30, c, 40)) continue;
        float h = heightAt(c.x, c.y);
        float yaw = dr.range(0, 2 * kPi);
        Vec2 tent{c.x + std::cos(yaw) * 4.0f, c.y + std::sin(yaw) * 4.0f};
        modelProp(PropModel::Tents, tent.x, tent.y, yaw + kPi / 2, 2.2f, {1.8f, 1.1f, 1.8f}, 120, true);
        for (int k = 0; k < 7; k++) {
            float a = k * 2 * kPi / 7;
            float sx = c.x + std::cos(a) * 0.9f, sz = c.y + std::sin(a) * 0.9f;
            box({sx - 0.18f, h - 0.1f, sz - 0.18f}, {sx + 0.18f, h + 0.25f, sz + 0.18f}, Material::Brick, rgb(120, 118, 115), 80, true, 6);
        }
        addProp(DecorKind::Lamp, c.x, c.y, 0, rgb(255, 170, 60));
        for (int s = -1; s <= 1; s += 2) {
            float lx = c.x + std::cos(yaw + kPi / 2) * 2.4f * s, lz = c.y + std::sin(yaw + kPi / 2) * 2.4f * s;
            box({lx - 1.1f, h - 0.1f, lz - 0.3f}, {lx + 1.1f, h + 0.45f, lz + 0.3f}, Material::Wood, rgb(110, 76, 48), 120, true, 7);
        }
        crate(c.x - std::cos(yaw) * 3.5f, c.y - std::sin(yaw) * 3.5f, 0.9f);
        addLoot({c.x - std::cos(yaw) * 2.5f + 1.2f, h + 0.05f, c.y - std::sin(yaw) * 2.5f});
        if (dr.chance(0.6f)) addChest({tent.x + std::cos(yaw) * 2.6f, h, tent.y + std::sin(yaw) * 2.6f}, yaw);
        claim(c.x - 7, c.y - 7, c.x + 7, c.y + 7);
    }

    // --- Ruins: broken stone walls around a courtyard with a chest
    for (int n = 0; n < 12; n++) {
        Vec2 c;
        if (!findSpot(8, 7, 2.5f, 30, c, 40)) continue;
        float base = footprintMin(c.x - 7, c.y - 6, c.x + 7, c.y + 6) - 0.3f;
        Color4 stone = jitter(dr, biomeAt(c.x, c.y) == Biome::Desert ? rgb(200, 170, 130) : rgb(150, 146, 138), 0.06f);
        const float hx = 7, hz = 6;
        for (int side = 0; side < 4; side++) {
            bool alongX = side < 2;
            float fixed = side == 0 ? c.y - hz : side == 1 ? c.y + hz : side == 2 ? c.x - hx : c.x + hx;
            float from = alongX ? c.x - hx : c.y - hz, to = alongX ? c.x + hx : c.y + hz;
            int segs = 4;
            for (int k = 0; k < segs; k++) {
                if (dr.chance(0.3f)) continue; // collapsed section
                float s0 = from + (to - from) * k / segs, s1 = from + (to - from) * (k + 1) / segs;
                float hh = dr.range(0.8f, 3.6f);
                if (alongX) box({s0, base, fixed - 0.4f}, {s1, base + hh, fixed + 0.4f}, Material::Brick, shade(stone, dr.range(0.9f, 1.05f)), 300, true, 0);
                else box({fixed - 0.4f, base, s0}, {fixed + 0.4f, base + hh, s1}, Material::Brick, shade(stone, dr.range(0.9f, 1.05f)), 300, true, 0);
            }
        }
        // Pillars and rubble
        for (int k = 0; k < 4; k++) {
            float px = c.x + (k % 2 ? 3.5f : -3.5f), pz = c.y + (k < 2 ? 2.5f : -2.5f);
            if (dr.chance(0.35f)) continue;
            box({px - 0.45f, base, pz - 0.45f}, {px + 0.45f, base + dr.range(1.5f, 4.5f), pz + 0.45f}, Material::Brick, shade(stone, 1.05f), 250, true, 0);
        }
        for (int k = 0; k < 6; k++) {
            float rx = c.x + dr.range(-9, 9), rz = c.y + dr.range(-8, 8);
            float sz = dr.range(0.3f, 0.7f);
            float ry = heightAt(rx, rz);
            box({rx - sz, ry - 0.2f, rz - sz}, {rx + sz, ry + sz, rz + sz * 0.8f}, Material::Brick, shade(stone, 0.9f), 80, true, 6);
        }
        addChest({c.x, heightAt(c.x, c.y), c.y}, dr.range(0, 2 * kPi));
        addLoot({c.x + 2, heightAt(c.x + 2, c.y + 1) + 0.05f, c.y + 1});
        claim(c.x - 9, c.y - 8, c.x + 9, c.y + 8);
    }

    // --- Radio towers on hilltops, each with a small shed
    {
        struct Cand { float h; Vec2 p; };
        std::vector<Cand> cands;
        for (float z = 80; z < W - 80; z += 36)
            for (float x = 80; x < W - 80; x += 36) {
                float h = heightAt(x, z);
                if (h > 12 && !inPoi(x, z, 30)) cands.push_back({h, {x, z}});
            }
        std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.h > b.h; });
        std::vector<Vec2> towers;
        for (auto& cd : cands) {
            if (towers.size() >= 6) break;
            bool far = true;
            for (auto& t : towers) if (dist2d(t, cd.p) < 260) far = false;
            if (!far || !areaFree(cd.p.x - 6, cd.p.y - 6, cd.p.x + 6, cd.p.y + 6, false)) continue;
            towers.push_back(cd.p);
            float base = footprintMin(cd.p.x - 2, cd.p.y - 2, cd.p.x + 2, cd.p.y + 2) - 0.3f;
            const float th = 26, hw = 1.4f;
            Color4 steel = rgb(170, 60, 50), white = rgb(225, 225, 230);
            for (int k = 0; k < 4; k++) {
                float lx = cd.p.x + (k % 2 ? hw : -hw), lz = cd.p.y + (k < 2 ? hw : -hw);
                box({lx - 0.15f, base, lz - 0.15f}, {lx + 0.15f, base + th, lz + 0.15f}, Material::Metal, steel, 500, true, 0);
            }
            for (float y = 3; y < th; y += 4) {
                Color4 bc = ((int)y / 4) % 2 ? white : steel;
                box({cd.p.x - hw, base + y, cd.p.y - hw - 0.08f}, {cd.p.x + hw, base + y + 0.18f, cd.p.y - hw + 0.08f}, Material::Metal, bc, 300, true, 0);
                box({cd.p.x - hw, base + y, cd.p.y + hw - 0.08f}, {cd.p.x + hw, base + y + 0.18f, cd.p.y + hw + 0.08f}, Material::Metal, bc, 300, true, 0);
                box({cd.p.x - hw - 0.08f, base + y, cd.p.y - hw}, {cd.p.x - hw + 0.08f, base + y + 0.18f, cd.p.y + hw}, Material::Metal, bc, 300, true, 0);
                box({cd.p.x + hw - 0.08f, base + y, cd.p.y - hw}, {cd.p.x + hw + 0.08f, base + y + 0.18f, cd.p.y + hw}, Material::Metal, bc, 300, true, 0);
            }
            box({cd.p.x - 2.2f, base + th, cd.p.y - 2.2f}, {cd.p.x + 2.2f, base + th + 0.3f, cd.p.y + 2.2f}, Material::Metal, rgb(90, 90, 96), 400, true, 0);
            box({cd.p.x - 0.1f, base + th + 0.3f, cd.p.y - 0.1f}, {cd.p.x + 0.1f, base + th + 5, cd.p.y + 0.1f}, Material::Metal, white, 200, true, 0);
            addProp(DecorKind::Lamp, cd.p.x, cd.p.y, 0, rgb(255, 60, 50));
            decor.back().pos.y = base + th + 5;
            addChest({cd.p.x, base + th + 0.3f, cd.p.y + 1.2f}, 0);
            // Shed with a doorway
            float sx = cd.p.x + 6, sz = cd.p.y;
            float sb = footprintMin(sx - 2.5f, sz - 2, sx + 2.5f, sz + 2) - 0.2f;
            Color4 shed = rgb(150, 160, 150);
            box({sx - 2.5f, sb, sz - 2}, {sx + 2.5f, sb + 2.8f, sz - 1.8f}, Material::Metal, shed, 300, true, 3);
            box({sx - 2.5f, sb, sz + 1.8f}, {sx + 2.5f, sb + 2.8f, sz + 2}, Material::Metal, shed, 300, true, 3);
            box({sx + 2.3f, sb, sz - 2}, {sx + 2.5f, sb + 2.8f, sz + 2}, Material::Metal, shed, 300, true, 3);
            box({sx - 2.5f, sb, sz - 2}, {sx - 2.3f, sb + 2.8f, sz - 0.7f}, Material::Metal, shed, 300, true, 3);
            box({sx - 2.5f, sb, sz + 0.7f}, {sx - 2.3f, sb + 2.8f, sz + 2}, Material::Metal, shed, 300, true, 3);
            box({sx - 2.7f, sb + 2.8f, sz - 2.2f}, {sx + 2.7f, sb + 3.0f, sz + 2.2f}, Material::Metal, rgb(100, 105, 110), 300, true, 3);
            addLoot({sx, sb + 0.25f, sz});
            claim(cd.p.x - 3, cd.p.y - 3, sx + 3, sz + 3);
        }
    }

    // --- Hay fields on farmland, container yards, parked wrecks and billboards along roads
    for (int n = 0; n < 60; n++) {
        float x = dr.range(60, W - 60), z = dr.range(60, W - 60);
        if (biomeAt(x, z) != Biome::Farm || inPoi(x, z, 10)) continue;
        int bales = dr.irange(3, 8);
        for (int k = 0; k < bales; k++) {
            float bx = x + dr.range(-10, 10), bz = z + dr.range(-10, 10);
            if (!areaFree(bx - 1, bz - 1, bx + 1, bz + 1, false)) continue;
            float h = heightAt(bx, bz);
            bool alongX = dr.chance(0.5f);
            float hx = alongX ? 1.1f : 0.65f, hz = alongX ? 0.65f : 1.1f;
            box({bx - hx, h - 0.1f, bz - hz}, {bx + hx, h + 1.1f, bz + hz}, Material::Wood, jitter(dr, rgb(222, 190, 100), 0.06f), 90, true, 0);
            if (dr.chance(0.25f)) box({bx - hx, h + 1.1f, bz - hz}, {bx + hx, h + 2.2f, bz + hz}, Material::Wood, jitter(dr, rgb(222, 190, 100), 0.06f), 90, true, 0);
            claim(bx - hx, bz - hz, bx + hx, bz + hz);
        }
    }
    Color4 boardCols[] = {rgb(230, 80, 60), rgb(60, 150, 230), rgb(250, 200, 60), rgb(120, 200, 110), rgb(200, 100, 220)};
    for (size_t ri = 0; ri < roads.size(); ri++) {
        const Road& rd = roads[ri];
        Vec2 d = rd.b - rd.a;
        float len = d.len();
        if (len < 80) continue;
        Vec2 dir = d * (1.0f / len), side{-dir.y, dir.x};
        bool alongX = std::fabs(dir.x) > std::fabs(dir.y);
        for (float t = 40; t < len - 40; t += dr.range(90, 150)) {
            float off = rd.width * 0.5f + dr.range(4, 7);
            float sgn = dr.chance(0.5f) ? 1.0f : -1.0f;
            Vec2 p = rd.a + dir * t + side * (off * sgn);
            if (inPoi(p.x, p.y, 0) || !areaFree(p.x - 4, p.y - 4, p.x + 4, p.y + 4, false)) continue;
            float roll = dr.uniform();
            if (roll < 0.35f) {
                // Billboard facing the road
                float base = heightAt(p.x, p.y) - 0.3f;
                Color4 bc = boardCols[dr.irange(0, 4)];
                if (alongX) {
                    for (int s = -1; s <= 1; s += 2) box({p.x + s * 3.0f - 0.15f, base, p.y - 0.15f}, {p.x + s * 3.0f + 0.15f, base + 7.5f, p.y + 0.15f}, Material::Metal, rgb(80, 80, 88), 300, true, 0);
                    box({p.x - 4.2f, base + 4.2f, p.y - 0.2f}, {p.x + 4.2f, base + 7.8f, p.y + 0.2f}, Material::Metal, bc, 300, true, 0);
                    addBoxDecor(DecorKind::Trim, AABB({p.x - 3.6f, base + 5.4f, p.y - 0.25f}, {p.x + 1.0f, base + 6.6f, p.y + 0.25f}), rgb(250, 250, 250), INVALID_ID);
                } else {
                    for (int s = -1; s <= 1; s += 2) box({p.x - 0.15f, base, p.y + s * 3.0f - 0.15f}, {p.x + 0.15f, base + 7.5f, p.y + s * 3.0f + 0.15f}, Material::Metal, rgb(80, 80, 88), 300, true, 0);
                    box({p.x - 0.2f, base + 4.2f, p.y - 4.2f}, {p.x + 0.2f, base + 7.8f, p.y + 4.2f}, Material::Metal, bc, 300, true, 0);
                    addBoxDecor(DecorKind::Trim, AABB({p.x - 0.25f, base + 5.4f, p.y - 3.6f}, {p.x + 0.25f, base + 6.6f, p.y + 1.0f}), rgb(250, 250, 250), INVALID_ID);
                }
            } else if (roll < 0.65f) {
                car(p.x, p.y, alongX, jitter(dr, boardCols[dr.irange(0, 4)], 0.1f));
                if (dr.chance(0.4f)) addLoot({p.x + side.x * 2.5f, heightAt(p.x, p.y) + 0.05f, p.y + side.y * 2.5f});
            } else if (roll < 0.8f) {
                // Container yard
                int count = dr.irange(2, 5);
                for (int k = 0; k < count; k++) {
                    float cx = p.x + (alongX ? k * 3.0f : 0), cz = p.y + (alongX ? 0 : k * 3.0f);
                    if (!areaFree(cx - 3.5f, cz - 3.5f, cx + 3.5f, cz + 3.5f, false)) break;
                    Color4 cc = jitter(dr, boardCols[dr.irange(0, 4)], 0.1f);
                    container(cx, cz, !alongX, cc);
                    if (dr.chance(0.3f)) {
                        float base = heightAt(cx, cz) + 2.3f;
                        float lx = !alongX ? 6.0f : 2.4f, lz = !alongX ? 2.4f : 6.0f;
                        box({cx - lx / 2, base, cz - lz / 2}, {cx + lx / 2, base + 2.6f, cz + lz / 2}, Material::Metal, shade(cc, 0.9f), 400, true, 3);
                    }
                    claim(cx - 3.5f, cz - 3.5f, cx + 3.5f, cz + 3.5f);
                }
                addChest({p.x, heightAt(p.x, p.y), p.y + (alongX ? 2.2f : 0)}, 0);
            } else {
                // Roadside rest stop: picnic table, bench, trash barrels
                float h = heightAt(p.x, p.y);
                box({p.x - 1.1f, h + 0.72f, p.y - 0.5f}, {p.x + 1.1f, h + 0.82f, p.y + 0.5f}, Material::Wood, rgb(150, 105, 65), 60, true, 0);
                box({p.x - 0.1f, h - 0.1f, p.y - 0.1f}, {p.x + 0.1f, h + 0.72f, p.y + 0.1f}, Material::Wood, rgb(110, 76, 48), 60, true, 0);
                for (int s = -1; s <= 1; s += 2) box({p.x - 1.0f, h + 0.4f, p.y + s * 0.95f - 0.18f}, {p.x + 1.0f, h + 0.48f, p.y + s * 0.95f + 0.18f}, Material::Wood, rgb(150, 105, 65), 40, true, 0);
                barrel(p.x + 2.4f, p.y + 1.2f, rgb(60, 120, 70));
                addProp(DecorKind::Bench, p.x - 2.6f, p.y + 1.6f, dr.range(0, kPi), rgb(130, 90, 55));
                addLoot({p.x + 1.5f, h + 0.05f, p.y - 1.5f});
            }
            claim(p.x - 5, p.y - 5, p.x + 5, p.y + 5);
        }
    }

    // --- Fallen logs in forests, rock arches and pillars in the desert / volcanic badlands
    for (int n = 0; n < 700; n++) {
        float x = dr.range(40, W - 40), z = dr.range(40, W - 40);
        Biome b = biomeAt(x, z);
        if (inPoi(x, z, 5)) continue;
        if ((b == Biome::Forest || b == Biome::Jungle || b == Biome::Snow) && dr.chance(0.35f)) {
            bool alongX = dr.chance(0.5f);
            float len = dr.range(4, 8), rr = dr.range(0.3f, 0.5f);
            float hx = alongX ? len / 2 : rr, hz = alongX ? rr : len / 2;
            if (!areaFree(x - hx, z - hz, x + hx, z + hz, false) || flatness(x - hx, z - hz, x + hx, z + hz) > 0.8f) continue;
            float h = footprintMin(x - hx, z - hz, x + hx, z + hz);
            box({x - hx, h - 0.15f, z - hz}, {x + hx, h + rr * 1.8f, z + hz}, Material::Wood, jitter(dr, rgb(105, 75, 50), 0.08f), 150, true, 7);
            if (dr.chance(0.5f)) {
                Decor d;
                d.kind = DecorKind::Bush;
                d.pos = {x + (alongX ? len * 0.3f : 0.8f), h, z + (alongX ? 0.8f : len * 0.3f)};
                d.scale = 0.7f;
                d.color = rgb(70, 130, 55);
                decor.push_back(d);
            }
            claim(x - hx, z - hz, x + hx, z + hz);
        } else if ((b == Biome::Desert || b == Biome::Volcanic) && dr.chance(0.12f)) {
            if (!areaFree(x - 6, z - 3, x + 6, z + 3, false)) continue;
            Color4 rc = b == Biome::Desert ? rgb(196, 132, 88) : rgb(70, 62, 60);
            float h = footprintMin(x - 6, z - 3, x + 6, z + 3) - 0.5f;
            if (dr.chance(0.5f)) {
                // Natural arch
                float ah = dr.range(5, 9);
                box({x - 5.5f, h, z - 1.5f}, {x - 3.0f, h + ah, z + 1.5f}, Material::Brick, jitter(dr, rc, 0.06f), 600, true, 6);
                box({x + 3.0f, h, z - 1.5f}, {x + 5.5f, h + ah, z + 1.5f}, Material::Brick, jitter(dr, rc, 0.06f), 600, true, 6);
                box({x - 6.0f, h + ah, z - 1.7f}, {x + 6.0f, h + ah + 2.2f, z + 1.7f}, Material::Brick, jitter(dr, shade(rc, 1.08f), 0.06f), 600, true, 6);
            } else {
                // Stacked hoodoo pillar
                float y = h, sz = dr.range(2.0f, 3.0f);
                for (int k = 0; k < 4; k++) {
                    float hh = dr.range(1.5f, 3.0f);
                    box({x - sz, y, z - sz * 0.8f}, {x + sz, y + hh, z + sz * 0.8f}, Material::Brick, jitter(dr, rc, 0.08f), 400, true, 6);
                    y += hh;
                    sz *= dr.range(0.7f, 0.95f);
                }
            }
            claim(x - 6, z - 3, x + 6, z + 3);
        }
    }

    // --- Denser ground cover: grass, flowers, bushes and pebbles in the open
    for (float z = 8; z < W - 8; z += 5.0f) {
        for (float x = 8; x < W - 8; x += 5.0f) {
            float px = x + dr.range(-2.2f, 2.2f), pz = z + dr.range(-2.2f, 2.2f);
            float h = heightAt(px, pz);
            if (h < WATER_LEVEL + 0.6f || onRoad(px, pz)) continue;
            int ix = (int)(px / 2), iz = (int)(pz / 2);
            if (occ[(size_t)iz * ON + ix]) continue;
            Biome b = biomeAt(px, pz);
            float r = dr.uniform();
            Decor d;
            d.pos = {px, h, pz};
            d.yaw = dr.range(0, 2 * kPi);
            d.scale = dr.range(0.6f, 1.3f);
            switch (b) {
                case Biome::Grass: case Biome::Forest: case Biome::Farm:
                    if (r < 0.42f) { d.kind = DecorKind::GrassTuft; d.color = jitter(dr, rgb(100, 160, 68), 0.1f); }
                    else if (r < 0.50f) { d.kind = DecorKind::Flower; d.color = r < 0.46f ? rgb(245, 210, 70) : r < 0.48f ? rgb(240, 120, 160) : rgb(170, 140, 240); }
                    else if (r < 0.56f) { d.kind = DecorKind::Bush; d.color = jitter(dr, rgb(72, 138, 60), 0.1f); }
                    else continue;
                    break;
                case Biome::Jungle:
                    if (r < 0.5f) { d.kind = DecorKind::GrassTuft; d.color = rgb(60, 145, 58); d.scale *= 1.3f; }
                    else if (r < 0.62f) { d.kind = DecorKind::Bush; d.color = jitter(dr, rgb(50, 125, 50), 0.1f); d.scale *= 1.2f; }
                    else continue;
                    break;
                case Biome::Desert:
                    if (r < 0.10f) { d.kind = DecorKind::GrassTuft; d.color = rgb(190, 170, 100); }
                    else if (r < 0.14f) { d.kind = DecorKind::Bush; d.color = rgb(150, 140, 80); d.scale *= 0.7f; }
                    else continue;
                    break;
                case Biome::Beach:
                    if (r < 0.08f) { d.kind = DecorKind::GrassTuft; d.color = rgb(170, 175, 100); }
                    else continue;
                    break;
                case Biome::Snow:
                    if (r < 0.05f) { d.kind = DecorKind::Bush; d.color = rgb(225, 235, 240); d.scale *= 0.8f; }
                    else continue;
                    break;
                default: continue;
            }
            decor.push_back(d);
        }
    }
}

// Parked vehicles: carts around residential areas, trolleys at shops, boards in the city and
// industrial zones, quads in the rough terrain, roller balls scattered around, plus a few
// along the roads. Uses its own random stream so it doesn't shift the rest of the map.
void GameMap::genVehicles() {
    Rng vr(seed ^ 0xCA75, 31);
    auto clearSpot = [&](float x, float z, float r) {
        if (x < 20 || z < 20 || x > W - 20 || z > W - 20) return false;
        if (!isLand(x, z)) return false;
        float h = heightAt(x, z);
        if (h < WATER_LEVEL + 0.6f) return false;
        for (int i = 0; i < 4; i++) {
            float a = i * kPi / 2;
            if (std::fabs(heightAt(x + std::cos(a) * r * 1.5f, z + std::sin(a) * r * 1.5f) - h) > 0.7f) return false;
        }
        AABB foot({x - r - 0.3f, h - 0.5f, z - r - 0.3f}, {x + r + 0.3f, h + 3.0f, z + r + 0.3f});
        for (auto& sh : shapes)
            if (sh.solid && sh.box.overlaps(foot)) return false;
        for (auto& v : vehicleSpawns)
            if (distXZ(v.pos, {x, 0, z}) < 5.0f) return false;
        return true;
    };
    auto place = [&](VehicleType t, float cx, float cz, float rMin, float rMax) {
        float r = vehicleDef(t).radius;
        for (int attempt = 0; attempt < 16; attempt++) {
            float a = vr.range(0, 2 * kPi), d = vr.range(rMin, rMax);
            float x = cx + std::cos(a) * d, z = cz + std::sin(a) * d;
            if (!clearSpot(x, z, r)) continue;
            vehicleSpawns.push_back({{x, heightAt(x, z), z}, vr.range(-kPi, kPi), t});
            return true;
        }
        return false;
    };
    for (auto& p : pois) {
        float r0 = p.radius * 0.35f, r1 = p.radius * 1.15f;
        switch (p.type) {
            case PoiType::Estates: case PoiType::Mansion: case PoiType::LakeHouse: case PoiType::Lodge: case PoiType::Hamlet:
                place(VehicleType::GolfCart, p.center.x, p.center.y, r0, r1);
                place(VehicleType::GolfCart, p.center.x, p.center.y, r0, r1);
                break;
            case PoiType::Suburb: case PoiType::SnowLodge:
                place(VehicleType::GolfCart, p.center.x, p.center.y, r0, r1);
                place(VehicleType::Trolley, p.center.x, p.center.y, r0, r1);
                place(VehicleType::CrashQuad, p.center.x, p.center.y, r0, r1);
                break;
            case PoiType::Mall: case PoiType::SmallTown:
                for (int i = 0; i < 3; i++) place(VehicleType::Trolley, p.center.x, p.center.y, r0, r1);
                place(VehicleType::GolfCart, p.center.x, p.center.y, r0, r1);
                break;
            case PoiType::FutureCity:
                for (int i = 0; i < 3; i++) place(VehicleType::Hoverboard, p.center.x, p.center.y, r0, r1);
                place(VehicleType::Trolley, p.center.x, p.center.y, r0, r1);
                place(VehicleType::RollerBall, p.center.x, p.center.y, r0, r1);
                break;
            case PoiType::Industrial: case PoiType::Airfield: case PoiType::LanternVillage: case PoiType::PirateCove:
                place(VehicleType::Hoverboard, p.center.x, p.center.y, r0, r1);
                place(VehicleType::Hoverboard, p.center.x, p.center.y, r0, r1);
                place(VehicleType::CrashQuad, p.center.x, p.center.y, r0, r1);
                break;
            case PoiType::DesertTown: case PoiType::Mines: case PoiType::Farm: case PoiType::Volcano: case PoiType::Junkyard:
                place(VehicleType::CrashQuad, p.center.x, p.center.y, r0, r1);
                place(VehicleType::CrashQuad, p.center.x, p.center.y, r0, r1);
                place(VehicleType::Trolley, p.center.x, p.center.y, r0, r1);
                break;
            case PoiType::StuntPark: {
                // Parked on the concrete pad, clear of the jumps
                const Vec2 spots[] = {{-14, 12}, {6, 2}, {-12, -4}};
                const VehicleType types[] = {VehicleType::CrashQuad, VehicleType::Hoverboard, VehicleType::RollerBall};
                for (int k = 0; k < 3; k++)
                    vehicleSpawns.push_back({{p.center.x + spots[k].x, p.baseHeight, p.center.y + spots[k].y}, kPi / 2, types[k]});
                break;
            }
            case PoiType::Temple:
                place(VehicleType::RollerBall, p.center.x, p.center.y, r0, r1);
                place(VehicleType::CrashQuad, p.center.x, p.center.y, r0, r1);
                break;
            default:
                place(vr.uniform() < 0.5f ? VehicleType::RollerBall : VehicleType::GolfCart, p.center.x, p.center.y, r0, r1);
                break;
        }
    }
    // Parked beside the roads
    for (size_t ri = 0; ri < roads.size(); ri++) {
        const Road& rd = roads[ri];
        Vec2 d = rd.b - rd.a;
        float len = d.len();
        if (len < 60 || ri % 3 != 0) continue;
        Vec2 dir = d * (1.0f / len), side{-dir.y, dir.x};
        for (float t = len * 0.5f; t < len - 30; t += 360.0f) {
            Vec2 p = rd.a + dir * t + side * (rd.width * 0.5f + 3.0f);
            VehicleType ty = vr.uniform() < 0.55f ? VehicleType::GolfCart : VehicleType::CrashQuad;
            if (clearSpot(p.x, p.y, vehicleDef(ty).radius))
                vehicleSpawns.push_back({{p.x, heightAt(p.x, p.y), p.y}, std::atan2(dir.x, dir.y), ty});
        }
    }
    // Scattered in the wild
    const VehicleType wild[] = {VehicleType::RollerBall, VehicleType::CrashQuad, VehicleType::Hoverboard, VehicleType::RollerBall};
    for (int i = 0; i < 10; i++) {
        Vec3 p = randomLandPoint(vr, 60.0f);
        place(wild[i % 4], p.x, p.z, 0, 25);
    }
}

void GameMap::generate(uint32_t s) {
    seed = s;
    rng_.reseed(s, 77);
    shapes.clear(); chests.clear(); floorLoot.clear(); ammoBoxes.clear();
    slipstreams.clear(); vents.clear(); decor.clear(); roads.clear(); pois.clear(); vehicleSpawns.clear();
    genTerrain();
    genBiomes();
    genRoads();
    genPois();
    genBiomes(); // refresh after flattening
    genLandmarkProps();
    genNature();
    genStreetProps();
    genSlipstreams();
    genDetails();
    genVehicles();
    const POI* c = nullptr;
    for (auto& p : pois) if (p.type == PoiType::FutureCity) c = &p;
    warmupSpawnCenter = c ? Vec3{c->center.x, c->baseHeight, c->center.y} : Vec3{W / 2, 10, W / 2};
    for (auto& sh : shapes) sh.id = (uint32_t)(&sh - &shapes[0]);
}

} // namespace si
