// Vehicles on the server: spawning, seats, driving, ramming, damage and destruction.
#include <algorithm>
#include <set>

#include "game.h"

namespace si {

Vehicle* Game::findVehicle(uint16_t id) {
    for (auto& v : vehicles) if (v.id == id && v.alive) return &v;
    return nullptr;
}

const Vehicle* Game::findVehicle(uint16_t id) const {
    for (auto& v : vehicles) if (v.id == id && v.alive) return &v;
    return nullptr;
}

void Game::spawnVehicles() {
    for (auto& [id, p] : players)
        if (p.inVehicle()) { p.vehicle = 0xFFFF; p.seat = NO_SEAT; if (p.move.mode == MoveMode::Vehicle) p.move.mode = MoveMode::Air; }
    vehicles.clear();
    uint16_t nextId = 1;
    for (auto& sp : map.vehicleSpawns) {
        Vehicle v;
        v.id = nextId++;
        v.type = sp.type;
        v.st.pos = sp.pos;
        v.st.yaw = sp.yaw;
        // Settle onto whatever is below (terrain or a floor)
        v.st.pos.y = world.groundHeight(sp.pos + Vec3{0, 1.0f, 0}, vehicleDef(sp.type).radius * 0.6f, sp.pos.y + 1.5f);
        v.maxHp = v.hp = vehicleDef(sp.type).maxHp;
        vehicles.push_back(v);
    }
}

bool Game::seatCanShoot(const Player& p) const {
    if (!p.inVehicle()) return true;
    const Vehicle* v = findVehicle(p.vehicle);
    if (!v || p.seat >= MAX_SEATS) return true;
    return vehicleDef(v->type).seatShoot[p.seat];
}

void Game::syncSeats(Vehicle& v) {
    for (int i = 0; i < vehicleDef(v.type).seats; i++) {
        if (v.seats[i] == 0xFFFF) continue;
        auto it = players.find(v.seats[i]);
        if (it == players.end() || it->second.vehicle != v.id) { v.seats[i] = 0xFFFF; continue; }
        Player& p = it->second;
        p.move.pos = vehicleSeatPos(v.st, v.type, i);
        p.move.vel = v.st.vel;
        p.move.mode = MoveMode::Vehicle;
        p.move.onGround = v.st.onGround;
        p.move.crouched = false;
        p.move.fallStartY = p.move.pos.y;
    }
}

bool Game::enterVehicle(Player& p, Vehicle& v) {
    if (!v.alive || p.inVehicle() || !p.active() || p.dbno) return false;
    const VehicleDef& d = vehicleDef(v.type);
    int seat = -1;
    for (int i = 0; i < d.seats; i++) if (v.seats[i] == 0xFFFF) { seat = i; break; }
    if (seat < 0) return false;
    // Teams share vehicles; enemies can only take empty ones.
    for (int i = 0; i < d.seats; i++) {
        if (v.seats[i] == 0xFFFF) continue;
        auto it = players.find(v.seats[i]);
        if (it != players.end() && (cfg.teamSize <= 1 || it->second.team != p.team)) return false;
    }
    v.seats[seat] = p.id;
    p.vehicle = v.id;
    p.seat = (uint8_t)seat;
    p.buildMode = false;
    p.emote = 0;
    if (p.action == ACT_INTERACT || p.action == ACT_REVIVE || p.action == ACT_CONSUME) cancelAction(p);
    syncSeats(v);
    return true;
}

void Game::exitVehicle(Player& p) {
    if (!p.inVehicle()) return;
    Vehicle* v = nullptr;
    for (auto& x : vehicles) if (x.id == p.vehicle) v = &x;
    p.vehicle = 0xFFFF;
    uint8_t seat = p.seat;
    p.seat = NO_SEAT;
    if (!v) { p.move.mode = MoveMode::Air; return; }
    if (seat < MAX_SEATS && v->seats[seat] == p.id) v->seats[seat] = 0xFFFF;
    const VehicleDef& d = vehicleDef(v->type);
    Vec3 f = yawForward(v->st.yaw), r = yawRight(v->st.yaw);
    float side = d.radius + PLAYER_RADIUS + 0.35f;
    Vec3 seatPos = vehicleSeatPos(v->st, v->type, seat < MAX_SEATS ? seat : 0);
    float sideSign = seat < MAX_SEATS && d.seatPos[seat].x > 0.05f ? 1.0f : -1.0f;
    Vec3 cands[] = {v->st.pos + r * (side * sideSign), v->st.pos - r * (side * sideSign), v->st.pos - f * (side + 0.4f),
                    v->st.pos + f * (side + 0.4f), seatPos + Vec3{0, d.height, 0}};
    Vec3 out = cands[4];
    for (auto& c : cands) {
        Vec3 feet = c;
        feet.y = std::max(v->st.pos.y, world.groundHeight(c + Vec3{0, 1.5f, 0}, PLAYER_RADIUS, v->st.pos.y + 1.5f)) + 0.05f;
        AABB box({feet.x - PLAYER_RADIUS, feet.y + 0.1f, feet.z - PLAYER_RADIUS}, {feet.x + PLAYER_RADIUS, feet.y + PLAYER_HEIGHT, feet.z + PLAYER_RADIUS});
        if (!world.overlapsSolid(box)) { out = feet; break; }
    }
    p.move.pos = out;
    p.move.vel = v->st.vel * 0.5f + Vec3{0, 3.0f, 0};
    p.move.mode = MoveMode::Air;
    p.move.onGround = false;
    p.move.fallStartY = p.move.pos.y;
    if (v->st.pos.y < WATER_LEVEL - 0.5f && !d.hover) p.move.mode = MoveMode::Swim;
}

void Game::changeSeat(Player& p) {
    Vehicle* v = findVehicle(p.vehicle);
    if (!v) return;
    const VehicleDef& d = vehicleDef(v->type);
    for (int k = 1; k < d.seats; k++) {
        int s = (p.seat + k) % d.seats;
        if (v->seats[s] != 0xFFFF) continue;
        v->seats[p.seat] = 0xFFFF;
        v->seats[s] = p.id;
        p.seat = (uint8_t)s;
        syncSeats(*v);
        return;
    }
}

void Game::driveVehicle(Player& p, const InputCmd& in, float dt) {
    Vehicle* v = findVehicle(p.vehicle);
    if (!v) { exitVehicle(p); return; }
    if (p.seat == 0) {
        MoveInput mi;
        mi.yaw = in.yaw;
        mi.pitch = in.pitch;
        mi.fwd = in.fwd;
        mi.right = in.right;
        mi.buttons = in.buttons;
        VehicleEvents ev = stepVehicle(v->st, v->type, mi, true, world, dt, &launchPads);
        afterVehicleStep(*v, ev, dt);
        // Drivers who can't shoot honk instead
        v->honkCooldown -= dt;
        if ((in.buttons & IN_FIRE) && !vehicleDef(v->type).seatShoot[0] && v->honkCooldown <= 0) {
            effects.push_back({FX_HONK, p.id, v->st.pos + Vec3{0, 1.0f, 0}, {}, (uint8_t)v->type});
            v->honkCooldown = 0.8f;
        }
    }
    syncSeats(*v);
}

void Game::afterVehicleStep(Vehicle& v, const VehicleEvents& ev, float dt) {
    const VehicleDef& d = vehicleDef(v.type);
    v.crashCooldown -= dt;
    uint16_t driver = v.seats[0];
    if (ev.boostStarted) effects.push_back({FX_BOOST, driver, v.st.pos, {}, (uint8_t)v.type});
    if (ev.trick || ev.bailed) {
        ByteWriter w;
        w.u8(EV_TRICK);
        w.u16((uint16_t)std::min(65535, ev.trickScore));
        w.u8((uint8_t)ev.halfSpins);
        w.i8((int8_t)ev.flips);
        w.u16((uint16_t)std::min(65535.0f, ev.trickAir * 1000.0f));
        w.u8((uint8_t)ev.combo);
        w.u8(ev.bailed ? 1 : 0);
        for (auto sid : v.seats) {
            if (sid == 0xFFFF) continue;
            auto it = players.find(sid);
            if (it != players.end() && !it->second.bot) emit(w, sid);
        }
        auto dit = players.find(driver);
        if (ev.trick && ev.trickScore >= 1500 && dit != players.end()) {
            char nb[64];
            broadcastMessage(dit->second.name + " landed " + trickName(ev.halfSpins, ev.flips, ev.trickAir, nb, sizeof(nb)) + "  +" + std::to_string(ev.trickScore), 3);
        }
        if (ev.bailed) effects.push_back({FX_CRASH, driver, v.st.pos + Vec3{0, 0.8f, 0}, {}, 200});
    }
    if (ev.impact > 9.0f && v.crashCooldown <= 0) {
        effects.push_back({FX_CRASH, driver, v.st.pos + Vec3{0, 0.8f, 0}, {}, (uint8_t)std::min(255.0f, ev.impact * 4)});
        if (v.type != VehicleType::RollerBall) damageVehicle(v, (ev.impact - 8.0f) * 6.0f, 0xFFFF);
        v.crashCooldown = 0.4f;
        if (!v.alive) return;
    }
    float speed = v.st.vel.len();
    // Boosting quads plough through player builds and flimsy props
    if (d.ramBuilds && v.st.boosting && speed > 8.0f) {
        AABB hull = vehicleHull(v.st, v.type).expanded(0.5f);
        hull.max.y = v.st.pos.y + d.height + 0.5f;
        std::set<uint32_t> structs;
        std::vector<uint32_t> statics;
        world.query(hull, [&](const Shape& s) {
            if (!s.solid) return;
            if (s.structure != INVALID_ID) structs.insert(s.structure);
            else if (s.destructible && s.maxHp <= 150) statics.push_back(s.id);
        });
        Player* drv = nullptr;
        auto dit = players.find(driver);
        if (dit != players.end()) drv = &dit->second;
        for (uint32_t id : structs) destroyStructure(id);
        for (uint32_t id : statics) damageShape(id, 1000, drv, false);
    }
    // Bowling players over
    if (speed > 7.0f) {
        AABB hull = vehicleHull(v.st, v.type);
        hull.max.y = v.st.pos.y + std::max(d.hitHeight, 1.2f);
        uint8_t team = 0;
        auto dit = players.find(driver);
        if (dit != players.end()) team = dit->second.team;
        for (auto& [pid, o] : players) {
            if (!o.active() || o.inVehicle() || o.move.mode == MoveMode::OnBus) continue;
            if (driver != 0xFFFF && cfg.teamSize > 1 && o.team == team) continue;
            if (time - o.lastRamTime < 0.7f) continue;
            float dx = o.move.pos.x - v.st.pos.x, dz = o.move.pos.z - v.st.pos.z;
            float reach = d.radius + PLAYER_RADIUS;
            if (dx * dx + dz * dz > reach * reach) continue;
            if (o.move.pos.y > hull.max.y || o.move.pos.y + PLAYER_HEIGHT < v.st.pos.y) continue;
            Vec3 away = Vec3{dx, 0, dz};
            float l = away.len();
            away = l > 0.01f ? away / l : yawForward(v.st.yaw);
            Vec3 dir = (away + v.st.vel.norm()).norm();
            o.move.vel = dir * (speed * 0.7f) + Vec3{0, 7.0f + speed * 0.2f, 0};
            o.move.mode = MoveMode::Air;
            o.move.onGround = false;
            o.move.fallStartY = o.move.pos.y + 1000; // no fall damage on top of the hit
            o.lastRamTime = time;
            float dmg = std::min(80.0f, (speed - 6.0f) * (v.st.boosting ? 4.0f : 2.2f));
            if (driver != 0xFFFF) damagePlayer(o, std::round(dmg), driver, ItemType::None, 0);
            else damagePlayer(o, std::round(dmg * 0.5f), 0xFFFF, ItemType::None, 0);
            effects.push_back({FX_CRASH, driver, o.move.pos + Vec3{0, 1, 0}, {}, (uint8_t)std::min(255.0f, speed * 4)});
        }
    }
}

void Game::updateVehicles(float dt) {
    vehAccum_ += dt;
    int steps = 0;
    while (vehAccum_ >= SIM_DT) { vehAccum_ -= SIM_DT; steps++; }
    for (auto& v : vehicles) {
        if (!v.alive) continue;
        // Driver gone (disconnected, eliminated) -> free the seat
        for (int i = 0; i < MAX_SEATS; i++) {
            if (v.seats[i] == 0xFFFF) continue;
            auto it = players.find(v.seats[i]);
            if (it == players.end() || !it->second.active() || it->second.vehicle != v.id) v.seats[i] = 0xFFFF;
        }
        if (!v.hasDriver()) {
            MoveInput none;
            for (int i = 0; i < steps; i++) {
                VehicleEvents ev = stepVehicle(v.st, v.type, none, false, world, SIM_DT, &launchPads);
                afterVehicleStep(v, ev, SIM_DT);
                if (!v.alive) break;
            }
            if (v.alive) syncSeats(v);
        }
        // Sunk or fell out of the world
        if (v.alive && v.st.pos.y < -20.0f) destroyVehicle(v, 0xFFFF);
    }
}

void Game::damageVehicle(Vehicle& v, float amount, uint16_t attacker) {
    if (!v.alive || amount <= 0) return;
    // Teams don't damage their own occupied vehicles
    if (attacker != 0xFFFF && cfg.teamSize > 1) {
        auto ait = players.find(attacker);
        if (ait != players.end())
            for (auto s : v.seats)
                if (s != 0xFFFF) {
                    auto it = players.find(s);
                    if (it != players.end() && it->second.team == ait->second.team) return;
                }
    }
    v.hp -= amount;
    if (attacker != 0xFFFF) v.lastDamager = attacker;
    if (attacker != 0xFFFF) {
        auto ait = players.find(attacker);
        if (ait != players.end() && !ait->second.bot) {
            ByteWriter w;
            w.u8(EV_DAMAGE);
            w.vec3(v.st.pos + Vec3{0, vehicleDef(v.type).hitHeight + 0.6f, 0});
            w.u16((uint16_t)std::round(amount));
            w.u8(DF_STRUCTURE);
            emit(w, ait->second.id);
        }
    }
    if (v.hp <= 0) destroyVehicle(v, attacker);
}

void Game::destroyVehicle(Vehicle& v, uint16_t attacker) {
    if (!v.alive) return;
    v.alive = false;
    v.hp = 0;
    for (int i = 0; i < MAX_SEATS; i++) {
        if (v.seats[i] == 0xFFFF) continue;
        auto it = players.find(v.seats[i]);
        v.seats[i] = 0xFFFF;
        if (it == players.end()) continue;
        Player& p = it->second;
        p.vehicle = 0xFFFF;
        p.seat = NO_SEAT;
        p.move.mode = MoveMode::Air;
        p.move.vel = Vec3{0, 6.0f, 0} + v.st.vel * 0.3f;
        p.move.fallStartY = p.move.pos.y;
        if (v.st.pos.y < WATER_LEVEL - 0.5f) p.move.mode = MoveMode::Swim;
    }
    uint8_t team = 0;
    auto ait = players.find(attacker);
    if (ait != players.end()) team = ait->second.team;
    explode(v.st.pos + Vec3{0, 0.8f, 0}, 4.5f, 30.0f, 0.5f, attacker, team, ItemType::None);
}

int Game::raycastVehicles(const Vec3& o, const Vec3& d, float maxT, uint16_t ignoreVehicle, float& tOut) const {
    int best = -1;
    float bestT = maxT;
    for (size_t i = 0; i < vehicles.size(); i++) {
        const Vehicle& v = vehicles[i];
        if (!v.alive || v.id == ignoreVehicle) continue;
        if (std::fabs(v.st.pos.x - o.x) > maxT + 5 || std::fabs(v.st.pos.z - o.z) > maxT + 5) continue;
        float t;
        if (rayAABB(o, d, vehicleHull(v.st, v.type), bestT, t) && t < bestT) {
            bestT = t;
            best = (int)i;
        }
    }
    tOut = bestT;
    return best;
}

} // namespace si
