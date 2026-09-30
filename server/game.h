// Authoritative match simulation: players, weapons, building, loot, storm, bus, bots.
#pragma once
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "../shared/game/building.h"
#include "../shared/game/cosmetics.h"
#include "../shared/game/items.h"
#include "../shared/game/map.h"
#include "../shared/game/movement.h"
#include "../shared/game/vehicles.h"
#include "../shared/game/world.h"
#include "../shared/net/protocol.h"

namespace si {

struct GameConfig {
    std::string playlist = "solo";
    int teamSize = 1;
    int maxPlayers = MAX_PLAYERS;
    int defaultBots = 20;              // bots added when the match starts
    BotDifficulty botDifficulty = BotDifficulty::Medium;
    int minHumansToStart = 1;
    float warmupSeconds = 30.0f;       // after min players reached
    float countdownSeconds = 10.0f;
    float stormSpeed = 1.0f;           // >1 = faster storm
    bool resetAfterMatch = true;       // false = server should exit after reporting
    uint32_t mapSeed = 1337;
    bool fillBotsToMax = false;        // alternative to defaultBots: fill all empty slots
};

struct Effect {
    EffectType type;
    uint16_t player = 0;
    Vec3 a, b;
    uint8_t extra = 0;
};

struct BotBrain {
    enum class State : uint8_t { Idle, Loot, Fight, Heal, Rotate, Flee } state = State::Idle;
    Vec3 target;               // movement target
    bool hasTarget = false;
    uint32_t lootTarget = INVALID_ID;  // item id or chest index (with lootIsChest)
    int lootKind = 0;          // 0 item, 1 chest, 2 ammo box, 3 supply drop
    uint16_t enemy = 0xFFFF;
    float thinkTimer = 0;
    float stuckTimer = 0;
    Vec3 lastPos;
    float strafeTimer = 0;
    int strafeDir = 1;
    float jumpTimer = 0;
    float buildCooldown = 0;
    float dropTime = 0;        // bus progress at which to jump
    Vec3 dropTarget;
    float aimYaw = 0, aimPitch = 0;
    float reactionTimer = 0;
    float harvestTimer = 0;
    float fireHold = 0;
    uint16_t actionId = 0;
    float wanderTimer = 0;
    float lastDamageTaken = -100;
};

struct Player {
    uint16_t id = 0;
    std::string name;
    std::string accountId;         // backend account (empty for bots/offline)
    bool bot = false;
    bool connected = true;
    uint8_t team = 0;
    Loadout loadout;

    MoveState move;
    float yaw = 0, pitch = 0;
    uint16_t buttons = 0, prevButtons = 0;
    std::deque<InputCmd> inputQueue;
    uint32_t lastInputSeq = 0;
    uint16_t lastActionId = 0;

    bool alive = true;
    bool dbno = false;
    bool eliminated = false;
    float health = MAX_HEALTH, shield = 0, dbnoHealth = 100;

    ItemStack inv[INVENTORY_SLOTS];
    int selected = 0;              // 0 = pickaxe, 1..5 = inventory slot
    int ammo[(int)AmmoType::Count] = {0};
    int mats[3] = {0, 0, 0};
    Material buildMat = Material::Wood;
    bool buildMode = false;
    PieceType buildPiece = PieceType::Wall;
    uint8_t buildRot = 0;

    float fireCooldown = 0, equipTimer = 0, buildCooldown = 0;
    int burstLeft = 0;
    float burstTimer = 0;
    float bloom = 0;
    float spin = 0;                // minigun spin up
    float lastShotTime = -10;
    ActionKind action = ACT_NONE;
    float actionTime = 0, actionTotal = 0;
    uint32_t actionTarget = INVALID_ID;
    int actionTargetKind = 0;
    float regenLeft = 0;           // seconds of regen soda remaining
    float regenAccum = 0;
    uint8_t emote = 0;             // 0 = none, else emote wheel slot+1
    float emoteTime = 0;
    float dbnoBleedAccum = 0;

    int kills = 0;
    float damageDealt = 0;
    int placement = 0;
    uint16_t lastAttacker = 0xFFFF;
    float lastAttackTime = -100;
    uint16_t spectating = 0xFFFF;
    float respawnTimer = 0;        // warmup respawn
    float stormAccum = 0;
    float time = 0;                // per-player simulated time (input driven)
    float lastInputTime = 0;       // wall clock of last received input

    BotBrain brain;

    uint16_t vehicle = 0xFFFF;     // vehicle id while riding
    uint8_t seat = NO_SEAT;
    float lastRamTime = -10;       // last time a vehicle bowled this player over
    bool inVehicle() const { return vehicle != 0xFFFF; }

    Vec3 eye() const { return move.pos + Vec3{0, move.crouched ? 1.15f : EYE_HEIGHT, 0}; }
    const ItemStack* held() const { return selected >= 1 && selected <= INVENTORY_SLOTS ? &inv[selected - 1] : nullptr; }
    ItemStack* held() { return selected >= 1 && selected <= INVENTORY_SLOTS ? &inv[selected - 1] : nullptr; }
    bool active() const { return alive && !eliminated; }
};

struct WorldItem {
    uint32_t id;
    ItemStack stack;
    Vec3 pos;
};

struct Projectile {
    uint32_t id;
    ItemType type;
    Rarity rarity;
    uint16_t owner;
    uint8_t team;
    Vec3 pos, vel;
    float fuse;
    bool alive = true;
    bool stuck = false;
    uint16_t stuckTo = 0xFFFF;  // player a sticky charge is attached to
    si::Vec3 stuckOffset;
};

struct SupplyDrop {
    uint32_t id;
    Vec3 pos;       // current
    float groundY;
    bool opened = false;
};

struct Vehicle {
    uint16_t id = 0;
    VehicleType type = VehicleType::GolfCart;
    VehicleState st;
    float hp = 100, maxHp = 100;
    uint16_t seats[MAX_SEATS] = {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF};
    bool alive = true;
    float honkCooldown = 0;
    float crashCooldown = 0;
    uint16_t lastDamager = 0xFFFF;
    bool hasDriver() const { return seats[0] != 0xFFFF; }
};

struct StormState {
    int phase = 0;              // index into phase table, -1 = not started
    bool shrinking = false;
    float timer = 0;            // time remaining in current wait/shrink
    Vec2 curCenter, nextCenter, startCenter;
    float curRadius = 2000, nextRadius = 2000, startRadius = 2000;
    float damage = 1;
    bool finished = false;
    bool paused = false;
};

struct MatchResultEntry {
    std::string accountId;
    std::string name;
    bool bot;
    int placement;
    int kills;
    float damage;
    float survived;
};

class Game {
public:
    GameConfig cfg;
    GameMap map;
    CollisionWorld world;
    StructureSet structures;
    std::map<uint16_t, Player> players;
    std::unordered_map<uint32_t, WorldItem> items;
    std::vector<uint8_t> chestOpened, ammoBoxOpened;
    std::vector<Projectile> projectiles;
    std::vector<Vec3> launchPads;
    std::vector<SupplyDrop> supplyDrops;
    std::vector<Vehicle> vehicles;
    StormState storm;
    MatchPhase phase = MatchPhase::Warmup;
    float phaseTimer = 0;
    float time = 0;
    float matchStartTime = 0;
    Vec3 busStart, busEnd;
    float busProgress = 0;
    bool busActive = false;
    int teamsAtStart = 0;
    int nextPlacement = 0;
    bool resultsReported = false;
    bool wantsExit = false;
    uint16_t winnerTeam = 0xFFFF;
    std::vector<Effect> effects;   // cleared by the network layer each tick

    // Hooks for the network layer
    std::function<void(const std::vector<uint8_t>& ev, int target)> emitEvent;  // target -1 = broadcast
    std::function<void(const std::vector<MatchResultEntry>&)> onMatchEnd;
    std::function<void(const std::string&)> log;

    void init(const GameConfig& c);
    void tick(float dt);

    Player* addPlayer(const std::string& name, const std::string& accountId, const Loadout& l, bool bot);
    void removePlayer(uint16_t id);
    void addBots(int n);
    void removeBots(int n);
    void startNow();
    void endMatchNow();
    void broadcastMessage(const std::string& text, uint8_t kind = 0);
    void queueInput(Player& p, const InputCmd& c);
    void handleAction(Player& p, const ClientAction& a);
    int humanCount() const;
    int botCount() const;
    int aliveCount() const;
    int aliveTeams() const;
    Vehicle* findVehicle(uint16_t id);
    const Vehicle* findVehicle(uint16_t id) const;

    // Serializers used when a new client joins (full state) and for broadcast.
    void writeFullStateEvents(std::vector<std::vector<uint8_t>>& out) const;
    std::vector<uint8_t> evPlayerInfo(const Player& p) const;

private:
    uint16_t nextPlayerId_ = 1;
    uint32_t nextItemId_ = 1, nextStructId_ = 1, nextProjId_ = 1, nextDropId_ = 1;
    Rng rng_;
    float botAccum_ = 0;
    float vehAccum_ = 0;

    void emit(const ByteWriter& w, int target = -1) { if (emitEvent) emitEvent(w.buf, target); }
    void setPhase(MatchPhase p);
    void resetWorldForMatch();
    void spawnInitialLoot();
    void startBus();
    void updateBus(float dt);
    void updateStorm(float dt);
    void pickNextStormCircle();
    void updateProjectiles(float dt);
    void updateSupplyDrops(float dt);
    void updateStructures(float dt);
    void checkEnd();
    void finishMatch();

    // Vehicles (vehicles_server.cpp)
    void spawnVehicles();
    void updateVehicles(float dt);
    void driveVehicle(Player& p, const InputCmd& in, float dt);
    void afterVehicleStep(Vehicle& v, const VehicleEvents& ev, float dt);
    void syncSeats(Vehicle& v);
    bool enterVehicle(Player& p, Vehicle& v);
    void exitVehicle(Player& p);
    void changeSeat(Player& p);
    bool seatCanShoot(const Player& p) const;
    void damageVehicle(Vehicle& v, float amount, uint16_t attacker);
    void destroyVehicle(Vehicle& v, uint16_t attacker);
    int raycastVehicles(const Vec3& o, const Vec3& d, float maxT, uint16_t ignoreVehicle, float& tOut) const;

    void simulatePlayer(Player& p, const InputCmd& in, float dt);
    void updateWeapon(Player& p, const InputCmd& in, float dt);
    void fireGun(Player& p);
    void swingPickaxe(Player& p);
    void throwItem(Player& p);
    void tryBuild(Player& p);
    void tryEdit(Player& p);
    void startInteract(Player& p);
    void finishAction(Player& p);
    void cancelAction(Player& p);
    void startReload(Player& p);
    void selectSlot(Player& p, int slot);

    struct AimResult { Vec3 origin, dir, point; };
    AimResult aim(const Player& p, float spread);
    uint16_t raycastPlayers(const Vec3& o, const Vec3& d, float maxT, uint16_t ignore, float& tOut, bool& head) const;
    void damagePlayer(Player& victim, float amount, uint16_t attacker, ItemType weapon, uint8_t killFlags, bool ignoreShield = false);
    void eliminate(Player& victim, uint16_t killer, ItemType weapon, uint8_t flags);
    void knock(Player& victim, uint16_t attacker, ItemType weapon, uint8_t flags);
    void damageShape(uint32_t shapeId, float amount, Player* attacker, bool harvest);
    void destroyStructure(uint32_t structId);
    void explode(const Vec3& pos, float radius, float damage, float structMul, uint16_t owner, uint8_t team, ItemType weapon);

    uint32_t spawnItem(const ItemStack& s, const Vec3& pos, bool scatter);
    void removeItem(uint32_t id);
    bool pickup(Player& p, uint32_t itemId);
    void dropInventory(Player& p);
    void dropSlot(Player& p, int slot);
    void giveLoadoutWarmup(Player& p);
    void spawnWarmup(Player& p);
    void openChest(Player& p, uint32_t idx);
    void openAmmoBox(Player& p, uint32_t idx);
    void openSupplyDrop(Player& p, uint32_t idx);
    int addToInventory(Player& p, const ItemStack& s); // returns leftover count

    // Bots (bots.cpp)
    void botThink(Player& p, float dt, InputCmd& out);
    void botBusLogic(Player& p);
    bool botFindEnemy(Player& p);
    void botChooseWeapon(Player& p, float dist);
    void botLoot(Player& p, InputCmd& out);
    void botMoveTo(Player& p, const Vec3& target, InputCmd& out, bool sprint);
    void botFight(Player& p, float dt, InputCmd& out);
    bool botNeedsHeal(Player& p);
    void botAct(Player& p, ActionType t, uint8_t a = 0, uint8_t b = 0);
};

std::vector<uint8_t> packItemSpawn(const WorldItem& it);

} // namespace si
