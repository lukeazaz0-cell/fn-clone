// Runs a game server loop (used by the dedicated server and by the client's offline mode).
#pragma once
#include <atomic>
#include <memory>
#include <string>

#include "backend_link.h"
#include "game.h"
#include "netserver.h"

namespace si {

struct RunnerOptions {
    GameConfig game;
    uint16_t port = 7777;
    std::string backendUrl;   // empty = standalone (no tickets required)
    std::string secret;
    std::string serverId;
    std::string publicHost = "127.0.0.1";
    bool verbose = true;
};

class ServerRunner {
public:
    explicit ServerRunner(const RunnerOptions& o);
    bool init();
    // Blocks until `stop` becomes true or the match asks the server to exit.
    void run(std::atomic<bool>& stop);
    uint16_t port() const { return net_.port(); }
    Game& game() { return game_; }

private:
    RunnerOptions opt_;
    Game game_;
    NetServer net_;
    std::unique_ptr<BackendLink> link_;
    void applyBackend();
    void publishStatus();
    void logLine(const std::string& s);
};

BotDifficulty parseDifficulty(const std::string& s);

} // namespace si
