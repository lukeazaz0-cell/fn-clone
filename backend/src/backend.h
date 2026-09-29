// Backend service: accounts, lockers, item shop, stats, friends, matchmaking,
// game server orchestration and the admin API.
#pragma once
#include <atomic>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "db.h"
#include "nlohmann/json.hpp"
#include "process.h"

namespace httplib { class Server; struct Request; struct Response; }

namespace backend {

using json = nlohmann::json;

struct BackendConfig {
    std::string bindHost = "0.0.0.0";
    int httpPort = 8080;
    std::string dbPath = "stormisland.db";
    std::string webDir;             // admin panel files
    std::string serverBinary;       // path to stormisland-server
    std::string publicHost = "127.0.0.1"; // address handed to clients for spawned servers
    int portMin = 7777, portMax = 7876;
    std::string secret;             // shared with game servers
    std::string adminUser = "admin";
    std::string adminPassword;      // created/updated on start when set
    bool spawnServers = true;
};

struct GameServerInfo {
    std::string id;
    std::string host;
    int port = 0;
    std::string playlist;
    std::string state = "starting";
    int players = 0, humans = 0, bots = 0, alive = 0, maxPlayers = 100;
    float phaseTimer = 0;
    int stormPhase = -1;
    std::vector<std::string> playerNames;
    double lastHeartbeat = 0;
    double created = 0;
    bool managed = false;           // spawned by this backend
    ChildProcess proc;
    std::vector<json> commands;     // pending admin commands
    int reserved = 0;               // tickets handed out but not yet used
};

struct QueueEntry {
    std::string ticket;
    std::string accountId;
    std::string playlist;
    double created = 0;
    std::string status = "queued"; // queued | assigned | cancelled | failed
    std::string serverId;
    std::string matchTicket;
    std::string host;
    int port = 0;
    std::string error;
};

struct MatchTicket {
    std::string accountId;
    std::string serverId;
    double expires;
};

class Backend {
public:
    explicit Backend(const BackendConfig& c);
    ~Backend();
    bool init();
    void run();   // blocks
    void stop();

private:
    BackendConfig cfg_;
    Db db_;
    std::unique_ptr<httplib::Server> http_;
    std::mutex mu_; // guards servers_, queue_, tickets_
    std::map<std::string, GameServerInfo> servers_;
    std::map<std::string, QueueEntry> queue_;
    std::map<std::string, MatchTicket> tickets_;
    std::atomic<bool> running_{false};
    std::thread mmThread_;
    int nextServerNum_ = 1;

    void migrate();
    void seedCatalog();
    void ensureAdmin();
    void routes();
    void matchmakerLoop();
    void matchmakerTick();
    bool spawnServer(const std::string& playlist, std::string& error, std::string* idOut = nullptr);
    int allocatePort();

    // helpers
    std::string setting(const std::string& key, const std::string& def);
    void setSetting(const std::string& key, const std::string& value);
    json settingsJson();
    std::optional<Row> authAccount(const httplib::Request& req);
    bool requireAdmin(const httplib::Request& req, httplib::Response& res, std::string* adminName = nullptr);
    bool checkServerSecret(const httplib::Request& req, httplib::Response& res);
    json accountJson(const Row& r);
    json lockerJson(const std::string& accountId);
    void grantItem(const std::string& accountId, const std::string& itemId);
    void grantLevelRewards(const std::string& accountId, int level);
    std::vector<std::string> shopRotation(int64_t day);
    void adminLog(const std::string& admin, const std::string& action, const std::string& details);
    json serverJson(const GameServerInfo& s, double now);
    static double now();
};

} // namespace backend
