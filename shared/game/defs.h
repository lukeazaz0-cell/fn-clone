// Core game constants and enums shared by every component.
#pragma once
#include <cstdint>

namespace si {

constexpr int PROTOCOL_VERSION = 5;
constexpr float SIM_DT = 1.0f / 60.0f;      // fixed movement step (client prediction + server)
constexpr float SERVER_TICK_DT = 1.0f / 30.0f;
constexpr int MAX_PLAYERS = 100;

// World
constexpr float WORLD_SIZE = 1536.0f;       // meters, map spans [0, WORLD_SIZE] on X and Z
constexpr float CELL = 4.0f;                // heightmap cell size
constexpr int HM_N = 384;                   // heightmap cells per side (HM_N * CELL == WORLD_SIZE)
constexpr float WATER_LEVEL = 0.0f;
constexpr float BUS_HEIGHT = 260.0f;

// Building grid
constexpr float TILE = 5.0f;                // tile width
constexpr float TILE_H = 4.0f;              // tile height
constexpr float WALL_T = 0.3f;              // piece thickness
constexpr float GRID_Y = TILE_H * 0.5f;     // vertical build snap (half tile)

// Player
constexpr float PLAYER_RADIUS = 0.45f;
constexpr float PLAYER_HEIGHT = 1.9f;
constexpr float PLAYER_CROUCH_HEIGHT = 1.3f;
constexpr float EYE_HEIGHT = 1.65f;
constexpr float WALK_SPEED = 6.2f;
constexpr float SPRINT_SPEED = 8.4f;
constexpr float CROUCH_SPEED = 3.2f;
constexpr float ADS_SPEED = 4.2f;
constexpr float SWIM_SPEED = 4.0f;
constexpr float JUMP_VEL = 7.2f;
constexpr float GRAVITY = 20.0f;
constexpr float SKYDIVE_FALL = 32.0f;
constexpr float SKYDIVE_DIVE = 52.0f;
constexpr float SKYDIVE_HSPEED = 22.0f;
constexpr float GLIDE_FALL = 9.0f;
constexpr float GLIDE_HSPEED = 17.0f;
constexpr float AUTO_GLIDE_HEIGHT = 55.0f;  // auto-deploy glider this far above ground
constexpr float MAX_HEALTH = 100.0f;
constexpr float MAX_SHIELD = 100.0f;
constexpr int MAX_MATS = 999;
constexpr int INVENTORY_SLOTS = 5;
constexpr float INTERACT_RANGE = 3.2f;

enum class Material : uint8_t { Wood = 0, Brick = 1, Metal = 2, None = 3 };
constexpr const char* MATERIAL_NAMES[] = {"Wood", "Brick", "Metal", "None"};

enum class Rarity : uint8_t { Common = 0, Uncommon, Rare, Epic, Legendary, Count };
constexpr const char* RARITY_NAMES[] = {"Common", "Uncommon", "Rare", "Epic", "Legendary"};

enum class AmmoType : uint8_t { None = 0, Light, Medium, Heavy, Shells, Rockets, Count };
constexpr const char* AMMO_NAMES[] = {"None", "Light Bullets", "Medium Bullets", "Heavy Bullets", "Shells", "Rockets"};

enum class ItemType : uint8_t {
    None = 0,
    // weapons
    AssaultRifle,
    BurstRifle,
    ScopedRifle,
    PumpShotgun,
    TacticalShotgun,
    SMG,
    Pistol,
    SniperRifle,
    RocketLauncher,
    Minigun,
    HeavyRifle,
    CompactSMG,
    DoubleBarrel,
    HeavyShotgun,
    HuntingRifle,
    HandCannon,
    GrenadeLauncher,
    Crossbow,
    // throwables
    Grenade,
    ImpulseGrenade,
    StickyCharge,
    // consumables
    Bandages,
    Medkit,
    SmallShield,
    ShieldPotion,
    RegenSoda,
    MegaFlask,
    FieldKit,
    // traps / utility
    LaunchPad,
    // pickups that never sit in the inventory
    AmmoPickup,
    MaterialPickup,
    Count
};

enum class ItemClass : uint8_t { None, Gun, Throwable, Consumable, Utility, Ammo, Mats };

// Build pieces
enum class PieceType : uint8_t { Wall = 0, Floor = 1, Ramp = 2, Cone = 3, Count };
constexpr const char* PIECE_NAMES[] = {"Wall", "Floor", "Ramp", "Roof"};

// Wall edits (a small subset of the real edit grid, which keeps collision simple)
enum class WallEdit : uint8_t { Full = 0, Door, Window, Arch, Count };
enum class FloorEdit : uint8_t { Full = 0, Hole, Count };

// Match flow
enum class MatchPhase : uint8_t { Warmup = 0, Countdown, Bus, Playing, Ended };
constexpr const char* PHASE_NAMES[] = {"warmup", "countdown", "bus", "playing", "ended"};

// Player movement modes
enum class MoveMode : uint8_t { Ground = 0, Air, OnBus, Skydive, Glide, Swim, Dead, Spectate, Vehicle };

// Player status flags (network)
enum PlayerFlags : uint16_t {
    PF_ALIVE = 1 << 0,
    PF_DBNO = 1 << 1,
    PF_CROUCH = 1 << 2,
    PF_ADS = 1 << 3,
    PF_BUILDING = 1 << 4,
    PF_FIRING = 1 << 5,
    PF_RELOADING = 1 << 6,
    PF_CONSUMING = 1 << 7,
    PF_EMOTING = 1 << 8,
    PF_BOT = 1 << 9,
    PF_HARVESTING = 1 << 10,
    PF_SPRINT = 1 << 11,
    PF_REVIVING = 1 << 12,
    PF_BEING_REVIVED = 1 << 13,
    PF_SPECTATOR = 1 << 14,
};

// Input buttons
enum InputButtons : uint16_t {
    IN_JUMP = 1 << 0,
    IN_CROUCH = 1 << 1,
    IN_FIRE = 1 << 2,
    IN_ADS = 1 << 3,
    IN_INTERACT = 1 << 4,
    IN_RELOAD = 1 << 5,
    IN_SPRINT = 1 << 6,
    IN_DIVE = 1 << 7,       // hold to dive faster while skydiving
    IN_EDIT = 1 << 8,       // hold-to-edit (not used for movement)
};

// Discrete client actions (reliably delivered via redundancy)
enum class ActionType : uint8_t {
    None = 0,
    SelectSlot,     // a = slot (0 = pickaxe, 1..5 = inventory)
    SetBuildMode,   // a = 0 off / 1 on, b = piece type
    SetBuildMat,    // a = material
    RotatePiece,
    EditPiece,      // cycles edit of the targeted own/any structure
    DropSlot,       // a = slot 1..5
    SwapSlots,      // a, b
    Emote,          // a = emote index
    JumpFromBus,
    ToggleGlide,
    DropMats,       // a = material, b = amount/10
    Spectate,       // cycle spectate target
    ChangeSeat,     // move to the next free seat of the current vehicle
};

enum class BotDifficulty : uint8_t { Easy = 0, Medium = 1, Hard = 2, Insane = 3 };
constexpr const char* BOT_DIFFICULTY_NAMES[] = {"easy", "medium", "hard", "insane"};

} // namespace si
