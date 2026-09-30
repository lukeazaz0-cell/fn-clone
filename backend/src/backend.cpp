#include "backend.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <set>
#include <sstream>

#include "crypto.h"
#include "httplib.h"
#include "shared/game/cosmetics.h"

namespace backend {

namespace {

constexpr double HEARTBEAT_TIMEOUT = 15.0;
constexpr double TICKET_TTL = 90.0;
constexpr int SESSION_DAYS = 30;

struct PlaylistDef { const char* id; const char* name; int teamSize; };
const PlaylistDef kPlaylists[] = {{"solo", "Solo", 1}, {"duos", "Duos", 2}, {"squads", "Squads", 4}};

bool validPlaylist(const std::string& p) {
    for (auto& d : kPlaylists) if (p == d.id) return true;
    return false;
}

void sendJson(httplib::Response& res, const json& j, int status = 200) {
    res.status = status;
    res.set_content(j.dump(), "application/json");
}
void sendError(httplib::Response& res, int status, const std::string& msg) { sendJson(res, {{"error", msg}}, status); }

bool parseBody(const httplib::Request& req, httplib::Response& res, json& out) {
    out = json::parse(req.body.empty() ? "{}" : req.body, nullptr, false);
    if (out.is_discarded() || !out.is_object()) {
        sendError(res, 400, "invalid JSON body");
        return false;
    }
    return true;
}

std::string bearer(const httplib::Request& req) {
    std::string h = req.get_header_value("Authorization");
    if (h.rfind("Bearer ", 0) == 0) return h.substr(7);
    return "";
}

bool validUsername(const std::string& u) {
    if (u.size() < 3 || u.size() > 16) return false;
    for (char c : u)
        if (!(std::isalnum((unsigned char)c) || c == '_' || c == '-')) return false;
    return true;
}

int levelForXp(int64_t xp) { return 1 + (int)(xp / 1000); }

std::string slotColumn(si::CosmeticType t) {
    switch (t) {
        case si::CosmeticType::Outfit: return "outfit";
        case si::CosmeticType::BackBling: return "backbling";
        case si::CosmeticType::Pickaxe: return "pickaxe";
        case si::CosmeticType::Glider: return "glider";
        case si::CosmeticType::Contrail: return "contrail";
        default: return "";
    }
}

json colorJson(const si::Color4& c) { return json::array({c.r, c.g, c.b, c.a}); }

} // namespace

double Backend::now() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

Backend::Backend(const BackendConfig& c) : cfg_(c) {}
Backend::~Backend() { stop(); }

bool Backend::init() {
    if (!db_.open(cfg_.dbPath)) {
        std::fprintf(stderr, "cannot open database %s: %s\n", cfg_.dbPath.c_str(), db_.lastError().c_str());
        return false;
    }
    migrate();
    seedCatalog();
    ensureAdmin();
    if (cfg_.secret.empty()) {
        cfg_.secret = setting("server_secret", "");
        if (cfg_.secret.empty()) {
            cfg_.secret = randomToken(24);
            setSetting("server_secret", cfg_.secret);
        }
    }
    http_ = std::make_unique<httplib::Server>();
    routes();
    return true;
}

void Backend::migrate() {
    const char* stmts[] = {
        "CREATE TABLE IF NOT EXISTS accounts ("
        " id TEXT PRIMARY KEY, username TEXT UNIQUE COLLATE NOCASE NOT NULL, display_name TEXT NOT NULL,"
        " pass_hash TEXT NOT NULL, created_at INTEGER NOT NULL, is_admin INTEGER DEFAULT 0, banned INTEGER DEFAULT 0,"
        " coins INTEGER DEFAULT 1000, xp INTEGER DEFAULT 0, level INTEGER DEFAULT 1, wins INTEGER DEFAULT 0,"
        " kills INTEGER DEFAULT 0, matches INTEGER DEFAULT 0, top10 INTEGER DEFAULT 0, damage REAL DEFAULT 0,"
        " last_login INTEGER DEFAULT 0)",
        "CREATE TABLE IF NOT EXISTS sessions (token TEXT PRIMARY KEY, account_id TEXT NOT NULL, created_at INTEGER, expires_at INTEGER)",
        "CREATE TABLE IF NOT EXISTS cosmetics (id TEXT PRIMARY KEY, name TEXT, type TEXT, rarity INTEGER, price INTEGER, unlock_level INTEGER)",
        "CREATE TABLE IF NOT EXISTS owned_items (account_id TEXT NOT NULL, item_id TEXT NOT NULL, acquired_at INTEGER,"
        " PRIMARY KEY(account_id, item_id))",
        "CREATE TABLE IF NOT EXISTS lockers (account_id TEXT PRIMARY KEY, outfit TEXT, backbling TEXT, pickaxe TEXT,"
        " glider TEXT, contrail TEXT, emotes TEXT)",
        "CREATE TABLE IF NOT EXISTS matches (id INTEGER PRIMARY KEY AUTOINCREMENT, server_id TEXT, playlist TEXT,"
        " ended_at INTEGER, players INTEGER, winner TEXT)",
        "CREATE TABLE IF NOT EXISTS match_players (match_id INTEGER, account_id TEXT, name TEXT, bot INTEGER, placement INTEGER,"
        " kills INTEGER, damage REAL, survived REAL)",
        "CREATE TABLE IF NOT EXISTS settings (key TEXT PRIMARY KEY, value TEXT)",
        "CREATE TABLE IF NOT EXISTS friends (account_id TEXT, friend_id TEXT, status TEXT, created_at INTEGER,"
        " PRIMARY KEY(account_id, friend_id))",
        "CREATE TABLE IF NOT EXISTS admin_log (id INTEGER PRIMARY KEY AUTOINCREMENT, at INTEGER, admin TEXT, action TEXT, details TEXT)",
        "CREATE TABLE IF NOT EXISTS purchases (id INTEGER PRIMARY KEY AUTOINCREMENT, account_id TEXT, item_id TEXT, price INTEGER, at INTEGER)",
        "CREATE INDEX IF NOT EXISTS idx_mp_account ON match_players(account_id)",
        "CREATE INDEX IF NOT EXISTS idx_sessions_account ON sessions(account_id)",
    };
    for (auto s : stmts) db_.exec(s);
    // Default settings
    auto def = [&](const char* k, const char* v) { db_.exec("INSERT OR IGNORE INTO settings(key, value) VALUES(?, ?)", {std::string(k), std::string(v)}); };
    def("default_bots", "20");
    def("bot_difficulty", "medium");
    def("storm_speed", "1.0");
    def("warmup_seconds", "30");
    def("min_players", "1");
    def("fill_bots", "0");
    def("max_servers", "8");
    def("max_players_per_server", "100");
    def("motd", "Welcome to Storm Island! Drop in, loot up and be the last one standing.");
    def("matchmaking_enabled", "1");
}

void Backend::seedCatalog() {
    for (auto& c : si::cosmeticCatalog()) {
        db_.exec("INSERT INTO cosmetics(id, name, type, rarity, price, unlock_level) VALUES(?,?,?,?,?,?) "
                 "ON CONFLICT(id) DO UPDATE SET name=excluded.name, type=excluded.type, rarity=excluded.rarity, "
                 "price=excluded.price, unlock_level=excluded.unlock_level",
                 {c.id, c.name, std::string(si::COSMETIC_TYPE_NAMES[(int)c.type]), (int64_t)c.rarity, (int64_t)c.price, (int64_t)c.unlockLevel});
    }
    // Everyone owns the free default cosmetics, including ones added in later versions.
    for (auto& c : si::cosmeticCatalog())
        if (c.price == 0 && c.unlockLevel == 0)
            db_.exec("INSERT OR IGNORE INTO owned_items(account_id, item_id, acquired_at) SELECT id, ?, ? FROM accounts", {c.id, (int64_t)std::time(nullptr)});
}

std::string Backend::setting(const std::string& key, const std::string& def) {
    auto r = db_.one("SELECT value FROM settings WHERE key=?", {key});
    return r ? asString((*r)["value"]) : def;
}

void Backend::setSetting(const std::string& key, const std::string& value) {
    db_.exec("INSERT INTO settings(key, value) VALUES(?, ?) ON CONFLICT(key) DO UPDATE SET value=excluded.value", {key, value});
}

json Backend::settingsJson() {
    json j;
    j["default_bots"] = std::atoi(setting("default_bots", "20").c_str());
    j["bot_difficulty"] = setting("bot_difficulty", "medium");
    j["storm_speed"] = std::atof(setting("storm_speed", "1.0").c_str());
    j["warmup_seconds"] = std::atof(setting("warmup_seconds", "30").c_str());
    j["min_players"] = std::atoi(setting("min_players", "1").c_str());
    j["fill_bots"] = setting("fill_bots", "0") == "1";
    j["max_servers"] = std::atoi(setting("max_servers", "8").c_str());
    j["max_players_per_server"] = std::atoi(setting("max_players_per_server", "100").c_str());
    j["motd"] = setting("motd", "");
    j["matchmaking_enabled"] = setting("matchmaking_enabled", "1") == "1";
    return j;
}

void Backend::grantItem(const std::string& accountId, const std::string& itemId) {
    db_.exec("INSERT OR IGNORE INTO owned_items(account_id, item_id, acquired_at) VALUES(?,?,?)", {accountId, itemId, (int64_t)std::time(nullptr)});
}

void Backend::grantLevelRewards(const std::string& accountId, int level) {
    for (auto& c : si::cosmeticCatalog())
        if (c.unlockLevel > 0 && c.unlockLevel <= level) grantItem(accountId, c.id);
}

void Backend::ensureAdmin() {
    std::string pw = cfg_.adminPassword;
    auto existing = db_.one("SELECT id FROM accounts WHERE username=?", {cfg_.adminUser});
    if (existing) {
        if (!pw.empty()) db_.exec("UPDATE accounts SET pass_hash=?, is_admin=1 WHERE id=?", {hashPassword(pw), asString((*existing)["id"])});
        return;
    }
    bool generated = false;
    if (pw.empty()) { pw = randomToken(6); generated = true; }
    std::string id = randomToken(8);
    db_.exec("INSERT INTO accounts(id, username, display_name, pass_hash, created_at, is_admin, coins) VALUES(?,?,?,?,?,1,100000)",
             {id, cfg_.adminUser, std::string("Admin"), hashPassword(pw), (int64_t)std::time(nullptr)});
    for (auto& c : si::cosmeticCatalog()) if (c.price == 0 && c.unlockLevel == 0) grantItem(id, c.id);
    db_.exec("INSERT OR IGNORE INTO lockers(account_id) VALUES(?)", {id});
    std::printf("==============================================================\n");
    std::printf(" Created admin account '%s'%s\n", cfg_.adminUser.c_str(), generated ? "" : " (password from --admin-password)");
    if (generated) std::printf(" Generated password: %s   (change it with --admin-password)\n", pw.c_str());
    std::printf("==============================================================\n");
}

std::optional<Row> Backend::authAccount(const httplib::Request& req) {
    std::string tok = bearer(req);
    if (tok.empty()) return std::nullopt;
    auto r = db_.one("SELECT a.* FROM sessions s JOIN accounts a ON a.id = s.account_id WHERE s.token=? AND s.expires_at > ?",
                     {tok, (int64_t)std::time(nullptr)});
    if (r && asInt((*r)["banned"])) return std::nullopt;
    return r;
}

bool Backend::requireAdmin(const httplib::Request& req, httplib::Response& res, std::string* adminName) {
    auto acc = authAccount(req);
    if (!acc) { sendError(res, 401, "not logged in"); return false; }
    if (!asInt((*acc)["is_admin"])) { sendError(res, 403, "admin only"); return false; }
    if (adminName) *adminName = asString((*acc)["username"]);
    return true;
}

bool Backend::checkServerSecret(const httplib::Request& req, httplib::Response& res) {
    if (!constantTimeEquals(req.get_header_value("X-Server-Secret"), cfg_.secret)) {
        sendError(res, 403, "bad server secret");
        return false;
    }
    return true;
}

void Backend::adminLog(const std::string& admin, const std::string& action, const std::string& details) {
    db_.exec("INSERT INTO admin_log(at, admin, action, details) VALUES(?,?,?,?)", {(int64_t)std::time(nullptr), admin, action, details});
}

json Backend::accountJson(const Row& r0) {
    Row r = r0;
    int64_t xp = asInt(r["xp"]);
    int level = levelForXp(xp);
    return {{"id", asString(r["id"])},
            {"username", asString(r["username"])},
            {"display_name", asString(r["display_name"])},
            {"is_admin", asInt(r["is_admin"]) != 0},
            {"coins", asInt(r["coins"])},
            {"xp", xp},
            {"level", level},
            {"level_progress", (double)(xp % 1000) / 1000.0},
            {"stats", {{"wins", asInt(r["wins"])}, {"kills", asInt(r["kills"])}, {"matches", asInt(r["matches"])},
                       {"top10", asInt(r["top10"])}, {"damage", asDouble(r["damage"])}}}};
}

json Backend::lockerJson(const std::string& accountId) {
    auto r = db_.one("SELECT * FROM lockers WHERE account_id=?", {accountId});
    si::Loadout def;
    json eq;
    auto get = [&](const char* col, const std::string& d) {
        if (!r) return d;
        std::string v = asString((*r)[col]);
        return v.empty() ? d : v;
    };
    eq["outfit"] = get("outfit", def.outfit);
    eq["backbling"] = get("backbling", def.backbling);
    eq["pickaxe"] = get("pickaxe", def.pickaxe);
    eq["glider"] = get("glider", def.glider);
    eq["contrail"] = get("contrail", def.contrail);
    json emotes = json::array();
    for (auto& e : def.emotes) emotes.push_back(e);
    if (r) {
        json parsed = json::parse(asString((*r)["emotes"]), nullptr, false);
        if (parsed.is_array() && parsed.size() == 6) emotes = parsed;
    }
    eq["emotes"] = emotes;
    json owned = json::array();
    for (auto& row : db_.query("SELECT item_id FROM owned_items WHERE account_id=?", {accountId})) owned.push_back(asString(row.at("item_id")));
    return {{"equipped", eq}, {"owned", owned}};
}

std::vector<std::string> Backend::shopRotation(int64_t day) {
    std::vector<const si::CosmeticDef*> pool;
    for (auto& c : si::cosmeticCatalog())
        if (c.price > 0) pool.push_back(&c);
    si::Rng rng((uint64_t)day * 7919u + 17u, 5);
    for (size_t i = pool.size(); i > 1; i--) std::swap(pool[i - 1], pool[rng.next() % i]);
    std::vector<std::string> ids;
    for (size_t i = 0; i < pool.size() && i < 8; i++) ids.push_back(pool[i]->id);
    return ids;
}

json Backend::serverJson(const GameServerInfo& s, double t) {
    return {{"id", s.id}, {"host", s.host}, {"port", s.port}, {"playlist", s.playlist}, {"state", s.state},
            {"players", s.players}, {"humans", s.humans}, {"bots", s.bots}, {"alive", s.alive}, {"max_players", s.maxPlayers},
            {"phase_timer", s.phaseTimer}, {"storm_phase", s.stormPhase}, {"player_names", s.playerNames},
            {"managed", s.managed}, {"reserved", s.reserved}, {"uptime", t - s.created},
            {"last_heartbeat", s.lastHeartbeat > 0 ? t - s.lastHeartbeat : -1}, {"pending_commands", s.commands.size()}};
}

int Backend::allocatePort() {
    std::set<int> used;
    for (auto& [id, s] : servers_) used.insert(s.port);
    for (int p = cfg_.portMin; p <= cfg_.portMax; p++)
        if (!used.count(p)) return p;
    return -1;
}

bool Backend::spawnServer(const std::string& playlist, std::string& error, std::string* idOut) {
    // mu_ must be held by the caller
    if (!cfg_.spawnServers) { error = "server spawning disabled"; return false; }
    if (cfg_.serverBinary.empty() || !fileExists(cfg_.serverBinary)) { error = "game server binary not found: " + cfg_.serverBinary; return false; }
    int maxServers = std::atoi(setting("max_servers", "8").c_str());
    int managedCount = 0;
    for (auto& [id, s] : servers_) if (s.managed) managedCount++;
    if (managedCount >= maxServers) { error = "max_servers reached"; return false; }
    int port = allocatePort();
    if (port < 0) { error = "no free ports"; return false; }
    std::string id = "gs-" + std::to_string(nextServerNum_++) + "-" + randomToken(3);
    std::vector<std::string> args = {
        "--port", std::to_string(port),
        "--playlist", playlist,
        "--backend", "http://127.0.0.1:" + std::to_string(cfg_.httpPort),
        "--server-id", id,
        "--public-host", cfg_.publicHost,
        "--max-players", setting("max_players_per_server", "100"),
        "--seed", std::to_string((uint32_t)std::time(nullptr) % 100000),
        "--exit-after-match",
    };
    GameServerInfo info;
    info.id = id;
    info.host = cfg_.publicHost;
    info.port = port;
    info.playlist = playlist;
    info.managed = true;
    info.created = now();
    info.lastHeartbeat = now(); // grace period for startup
    if (!spawnProcess(cfg_.serverBinary, args, {{"STORM_SERVER_SECRET", cfg_.secret}}, info.proc, error)) return false;
    servers_[id] = info;
    if (idOut) *idOut = id;
    std::printf("[backend] spawned game server %s (%s) on port %d\n", id.c_str(), playlist.c_str(), port);
    return true;
}

// ================================================================== matchmaking

void Backend::matchmakerLoop() {
    while (running_) {
        matchmakerTick();
        for (int i = 0; i < 5 && running_; i++) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void Backend::matchmakerTick() {
    std::lock_guard<std::mutex> lk(mu_);
    double t = now();
    // Expire servers & tickets
    for (auto it = servers_.begin(); it != servers_.end();) {
        GameServerInfo& s = it->second;
        bool dead = t - s.lastHeartbeat > HEARTBEAT_TIMEOUT;
        if (s.managed && !processAlive(s.proc)) dead = true;
        if (dead) {
            std::printf("[backend] game server %s went away\n", s.id.c_str());
            if (s.managed) killProcess(s.proc);
            it = servers_.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = tickets_.begin(); it != tickets_.end();) {
        if (it->second.expires < t) {
            auto sit = servers_.find(it->second.serverId);
            if (sit != servers_.end()) sit->second.reserved = std::max(0, sit->second.reserved - 1);
            it = tickets_.erase(it);
        } else {
            ++it;
        }
    }
    for (auto it = queue_.begin(); it != queue_.end();) {
        if (it->second.status != "queued" && t - it->second.created > 600) it = queue_.erase(it);
        else ++it;
    }
    if (setting("matchmaking_enabled", "1") != "1") return;

    std::map<std::string, int> waitingPerPlaylist;
    for (auto& [tk, q] : queue_) {
        if (q.status != "queued") continue;
        // Find an open server
        GameServerInfo* best = nullptr;
        for (auto& [id, s] : servers_) {
            if (s.playlist != q.playlist) continue;
            if (s.state != "warmup" && s.state != "countdown") continue;
            if (s.players + s.reserved >= s.maxPlayers) continue;
            if (!best || s.humans + s.reserved > best->humans + best->reserved) best = &s; // pack players together
        }
        if (best) {
            MatchTicket mt;
            mt.accountId = q.accountId;
            mt.serverId = best->id;
            mt.expires = t + TICKET_TTL;
            std::string mtk = randomToken(20);
            tickets_[mtk] = mt;
            best->reserved++;
            q.status = "assigned";
            q.serverId = best->id;
            q.matchTicket = mtk;
            q.host = best->host;
            q.port = best->port;
        } else {
            waitingPerPlaylist[q.playlist]++;
        }
    }
    // Spawn servers for playlists that have waiting players and nothing starting up.
    for (auto& [playlist, n] : waitingPerPlaylist) {
        bool starting = false;
        for (auto& [id, s] : servers_)
            if (s.playlist == playlist && s.state == "starting") starting = true;
        if (starting) continue;
        std::string err;
        if (!spawnServer(playlist, err)) {
            for (auto& [tk, q] : queue_)
                if (q.status == "queued" && q.playlist == playlist && t - q.created > 20) {
                    q.status = "failed";
                    q.error = "No game servers available (" + err + ")";
                }
        }
    }
}

// ================================================================== routes

void Backend::routes() {
    auto& S = *http_;

    S.set_default_headers({{"Access-Control-Allow-Origin", "*"},
                           {"Access-Control-Allow-Headers", "Authorization, Content-Type"},
                           {"Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS"}});
    S.Options(R"(.*)", [](const httplib::Request&, httplib::Response& res) { res.status = 204; });

    S.Get("/", [](const httplib::Request&, httplib::Response& res) { res.set_redirect("/admin"); });
    S.Get("/health", [](const httplib::Request&, httplib::Response& res) { sendJson(res, {{"ok", true}}); });

    // ---------------- static admin panel
    S.Get("/admin", [this](const httplib::Request&, httplib::Response& res) {
        std::ifstream f(cfg_.webDir + "/admin.html", std::ios::binary);
        if (!f) { res.status = 404; res.set_content("admin.html not found in " + cfg_.webDir, "text/plain"); return; }
        std::stringstream ss;
        ss << f.rdbuf();
        res.set_content(ss.str(), "text/html; charset=utf-8");
    });

    // ---------------- accounts
    S.Post("/api/register", [this](const httplib::Request& req, httplib::Response& res) {
        json b;
        if (!parseBody(req, res, b)) return;
        std::string user = b.value("username", ""), pass = b.value("password", ""), display = b.value("display_name", "");
        if (!validUsername(user)) return sendError(res, 400, "username must be 3-16 characters: letters, digits, _ or -");
        if (pass.size() < 6 || pass.size() > 128) return sendError(res, 400, "password must be 6-128 characters");
        if (display.empty()) display = user;
        if (display.size() > 20) display.resize(20);
        if (db_.one("SELECT id FROM accounts WHERE username=?", {user})) return sendError(res, 409, "username already taken");
        std::string id = randomToken(8);
        if (!db_.exec("INSERT INTO accounts(id, username, display_name, pass_hash, created_at) VALUES(?,?,?,?,?)",
                      {id, user, display, hashPassword(pass), (int64_t)std::time(nullptr)}))
            return sendError(res, 500, "could not create account");
        for (auto& c : si::cosmeticCatalog()) if (c.price == 0 && c.unlockLevel == 0) grantItem(id, c.id);
        db_.exec("INSERT OR IGNORE INTO lockers(account_id) VALUES(?)", {id});
        std::string tok = randomToken(24);
        db_.exec("INSERT INTO sessions(token, account_id, created_at, expires_at) VALUES(?,?,?,?)",
                 {tok, id, (int64_t)std::time(nullptr), (int64_t)std::time(nullptr) + SESSION_DAYS * 86400});
        auto acc = db_.one("SELECT * FROM accounts WHERE id=?", {id});
        sendJson(res, {{"token", tok}, {"account", accountJson(*acc)}});
    });

    S.Post("/api/login", [this](const httplib::Request& req, httplib::Response& res) {
        json b;
        if (!parseBody(req, res, b)) return;
        auto acc = db_.one("SELECT * FROM accounts WHERE username=?", {b.value("username", "")});
        if (!acc || !verifyPassword(b.value("password", ""), asString((*acc)["pass_hash"]))) return sendError(res, 401, "invalid username or password");
        if (asInt((*acc)["banned"])) return sendError(res, 403, "this account is banned");
        std::string id = asString((*acc)["id"]);
        std::string tok = randomToken(24);
        db_.exec("INSERT INTO sessions(token, account_id, created_at, expires_at) VALUES(?,?,?,?)",
                 {tok, id, (int64_t)std::time(nullptr), (int64_t)std::time(nullptr) + SESSION_DAYS * 86400});
        db_.exec("UPDATE accounts SET last_login=? WHERE id=?", {(int64_t)std::time(nullptr), id});
        db_.exec("DELETE FROM sessions WHERE expires_at < ?", {(int64_t)std::time(nullptr)});
        sendJson(res, {{"token", tok}, {"account", accountJson(*acc)}});
    });

    S.Post("/api/logout", [this](const httplib::Request& req, httplib::Response& res) {
        db_.exec("DELETE FROM sessions WHERE token=?", {bearer(req)});
        sendJson(res, {{"ok", true}});
    });

    S.Get("/api/me", [this](const httplib::Request& req, httplib::Response& res) {
        auto acc = authAccount(req);
        if (!acc) return sendError(res, 401, "not logged in");
        sendJson(res, accountJson(*acc));
    });

    S.Put("/api/me", [this](const httplib::Request& req, httplib::Response& res) {
        auto acc = authAccount(req);
        if (!acc) return sendError(res, 401, "not logged in");
        json b;
        if (!parseBody(req, res, b)) return;
        std::string display = b.value("display_name", "");
        if (display.empty() || display.size() > 20) return sendError(res, 400, "display name must be 1-20 characters");
        db_.exec("UPDATE accounts SET display_name=? WHERE id=?", {display, asString((*acc)["id"])});
        sendJson(res, accountJson(*db_.one("SELECT * FROM accounts WHERE id=?", {asString((*acc)["id"])})));
    });

    S.Get("/api/news", [this](const httplib::Request&, httplib::Response& res) { sendJson(res, {{"motd", setting("motd", "")}}); });

    // ---------------- catalog & locker
    S.Get("/api/catalog", [](const httplib::Request&, httplib::Response& res) {
        json arr = json::array();
        for (auto& c : si::cosmeticCatalog()) {
            arr.push_back({{"id", c.id}, {"name", c.name}, {"type", si::COSMETIC_TYPE_NAMES[(int)c.type]},
                           {"rarity", si::RARITY_NAMES[(int)c.rarity]}, {"price", c.price}, {"unlock_level", c.unlockLevel},
                           {"style", {{"primary", colorJson(c.style.primary)}, {"secondary", colorJson(c.style.secondary)},
                                      {"accent", colorJson(c.style.accent)}, {"shape", c.style.shape}}}});
        }
        sendJson(res, arr);
    });

    S.Get("/api/locker", [this](const httplib::Request& req, httplib::Response& res) {
        auto acc = authAccount(req);
        if (!acc) return sendError(res, 401, "not logged in");
        sendJson(res, lockerJson(asString((*acc)["id"])));
    });

    S.Put("/api/locker", [this](const httplib::Request& req, httplib::Response& res) {
        auto acc = authAccount(req);
        if (!acc) return sendError(res, 401, "not logged in");
        json b;
        if (!parseBody(req, res, b)) return;
        std::string id = asString((*acc)["id"]);
        std::string item = b.value("item_id", "");
        const si::CosmeticDef* def = si::findCosmetic(item);
        if (!def) return sendError(res, 404, "unknown item");
        if (!db_.one("SELECT 1 AS x FROM owned_items WHERE account_id=? AND item_id=?", {id, item})) return sendError(res, 403, "you don't own this item");
        db_.exec("INSERT OR IGNORE INTO lockers(account_id) VALUES(?)", {id});
        if (def->type == si::CosmeticType::Emote) {
            int slot = b.value("slot", 0);
            if (slot < 0 || slot > 5) return sendError(res, 400, "emote slot must be 0-5");
            json lj = lockerJson(id);
            json emotes = lj["equipped"]["emotes"];
            emotes[slot] = item;
            db_.exec("UPDATE lockers SET emotes=? WHERE account_id=?", {emotes.dump(), id});
        } else {
            std::string col = slotColumn(def->type);
            db_.exec("UPDATE lockers SET " + col + "=? WHERE account_id=?", {item, id});
        }
        sendJson(res, lockerJson(id));
    });

    // ---------------- item shop
    S.Get("/api/shop", [this](const httplib::Request& req, httplib::Response& res) {
        auto acc = authAccount(req);
        int64_t day = (int64_t)std::time(nullptr) / 86400;
        int64_t resetIn = (day + 1) * 86400 - (int64_t)std::time(nullptr);
        std::set<std::string> owned;
        if (acc)
            for (auto& r : db_.query("SELECT item_id FROM owned_items WHERE account_id=?", {asString((*acc)["id"])})) owned.insert(asString(r.at("item_id")));
        json items = json::array();
        auto ids = shopRotation(day);
        for (size_t i = 0; i < ids.size(); i++) {
            const si::CosmeticDef* c = si::findCosmetic(ids[i]);
            if (!c) continue;
            items.push_back({{"id", c->id}, {"name", c->name}, {"type", si::COSMETIC_TYPE_NAMES[(int)c->type]},
                             {"rarity", si::RARITY_NAMES[(int)c->rarity]}, {"price", c->price}, {"owned", owned.count(c->id) > 0},
                             {"section", i < 2 ? "featured" : "daily"}});
        }
        sendJson(res, {{"items", items}, {"reset_in", resetIn}});
    });

    S.Post("/api/shop/buy", [this](const httplib::Request& req, httplib::Response& res) {
        auto acc = authAccount(req);
        if (!acc) return sendError(res, 401, "not logged in");
        json b;
        if (!parseBody(req, res, b)) return;
        std::string item = b.value("item_id", "");
        auto ids = shopRotation((int64_t)std::time(nullptr) / 86400);
        if (std::find(ids.begin(), ids.end(), item) == ids.end()) return sendError(res, 404, "item is not in today's shop");
        const si::CosmeticDef* c = si::findCosmetic(item);
        std::string id = asString((*acc)["id"]);
        std::lock_guard<std::recursive_mutex> lk(db_.mutex());
        if (db_.one("SELECT 1 AS x FROM owned_items WHERE account_id=? AND item_id=?", {id, item})) return sendError(res, 409, "already owned");
        int changes = 0;
        db_.exec("UPDATE accounts SET coins = coins - ? WHERE id=? AND coins >= ?", {(int64_t)c->price, id, (int64_t)c->price}, nullptr, &changes);
        if (changes == 0) return sendError(res, 402, "not enough coins");
        grantItem(id, item);
        db_.exec("INSERT INTO purchases(account_id, item_id, price, at) VALUES(?,?,?,?)", {id, item, (int64_t)c->price, (int64_t)std::time(nullptr)});
        sendJson(res, {{"ok", true}, {"account", accountJson(*db_.one("SELECT * FROM accounts WHERE id=?", {id}))}});
    });

    // ---------------- stats / leaderboard / history
    S.Get("/api/leaderboard", [this](const httplib::Request& req, httplib::Response& res) {
        std::string by = req.has_param("by") ? req.get_param_value("by") : "wins";
        if (by != "wins" && by != "kills" && by != "matches" && by != "xp") by = "wins";
        json arr = json::array();
        for (auto& r : db_.query("SELECT display_name, wins, kills, matches, xp FROM accounts WHERE banned=0 ORDER BY " + by + " DESC LIMIT 50"))
            arr.push_back({{"display_name", asString(r.at("display_name"))}, {"wins", asInt(r.at("wins"))}, {"kills", asInt(r.at("kills"))},
                           {"matches", asInt(r.at("matches"))}, {"level", levelForXp(asInt(r.at("xp")))}});
        sendJson(res, arr);
    });

    S.Get("/api/matches", [this](const httplib::Request& req, httplib::Response& res) {
        auto acc = authAccount(req);
        if (!acc) return sendError(res, 401, "not logged in");
        json arr = json::array();
        for (auto& r : db_.query("SELECT m.id, m.playlist, m.ended_at, m.players, mp.placement, mp.kills, mp.damage FROM match_players mp "
                                 "JOIN matches m ON m.id = mp.match_id WHERE mp.account_id=? ORDER BY m.id DESC LIMIT 25",
                                 {asString((*acc)["id"])}))
            arr.push_back({{"id", asInt(r.at("id"))}, {"playlist", asString(r.at("playlist"))}, {"ended_at", asInt(r.at("ended_at"))},
                           {"players", asInt(r.at("players"))}, {"placement", asInt(r.at("placement"))}, {"kills", asInt(r.at("kills"))},
                           {"damage", asDouble(r.at("damage"))}});
        sendJson(res, arr);
    });

    // ---------------- friends
    S.Get("/api/friends", [this](const httplib::Request& req, httplib::Response& res) {
        auto acc = authAccount(req);
        if (!acc) return sendError(res, 401, "not logged in");
        std::string id = asString((*acc)["id"]);
        json friends = json::array(), incoming = json::array(), outgoing = json::array();
        for (auto& r : db_.query("SELECT f.friend_id, f.status, a.display_name, a.username, a.last_login FROM friends f JOIN accounts a ON a.id=f.friend_id WHERE f.account_id=?", {id})) {
            json e = {{"id", asString(r.at("friend_id"))}, {"display_name", asString(r.at("display_name"))}, {"username", asString(r.at("username"))}};
            std::string st = asString(r.at("status"));
            if (st == "accepted") friends.push_back(e);
            else outgoing.push_back(e);
        }
        for (auto& r : db_.query("SELECT f.account_id, a.display_name, a.username FROM friends f JOIN accounts a ON a.id=f.account_id WHERE f.friend_id=? AND f.status='pending'", {id}))
            incoming.push_back({{"id", asString(r.at("account_id"))}, {"display_name", asString(r.at("display_name"))}, {"username", asString(r.at("username"))}});
        sendJson(res, {{"friends", friends}, {"incoming", incoming}, {"outgoing", outgoing}});
    });

    S.Post("/api/friends/add", [this](const httplib::Request& req, httplib::Response& res) {
        auto acc = authAccount(req);
        if (!acc) return sendError(res, 401, "not logged in");
        json b;
        if (!parseBody(req, res, b)) return;
        auto other = db_.one("SELECT id FROM accounts WHERE username=?", {b.value("username", "")});
        if (!other) return sendError(res, 404, "no such user");
        std::string me = asString((*acc)["id"]), them = asString((*other)["id"]);
        if (me == them) return sendError(res, 400, "you can't add yourself");
        // If they already asked us, accept both ways.
        auto reverse = db_.one("SELECT status FROM friends WHERE account_id=? AND friend_id=?", {them, me});
        if (reverse) {
            db_.exec("UPDATE friends SET status='accepted' WHERE account_id=? AND friend_id=?", {them, me});
            db_.exec("INSERT OR REPLACE INTO friends(account_id, friend_id, status, created_at) VALUES(?,?, 'accepted', ?)", {me, them, (int64_t)std::time(nullptr)});
        } else {
            db_.exec("INSERT OR IGNORE INTO friends(account_id, friend_id, status, created_at) VALUES(?,?, 'pending', ?)", {me, them, (int64_t)std::time(nullptr)});
        }
        sendJson(res, {{"ok", true}});
    });

    S.Post("/api/friends/remove", [this](const httplib::Request& req, httplib::Response& res) {
        auto acc = authAccount(req);
        if (!acc) return sendError(res, 401, "not logged in");
        json b;
        if (!parseBody(req, res, b)) return;
        std::string me = asString((*acc)["id"]), them = b.value("id", "");
        db_.exec("DELETE FROM friends WHERE (account_id=? AND friend_id=?) OR (account_id=? AND friend_id=?)", {me, them, them, me});
        sendJson(res, {{"ok", true}});
    });

    // ---------------- playlists & matchmaking
    S.Get("/api/playlists", [this](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(mu_);
        json arr = json::array();
        for (auto& p : kPlaylists) {
            int online = 0, servers = 0;
            for (auto& [id, s] : servers_) if (s.playlist == p.id) { online += s.humans; servers++; }
            int queued = 0;
            for (auto& [tk, q] : queue_) if (q.playlist == p.id && q.status == "queued") queued++;
            arr.push_back({{"id", p.id}, {"name", p.name}, {"team_size", p.teamSize}, {"online", online}, {"servers", servers}, {"queued", queued}});
        }
        sendJson(res, arr);
    });

    S.Post("/api/matchmaking/join", [this](const httplib::Request& req, httplib::Response& res) {
        auto acc = authAccount(req);
        if (!acc) return sendError(res, 401, "not logged in");
        json b;
        if (!parseBody(req, res, b)) return;
        std::string playlist = b.value("playlist", "solo");
        if (!validPlaylist(playlist)) return sendError(res, 400, "unknown playlist");
        if (setting("matchmaking_enabled", "1") != "1") return sendError(res, 503, "matchmaking is disabled by an administrator");
        std::string accId = asString((*acc)["id"]);
        std::lock_guard<std::mutex> lk(mu_);
        for (auto it = queue_.begin(); it != queue_.end();) {
            if (it->second.accountId == accId && it->second.status == "queued") it = queue_.erase(it);
            else ++it;
        }
        QueueEntry q;
        q.ticket = randomToken(12);
        q.accountId = accId;
        q.playlist = playlist;
        q.created = now();
        queue_[q.ticket] = q;
        sendJson(res, {{"ticket", q.ticket}, {"status", "queued"}});
    });

    S.Get("/api/matchmaking/status", [this](const httplib::Request& req, httplib::Response& res) {
        auto acc = authAccount(req);
        if (!acc) return sendError(res, 401, "not logged in");
        std::string tk = req.get_param_value("ticket");
        std::lock_guard<std::mutex> lk(mu_);
        auto it = queue_.find(tk);
        if (it == queue_.end() || it->second.accountId != asString((*acc)["id"])) return sendError(res, 404, "unknown ticket");
        QueueEntry& q = it->second;
        json j = {{"status", q.status}, {"elapsed", now() - q.created}};
        if (q.status == "assigned") {
            j["host"] = q.host;
            j["port"] = q.port;
            j["match_ticket"] = q.matchTicket;
            j["server_id"] = q.serverId;
        }
        if (q.status == "failed") j["error"] = q.error;
        if (q.status == "queued") {
            int pos = 1;
            for (auto& [k, o] : queue_) if (o.status == "queued" && o.playlist == q.playlist && o.created < q.created) pos++;
            j["position"] = pos;
        }
        sendJson(res, j);
    });

    S.Post("/api/matchmaking/cancel", [this](const httplib::Request& req, httplib::Response& res) {
        auto acc = authAccount(req);
        if (!acc) return sendError(res, 401, "not logged in");
        json b;
        if (!parseBody(req, res, b)) return;
        std::lock_guard<std::mutex> lk(mu_);
        auto it = queue_.find(b.value("ticket", ""));
        if (it != queue_.end() && it->second.accountId == asString((*acc)["id"])) it->second.status = "cancelled";
        sendJson(res, {{"ok", true}});
    });

    // ---------------- internal API (game servers)
    S.Post("/internal/servers/register", [this](const httplib::Request& req, httplib::Response& res) {
        if (!checkServerSecret(req, res)) return;
        json b;
        if (!parseBody(req, res, b)) return;
        std::lock_guard<std::mutex> lk(mu_);
        std::string id = b.value("server_id", "");
        if (id.empty()) id = "ext-" + randomToken(4);
        GameServerInfo& s = servers_[id];
        if (s.id.empty()) { s.created = now(); s.managed = false; }
        s.id = id;
        s.host = b.value("host", std::string("127.0.0.1"));
        if (s.host.empty() || s.host == "0.0.0.0") s.host = req.remote_addr;
        s.port = b.value("port", 7777);
        s.playlist = b.value("playlist", std::string("solo"));
        s.maxPlayers = b.value("max_players", 100);
        s.state = "warmup";
        s.lastHeartbeat = now();
        std::printf("[backend] game server registered: %s %s:%d (%s)\n", id.c_str(), s.host.c_str(), s.port, s.playlist.c_str());
        sendJson(res, {{"server_id", id}, {"settings", settingsJson()}});
    });

    S.Post("/internal/servers/heartbeat", [this](const httplib::Request& req, httplib::Response& res) {
        if (!checkServerSecret(req, res)) return;
        json b;
        if (!parseBody(req, res, b)) return;
        std::lock_guard<std::mutex> lk(mu_);
        auto it = servers_.find(b.value("server_id", ""));
        if (it == servers_.end()) return sendError(res, 404, "unknown server; re-register");
        GameServerInfo& s = it->second;
        s.state = b.value("state", s.state);
        s.players = b.value("players", 0);
        s.humans = b.value("humans", 0);
        s.bots = b.value("bots", 0);
        s.alive = b.value("alive", 0);
        s.maxPlayers = b.value("max_players", s.maxPlayers);
        s.phaseTimer = b.value("phase_timer", 0.0f);
        s.stormPhase = b.value("storm_phase", -1);
        s.playerNames.clear();
        if (b.contains("player_names") && b["player_names"].is_array())
            for (auto& n : b["player_names"]) if (n.is_string()) s.playerNames.push_back(n.get<std::string>());
        s.lastHeartbeat = now();
        json cmds = json::array();
        for (auto& c : s.commands) cmds.push_back(c);
        s.commands.clear();
        sendJson(res, {{"settings", settingsJson()}, {"commands", cmds}});
    });

    S.Post("/internal/tickets/validate", [this](const httplib::Request& req, httplib::Response& res) {
        if (!checkServerSecret(req, res)) return;
        json b;
        if (!parseBody(req, res, b)) return;
        std::string tk = b.value("ticket", ""), sid = b.value("server_id", "");
        MatchTicket mt;
        {
            std::lock_guard<std::mutex> lk(mu_);
            auto it = tickets_.find(tk);
            if (it == tickets_.end() || it->second.expires < now()) return sendJson(res, {{"ok", false}, {"error", "Invalid or expired match ticket"}});
            if (it->second.serverId != sid) return sendJson(res, {{"ok", false}, {"error", "Ticket is for a different server"}});
            mt = it->second;
            tickets_.erase(it);
            auto sit = servers_.find(sid);
            if (sit != servers_.end()) sit->second.reserved = std::max(0, sit->second.reserved - 1);
        }
        auto acc = db_.one("SELECT * FROM accounts WHERE id=?", {mt.accountId});
        if (!acc || asInt((*acc)["banned"])) return sendJson(res, {{"ok", false}, {"error", "Account not allowed"}});
        json lj = lockerJson(mt.accountId);
        sendJson(res, {{"ok", true}, {"account_id", mt.accountId}, {"display_name", asString((*acc)["display_name"])}, {"loadout", lj["equipped"]}});
    });

    S.Post("/internal/matches/report", [this](const httplib::Request& req, httplib::Response& res) {
        if (!checkServerSecret(req, res)) return;
        json b;
        if (!parseBody(req, res, b)) return;
        if (!b.contains("results") || !b["results"].is_array()) return sendError(res, 400, "results missing");
        std::lock_guard<std::recursive_mutex> lk(db_.mutex());
        int64_t matchId = 0;
        std::string winner;
        for (auto& r : b["results"]) if (r.value("placement", 0) == 1) winner += (winner.empty() ? "" : ", ") + r.value("name", std::string());
        db_.exec("INSERT INTO matches(server_id, playlist, ended_at, players, winner) VALUES(?,?,?,?,?)",
                 {b.value("server_id", std::string()), b.value("playlist", std::string()), (int64_t)std::time(nullptr),
                  (int64_t)b["results"].size(), winner}, &matchId);
        for (auto& r : b["results"]) {
            std::string acc = r.value("account_id", std::string());
            int place = r.value("placement", 0), kills = r.value("kills", 0);
            double dmg = r.value("damage", 0.0);
            db_.exec("INSERT INTO match_players(match_id, account_id, name, bot, placement, kills, damage, survived) VALUES(?,?,?,?,?,?,?,?)",
                     {matchId, acc, r.value("name", std::string()), (int64_t)(r.value("bot", false) ? 1 : 0), (int64_t)place, (int64_t)kills, dmg,
                      r.value("survived", 0.0)});
            if (acc.empty()) continue;
            auto a = db_.one("SELECT xp FROM accounts WHERE id=?", {acc});
            if (!a) continue;
            int64_t oldXp = asInt((*a)["xp"]);
            int64_t gained = 100 + kills * 60 + (place == 1 ? 400 : place <= 10 ? 150 : 0) + (int64_t)(dmg / 10);
            int oldLevel = levelForXp(oldXp), newLevel = levelForXp(oldXp + gained);
            int64_t coins = kills * 10 + (place == 1 ? 150 : 0) + (newLevel - oldLevel) * 200;
            db_.exec("UPDATE accounts SET xp = xp + ?, level = ?, coins = coins + ?, kills = kills + ?, matches = matches + 1, "
                     "wins = wins + ?, top10 = top10 + ?, damage = damage + ? WHERE id=?",
                     {gained, (int64_t)newLevel, coins, (int64_t)kills, (int64_t)(place == 1 ? 1 : 0), (int64_t)(place <= 10 ? 1 : 0), dmg, acc});
            if (newLevel > oldLevel) grantLevelRewards(acc, newLevel);
        }
        sendJson(res, {{"ok", true}, {"match_id", matchId}});
    });

    // ---------------- admin API
    S.Get("/admin/api/overview", [this](const httplib::Request& req, httplib::Response& res) {
        if (!requireAdmin(req, res)) return;
        json servers = json::array();
        int queued = 0;
        {
            std::lock_guard<std::mutex> lk(mu_);
            double t = now();
            for (auto& [id, s] : servers_) servers.push_back(serverJson(s, t));
            for (auto& [tk, q] : queue_) if (q.status == "queued") queued++;
        }
        auto cnt = [&](const char* sql) { auto r = db_.one(sql); return r ? asInt((*r)["n"]) : 0; };
        sendJson(res, {{"servers", servers}, {"queued", queued}, {"settings", settingsJson()},
                       {"stats", {{"accounts", cnt("SELECT COUNT(*) AS n FROM accounts")}, {"matches", cnt("SELECT COUNT(*) AS n FROM matches")},
                                  {"banned", cnt("SELECT COUNT(*) AS n FROM accounts WHERE banned=1")}}},
                       {"spawning", {{"enabled", cfg_.spawnServers}, {"binary", cfg_.serverBinary}, {"binary_found", fileExists(cfg_.serverBinary)},
                                     {"port_range", std::to_string(cfg_.portMin) + "-" + std::to_string(cfg_.portMax)}}}});
    });

    S.Post(R"(/admin/api/servers/([A-Za-z0-9_\-]+)/command)", [this](const httplib::Request& req, httplib::Response& res) {
        std::string admin;
        if (!requireAdmin(req, res, &admin)) return;
        json b;
        if (!parseBody(req, res, b)) return;
        static const std::set<std::string> allowed = {"add_bots", "remove_bots", "start_now", "end_match", "kick", "message", "pause_storm", "shutdown"};
        std::string type = b.value("type", "");
        if (!allowed.count(type)) return sendError(res, 400, "unknown command");
        if ((type == "add_bots" || type == "remove_bots")) {
            int n = b.value("count", 0);
            if (n < 1 || n > 100) return sendError(res, 400, "count must be 1-100");
        }
        std::string id = req.matches[1];
        {
            std::lock_guard<std::mutex> lk(mu_);
            auto it = servers_.find(id);
            if (it == servers_.end()) return sendError(res, 404, "server not found");
            it->second.commands.push_back(b);
        }
        adminLog(admin, type, id + " " + b.dump());
        sendJson(res, {{"ok", true}});
    });

    S.Post("/admin/api/servers/spawn", [this](const httplib::Request& req, httplib::Response& res) {
        std::string admin;
        if (!requireAdmin(req, res, &admin)) return;
        json b;
        if (!parseBody(req, res, b)) return;
        std::string playlist = b.value("playlist", "solo");
        if (!validPlaylist(playlist)) return sendError(res, 400, "unknown playlist");
        std::string err, id;
        {
            std::lock_guard<std::mutex> lk(mu_);
            if (!spawnServer(playlist, err, &id)) return sendError(res, 500, err);
            int bots = b.value("bots", 0);
            if (bots > 0) servers_[id].commands.push_back({{"type", "add_bots"}, {"count", std::min(bots, 99)}});
        }
        adminLog(admin, "spawn_server", id + " " + playlist);
        sendJson(res, {{"ok", true}, {"server_id", id}});
    });

    S.Get("/admin/api/settings", [this](const httplib::Request& req, httplib::Response& res) {
        if (!requireAdmin(req, res)) return;
        sendJson(res, settingsJson());
    });

    S.Put("/admin/api/settings", [this](const httplib::Request& req, httplib::Response& res) {
        std::string admin;
        if (!requireAdmin(req, res, &admin)) return;
        json b;
        if (!parseBody(req, res, b)) return;
        auto clampInt = [&](const char* k, int lo, int hi) {
            if (!b.contains(k)) return;
            if (!b[k].is_number()) return;
            int v = std::max(lo, std::min(hi, b[k].get<int>()));
            setSetting(k, std::to_string(v));
        };
        clampInt("default_bots", 0, 99);
        clampInt("min_players", 0, 100);
        clampInt("max_servers", 0, 64);
        clampInt("max_players_per_server", 2, 100);
        if (b.contains("warmup_seconds") && b["warmup_seconds"].is_number())
            setSetting("warmup_seconds", std::to_string(std::max(0.0, std::min(600.0, b["warmup_seconds"].get<double>()))));
        if (b.contains("storm_speed") && b["storm_speed"].is_number())
            setSetting("storm_speed", std::to_string(std::max(0.1, std::min(10.0, b["storm_speed"].get<double>()))));
        if (b.contains("bot_difficulty") && b["bot_difficulty"].is_string()) {
            std::string d = b["bot_difficulty"];
            if (d == "easy" || d == "medium" || d == "hard" || d == "insane") setSetting("bot_difficulty", d);
        }
        if (b.contains("fill_bots") && b["fill_bots"].is_boolean()) setSetting("fill_bots", b["fill_bots"].get<bool>() ? "1" : "0");
        if (b.contains("matchmaking_enabled") && b["matchmaking_enabled"].is_boolean())
            setSetting("matchmaking_enabled", b["matchmaking_enabled"].get<bool>() ? "1" : "0");
        if (b.contains("motd") && b["motd"].is_string()) setSetting("motd", b["motd"].get<std::string>().substr(0, 500));
        adminLog(admin, "update_settings", b.dump());
        sendJson(res, settingsJson());
    });

    S.Get("/admin/api/users", [this](const httplib::Request& req, httplib::Response& res) {
        if (!requireAdmin(req, res)) return;
        std::string q = req.has_param("q") ? req.get_param_value("q") : "";
        json arr = json::array();
        for (auto& r : db_.query("SELECT * FROM accounts WHERE username LIKE ? OR display_name LIKE ? ORDER BY created_at DESC LIMIT 100",
                                 {"%" + q + "%", "%" + q + "%"})) {
            json j = accountJson(r);
            j["banned"] = asInt(r.at("banned")) != 0;
            j["created_at"] = asInt(r.at("created_at"));
            j["last_login"] = asInt(r.at("last_login"));
            arr.push_back(j);
        }
        sendJson(res, arr);
    });

    S.Post(R"(/admin/api/users/([a-f0-9]+)/(ban|coins|admin|grant))", [this](const httplib::Request& req, httplib::Response& res) {
        std::string admin;
        if (!requireAdmin(req, res, &admin)) return;
        json b;
        if (!parseBody(req, res, b)) return;
        std::string id = req.matches[1], action = req.matches[2];
        if (!db_.one("SELECT id FROM accounts WHERE id=?", {id})) return sendError(res, 404, "user not found");
        if (action == "ban") {
            bool banned = b.value("banned", true);
            db_.exec("UPDATE accounts SET banned=? WHERE id=?", {(int64_t)(banned ? 1 : 0), id});
            if (banned) db_.exec("DELETE FROM sessions WHERE account_id=?", {id});
        } else if (action == "coins") {
            int64_t amount = b.value("amount", 0);
            db_.exec("UPDATE accounts SET coins = MAX(0, coins + ?) WHERE id=?", {amount, id});
        } else if (action == "admin") {
            db_.exec("UPDATE accounts SET is_admin=? WHERE id=?", {(int64_t)(b.value("is_admin", false) ? 1 : 0), id});
        } else if (action == "grant") {
            std::string item = b.value("item_id", "");
            if (item == "*") { for (auto& c : si::cosmeticCatalog()) grantItem(id, c.id); }
            else if (si::findCosmetic(item)) grantItem(id, item);
            else return sendError(res, 404, "unknown item");
        }
        adminLog(admin, "user_" + action, id + " " + b.dump());
        sendJson(res, {{"ok", true}});
    });

    S.Get("/admin/api/matches", [this](const httplib::Request& req, httplib::Response& res) {
        if (!requireAdmin(req, res)) return;
        json arr = json::array();
        for (auto& r : db_.query("SELECT * FROM matches ORDER BY id DESC LIMIT 50"))
            arr.push_back({{"id", asInt(r.at("id"))}, {"server_id", asString(r.at("server_id"))}, {"playlist", asString(r.at("playlist"))},
                           {"ended_at", asInt(r.at("ended_at"))}, {"players", asInt(r.at("players"))}, {"winner", asString(r.at("winner"))}});
        sendJson(res, arr);
    });

    S.Get("/admin/api/log", [this](const httplib::Request& req, httplib::Response& res) {
        if (!requireAdmin(req, res)) return;
        json arr = json::array();
        for (auto& r : db_.query("SELECT * FROM admin_log ORDER BY id DESC LIMIT 100"))
            arr.push_back({{"at", asInt(r.at("at"))}, {"admin", asString(r.at("admin"))}, {"action", asString(r.at("action"))}, {"details", asString(r.at("details"))}});
        sendJson(res, arr);
    });
}

void Backend::run() {
    running_ = true;
    mmThread_ = std::thread([this] { matchmakerLoop(); });
    std::printf("[backend] listening on http://%s:%d  (admin panel: /admin)\n", cfg_.bindHost.c_str(), cfg_.httpPort);
    std::printf("[backend] game server binary: %s%s\n", cfg_.serverBinary.c_str(), fileExists(cfg_.serverBinary) ? "" : "  (NOT FOUND)");
    std::printf("[backend] spawned servers: %s, UDP ports %d-%d, advertised as %s\n", cfg_.spawnServers ? "on" : "off", cfg_.portMin,
                cfg_.portMax, cfg_.publicHost.c_str());
    if (cfg_.spawnServers && (cfg_.publicHost == "127.0.0.1" || cfg_.publicHost == "localhost"))
        std::printf("[backend] WARNING: public host is %s - players on other machines can't join matches. "
                    "Set STORM_PUBLIC_HOST (or --public-host) to this server's public IP or hostname.\n", cfg_.publicHost.c_str());
    std::fflush(stdout);
    if (!http_->listen(cfg_.bindHost, cfg_.httpPort)) std::fprintf(stderr, "[backend] failed to listen on port %d\n", cfg_.httpPort);
    running_ = false;
    if (mmThread_.joinable()) mmThread_.join();
}

void Backend::stop() {
    if (http_) http_->stop();
    running_ = false;
    std::lock_guard<std::mutex> lk(mu_);
    for (auto& [id, s] : servers_)
        if (s.managed) killProcess(s.proc);
}

} // namespace backend
