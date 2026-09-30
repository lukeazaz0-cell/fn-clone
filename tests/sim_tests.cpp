// Headless tests: map generation, building rules, and a full bots-only match.
#include <chrono>
#include <cstdio>
#include <cstdlib>

#include "server/game.h"

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

int main() {
    testMap();
    testBuilding();
    // Bots should have built something at some point. Checked over both matches: short
    // storm-compressed matches can legitimately see no fights that trigger building.
    size_t built = testMatch(30, 1) + testMatch(24, 2);
    CHECK(built > 0);
    if (failures) { std::printf("%d failure(s)\n", failures); return 1; }
    std::printf("all tests passed\n");
    return 0;
}
