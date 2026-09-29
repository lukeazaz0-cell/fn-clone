// Deterministic character movement used for both server simulation and client prediction.
#pragma once
#include <vector>

#include "../common/math.h"
#include "defs.h"
#include "map.h"
#include "world.h"

namespace si {

struct MoveInput {
    float yaw = 0, pitch = 0;
    int8_t fwd = 0, right = 0;   // -1..1 (scaled by 127 on the wire)
    uint16_t buttons = 0;
};

struct MoveState {
    Vec3 pos;                    // feet position
    Vec3 vel;
    MoveMode mode = MoveMode::Ground;
    bool onGround = false;
    bool canGlide = false;       // after launch pads / vents / slipstreams
    bool crouched = false;
    uint16_t prevButtons = 0;
    float airTime = 0;
    float fallStartY = 0;
};

struct MoveParams {
    float speedMul = 1.0f;       // weapon/ADS/consumable slowdown
    bool dbno = false;           // knocked players crawl
    bool rooted = false;         // e.g. drinking a mega flask
    const std::vector<Vec3>* launchPads = nullptr;
};

struct MoveEvents {
    float fallDamage = 0;
    bool landed = false;
    bool launched = false;
    bool jumped = false;
};

MoveEvents stepMovement(MoveState& st, const MoveInput& in, const CollisionWorld& world, const GameMap& map, float dt,
                        const MoveParams& params);

float playerHeight(const MoveState& st);

} // namespace si
