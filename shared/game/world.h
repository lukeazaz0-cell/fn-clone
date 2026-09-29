// Collision world: static map shapes + player-built structures, spatial grid,
// ray casts and ground queries. Shared by client prediction and the server.
#pragma once
#include <functional>
#include <unordered_map>
#include <vector>

#include "../common/math.h"
#include "defs.h"

namespace si {

enum class ShapeKind : uint8_t { Box = 0, Ramp = 1, Cone = 2 };

// Ramp rising direction.
enum RampDir : uint8_t { RAMP_PX = 0, RAMP_NX = 1, RAMP_PZ = 2, RAMP_NZ = 3 };

constexpr uint32_t STRUCTURE_SHAPE_BASE = 1000000u;
constexpr uint32_t INVALID_ID = 0xFFFFFFFFu;

struct Shape {
    uint32_t id = 0;
    ShapeKind kind = ShapeKind::Box;
    uint8_t rampDir = 0;
    AABB box;
    Material mat = Material::Wood;
    Color4 color;
    float hp = 100, maxHp = 100;
    bool alive = true;
    bool destructible = true;
    bool solid = true;         // blocks movement
    bool blocksShots = true;
    uint32_t structure = INVALID_ID; // owning player structure (INVALID_ID for map geometry)
    uint8_t style = 0;         // renderer hint (glass, roof, etc.)
};

// Surface height of a ramp/cone at (x,z) (undefined outside xz bounds).
float shapeSurfaceY(const Shape& s, float x, float z);
bool rayShape(const Shape& s, const Vec3& o, const Vec3& d, float maxT, float& t, Vec3* n = nullptr);

class ShapeGrid {
public:
    static constexpr float CELL_SIZE = 8.0f;
    static constexpr int N = (int)(WORLD_SIZE / CELL_SIZE) + 2;
    void clear();
    void insert(uint32_t idx, const AABB& b);
    void remove(uint32_t idx, const AABB& b);
    const std::vector<uint32_t>* cell(int cx, int cz) const {
        if (cx < 0 || cz < 0 || cx >= N || cz >= N) return nullptr;
        return &cells_[(size_t)cz * N + cx];
    }
    static int toCell(float v) { return (int)std::floor(v / CELL_SIZE) + 1; }

private:
    std::vector<std::vector<uint32_t>> cells_ = std::vector<std::vector<uint32_t>>((size_t)N * N);
};

struct RayHit {
    bool hit = false;
    float t = 0;
    Vec3 point, normal;
    bool terrain = false;
    uint32_t shapeId = INVALID_ID;
};

class GameMap;

class CollisionWorld {
public:
    const GameMap* map = nullptr;

    void setMap(const GameMap* m);
    // Static shape (map) access. Dynamic shapes use ids >= STRUCTURE_SHAPE_BASE.
    Shape* shape(uint32_t id);
    const Shape* shape(uint32_t id) const;
    uint32_t addDynamicShape(const Shape& s);    // returns id
    void removeShape(uint32_t id);               // marks dead + removes from grid
    void clearDynamic();

    float terrainHeight(float x, float z) const;
    // Highest walkable surface under (x,z) whose top is <= maxY. Considers footprint radius.
    float groundHeight(const Vec3& feet, float radius, float maxY, uint32_t* shapeOut = nullptr) const;
    // Pushes a vertical cylinder (approximated by a square) out of solid boxes.
    // Returns true when a collision was resolved.
    bool resolveHorizontal(Vec3& feet, float radius, float height, float stepHeight) const;
    // Lowest ceiling above the head, or +inf.
    float ceilingHeight(const Vec3& feet, float radius, float height) const;
    bool overlapsSolid(const AABB& b, uint32_t ignoreStructure = INVALID_ID) const;

    RayHit raycast(const Vec3& origin, const Vec3& dir, float maxDist, bool includeTerrain = true) const;
    bool lineOfSight(const Vec3& a, const Vec3& b) const;

    // Visit every live shape whose AABB overlaps b.
    void query(const AABB& b, const std::function<void(const Shape&)>& fn) const;

    std::vector<Shape>& staticShapes() { return static_; }
    const std::vector<Shape>& staticShapes() const { return static_; }
    std::vector<Shape>& dynamicShapes() { return dynamic_; }
    const std::vector<Shape>& dynamicShapes() const { return dynamic_; }

private:
    std::vector<Shape> static_;
    std::vector<Shape> dynamic_;             // index = id - STRUCTURE_SHAPE_BASE
    std::vector<uint32_t> freeDynamic_;
    ShapeGrid grid_;                         // stores shape ids
    mutable std::vector<uint32_t> stamp_;    // visit stamps for queries (static)
    mutable std::vector<uint32_t> stampDyn_;
    mutable uint32_t stampCounter_ = 1;
    bool visit(uint32_t id) const;
    void newStamp() const;
};

} // namespace si
