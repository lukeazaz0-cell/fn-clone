// Drivable vehicles: definitions and the deterministic vehicle physics step shared by the
// server simulation and the driver's client-side prediction.
#pragma once
#include <cstdint>

#include "../common/math.h"
#include "defs.h"
#include "world.h"

namespace si {

struct MoveInput; // movement.h

enum class VehicleType : uint8_t {
    GolfCart = 0,   // four-seat cart: slow, sturdy, passengers can shoot
    CrashQuad,      // two-seat quad: boost smashes through builds and bowls players over
    Hoverboard,     // one rider: floats over water, the rider can shoot, boost and hop
    Trolley,        // shopping cart: pusher + passenger, very fast downhill
    RollerBall,     // armoured hamster ball: bouncy, protects the rider, jump + boost
    Count
};

constexpr int MAX_SEATS = 4;
constexpr uint8_t NO_SEAT = 0xFF;

enum SeatPose : uint8_t { SEAT_SIT = 1, SEAT_STAND = 2, SEAT_HIDDEN = 3 };

struct VehicleDef {
    const char* name;
    const char* description;
    int seats;
    float radius;       // collision cylinder radius
    float height;       // collision height
    float hitHeight;    // height of the damageable hull (occupants above it can be shot)
    float maxSpeed;
    float boostSpeed;   // 0 = no boost
    float accel;
    float brake;
    float reverseSpeed;
    float turnRate;     // rad/s at full steering
    float grip;         // lateral friction (1/s): high = on rails, low = drifty
    float rolling;      // coasting deceleration (m/s^2)
    float slopeGain;    // how strongly slopes accelerate the vehicle
    float maxHp;
    float jumpVel;      // 0 = cannot jump
    float boostDrain;   // meter per second while boosting
    float boostRegen;   // meter per second while not boosting
    float bounce;       // wall restitution (0 = stop dead)
    bool cameraSteer;   // steer toward the movement direction relative to the camera
    bool hover;         // glides over water
    bool ramBuilds;     // boosting destroys player builds
    Vec3 seatPos[MAX_SEATS];     // local feet position of each seat (x right, y up, z forward)
    bool seatShoot[MAX_SEATS];   // can this seat use weapons
    uint8_t seatPose[MAX_SEATS];
};

const VehicleDef& vehicleDef(VehicleType t);

struct VehicleState {
    Vec3 pos;                   // bottom centre
    Vec3 vel;
    float yaw = 0;
    float pitch = 0, roll = 0;  // body tilt from the terrain (visual + hit boxes)
    float boost = 1.0f;         // boost meter 0..1
    float airTime = 0;
    bool onGround = true;
    bool boosting = false;
    bool inWater = false;
    uint16_t prevButtons = 0;
};

struct VehicleEvents {
    float impact = 0;           // speed lost against a wall this step (m/s)
    bool jumped = false;
    bool landed = false;
    bool boostStarted = false;
};

// Advances the vehicle one fixed step. `in` is the driver's input (ignored when !hasDriver).
VehicleEvents stepVehicle(VehicleState& s, VehicleType t, const MoveInput& in, bool hasDriver, const CollisionWorld& world, float dt);

// World-space feet position of a seat (ignores tilt so occupants stay upright).
Vec3 vehicleSeatPos(const VehicleState& s, VehicleType t, int seat);

// Hull box used for bullets, explosions and collisions with players.
AABB vehicleHull(const VehicleState& s, VehicleType t);

// Horizontal forward / right vectors for a yaw (same convention as movement).
inline Vec3 yawForward(float yaw) { return {std::sin(yaw), 0, std::cos(yaw)}; }
inline Vec3 yawRight(float yaw) { return {-std::cos(yaw), 0, std::sin(yaw)}; }

} // namespace si
