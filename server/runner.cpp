#include "runner.h"

#include <chrono>
#include <cstdio>
#include <thread>

namespace si {

BotDifficulty parseDifficulty(const std::string& s) {
    if (s == "easy") return BotDifficulty::Easy;
    if (s == "hard") return BotDifficulty::Hard;
    if (s == "insane") return BotDifficulty::Insane;
    return BotDifficulty::Medium;
}

ServerRunner::ServerRunner(const RunnerOptions& o) : opt_(o), net_(game_) {}

void ServerRunner::logLine(const std::string& s) {
    if (opt_.verbose) std::printf("[server:%u] %s\n", (unsigned)net_.port(), s.c_str());
    std::fflush(stdout);
}

bool ServerRunner::init() {
    game_.log = [this](const std::string& s) { logLine(s); };
    net_.log = [this](const std::string& s) { logLine(s); };
    game_.init(opt_.game);
    if (!net_.start(opt_.port)) {
        std::fprintf(stderr, "failed to bind UDP port %u\n", (unsigned)opt_.port);
        return false;
    }
    logLine("listening, playlist=" + opt_.game.playlist + " seed=" + std::to_string(opt_.game.mapSeed) +
            " shapes=" + std::to_string(game_.map.shapes.size()) + " chests=" + std::to_string(game_.map.chests.size()));
    if (!opt_.backendUrl.empty()) {
        link_ = std::make_unique<BackendLink>(opt_.backendUrl, opt_.secret, opt_.serverId, opt_.publicHost, net_.port());
        if (!link_->registerServer(opt_.game.playlist, opt_.game.maxPlayers)) {
            std::fprintf(stderr, "could not register with backend at %s\n", opt_.backendUrl.c_str());
            return false;
        }
        logLine("registered with backend as " + link_->serverId());
        BackendLink* link = link_.get();
        net_.validateTicket = [link](const std::string& t) { return link->validate(t); };
        std::string playlist = opt_.game.playlist;
        game_.onMatchEnd = [link, playlist](const std::vector<MatchResultEntry>& r) { link->reportMatch(r, playlist); };
        applyBackend();
        link_->start();
    }
    return true;
}

void ServerRunner::applyBackend() {
    if (!link_) return;
    BackendSettings s = link_->settings();
    if (s.received) {
        game_.cfg.defaultBots = s.defaultBots;
        game_.cfg.botDifficulty = parseDifficulty(s.botDifficulty);
        game_.cfg.stormSpeed = std::max(0.1f, s.stormSpeed);
        game_.cfg.warmupSeconds = s.warmupSeconds;
        game_.cfg.minHumansToStart = s.minPlayers;
        game_.cfg.fillBotsToMax = s.fillBots;
    }
    for (auto& c : link_->takeCommands()) {
        logLine("admin command: " + c.type);
        if (c.type == "add_bots") game_.addBots(std::max(0, std::min(100, c.count)));
        else if (c.type == "remove_bots") game_.removeBots(std::max(0, c.count));
        else if (c.type == "start_now") game_.startNow();
        else if (c.type == "end_match") game_.endMatchNow();
        else if (c.type == "kick") net_.kickPlayer((uint16_t)c.playerId, c.text.empty() ? "Kicked by admin" : c.text);
        else if (c.type == "message") game_.broadcastMessage("[ADMIN] " + c.text, 3);
        else if (c.type == "pause_storm") game_.storm.paused = c.flag;
        else if (c.type == "shutdown") game_.wantsExit = true;
    }
}

void ServerRunner::publishStatus() {
    if (!link_) return;
    ServerStatus st;
    st.state = PHASE_NAMES[(int)game_.phase];
    st.players = (int)game_.players.size();
    st.humans = game_.humanCount();
    st.bots = game_.botCount();
    st.alive = game_.aliveCount();
    st.maxPlayers = game_.cfg.maxPlayers;
    st.playlist = game_.cfg.playlist;
    st.phaseTimer = game_.phaseTimer;
    st.stormPhase = game_.storm.phase;
    for (auto& [id, p] : game_.players)
        st.playerNames.push_back(std::to_string(id) + ":" + p.name + (p.bot ? " [bot]" : "") + (p.active() ? "" : " (out)"));
    link_->setStatus(st);
}

void ServerRunner::run(std::atomic<bool>& stop) {
    using clock = std::chrono::steady_clock;
    auto next = clock::now();
    auto dt = std::chrono::duration<double>(SERVER_TICK_DT);
    int tickCount = 0;
    while (!stop && !game_.wantsExit) {
        net_.poll();
        if (tickCount % 3 == 0) applyBackend();
        game_.tick(SERVER_TICK_DT);
        net_.sendSnapshots();
        if (tickCount % 30 == 0) publishStatus();
        tickCount++;
        next += std::chrono::duration_cast<clock::duration>(dt);
        auto nowT = clock::now();
        if (next > nowT) std::this_thread::sleep_until(next);
        else if (nowT - next > std::chrono::milliseconds(250)) next = nowT; // running behind; don't spiral
    }
    publishStatus();
    // Give the result report a moment to leave.
    if (link_) std::this_thread::sleep_for(std::chrono::milliseconds(500));
    net_.stop();
    if (link_) link_->stop();
    logLine("server stopped");
}

} // namespace si
