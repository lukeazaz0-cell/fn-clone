#include "vehicles.h"

#include <algorithm>
#include <cmath>

#include "movement.h"

namespace si {

namespace {

// Seat feet positions sit 0.55 m below the seat surface so the standard eye height lands
// where a seated character's head is.
const VehicleDef kDefs[(int)VehicleType::Count] = {
    // GolfCart
    {"Fairway Cart", "Four seats. Passengers can shoot while the driver steers.",
     4, 1.25f, 1.9f, 1.05f,
     19.0f, 0.0f, 9.0f, 24.0f, 7.0f, 1.8f, 7.0f, 2.5f, 0.7f,
     800.0f, 0.0f, 0.0f, 0.0f, 0.08f,
     false, false, false,
     {{0.36f, 0.22f, 0.15f}, {-0.36f, 0.22f, 0.15f}, {0.36f, 0.32f, -0.95f}, {-0.36f, 0.32f, -0.95f}},
     {false, true, true, true},
     {SEAT_SIT, SEAT_SIT, SEAT_SIT, SEAT_SIT}},
    // CrashQuad
    {"Crash Quad", "Two seats. Hold Shift to boost: smashes builds and bowls players over.",
     2, 1.05f, 1.5f, 0.95f,
     22.0f, 33.0f, 12.0f, 26.0f, 8.0f, 2.3f, 6.0f, 2.5f, 0.7f,
     600.0f, 9.0f, 0.45f, 0.2f, 0.12f,
     false, false, true,
     {{0.0f, 0.38f, 0.05f}, {0.0f, 0.5f, -0.6f}, {}, {}},
     {false, true, false, false},
     {SEAT_SIT, SEAT_SIT, 0, 0}},
    // Hoverboard
    {"Skyboard", "Rider can shoot. Floats over water. Shift to boost, Space to hop.",
     1, 0.55f, 1.8f, 0.35f,
     17.0f, 27.0f, 14.0f, 18.0f, 0.0f, 3.2f, 2.4f, 1.5f, 1.0f,
     300.0f, 10.0f, 0.5f, 0.25f, 0.25f,
     true, true, false,
     {{0.0f, 0.42f, 0.0f}, {}, {}, {}},
     {true, false, false, false},
     {SEAT_STAND, 0, 0, 0}},
    // Trolley
    {"Trolley", "Pusher and a passenger in the basket. Gets very fast downhill.",
     2, 0.8f, 1.25f, 0.95f,
     11.0f, 0.0f, 6.0f, 11.0f, 4.0f, 2.4f, 5.5f, 0.6f, 1.7f,
     300.0f, 0.0f, 0.0f, 0.0f, 0.15f,
     false, false, false,
     {{0.0f, 0.0f, -0.95f}, {0.0f, 0.4f, 0.1f}, {}, {}},
     {false, true, false, false},
     {SEAT_STAND, SEAT_SIT, 0, 0}},
    // RollerBall
    {"Roller Ball", "Armoured ball that shields its rider. Bouncy; Space to jump, Shift to boost.",
     1, 1.2f, 2.4f, 2.4f,
     19.0f, 29.0f, 16.0f, 14.0f, 0.0f, 6.0f, 1.2f, 1.4f, 1.2f,
     450.0f, 11.0f, 0.5f, 0.22f, 0.6f,
     true, false, false,
     {{0.0f, 0.3f, 0.0f}, {}, {}, {}},
     {false, false, false, false},
     {SEAT_SIT, 0, 0, 0}},
};

constexpr float kMaxVehicleSpeed = 36.0f;

bool isBall(VehicleType t) { return t == VehicleType::RollerBall; }

} // namespace

const VehicleDef& vehicleDef(VehicleType t) {
    int i = (int)t;
    if (i < 0 || i >= (int)VehicleType::Count) i = 0;
    return kDefs[i];
}

Vec3 vehicleSeatPos(const VehicleState& s, VehicleType t, int seat) {
    const VehicleDef& d = vehicleDef(t);
    if (seat < 0 || seat >= d.seats) seat = 0;
    const Vec3& o = d.seatPos[seat];
    return s.pos + yawRight(s.yaw) * o.x + Vec3{0, o.y, 0} + yawForward(s.yaw) * o.z;
}

AABB vehicleHull(const VehicleState& s, VehicleType t) {
    const VehicleDef& d = vehicleDef(t);
    float r = d.radius * (isBall(t) ? 1.0f : 0.95f);
    return AABB({s.pos.x - r, s.pos.y, s.pos.z - r}, {s.pos.x + r, s.pos.y + d.hitHeight, s.pos.z + r});
}

VehicleEvents stepVehicle(VehicleState& s, VehicleType t, const MoveInput& in, bool hasDriver, const CollisionWorld& world, float dt) {
    VehicleEvents ev;
    const VehicleDef& d = vehicleDef(t);
    uint16_t buttons = hasDriver ? in.buttons : 0;
    uint16_t pressed = buttons & ~s.prevButtons;
    s.prevButtons = buttons;

    // Parked and settled: nothing to do (keeps idle vehicles cheap on the server).
    if (!hasDriver && s.onGround && !s.inWater && s.vel.len2() < 0.0025f) {
        s.vel = {};
        s.boost = std::min(1.0f, s.boost + d.boostRegen * dt);
        return ev;
    }

    float footR = d.radius * 0.6f;
    float probe = std::max(0.9f, d.radius * 0.8f);
    auto sampleGround = [&](const Vec3& p) {
        float g = world.groundHeight(p, 0.1f, s.pos.y + 1.2f);
        if (g < WATER_LEVEL - 0.6f) g = d.hover ? WATER_LEVEL : WATER_LEVEL - 0.55f;
        return g;
    };

    // --- boost meter
    float throttle = hasDriver ? clampf((float)in.fwd, -1, 1) : 0.0f;
    float steer = hasDriver ? clampf((float)in.right, -1, 1) : 0.0f;
    bool wantBoost = hasDriver && (buttons & IN_SPRINT) && d.boostSpeed > 0;
    bool wasBoosting = s.boosting;
    s.boosting = wantBoost && s.boost > (wasBoosting ? 0.01f : 0.15f) && !s.inWater;
    if (s.boosting) s.boost = std::max(0.0f, s.boost - d.boostDrain * dt);
    else s.boost = std::min(1.0f, s.boost + d.boostRegen * dt);
    if (s.boosting && !wasBoosting) ev.boostStarted = true;
    float top = s.boosting ? d.boostSpeed : d.maxSpeed;
    float accel = d.accel * (s.boosting ? 1.7f : 1.0f);

    // Camera-relative wish direction (hoverboard, ball)
    Vec3 wish{};
    if (d.cameraSteer && hasDriver) {
        wish = yawForward(in.yaw) * (float)in.fwd + yawRight(in.yaw) * (float)in.right;
        float l = wish.len();
        wish = l > 0.01f ? wish / l : Vec3{};
        if (s.boosting && l <= 0.01f) wish = yawForward(s.yaw);
    }

    // --- slope acceleration from the terrain/build gradient
    Vec3 f = yawForward(s.yaw), r = yawRight(s.yaw);
    float hF = sampleGround(s.pos + f * probe), hB = sampleGround(s.pos - f * probe);
    float hR = sampleGround(s.pos + r * probe), hL = sampleGround(s.pos - r * probe);
    bool grounded = s.onGround || s.inWater;
    if (s.onGround && !s.inWater) {
        // Gradient in world space from the four samples
        Vec3 grad = f * ((hF - hB) / (2 * probe)) + r * ((hR - hL) / (2 * probe));
        s.vel.x -= grad.x * GRAVITY * d.slopeGain * dt;
        s.vel.z -= grad.z * GRAVITY * d.slopeGain * dt;
    }

    // --- traction
    if (isBall(t)) {
        Vec3 hv{s.vel.x, 0, s.vel.z};
        float control = grounded ? 1.0f : 0.35f;
        if (wish.len2() > 0) {
            float before = hv.len();
            hv += wish * (accel * control * dt);
            float limit = std::max(top, before);
            float after = hv.len();
            if (after > limit) hv = hv * (limit / after);
        } else if (grounded) {
            float sp = hv.len();
            float dec = (hasDriver ? d.rolling : d.brake * 0.5f) * dt;
            hv = sp > dec ? hv * ((sp - dec) / sp) : Vec3{};
        }
        float sp = hv.len();
        if (sp > top && grounded) hv = hv * (std::max(top, sp - d.rolling * 3 * dt) / sp);
        s.vel.x = hv.x;
        s.vel.z = hv.z;
        if (sp > 0.5f) {
            float target = std::atan2(hv.x, hv.z);
            s.yaw = wrapAngle(s.yaw + clampf(wrapAngle(target - s.yaw), -d.turnRate * dt, d.turnRate * dt));
        }
    } else {
        if (d.cameraSteer && hasDriver) {
            // Turn the board toward the wish direction; throttle by how aligned it is.
            if (wish.len2() > 0) {
                float target = std::atan2(wish.x, wish.z);
                float diff = wrapAngle(target - s.yaw);
                float rate = d.turnRate * (grounded ? 1.0f : 0.5f);
                s.yaw = wrapAngle(s.yaw + clampf(diff, -rate * dt, rate * dt));
                float c = std::cos(diff);
                throttle = c > 0.2f ? c : (c < -0.5f ? -1.0f : 0.0f);
            } else {
                throttle = 0;
            }
            steer = 0;
        }
        if (s.boosting) throttle = 1.0f;
        f = yawForward(s.yaw);
        r = yawRight(s.yaw);
        float vf = s.vel.x * f.x + s.vel.z * f.z;
        float vl = s.vel.x * r.x + s.vel.z * r.z;
        if (grounded) {
            if (throttle > 0) {
                if (vf < -0.5f) vf = std::min(0.0f, vf + d.brake * dt);
                else if (vf < top) vf = std::min(top, vf + accel * throttle * dt);
            } else if (throttle < 0) {
                if (vf > 0.5f) vf = std::max(0.0f, vf - d.brake * dt);
                else if (d.reverseSpeed > 0) vf = std::max(-d.reverseSpeed, vf - accel * 0.7f * dt);
            } else {
                float dec = (hasDriver ? d.rolling : d.brake * 0.6f) * dt;
                vf = vf > 0 ? std::max(0.0f, vf - dec) : std::min(0.0f, vf + dec);
            }
            if (vf > top && !(s.onGround && hF < hB - 0.05f)) vf = std::max(top, vf - d.rolling * 3 * dt);
            vl *= std::exp(-d.grip * dt);
            // Steering scales in with speed and reverses when backing up.
            float sf = clampf(std::fabs(vf) / 3.5f, 0, 1) * (vf < 0 ? -1.0f : 1.0f);
            float turn = steer * d.turnRate * sf * (s.boosting ? 0.8f : 1.0f);
            s.yaw = wrapAngle(s.yaw - turn * dt);
        } else {
            s.yaw = wrapAngle(s.yaw - steer * d.turnRate * 0.35f * dt);
        }
        // Velocity follows the new heading (arcade handling)
        f = yawForward(s.yaw);
        r = yawRight(s.yaw);
        if (grounded) {
            s.vel.x = f.x * vf + r.x * vl;
            s.vel.z = f.z * vf + r.z * vl;
        }
    }
    if (s.inWater && !d.hover) {
        float sp = s.vel.lenXZ();
        if (sp > 4.0f) { float k = std::exp(-2.5f * dt); s.vel.x *= k; s.vel.z *= k; }
    }
    {
        float sp = s.vel.len();
        if (sp > kMaxVehicleSpeed) s.vel = s.vel * (kMaxVehicleSpeed / sp);
    }

    // --- jump
    if ((pressed & IN_JUMP) && d.jumpVel > 0 && s.onGround) {
        s.vel.y = d.jumpVel;
        s.onGround = false;
        ev.jumped = true;
    }

    // --- horizontal move + collision (bounce off walls)
    Vec3 moved = s.pos + Vec3{s.vel.x * dt, 0, s.vel.z * dt};
    Vec3 fixedPos = moved;
    world.resolveHorizontal(fixedPos, d.radius, d.height, s.onGround ? 0.9f : 0.35f);
    Vec3 corr{fixedPos.x - moved.x, 0, fixedPos.z - moved.z};
    float cl = corr.len();
    if (cl > 1e-4f) {
        Vec3 n = corr / cl;
        float vn = s.vel.x * n.x + s.vel.z * n.z;
        if (vn < 0) {
            s.vel.x -= n.x * vn * (1.0f + d.bounce);
            s.vel.z -= n.z * vn * (1.0f + d.bounce);
            ev.impact = -vn;
        }
    }
    s.pos.x = clampf(fixedPos.x, 3, WORLD_SIZE - 3);
    s.pos.z = clampf(fixedPos.z, 3, WORLD_SIZE - 3);

    // --- vertical: follow the surface, launch off ramps, fall, land
    float gy = world.groundHeight(s.pos, footR, s.pos.y + (s.onGround ? 0.9f : 0.35f));
    bool water = false;
    if (gy < WATER_LEVEL - 0.6f) {
        if (d.hover) gy = WATER_LEVEL + 0.02f;
        else { gy = WATER_LEVEL - 0.55f; water = true; }
    }
    if (s.onGround) {
        float predicted = s.pos.y + s.vel.y * dt;
        if (s.vel.y > 3.5f && gy < predicted - 0.02f) {
            // Crest of a ramp: carry the vertical speed into the air
            s.onGround = false;
            s.pos.y = predicted;
            s.vel.y -= GRAVITY * dt;
        } else if (gy >= s.pos.y - 0.45f) {
            float dy = gy - s.pos.y;
            s.vel.y = std::fabs(dy) < 0.3f ? clampf(dy / dt, -20.0f, 20.0f) : 0.0f;
            s.pos.y = gy;
        } else {
            s.onGround = false;
            s.vel.y = std::min(s.vel.y, 0.0f);
        }
    } else {
        s.vel.y -= GRAVITY * dt;
        float ny = s.pos.y + s.vel.y * dt;
        if (s.vel.y > 0) {
            float ceil = world.ceilingHeight(s.pos, footR, d.height);
            if (ny + d.height > ceil) { ny = ceil - d.height; s.vel.y = 0; }
        }
        if (ny <= gy) {
            s.pos.y = gy;
            if (d.bounce > 0.4f && s.vel.y < -7.0f) {
                s.vel.y = -s.vel.y * 0.4f;
            } else {
                if (s.vel.y < -14.0f) ev.impact = std::max(ev.impact, -s.vel.y - 14.0f);
                s.vel.y = 0;
                s.onGround = true;
                ev.landed = s.airTime > 0.25f;
            }
        } else {
            s.pos.y = ny;
        }
    }
    s.inWater = water && s.onGround;
    if (s.pos.y < world.terrainHeight(s.pos.x, s.pos.z) - 1.5f && !water) s.pos.y = world.terrainHeight(s.pos.x, s.pos.z);
    s.airTime = s.onGround ? 0.0f : s.airTime + dt;

    // --- body tilt (smoothed)
    float tp = 0, tr = 0;
    if (s.onGround && !isBall(t)) {
        f = yawForward(s.yaw);
        r = yawRight(s.yaw);
        float a = sampleGround(s.pos + f * probe), b = sampleGround(s.pos - f * probe);
        float c = sampleGround(s.pos + r * probe), e = sampleGround(s.pos - r * probe);
        tp = std::atan2(a - b, 2 * probe);
        tr = std::atan2(c - e, 2 * probe);
    } else if (!s.onGround && !isBall(t)) {
        tp = clampf(std::atan2(s.vel.y, std::max(1.0f, s.vel.lenXZ())) * 0.6f, -0.6f, 0.6f);
    }
    float k = clampf(dt * 10.0f, 0, 1);
    s.pitch = lerpf(s.pitch, clampf(tp, -0.7f, 0.7f), k);
    s.roll = lerpf(s.roll, clampf(tr, -0.6f, 0.6f), k);
    (void)hL;
    (void)hR;
    return ev;
}

} // namespace si
