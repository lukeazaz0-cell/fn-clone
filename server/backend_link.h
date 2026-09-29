// HTTP link between a dedicated game server and the backend: registration,
// heartbeats (which carry admin commands + settings back), ticket checks and results.
#pragma once
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "game.h"
#include "netserver.h"

namespace si {

struct AdminCommand {
    std::string type;   // add_bots, remove_bots, start_now, end_match, kick, message, pause_storm
    int count = 0;
    int playerId = 0;
    std::string text;
    bool flag = false;
};

struct ServerStatus {
    std::string state;
    int players = 0, humans = 0, bots = 0, alive = 0;
    int maxPlayers = 0;
    std::string playlist;
    float phaseTimer = 0;
    int stormPhase = -1;
    std::vector<std::string> playerNames;
};

struct BackendSettings {
    bool received = false;
    int defaultBots = 20;
    std::string botDifficulty = "medium";
    float stormSpeed = 1.0f;
    float warmupSeconds = 30.0f;
    int minPlayers = 1;
    bool fillBots = false;
};

class BackendLink {
public:
    BackendLink(std::string url, std::string secret, std::string serverId, std::string publicHost, uint16_t port);
    ~BackendLink();
    bool registerServer(const std::string& playlist, int maxPlayers);
    void start();
    void stop();
    void setStatus(const ServerStatus& s);
    std::vector<AdminCommand> takeCommands();
    BackendSettings settings();
    TicketInfo validate(const std::string& ticket);
    void reportMatch(const std::vector<MatchResultEntry>& results, const std::string& playlist);
    const std::string& serverId() const { return serverId_; }

private:
    std::string url_, secret_, serverId_, host_;
    uint16_t port_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::mutex mu_;
    ServerStatus status_;
    std::vector<AdminCommand> commands_;
    BackendSettings settings_;
    void loop();
    void heartbeat();
};

} // namespace si
