// Player building: grid slots, placement targeting, edits and structural support.
#pragma once
#include <unordered_map>
#include <vector>

#include "../common/math.h"
#include "defs.h"
#include "world.h"

namespace si {

constexpr int BUILD_COST = 10;

struct Structure {
    uint32_t id = 0;
    PieceType piece = PieceType::Wall;
    int gx = 0, gy = 0, gz = 0;
    uint8_t rot = 0;          // wall: 0 = along X, 1 = along Z; ramp: RampDir
    Material mat = Material::Wood;
    uint8_t edit = 0;
    float hp = 0, maxHp = 0;
    float buildTime = 0;      // seconds to reach full health
    float age = 0;            // seconds since placed
    uint16_t owner = 0;       // player id
    uint8_t team = 0;
    std::vector<uint32_t> shapes; // collision shape ids in the CollisionWorld
};

struct BuildTarget {
    bool valid = false;
    PieceType piece = PieceType::Wall;
    int gx = 0, gy = 0, gz = 0;
    uint8_t rot = 0;
};

float materialMaxHp(Material m);
float materialBuildTime(Material m);
AABB pieceBounds(PieceType p, int gx, int gy, int gz, uint8_t rot);
// Collision shapes for a piece (with edits applied).
void pieceShapes(const Structure& s, std::vector<Shape>& out);

// Where a piece would be placed for a player at `feet` looking along yaw/pitch.
BuildTarget computeBuildTarget(const Vec3& feet, float yaw, float pitch, PieceType piece, uint8_t rotOffset);

class StructureSet {
public:
    std::unordered_map<uint32_t, Structure> byId;

    bool slotFree(const BuildTarget& t) const;
    uint32_t slotOwner(const BuildTarget& t) const;
    bool canPlace(const BuildTarget& t, const CollisionWorld& world) const;
    // Adds a structure (id must be set by the caller) and registers its shapes.
    Structure& add(const Structure& s, CollisionWorld& world);
    void remove(uint32_t id, CollisionWorld& world);
    void setEdit(uint32_t id, uint8_t edit, CollisionWorld& world);
    Structure* find(uint32_t id);
    // After removing `removed` (bounds), returns structures that lost support.
    std::vector<uint32_t> findUnsupported(const AABB& removedBounds, const CollisionWorld& world) const;
    bool grounded(const Structure& s, const CollisionWorld& world) const;
    void clear(CollisionWorld& world);
    // Structure owning a collision shape
    uint32_t structureOfShape(uint32_t shapeId, const CollisionWorld& world) const;

private:
    std::unordered_map<uint64_t, uint32_t> slots_;
    static uint64_t slotKey(PieceType p, int gx, int gy, int gz, uint8_t rot);
    void neighbors(const Structure& s, std::vector<uint32_t>& out) const;
};

uint8_t nextEdit(PieceType p, uint8_t edit);

} // namespace si
