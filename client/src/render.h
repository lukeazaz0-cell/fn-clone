// Rendering: lit low-poly shader, chunked world meshes and primitive helpers.
#pragma once
#include <map>
#include <unordered_map>
#include <vector>

#include "raylib.h"
#include "shared/game/building.h"
#include "shared/game/cosmetics.h"
#include "shared/game/map.h"
#include "shared/game/world.h"

namespace client {

using namespace si;

inline Vector3 V(const si::Vec3& v) { return {v.x, v.y, v.z}; }
inline si::Vec3 S(const Vector3& v) { return {v.x, v.y, v.z}; }
inline Color C(const Color4& c) { return {c.r, c.g, c.b, c.a}; }

struct Lighting {
    Shader shader{};
    int locViewPos = -1, locSunDir = -1, locFogColor = -1, locFogDensity = -1, locModel = -1, locNormal = -1, locTint = -1;
    Color fogColor = {168, 206, 240, 255};
    float fogDensity = 0.0016f;
    void load();
    void unload();
    void begin(const Vector3& camPos);
    void setTint(Color c);
};

// Oriented box / pyramid helpers usable inside BeginShaderMode (world-space normals).
struct Basis { si::Vec3 r{1, 0, 0}, u{0, 1, 0}, f{0, 0, 1}; };
Basis yawBasis(float yaw);
Basis yawPitchBasis(float yaw, float pitch);
Basis compose(const Basis& parent, const Basis& local);
void drawBox(const si::Vec3& center, const si::Vec3& half, const Basis& b, Color c);
void drawAABB(const AABB& box, Color c);
void drawRampShape(const AABB& box, uint8_t dir, Color c);
void drawPyramid(const AABB& box, Color c);

// Static world geometry split into chunks so destroyed props only rebuild a small mesh.
class WorldRenderer {
public:
    static constexpr float CHUNK = 64.0f;
    void build(const GameMap& map, const CollisionWorld& world, Lighting& light);
    void unload();
    void markShapeRemoved(uint32_t shapeId);
    void markAllDirty();
    void rebuildDirty(const CollisionWorld& world, int maxPerFrame = 3);
    void draw(const Camera3D& cam, float viewDist, float time);
    void drawWater(const Camera3D& cam, float time);
    Texture2D minimap{};
    int minimapSize = 512;

private:
    struct Chunk {
        std::vector<Model> models;
        std::vector<uint32_t> shapes;  // static shape ids in this chunk
        std::vector<uint32_t> decor;   // decor indices
        Vector3 center;
        bool dirty = false;
    };
    const GameMap* map_ = nullptr;
    Lighting* light_ = nullptr;
    std::vector<Model> terrain_;
    std::vector<Vector3> terrainCenters_;
    std::unordered_map<int, Chunk> chunks_;
    std::unordered_map<uint32_t, int> shapeChunk_;
    std::vector<uint8_t> decorDead_;
    Model water_{};
    bool loaded_ = false;
    int chunkKey(float x, float z) const;
    void buildTerrain();
    void buildChunk(Chunk& c, const CollisionWorld& world);
    void buildMinimap();
};

// Character renderer (procedural, cosmetics-driven).
struct CharPose {
    si::Vec3 pos;
    float yaw = 0, pitch = 0;
    MoveMode mode = MoveMode::Ground;
    uint16_t flags = 0;
    float speed = 0;          // horizontal speed for walk cycle
    float animTime = 0;
    uint8_t heldType = 0, heldRarity = 0;
    uint8_t emote = 0;        // emote animation id (shape), 0 none
    float swing = 0;          // pickaxe swing phase 0..1
    bool building = false;
};
void drawCharacter(const CharPose& p, const Loadout& l, float time);
void drawGlider(const si::Vec3& pos, float yaw, const Loadout& l);
void drawHeldItem(const si::Vec3& hand, const Basis& b, uint8_t type, uint8_t rarity, const Loadout& l, float swing);

Color materialColor(si::Material m);

} // namespace client
