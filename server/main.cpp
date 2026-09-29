// Dedicated game server entry point.
#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

#include "runner.h"

using namespace si;

static std::atomic<bool> gStop{false};

static void usage() {
    std::printf(
        "stormisland-server [options]\n"
        "  --port N              UDP port (default 7777)\n"
        "  --playlist NAME       solo | duos | squads (default solo)\n"
        "  --bots N              default bots added when a match starts (default 20)\n"
        "  --difficulty D        easy | medium | hard | insane\n"
        "  --max-players N       (default 100)\n"
        "  --min-players N       humans required before warmup countdown (default 1)\n"
        "  --warmup SECONDS      warmup length once enough players joined (default 30)\n"
        "  --seed N              map seed (default 1337)\n"
        "  --backend URL         backend base URL, e.g. http://127.0.0.1:8080 (enables tickets)\n"
        "  --secret S            shared secret for the backend internal API\n"
        "  --server-id ID        id assigned by the backend when it spawns this server\n"
        "  --public-host HOST    address clients should use to reach this server\n"
        "  --exit-after-match    exit when the match ends instead of returning to warmup\n");
}

int main(int argc, char** argv) {
    RunnerOptions o;
    o.game.mapSeed = 1337;
    const char* envSecret = std::getenv("STORM_SERVER_SECRET");
    if (envSecret) o.secret = envSecret;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--port") o.port = (uint16_t)std::atoi(next().c_str());
        else if (a == "--playlist") o.game.playlist = next();
        else if (a == "--bots") o.game.defaultBots = std::atoi(next().c_str());
        else if (a == "--difficulty") o.game.botDifficulty = parseDifficulty(next());
        else if (a == "--max-players") o.game.maxPlayers = std::max(1, std::min(MAX_PLAYERS, std::atoi(next().c_str())));
        else if (a == "--min-players") o.game.minHumansToStart = std::atoi(next().c_str());
        else if (a == "--warmup") o.game.warmupSeconds = (float)std::atof(next().c_str());
        else if (a == "--seed") o.game.mapSeed = (uint32_t)std::strtoul(next().c_str(), nullptr, 10);
        else if (a == "--backend") o.backendUrl = next();
        else if (a == "--secret") o.secret = next();
        else if (a == "--server-id") o.serverId = next();
        else if (a == "--public-host") o.publicHost = next();
        else if (a == "--exit-after-match") o.game.resetAfterMatch = false;
        else if (a == "--help" || a == "-h") { usage(); return 0; }
        else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); usage(); return 1; }
    }
    if (o.game.playlist == "duos") o.game.teamSize = 2;
    else if (o.game.playlist == "squads") o.game.teamSize = 4;
    else o.game.teamSize = 1;

    std::signal(SIGINT, [](int) { gStop = true; });
    std::signal(SIGTERM, [](int) { gStop = true; });

    ServerRunner runner(o);
    if (!runner.init()) return 1;
    runner.run(gStop);
    return 0;
}
