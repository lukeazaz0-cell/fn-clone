#include "app.h"

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fstream>

#include "server/runner.h"
#include "ui.h"

namespace client {

static const char* kSettingsFile = "stormisland_settings.json";

App::App() {}

App::~App() {
    stopOffline();
    if (game_) game_.reset();
    net_.disconnect();
}

void App::loadSettings() {
    std::ifstream f(kSettingsFile);
    if (!f) return;
    json j = json::parse(f, nullptr, false);
    if (!j.is_object()) return;
    settings_.sensitivity = j.value("sensitivity", settings_.sensitivity);
    settings_.fov = j.value("fov", settings_.fov);
    settings_.volume = j.value("volume", settings_.volume);
    settings_.viewDistance = j.value("view_distance", settings_.viewDistance);
    settings_.showFps = j.value("show_fps", settings_.showFps);
    settings_.invertY = j.value("invert_y", settings_.invertY);
    settings_.shadows = j.value("shadows", settings_.shadows);
    settings_.backendUrl = j.value("backend_url", settings_.backendUrl);
    settings_.lastUser = j.value("last_user", settings_.lastUser);
    api_.token = j.value("token", std::string());
    if (j.contains("offline_loadout")) {
        const json& l = j["offline_loadout"];
        offlineLoadout_.outfit = l.value("outfit", offlineLoadout_.outfit);
        offlineLoadout_.backbling = l.value("backbling", offlineLoadout_.backbling);
        offlineLoadout_.pickaxe = l.value("pickaxe", offlineLoadout_.pickaxe);
        offlineLoadout_.glider = l.value("glider", offlineLoadout_.glider);
        offlineLoadout_.contrail = l.value("contrail", offlineLoadout_.contrail);
        if (l.contains("emotes") && l["emotes"].is_array())
            for (size_t i = 0; i < 6 && i < l["emotes"].size(); i++) offlineLoadout_.emotes[i] = l["emotes"][i].get<std::string>();
    }
    offlineBots_ = j.value("offline_bots", offlineBots_);
    offlineDiff_ = j.value("offline_difficulty", offlineDiff_);
    directHost_ = j.value("direct_host", directHost_);
}

void App::saveSettings() {
    json emotes = json::array();
    for (auto& e : offlineLoadout_.emotes) emotes.push_back(e);
    json j = {{"sensitivity", settings_.sensitivity}, {"fov", settings_.fov}, {"volume", settings_.volume},
              {"view_distance", settings_.viewDistance}, {"show_fps", settings_.showFps}, {"invert_y", settings_.invertY}, {"shadows", settings_.shadows},
              {"backend_url", settings_.backendUrl}, {"last_user", settings_.lastUser}, {"token", offline_ ? "" : api_.token},
              {"offline_bots", offlineBots_}, {"offline_difficulty", offlineDiff_}, {"direct_host", directHost_},
              {"offline_loadout", {{"outfit", offlineLoadout_.outfit}, {"backbling", offlineLoadout_.backbling}, {"pickaxe", offlineLoadout_.pickaxe},
                                   {"glider", offlineLoadout_.glider}, {"contrail", offlineLoadout_.contrail}, {"emotes", emotes}}}};
    std::ofstream f(kSettingsFile);
    f << j.dump(2);
}

void App::setStatus(const std::string& s) {
    status_ = s;
    statusTimer_ = 4.0f;
}

void App::refreshProfile() {
    if (offline_) return;
    fMe_ = api_.get("/api/me");
    fLocker_ = api_.get("/api/locker");
    fShop_ = api_.get("/api/shop");
    fPlaylists_ = api_.get("/api/playlists");
    fMatches_ = api_.get("/api/matches");
    fBoard_ = api_.get("/api/leaderboard");
    fNews_ = api_.get("/api/news");
}

Loadout App::currentLoadout() const {
    if (offline_ || !locker_.is_object() || !locker_.contains("equipped")) return offlineLoadout_;
    Loadout l;
    const json& e = locker_["equipped"];
    l.outfit = e.value("outfit", l.outfit);
    l.backbling = e.value("backbling", l.backbling);
    l.pickaxe = e.value("pickaxe", l.pickaxe);
    l.glider = e.value("glider", l.glider);
    l.contrail = e.value("contrail", l.contrail);
    if (e.contains("emotes") && e["emotes"].is_array())
        for (size_t i = 0; i < 6 && i < e["emotes"].size(); i++) l.emotes[i] = e["emotes"][i].get<std::string>();
    return l;
}

std::string App::displayName() const {
    if (!offline_ && account_.is_object()) return account_.value("display_name", std::string("Player"));
    return user_.empty() ? "Player" : user_;
}

bool App::ownsItem(const std::string& id) const {
    if (offline_) return true;
    if (!locker_.is_object() || !locker_.contains("owned")) return false;
    for (auto& o : locker_["owned"])
        if (o.is_string() && o.get<std::string>() == id) return true;
    return false;
}

void App::equip(const std::string& id, int emoteSlot) {
    const CosmeticDef* d = findCosmetic(id);
    if (!d) return;
    if (offline_) {
        switch (d->type) {
            case CosmeticType::Outfit: offlineLoadout_.outfit = id; break;
            case CosmeticType::BackBling: offlineLoadout_.backbling = id; break;
            case CosmeticType::Pickaxe: offlineLoadout_.pickaxe = id; break;
            case CosmeticType::Glider: offlineLoadout_.glider = id; break;
            case CosmeticType::Contrail: offlineLoadout_.contrail = id; break;
            case CosmeticType::Emote: offlineLoadout_.emotes[std::max(0, std::min(5, emoteSlot))] = id; break;
            default: break;
        }
        saveSettings();
        return;
    }
    // Optimistic local update so the preview changes immediately
    if (locker_.is_object() && locker_.contains("equipped")) {
        json& e = locker_["equipped"];
        switch (d->type) {
            case CosmeticType::Outfit: e["outfit"] = id; break;
            case CosmeticType::BackBling: e["backbling"] = id; break;
            case CosmeticType::Pickaxe: e["pickaxe"] = id; break;
            case CosmeticType::Glider: e["glider"] = id; break;
            case CosmeticType::Contrail: e["contrail"] = id; break;
            case CosmeticType::Emote: if (e.contains("emotes") && e["emotes"].is_array() && emoteSlot >= 0 && emoteSlot < 6) e["emotes"][emoteSlot] = id; break;
            default: break;
        }
    }
    fEquip_ = api_.put("/api/locker", {{"item_id", id}, {"slot", emoteSlot}});
}

void App::pollFutures() {
    if (ready(fLogin_)) {
        ApiResult r = fLogin_.get();
        if (r.ok()) {
            api_.token = r.body.value("token", std::string());
            account_ = r.body["account"];
            offline_ = false;
            settings_.lastUser = user_;
            saveSettings();
            screen_ = Screen::Lobby;
            pass_.clear();
            refreshProfile();
        } else {
            setStatus(r.error());
        }
    }
    if (ready(fMe_)) {
        ApiResult r = fMe_.get();
        if (r.ok()) {
            account_ = r.body;
            if (screen_ == Screen::Login) { screen_ = Screen::Lobby; offline_ = false; refreshProfile(); }
        } else if (r.status == 401 && screen_ != Screen::Login) {
            setStatus("Your session expired, please sign in again");
            screen_ = Screen::Login;
        }
    }
    if (ready(fLocker_)) { ApiResult r = fLocker_.get(); if (r.ok()) locker_ = r.body; }
    if (ready(fShop_)) { ApiResult r = fShop_.get(); if (r.ok()) shop_ = r.body; }
    if (ready(fPlaylists_)) { ApiResult r = fPlaylists_.get(); if (r.ok()) playlists_ = r.body; }
    if (ready(fMatches_)) { ApiResult r = fMatches_.get(); if (r.ok()) matches_ = r.body; }
    if (ready(fBoard_)) { ApiResult r = fBoard_.get(); if (r.ok()) leaderboard_ = r.body; }
    if (ready(fNews_)) { ApiResult r = fNews_.get(); if (r.ok()) motd_ = r.body.value("motd", std::string()); }
    if (ready(fEquip_)) {
        ApiResult r = fEquip_.get();
        if (r.ok()) locker_ = r.body;
        else { setStatus(r.error()); fLocker_ = api_.get("/api/locker"); }
    }
    if (ready(fBuy_)) {
        ApiResult r = fBuy_.get();
        if (r.ok()) {
            setStatus("Purchased! Find it in your locker.");
            audio_.play(Sfx::Chest, 0.7f);
            account_ = r.body["account"];
            fLocker_ = api_.get("/api/locker");
            fShop_ = api_.get("/api/shop");
        } else {
            setStatus(r.error());
        }
    }
    if (ready(fMm_)) {
        ApiResult r = fMm_.get();
        if (r.ok()) {
            mmTicket_ = r.body.value("ticket", std::string());
            screen_ = Screen::Matchmaking;
            mmElapsed_ = 0;
            mmPoll_ = 0.5f;
        } else {
            setStatus(r.error());
        }
    }
    if (ready(fMmStatus_)) {
        ApiResult r = fMmStatus_.get();
        if (screen_ == Screen::Matchmaking) {
            if (!r.ok()) {
                setStatus(r.error());
                screen_ = Screen::Lobby;
            } else {
                std::string st = r.body.value("status", std::string());
                if (st == "assigned") {
                    joinMatch(r.body.value("host", std::string("127.0.0.1")), (uint16_t)r.body.value("port", 7777), r.body.value("match_ticket", std::string()));
                } else if (st == "failed") {
                    setStatus(r.body.value("error", std::string("Matchmaking failed")));
                    screen_ = Screen::Lobby;
                } else if (st == "cancelled") {
                    screen_ = Screen::Lobby;
                }
            }
        }
    }
}

void App::startOffline() {
    stopOffline();
    si::RunnerOptions o;
    o.port = 0;
    o.verbose = false;
    o.game.defaultBots = (int)offlineBots_;
    o.game.botDifficulty = (BotDifficulty)offlineDiff_;
    o.game.warmupSeconds = 8;
    o.game.countdownSeconds = 5;
    o.game.minHumansToStart = 1;
    o.game.resetAfterMatch = true;
    o.game.teamSize = offlineMode_ == 0 ? 1 : offlineMode_ == 1 ? 2 : 4;
    o.game.playlist = offlineMode_ == 0 ? "solo" : offlineMode_ == 1 ? "duos" : "squads";
    o.game.mapSeed = (uint32_t)(std::time(nullptr) % 100000);
    server_ = std::make_unique<si::ServerRunner>(o);
    if (!server_->init()) {
        setStatus("Could not start the local server");
        server_.reset();
        return;
    }
    serverStop_ = false;
    si::ServerRunner* srv = server_.get();
    serverThread_ = std::thread([this, srv] { srv->run(serverStop_); });
    joinMatch("127.0.0.1", server_->port(), "");
}

void App::stopOffline() {
    if (!server_) return;
    serverStop_ = true;
    if (serverThread_.joinable()) serverThread_.join();
    server_.reset();
}

void App::joinMatch(const std::string& host, uint16_t port, const std::string& ticket) {
    if (!net_.connect(host, port, ticket, displayName(), currentLoadout())) {
        setStatus(net_.rejectReason);
        screen_ = Screen::Lobby;
        return;
    }
    game_ = std::make_unique<GameClient>(net_, settings_, audio_);
    screen_ = Screen::InGame;
}

void App::leaveMatch() {
    net_.disconnect();
    game_.reset();
    stopOffline();
    ui::setMouseCaptured(false);
    screen_ = Screen::Lobby;
    refreshProfile();
    saveSettings();
}

void App::run() {
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(1600, 900, "Storm Island");
    SetExitKey(KEY_NULL);
    SetWindowMinSize(960, 540);
    ui::loadFonts();
    loadSettings();
    api_.baseUrl = settings_.backendUrl;
    user_ = settings_.lastUser;
    audio_.init();
    audio_.masterVolume = settings_.volume;
    previewLight_.load();
    if (!api_.token.empty()) fMe_ = api_.get("/api/me");

    // Scripted run used for screenshots and smoke tests: STORM_AUTOTEST=1
    const char* autotestEnv = std::getenv("STORM_AUTOTEST");
    settings_.autotest = autotestEnv && *autotestEnv && std::string(autotestEnv) != "0";
    float autoT = 0;
    int autoShot = 0;
    const float shotTimes[] = {1.0f, 9.0f, 16.0f, 24.0f, 32.0f, 42.0f, 55.0f, 70.0f};
    if (settings_.autotest) { offline_ = true; screen_ = Screen::Lobby; offlineBots_ = 40; offlineDiff_ = 1; }

    while (!WindowShouldClose() && !quit_) {
        float dt = std::min(GetFrameTime(), 0.1f);
        if (settings_.autotest) {
            autoT += dt;
            if (autoT > 1.5f && screen_ == Screen::Lobby && !server_) startOffline();
            if (autoShot < 8 && autoT > shotTimes[autoShot]) {
                TakeScreenshot(TextFormat("autotest_%d.png", autoShot));
                autoShot++;
            }
            if (autoT > 75.0f) quit_ = true;
        }
        pollFutures();
        if (statusTimer_ > 0) statusTimer_ -= dt;
        BeginDrawing();
        ClearBackground(ui::BG); // also clears the depth buffer
        ui::beginFrame();
        switch (screen_) {
            case Screen::Login: drawLogin(); break;
            case Screen::Lobby:
                refreshTimer_ -= dt;
                if (refreshTimer_ <= 0 && !offline_) { refreshTimer_ = 10; fPlaylists_ = api_.get("/api/playlists"); }
                drawLobby();
                break;
            case Screen::Matchmaking:
                mmElapsed_ += dt;
                mmPoll_ -= dt;
                if (mmPoll_ <= 0 && !fMmStatus_.valid()) {
                    fMmStatus_ = api_.get("/api/matchmaking/status?ticket=" + mmTicket_);
                    mmPoll_ = 1.0f;
                }
                drawMatchmaking();
                break;
            case Screen::InGame:
                if (!game_ || !game_->frame(dt)) leaveMatch();
                break;
        }
        drawStatus();
        EndDrawing();
    }
    saveSettings();
    if (game_) leaveMatch();
    // GPU/audio resources must be released while the window/context still exists.
    if (previewRT_.id) UnloadRenderTexture(previewRT_);
    previewRT_ = {};
    previewLight_.unload();
    audio_.shutdown();
    ui::unloadFonts();
    CloseWindow();
}

} // namespace client
