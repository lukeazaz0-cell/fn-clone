#include "backend_link.h"

#include <chrono>
#include <cstdio>

#include "httplib.h"
#include "nlohmann/json.hpp"

using json = nlohmann::json;

namespace si {

namespace {

struct UrlParts { std::string host; int port = 80; };
UrlParts parseUrl(const std::string& url) {
    UrlParts u;
    std::string s = url;
    auto p = s.find("://");
    if (p != std::string::npos) s = s.substr(p + 3);
    auto slash = s.find('/');
    if (slash != std::string::npos) s = s.substr(0, slash);
    auto colon = s.find(':');
    if (colon != std::string::npos) {
        u.host = s.substr(0, colon);
        u.port = std::stoi(s.substr(colon + 1));
    } else {
        u.host = s;
    }
    return u;
}

std::unique_ptr<httplib::Client> makeClient(const std::string& url) {
    UrlParts u = parseUrl(url);
    auto c = std::make_unique<httplib::Client>(u.host, u.port);
    c->set_connection_timeout(3, 0);
    c->set_read_timeout(5, 0);
    return c;
}

void applySettings(BackendSettings& s, const json& j) {
    if (!j.is_object()) return;
    s.received = true;
    s.defaultBots = j.value("default_bots", s.defaultBots);
    s.botDifficulty = j.value("bot_difficulty", s.botDifficulty);
    s.stormSpeed = j.value("storm_speed", s.stormSpeed);
    s.warmupSeconds = j.value("warmup_seconds", s.warmupSeconds);
    s.minPlayers = j.value("min_players", s.minPlayers);
    s.fillBots = j.value("fill_bots", s.fillBots);
}

} // namespace

BackendLink::BackendLink(std::string url, std::string secret, std::string serverId, std::string publicHost, uint16_t port)
    : url_(std::move(url)), secret_(std::move(secret)), serverId_(std::move(serverId)), host_(std::move(publicHost)), port_(port) {}

BackendLink::~BackendLink() { stop(); }

bool BackendLink::registerServer(const std::string& playlist, int maxPlayers) {
    auto cli = makeClient(url_);
    json body = {{"server_id", serverId_}, {"host", host_}, {"port", port_}, {"playlist", playlist}, {"max_players", maxPlayers}};
    httplib::Headers h = {{"X-Server-Secret", secret_}};
    for (int attempt = 0; attempt < 10; attempt++) {
        auto res = cli->Post("/internal/servers/register", h, body.dump(), "application/json");
        if (res && res->status == 200) {
            json j = json::parse(res->body, nullptr, false);
            if (!j.is_discarded()) {
                serverId_ = j.value("server_id", serverId_);
                std::lock_guard<std::mutex> lk(mu_);
                if (j.contains("settings")) applySettings(settings_, j["settings"]);
            }
            return true;
        }
        std::fprintf(stderr, "[backend] register failed (%s), retrying...\n", res ? std::to_string(res->status).c_str() : "no connection");
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    return false;
}

void BackendLink::start() {
    running_ = true;
    thread_ = std::thread([this] { loop(); });
}

void BackendLink::stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
}

void BackendLink::setStatus(const ServerStatus& s) {
    std::lock_guard<std::mutex> lk(mu_);
    status_ = s;
}

std::vector<AdminCommand> BackendLink::takeCommands() {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<AdminCommand> out;
    out.swap(commands_);
    return out;
}

BackendSettings BackendLink::settings() {
    std::lock_guard<std::mutex> lk(mu_);
    return settings_;
}

void BackendLink::loop() {
    while (running_) {
        heartbeat();
        for (int i = 0; i < 10 && running_; i++) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void BackendLink::heartbeat() {
    ServerStatus st;
    {
        std::lock_guard<std::mutex> lk(mu_);
        st = status_;
    }
    json body = {{"server_id", serverId_}, {"state", st.state}, {"players", st.players}, {"humans", st.humans},
                 {"bots", st.bots}, {"alive", st.alive}, {"max_players", st.maxPlayers}, {"playlist", st.playlist},
                 {"phase_timer", st.phaseTimer}, {"storm_phase", st.stormPhase}, {"player_names", st.playerNames},
                 {"port", port_}, {"host", host_}};
    auto cli = makeClient(url_);
    httplib::Headers h = {{"X-Server-Secret", secret_}};
    auto res = cli->Post("/internal/servers/heartbeat", h, body.dump(), "application/json");
    if (!res || res->status != 200) return;
    json j = json::parse(res->body, nullptr, false);
    if (j.is_discarded()) return;
    std::lock_guard<std::mutex> lk(mu_);
    if (j.contains("settings")) applySettings(settings_, j["settings"]);
    if (j.contains("commands") && j["commands"].is_array()) {
        for (auto& c : j["commands"]) {
            AdminCommand cmd;
            cmd.type = c.value("type", "");
            cmd.count = c.value("count", 0);
            cmd.playerId = c.value("player_id", 0);
            cmd.text = c.value("text", "");
            cmd.flag = c.value("flag", false);
            commands_.push_back(cmd);
        }
    }
}

TicketInfo BackendLink::validate(const std::string& ticket) {
    TicketInfo info;
    auto cli = makeClient(url_);
    httplib::Headers h = {{"X-Server-Secret", secret_}};
    json body = {{"ticket", ticket}, {"server_id", serverId_}};
    auto res = cli->Post("/internal/tickets/validate", h, body.dump(), "application/json");
    if (!res) { info.reason = "Backend unreachable"; return info; }
    json j = json::parse(res->body, nullptr, false);
    if (j.is_discarded() || res->status != 200 || !j.value("ok", false)) {
        info.reason = j.is_discarded() ? "Ticket rejected" : j.value("error", std::string("Ticket rejected"));
        return info;
    }
    info.ok = true;
    info.accountId = j.value("account_id", "");
    info.displayName = j.value("display_name", "");
    if (j.contains("loadout")) {
        const json& l = j["loadout"];
        info.loadout.outfit = l.value("outfit", info.loadout.outfit);
        info.loadout.backbling = l.value("backbling", info.loadout.backbling);
        info.loadout.pickaxe = l.value("pickaxe", info.loadout.pickaxe);
        info.loadout.glider = l.value("glider", info.loadout.glider);
        info.loadout.contrail = l.value("contrail", info.loadout.contrail);
        if (l.contains("emotes") && l["emotes"].is_array())
            for (size_t i = 0; i < 6 && i < l["emotes"].size(); i++) info.loadout.emotes[i] = l["emotes"][i].get<std::string>();
    }
    return info;
}

void BackendLink::reportMatch(const std::vector<MatchResultEntry>& results, const std::string& playlist) {
    json arr = json::array();
    for (auto& r : results)
        arr.push_back({{"account_id", r.accountId}, {"name", r.name}, {"bot", r.bot}, {"placement", r.placement},
                       {"kills", r.kills}, {"damage", r.damage}, {"survived", r.survived}});
    json body = {{"server_id", serverId_}, {"playlist", playlist}, {"results", arr}};
    std::string payload = body.dump();
    std::string url = url_, secret = secret_;
    // Fire and forget on a worker thread so the game loop never blocks.
    std::thread([url, secret, payload] {
        auto cli = makeClient(url);
        httplib::Headers h = {{"X-Server-Secret", secret}};
        for (int i = 0; i < 3; i++) {
            auto res = cli->Post("/internal/matches/report", h, payload, "application/json");
            if (res && res->status == 200) return;
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }).detach();
}

} // namespace si
