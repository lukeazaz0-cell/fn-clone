// Deterministic procedural island. The layout follows the general geography of a
// classic late-2019 style battle royale island (futuristic city in the middle, a
// volcano/industrial zone in the north-west, snow in the south-west, desert in the
// south-east, jungle temple in the north-east, a mall in the east, lake, farm, etc.)
// but every location, name and building here is original.
#pragma once
#include <string>
#include <vector>

#include "../common/math.h"
#include "defs.h"
#include "vehicles.h"
#include "world.h"

namespace si {

enum class Biome : uint8_t { Grass = 0, Forest, Snow, Desert, Jungle, Volcanic, Beach, City, Farm, Count };

enum class PoiType : uint8_t {
    FutureCity, Mall, Industrial, PirateCove, SnowLodge, Airfield, DesertTown, LanternVillage, Farm, Suburb,
    SmallTown, Lodge, Junkyard, Mansion, Temple, Estates, Mines, Hamlet, LakeHouse, Volcano, Landmark
};

struct POI {
    std::string name;
    PoiType type;
    Vec2 center;   // world XZ
    float radius;
    bool major;    // shown in big text on the map
    float baseHeight = 0;
};

struct ChestSpawn { Vec3 pos; float yaw; };

struct Slipstream {
    std::vector<Vec3> points;
    float radius = 7.0f;
    float speed = 38.0f;
};

struct Vent {
    Vec3 pos;
    float radius = 3.0f;
};

enum class DecorKind : uint8_t { PineTree = 0, OakTree, PalmTree, JungleTree, Bush, Cactus, Flower, Lamp, Crop, SnowPine, DeadTree, Glass, Trim,
                                 Bench, Crate, Fence, Barrel, StreetLamp, Mailbox, Hydrant, RoadLine, GrassTuft, Model, Count };

// Prop models drawn by the client from 3D model files (CC0 Kenney assets). Each has a
// collision box in the map (style 11) and a procedural fallback when the file is missing.
enum class PropModel : uint8_t { None = 0, TruckGreen, TruckPurple, TruckRed, TruckYellow, Motorcycle, Statue, Column, Banner,
                                 WeaponRack, Fountain, Flag, PineModel, TreeCluster, Count };

struct Decor {
    DecorKind kind;
    Vec3 pos;
    float scale = 1.0f;
    uint32_t linkedShape = INVALID_ID; // disappears when this shape is destroyed
    Color4 color;
    Vec3 size;                         // half extents for Glass / Trim / RoadLine boxes
    float yaw = 0;                     // orientation for props
    PropModel model = PropModel::None; // DecorKind::Model only
};

struct Road { Vec2 a, b; float width; };

struct VehicleSpawn { Vec3 pos; float yaw; VehicleType type; };

class GameMap {
public:
    uint32_t seed = 0;
    std::vector<float> heights;   // (HM_N+1)^2 vertices
    std::vector<uint8_t> biomes;  // HM_N^2 cells
    std::vector<uint8_t> roadMask;// HM_N^2 cells, 1 = road
    std::vector<POI> pois;
    std::vector<Shape> shapes;    // static geometry
    std::vector<ChestSpawn> chests;
    std::vector<Vec3> floorLoot;
    std::vector<Vec3> ammoBoxes;
    std::vector<Slipstream> slipstreams;
    std::vector<Vent> vents;
    std::vector<Decor> decor;
    std::vector<Road> roads;
    std::vector<VehicleSpawn> vehicleSpawns;
    Vec3 warmupSpawnCenter;       // players spawn around here during warmup

    void generate(uint32_t seed);
    float heightAt(float x, float z) const;
    float vertexHeight(int ix, int iz) const { return heights[(size_t)iz * (HM_N + 1) + ix]; }
    Biome biomeAt(float x, float z) const;
    const POI* nearestPoi(float x, float z, float maxDist = 1e9f) const;
    bool isLand(float x, float z) const { return heightAt(x, z) > WATER_LEVEL + 0.3f; }
    // Returns a random dry land point (used for spawns and supply drops).
    Vec3 randomLandPoint(Rng& rng, float margin = 80.0f) const;

private:
    Rng rng_;
    // generation passes
    void genTerrain();
    void genBiomes();
    void genRoads();
    void genPois();
    void genNature();
    void genSlipstreams();
    void genVehicles();

    // building toolkit
    uint32_t box(const Vec3& mn, const Vec3& mx, Material m, Color4 c, float hp = 200, bool destructible = true, uint8_t style = 0);
    uint32_t ramp(const Vec3& mn, const Vec3& mx, uint8_t dir, Material m, Color4 c, float hp = 150);
    void flatten(Vec2 c, float halfX, float halfZ, float h, float blend);
    float footprintMax(float x0, float z0, float x1, float z1) const;
    float footprintMin(float x0, float z0, float x1, float z1) const;
    void foundation(float x0, float z0, float x1, float z1, float top, Color4 c);
    // A wall run along X or Z made from destructible panels with doors/windows.
    void wallRun(bool alongX, float fixed, float from, float to, float y0, float h, Material m, Color4 c,
                 float doorAt = -1, int windowEvery = 0, float thick = 0.3f, uint8_t style = 0);
    // Returns top-of-roof height.
    float house(float cx, float cz, float w, float d, int floors, Material m, Color4 wallC, Color4 roofC, bool loot = true, int roofStyle = 0);
    void tower(float cx, float cz, float w, float d, int floors, Color4 c, Color4 glass);
    void mall(float cx, float cz);
    void barn(float cx, float cz, float w, float d);
    void silo(float cx, float cz, float r, float h, Color4 c);
    void container(float x, float z, bool alongX, Color4 c);
    void car(float x, float z, bool alongX, Color4 c);
    void pagoda(float cx, float cz, int levels);
    void templePyramid(float cx, float cz);
    void hangar(float cx, float cz, bool alongX);
    void factory(float cx, float cz, float w, float d);
    void watchtower(float cx, float cz, float h);
    void ship(float cx, float cz);
    void rock(float x, float z, float size, Color4 c);
    void fenceRing(float x0, float z0, float x1, float z1, Color4 c);
    void gasStation(float cx, float cz);
    void mansion(float cx, float cz);
    void crossroadsSign(float x, float z);
    void addChest(const Vec3& p, float yaw = 0);
    void addLoot(const Vec3& p);
    void addTree(DecorKind k, float x, float z, float scale);
    void addBoxDecor(DecorKind k, const AABB& b, Color4 c, uint32_t link);
    void addProp(DecorKind k, float x, float z, float yaw, Color4 c, uint32_t link = INVALID_ID);
    void barrel(float x, float z, Color4 c);
    void crate(float x, float z, float size);
    void propCluster(float cx, float cz, float radius, int count);
    void genStreetProps();
    void modelProp(PropModel m, float x, float z, float yaw, float scale, Vec3 colHalf, float hp = 0, bool linkCollision = true);
    void genLandmarkProps();

    void buildPoi(POI& p);
};

} // namespace si
