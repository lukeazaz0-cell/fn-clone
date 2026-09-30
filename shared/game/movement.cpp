#include "movement.h"

namespace si {

float playerHeight(const MoveState& st) {
    if (st.mode == MoveMode::Swim) return PLAYER_CROUCH_HEIGHT;
    return st.crouched ? PLAYER_CROUCH_HEIGHT : PLAYER_HEIGHT;
}

static Vec3 wishDir(const MoveInput& in) {
    Vec3 f{std::sin(in.yaw), 0, std::cos(in.yaw)};
    Vec3 r{-std::cos(in.yaw), 0, std::sin(in.yaw)}; // right-hand side when looking along f
    Vec3 w = f * (float)in.fwd + r * (float)in.right;
    float l = w.len();
    return l > 0.01f ? w / l : Vec3{0, 0, 0};
}

static void approach(Vec3& vel, const Vec3& target, float accel, float dt) {
    Vec3 d{target.x - vel.x, 0, target.z - vel.z};
    float l = d.len();
    float maxStep = accel * dt;
    if (l <= maxStep) { vel.x = target.x; vel.z = target.z; }
    else { vel.x += d.x / l * maxStep; vel.z += d.z / l * maxStep; }
}

MoveEvents stepMovement(MoveState& st, const MoveInput& in, const CollisionWorld& world, const GameMap& map, float dt,
                        const MoveParams& params) {
    MoveEvents ev;
    uint16_t pressed = in.buttons & ~st.prevButtons;
    st.prevButtons = in.buttons;
    if (st.mode == MoveMode::OnBus || st.mode == MoveMode::Dead || st.mode == MoveMode::Spectate || st.mode == MoveMode::Vehicle) return ev;

    Vec3 wish = wishDir(in);
    float groundY = world.terrainHeight(st.pos.x, st.pos.z);

    // --- Slipstreams: strong directional wind tunnels
    bool inStream = false;
    for (const auto& s : map.slipstreams) {
        for (size_t i = 0; i + 1 < s.points.size(); i++) {
            const Vec3& a = s.points[i];
            const Vec3& b = s.points[i + 1];
            Vec3 ab = b - a;
            float len2 = ab.len2();
            float t = clampf((st.pos + Vec3{0, 1, 0} - a).dot(ab) / len2, 0, 1);
            Vec3 closest = a + ab * t;
            Vec3 off = closest - (st.pos + Vec3{0, 1, 0});
            if (off.len() < s.radius) {
                Vec3 dir = ab.norm();
                Vec3 target = dir * s.speed + off * 2.0f + wish * 8.0f;
                st.vel = lerp3(st.vel, target, clampf(dt * 4.0f, 0, 1));
                inStream = true;
                break;
            }
        }
        if (inStream) break;
    }
    if (inStream) {
        st.mode = MoveMode::Air;
        st.canGlide = true;
        st.onGround = false;
        st.pos += st.vel * dt;
        st.fallStartY = st.pos.y;
        world.resolveHorizontal(st.pos, PLAYER_RADIUS, PLAYER_HEIGHT, 0.3f);
        float g = world.groundHeight(st.pos, PLAYER_RADIUS, st.pos.y + 0.6f);
        if (st.pos.y < g) st.pos.y = g;
        return ev;
    }

    // --- Vents and launch pads
    auto launch = [&](float up) {
        st.vel.y = up;
        st.mode = MoveMode::Air;
        st.canGlide = true;
        st.onGround = false;
        st.fallStartY = st.pos.y + 1000; // no fall damage after a launch
        ev.launched = true;
    };
    if (st.mode == MoveMode::Ground || st.mode == MoveMode::Air) {
        for (const auto& v : map.vents)
            if (distXZ(v.pos, st.pos) < v.radius && st.pos.y < v.pos.y + 3.0f) launch(55.0f);
        if (params.launchPads)
            for (const auto& lp : *params.launchPads)
                if (distXZ(lp, st.pos) < 1.8f && std::fabs(st.pos.y - lp.y) < 1.0f) launch(48.0f);
    }

    switch (st.mode) {
        case MoveMode::Skydive: {
            bool dive = (in.buttons & IN_DIVE) || (in.fwd > 0 && in.pitch < -0.6f);
            float fall = dive ? SKYDIVE_DIVE : SKYDIVE_FALL;
            st.vel.y = lerpf(st.vel.y, -fall, clampf(dt * 2.0f, 0, 1));
            Vec3 target = wish * (dive ? SKYDIVE_HSPEED * 0.6f : SKYDIVE_HSPEED);
            approach(st.vel, target, 20.0f, dt);
            st.pos += st.vel * dt;
            float gy = world.groundHeight(st.pos, PLAYER_RADIUS, st.pos.y + 0.5f);
            if (st.pos.y - gy < AUTO_GLIDE_HEIGHT || ((pressed & IN_JUMP) && st.pos.y - gy < 150.0f)) st.mode = MoveMode::Glide;
            if (st.pos.y <= gy) { st.pos.y = gy; st.mode = MoveMode::Ground; st.vel = {}; ev.landed = true; }
            break;
        }
        case MoveMode::Glide: {
            bool dive = in.fwd > 0 && in.pitch < -0.5f;
            st.vel.y = lerpf(st.vel.y, dive ? -GLIDE_FALL * 1.8f : -GLIDE_FALL, clampf(dt * 3.0f, 0, 1));
            Vec3 fwd{std::sin(in.yaw), 0, std::cos(in.yaw)};
            Vec3 target = (wish.len() > 0 ? wish : fwd * 0.35f) * GLIDE_HSPEED;
            approach(st.vel, target, 14.0f, dt);
            st.pos += st.vel * dt;
            world.resolveHorizontal(st.pos, PLAYER_RADIUS, PLAYER_HEIGHT, 0.3f);
            float gy = world.groundHeight(st.pos, PLAYER_RADIUS, st.pos.y + 0.5f);
            if (st.pos.y <= gy + 0.05f) {
                st.pos.y = gy;
                st.mode = MoveMode::Ground;
                st.vel = {0, 0, 0};
                st.canGlide = false;
                ev.landed = true;
            }
            if (st.pos.y < WATER_LEVEL - 0.8f && gy < WATER_LEVEL - 1.2f) { st.mode = MoveMode::Swim; st.canGlide = false; }
            break;
        }
        case MoveMode::Swim: {
            float speed = SWIM_SPEED * params.speedMul;
            approach(st.vel, wish * speed, 20.0f, dt);
            st.vel.y = 0;
            st.pos.x += st.vel.x * dt;
            st.pos.z += st.vel.z * dt;
            world.resolveHorizontal(st.pos, PLAYER_RADIUS, PLAYER_CROUCH_HEIGHT, 0.6f);
            float gy = world.groundHeight(st.pos, PLAYER_RADIUS, st.pos.y + 1.2f);
            st.pos.y = WATER_LEVEL - 1.15f;
            if (gy > WATER_LEVEL - 1.2f) {
                st.pos.y = gy;
                st.mode = MoveMode::Ground;
            }
            // Clamp to map bounds
            st.pos.x = clampf(st.pos.x, 2, WORLD_SIZE - 2);
            st.pos.z = clampf(st.pos.z, 2, WORLD_SIZE - 2);
            break;
        }
        case MoveMode::Ground:
        case MoveMode::Air:
        default: {
            if (params.rooted) wish = {0, 0, 0};
            st.crouched = (in.buttons & IN_CROUCH) != 0 && st.mode == MoveMode::Ground;
            float speed = WALK_SPEED;
            if (params.dbno) speed = 2.0f;
            else if (st.crouched) speed = CROUCH_SPEED;
            else if (in.buttons & IN_ADS) speed = ADS_SPEED;
            else if ((in.buttons & IN_SPRINT) && in.fwd > 0) speed = SPRINT_SPEED;
            speed *= params.speedMul;
            // Moving backwards is a bit slower
            if (in.fwd < 0) speed *= 0.85f;

            bool grounded = st.mode == MoveMode::Ground;
            approach(st.vel, wish * speed, grounded ? 60.0f : 12.0f, dt);

            if (grounded && (pressed & IN_JUMP) && !params.dbno && !params.rooted) {
                st.vel.y = JUMP_VEL;
                st.mode = MoveMode::Air;
                grounded = false;
                st.fallStartY = st.pos.y;
                ev.jumped = true;
            } else if (!grounded && st.canGlide && (pressed & IN_JUMP)) {
                float gy = world.groundHeight(st.pos, PLAYER_RADIUS, st.pos.y + 0.5f);
                if (st.pos.y - gy > 8.0f) {
                    st.mode = MoveMode::Glide;
                    st.vel.y = std::max(st.vel.y, -GLIDE_FALL);
                    break;
                }
            }

            // Horizontal move + collide
            Vec3 before = st.pos;
            st.pos.x += st.vel.x * dt;
            st.pos.z += st.vel.z * dt;
            float h = playerHeight(st);
            world.resolveHorizontal(st.pos, PLAYER_RADIUS, h, grounded ? 0.55f : 0.25f);
            // Kill velocity into walls
            if (dt > 0) {
                Vec3 real = (st.pos - before) / dt;
                if (std::fabs(real.x) < std::fabs(st.vel.x) * 0.5f) st.vel.x = real.x;
                if (std::fabs(real.z) < std::fabs(st.vel.z) * 0.5f) st.vel.z = real.z;
            }
            st.pos.x = clampf(st.pos.x, 2, WORLD_SIZE - 2);
            st.pos.z = clampf(st.pos.z, 2, WORLD_SIZE - 2);

            // Vertical
            if (!grounded) st.vel.y -= GRAVITY * dt;
            float maxStep = grounded ? 0.65f : 0.3f;
            float newY = st.pos.y + st.vel.y * dt;
            if (st.vel.y > 0) {
                float ceil = world.ceilingHeight(st.pos, PLAYER_RADIUS, h);
                if (newY + h > ceil) { newY = ceil - h; st.vel.y = 0; }
            }
            float gy = world.groundHeight(st.pos, PLAYER_RADIUS, std::max(st.pos.y, newY) + maxStep);
            groundY = gy;
            if (grounded) {
                // Stick to ground when walking down slopes/ramps
                if (gy >= st.pos.y - 0.7f) {
                    st.pos.y = gy;
                    st.vel.y = 0;
                } else {
                    st.mode = MoveMode::Air;
                    st.pos.y = newY;
                    st.fallStartY = st.pos.y;
                }
            } else {
                if (newY <= gy) {
                    st.pos.y = gy;
                    float fallDist = st.fallStartY - gy;
                    if (st.vel.y < -18.0f && fallDist > 7.0f) ev.fallDamage = std::min(100.0f, (fallDist - 6.0f) * 4.0f);
                    st.vel.y = 0;
                    st.mode = MoveMode::Ground;
                    st.canGlide = false;
                    ev.landed = true;
                } else {
                    st.pos.y = newY;
                    if (st.vel.y > 0) st.fallStartY = std::max(st.fallStartY, st.pos.y);
                    else st.fallStartY = std::max(st.fallStartY, st.pos.y);
                }
            }
            // Water
            if (st.pos.y < WATER_LEVEL - 1.2f && groundY < WATER_LEVEL - 1.2f) {
                st.mode = MoveMode::Swim;
                st.pos.y = WATER_LEVEL - 1.15f;
                st.vel.y = 0;
                st.canGlide = false;
            }
            // Out of world safety
            if (st.pos.y < world.terrainHeight(st.pos.x, st.pos.z) - 2.0f) st.pos.y = world.terrainHeight(st.pos.x, st.pos.z);
            break;
        }
    }
    st.onGround = st.mode == MoveMode::Ground;
    return ev;
}

} // namespace si
