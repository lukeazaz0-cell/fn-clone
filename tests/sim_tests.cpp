// Headless tests: map generation, building rules, and a full bots-only match.
#include <chrono>
#include <cstdio>
#include <cstdlib>

#include "server/game.h"
#include "shared/net/snapshot.h"

using namespace si;

static int failures = 0;
#define CHECK(cond)                                                                 \
    do {                                                                            \
        if (!(cond)) {                                                              \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);             \
            failures++;                                                             \
        }                                                                           \
    } while (0)

static void testMap() {
    GameMap a, b;
    a.generate(42);
    b.generate(42);
    CHECK(a.shapes.size() == b.shapes.size());
    CHECK(a.chests.size() == b.chests.size());
    CHECK(a.shapes.size() > 5000);
    CHECK(a.chests.size() > 100);
    CHECK(a.pois.size() >= 18);
    CHECK(std::fabs(a.heightAt(700, 700) - b.heightAt(700, 700)) < 1e-6f);
    std::printf("map: %zu shapes, %zu chests, %zu floor loot, %zu decor\n", a.shapes.size(), a.chests.size(), a.floorLoot.size(),
                a.decor.size());
}

static void testBuilding() {
    GameMap m;
    m.generate(7);
    CollisionWorld w;
    w.setMap(&m);
    StructureSet s;
    Vec3 p = m.randomLandPoint(*new Rng(3));
    p.y = w.groundHeight({p.x, p.y + 5, p.z}, 0.4f, p.y + 5);
    BuildTarget floorT = computeBuildTarget(p, 0, -0.2f, PieceType::Floor, 0);
    CHECK(s.canPlace(floorT, w));
    Structure st;
    st.id = 1; st.piece = floorT.piece; st.gx = floorT.gx; st.gy = floorT.gy; st.gz = floorT.gz; st.rot = floorT.rot;
    st.maxHp = st.hp = 150;
    s.add(st, w);
    CHECK(!s.canPlace(floorT, w)); // slot taken
    // Floating floor two levels up attached to nothing -> not placeable
    BuildTarget high = floorT;
    high.gy += 6;
    CHECK(!s.canPlace(high, w));
    // Wall on top of the floor edge is supported through the floor
    BuildTarget wall;
    wall.valid = true; wall.piece = PieceType::Wall; wall.gx = floorT.gx; wall.gy = floorT.gy; wall.gz = floorT.gz; wall.rot = 0;
    CHECK(s.canPlace(wall, w));
    std::printf("building: ok\n");
}

static size_t testMatch(int bots, int teamSize) {
    Game g;
    GameConfig cfg;
    cfg.defaultBots = bots;
    cfg.minHumansToStart = 0;
    cfg.warmupSeconds = 1;
    cfg.countdownSeconds = 1;
    cfg.stormSpeed = 4.0f;
    cfg.resetAfterMatch = false;
    cfg.teamSize = teamSize;
    cfg.botDifficulty = BotDifficulty::Hard;
    g.init(cfg);
    int events = 0;
    int stormDeaths = 0, fallDeaths = 0, gunKills = 0;
    g.emitEvent = [&](const std::vector<uint8_t>& e, int) {
        events++;
        if (e.size() >= 7 && e[0] == EV_KILLFEED && !(e[6] & KF_KNOCKED)) {
            if (e[6] & KF_STORM) stormDeaths++;
            else if (e[6] & KF_FALL) fallDeaths++;
            else gunKills++;
        }
    };
    bool ended = false;
    g.onMatchEnd = [&](const std::vector<MatchResultEntry>& r) {
        ended = true;
        int winners = 0, totalKills = 0;
        for (auto& e : r) { if (e.placement == 1) winners++; totalKills += e.kills; }
        std::printf("match ended: %zu players, %d winners, %d kills\n", r.size(), winners, totalKills);
    };
    g.addBots(1); // someone must be present for warmup to progress
    auto t0 = std::chrono::steady_clock::now();
    int ticks = 0;
    int maxAliveDuringBus = 0;
    size_t peakStructures = 0;
    while (!g.wantsExit && ticks < 30 * 60 * 20) {
        g.tick(SERVER_TICK_DT);
        g.effects.clear();
        if (g.phase == MatchPhase::Bus) maxAliveDuringBus = std::max(maxAliveDuringBus, g.aliveCount());
        peakStructures = std::max(peakStructures, g.structures.byId.size());
        ticks++;
    }
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("simulated %.0f game-seconds in %.2fs (%d events, %d teams at start)\n", ticks * SERVER_TICK_DT, secs, events, g.teamsAtStart);
    std::printf("eliminations: %d combat, %d storm, %d fall\n", gunKills, stormDeaths, fallDeaths);
    std::printf("structures: peak %zu, at end %zu\n", peakStructures, g.structures.byId.size());
    CHECK(ended);
    CHECK(maxAliveDuringBus >= bots);
    return peakStructures;
}

// Drives one vehicle of each type through the real server input path: walk up, press E,
// hold forward (and boost where available), then press E again to get out.
static void testVehicles() {
    Game g;
    GameConfig cfg;
    cfg.minHumansToStart = 5;   // stay in warmup
    cfg.warmupSeconds = 1000;
    g.init(cfg);
    int counts[(int)VehicleType::Count] = {0};
    for (auto& v : g.vehicles) counts[(int)v.type]++;
    std::printf("vehicles: %zu (cart %d, quad %d, board %d, trolley %d, ball %d)\n", g.vehicles.size(), counts[0], counts[1], counts[2],
                counts[3], counts[4]);
    CHECK(g.vehicles.size() >= 40);
    for (int c : counts) CHECK(c >= 3);

    Player* p = g.addPlayer("driver", "", Loadout(), false);
    CHECK(p != nullptr);
    if (!p) return;
    uint32_t seq = 0;
    auto run = [&](int8_t fwd, uint16_t buttons, int frames, float yaw) {
        for (int i = 0; i < frames; i++) {
            InputCmd in;
            in.seq = ++seq;
            in.fwd = fwd;
            in.yaw = yaw;
            in.buttons = buttons;
            g.queueInput(*p, in);
            g.tick(SIM_DT); // one input per tick keeps the test simple
        }
    };
    for (int t = 0; t < (int)VehicleType::Count; t++) {
        Vehicle* target = nullptr;
        for (auto& v : g.vehicles)
            if ((int)v.type == t && v.alive && !v.hasDriver() && v.st.pos.y > 2.0f) { target = &v; break; }
        CHECK(target != nullptr);
        if (!target) continue;
        uint16_t vid = target->id;
        // Stand beside it facing it
        Vec3 side = target->st.pos + yawRight(target->st.yaw) * (vehicleDef(target->type).radius + 1.0f);
        p->move = MoveState();
        p->move.pos = side;
        p->move.pos.y = g.world.groundHeight(side + Vec3{0, 2, 0}, PLAYER_RADIUS, side.y + 2);
        p->move.mode = MoveMode::Ground;
        float face = yawFromDir(target->st.pos - side);
        run(0, 0, 2, face);
        run(0, IN_INTERACT, 1, face);
        run(0, 0, 1, face);
        CHECK(p->vehicle == vid);
        CHECK(p->seat == 0);
        CHECK(p->move.mode == MoveMode::Vehicle);
        Vehicle* v = g.findVehicle(vid);
        if (!v || p->vehicle != vid) continue;
        Vec3 start = v->st.pos;
        float yaw = v->st.yaw;
        uint16_t boost = vehicleDef(v->type).boostSpeed > 0 ? IN_SPRINT : 0;
        run(1, boost, 150, yaw);
        v = g.findVehicle(vid);
        float moved = v ? distXZ(v->st.pos, start) : 0;
        std::printf("  %-12s moved %.1f m in 2.5 s (hp %.0f)\n", vehicleDef((VehicleType)t).name, moved, v ? v->hp : 0.0f);
        CHECK(moved > 6.0f);
        if (v) CHECK(distXZ(p->move.pos, v->st.pos) < 2.0f); // rider stays in the seat
        run(0, 0, 40, yaw);
        run(0, IN_INTERACT, 1, yaw);
        run(0, 0, 1, yaw);
        CHECK(p->vehicle == 0xFFFF);
        CHECK(p->move.mode != MoveMode::Vehicle);
        if (v) CHECK(!v->hasDriver());
    }

    // Prediction depends on the step being deterministic
    CollisionWorld& w = g.world;
    VehicleState a;
    a.pos = g.vehicles[0].st.pos;
    a.yaw = 0.3f;
    VehicleState b = a;
    MoveInput mi;
    mi.fwd = 1;
    mi.right = 1;
    mi.buttons = IN_SPRINT;
    for (int i = 0; i < 200; i++) {
        stepVehicle(a, VehicleType::CrashQuad, mi, true, w, SIM_DT);
        stepVehicle(b, VehicleType::CrashQuad, mi, true, w, SIM_DT);
    }
    CHECK(a.pos.x == b.pos.x && a.pos.y == b.pos.y && a.pos.z == b.pos.z && a.yaw == b.yaw);

    // Snapshot round trip with vehicles and a driving player
    Snapshot snap;
    snap.hasSelf = true;
    snap.self.vehicle = 7;
    snap.self.seat = 0;
    snap.self.vehicleType = (uint8_t)VehicleType::Hoverboard;
    snap.self.veh = a;
    SnapVehicle sv;
    sv.id = 7; sv.type = 2; sv.pos = {1, 2, 3}; sv.yaw = 1.0f; sv.hp = 200; sv.flags = VF_BOOST;
    snap.vehicles.push_back(sv);
    SnapPlayer sp;
    sp.id = 3; sp.vehicle = 7; sp.seat = 1;
    snap.players.push_back(sp);
    ByteWriter bw;
    writeSnapshot(bw, snap);
    ByteReader br(bw.buf.data() + 1, bw.buf.size() - 1);
    Snapshot back;
    CHECK(readSnapshot(br, back));
    CHECK(back.vehicles.size() == 1 && back.vehicles[0].id == 7 && back.vehicles[0].hp == 200 && back.vehicles[0].flags == VF_BOOST);
    CHECK(back.self.vehicle == 7 && back.self.seat == 0 && std::fabs(back.self.veh.pos.x - a.pos.x) < 1e-4f);
    CHECK(back.players.size() == 1 && back.players[0].vehicle == 7 && back.players[0].seat == 1);
    std::printf("vehicles: ok\n");
}

// Drives a boosting quad along a raised platform, up a ramp and off the end while spinning:
// it must take off, stay airborne for a while and land a scored trick.
static void testVehicleAir() {
    GameMap m;
    m.generate(42);
    CollisionWorld w;
    w.setMap(&m);
    const float x0 = 300, z0 = 300, top = 90;
    Shape deck;
    deck.kind = ShapeKind::Box;
    deck.box = AABB({x0, top - 1, z0 - 4}, {x0 + 60, top, z0 + 4});
    w.addDynamicShape(deck);
    Shape ramp;
    ramp.kind = ShapeKind::Ramp;
    ramp.rampDir = RAMP_PX;
    ramp.box = AABB({x0 + 60, top, z0 - 4}, {x0 + 70, top + 3.5f, z0 + 4});
    w.addDynamicShape(ramp);
    VehicleState st;
    st.pos = {x0 + 3, top, z0};
    st.yaw = kPi / 2; // facing +X
    MoveInput mi;
    mi.fwd = 1;
    mi.buttons = IN_SPRINT;
    bool tookOff = false, trick = false;
    float maxY = st.pos.y, air = 0;
    int halves = 0;
    for (int i = 0; i < 60 * 12 && !trick; i++) {
        if (!st.onGround && st.pos.x > x0 + 69) { tookOff = true; mi.right = 1; mi.buttons = 0; }
        VehicleEvents ev = stepVehicle(st, VehicleType::CrashQuad, mi, true, w, SIM_DT);
        maxY = std::max(maxY, st.pos.y);
        if (ev.trick) { trick = true; air = ev.trickAir; halves = ev.halfSpins; }
    }
    std::printf("vehicle air: took off %d, peak +%.1f m over the lip, %.1f s air, %d x 180, trick %d\n", tookOff, maxY - (top + 3.5f), air, halves * 1, trick);
    CHECK(tookOff);
    CHECK(maxY > top + 3.5f + 6.0f);   // launched well above the ramp lip
    CHECK(trick);
    CHECK(air > 1.0f);
    CHECK(halves >= 2);         // spun at least 360 while airborne

    // Driving off a flat ledge must not glue the vehicle to the drop
    VehicleState ledge;
    ledge.pos = {x0 + 40, top, z0};
    ledge.yaw = -kPi / 2; // facing -X, toward the deck's start edge
    MoveInput go;
    go.fwd = 1;
    bool airborne = false;
    for (int i = 0; i < 60 * 4; i++) {
        stepVehicle(ledge, VehicleType::GolfCart, go, true, w, SIM_DT);
        if (!ledge.onGround && ledge.pos.x < x0) { airborne = true; break; }
    }
    CHECK(airborne);
}

int main() {
    testMap();
    testBuilding();
    testVehicles();
    testVehicleAir();
    // Bots should have built something at some point. Checked over both matches: short
    // storm-compressed matches can legitimately see no fights that trigger building.
    size_t built = testMatch(30, 1) + testMatch(24, 2);
    CHECK(built > 0);
    if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::printf("all tests passed\n");
    return 0;
}
