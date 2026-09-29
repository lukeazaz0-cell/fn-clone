// Backend entry point.
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "backend.h"
#include "process.h"

using namespace backend;

static Backend* gBackend = nullptr;

static void usage() {
    std::printf(
        "stormisland-backend [options]\n"
        "  --port N             HTTP port (default 8080)\n"
        "  --bind HOST          bind address (default 0.0.0.0)\n"
        "  --db PATH            SQLite database file (default stormisland.db)\n"
        "  --web DIR            admin panel directory (default <exe dir>/web)\n"
        "  --server-bin PATH    game server executable to spawn (default <exe dir>/stormisland-server)\n"
        "  --public-host HOST   address given to clients for spawned servers (default 127.0.0.1)\n"
        "  --ports A-B          UDP port range for spawned servers (default 7777-7876)\n"
        "  --secret S           shared secret for game servers (default: generated and stored in the DB)\n"
        "  --admin-user NAME    admin account name (default admin)\n"
        "  --admin-password PW  set/reset the admin password (env STORM_ADMIN_PASSWORD also works)\n"
        "  --no-spawn           never spawn game servers (only accept externally started ones)\n");
}

int main(int argc, char** argv) {
    BackendConfig c;
    std::string dir = executableDir();
    c.webDir = dir + "/web";
#ifdef _WIN32
    c.serverBinary = dir + "\\stormisland-server.exe";
#else
    c.serverBinary = dir + "/stormisland-server";
#endif
    if (const char* pw = std::getenv("STORM_ADMIN_PASSWORD")) c.adminPassword = pw;
    if (const char* s = std::getenv("STORM_SERVER_SECRET")) c.secret = s;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--port") c.httpPort = std::atoi(next().c_str());
        else if (a == "--bind") c.bindHost = next();
        else if (a == "--db") c.dbPath = next();
        else if (a == "--web") c.webDir = next();
        else if (a == "--server-bin") c.serverBinary = next();
        else if (a == "--public-host") c.publicHost = next();
        else if (a == "--ports") {
            std::string r = next();
            auto dash = r.find('-');
            if (dash != std::string::npos) { c.portMin = std::atoi(r.substr(0, dash).c_str()); c.portMax = std::atoi(r.substr(dash + 1).c_str()); }
        }
        else if (a == "--secret") c.secret = next();
        else if (a == "--admin-user") c.adminUser = next();
        else if (a == "--admin-password") c.adminPassword = next();
        else if (a == "--no-spawn") c.spawnServers = false;
        else if (a == "--help" || a == "-h") { usage(); return 0; }
        else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); usage(); return 1; }
    }
    Backend b(c);
    if (!b.init()) return 1;
    gBackend = &b;
    std::signal(SIGINT, [](int) { if (gBackend) gBackend->stop(); });
    std::signal(SIGTERM, [](int) { if (gBackend) gBackend->stop(); });
    b.run();
    b.stop();
    return 0;
}
