// Top-level client application: screens, backend session, offline server.
#pragma once
#include <atomic>
#include <future>
#include <memory>
#include <string>
#include <thread>

#include "api.h"
#include "audio.h"
#include "game_client.h"
#include "models.h"
#include "net_client.h"
#include "render.h"

namespace si { class ServerRunner; }

namespace client {

enum class Screen { Login, Lobby, Matchmaking, InGame };
enum class LobbyTab { Play, Locker, Shop, Career, Settings };

class App {
public:
    App();
    ~App();
    void run();

private:
    Settings settings_;
    Api api_;
    AudioSystem audio_;
    NetClient net_;
    Lighting previewLight_;
    RenderTexture2D previewRT_{};
    ModelLibrary models_;
    std::unique_ptr<GameClient> game_;
    Screen screen_ = Screen::Login;
    LobbyTab tab_ = LobbyTab::Play;
    bool quit_ = false;

    // session
    bool offline_ = false;
    json account_, locker_, shop_, playlists_, matches_, leaderboard_;
    std::string motd_;
    Loadout offlineLoadout_;
    std::future<ApiResult> fLogin_, fMe_, fLocker_, fShop_, fPlaylists_, fMatches_, fBoard_, fNews_, fEquip_, fBuy_, fMm_, fMmStatus_;
    std::string status_;
    float statusTimer_ = 0;
    float refreshTimer_ = 0;

    // login form
    std::string user_, pass_, display_;
    bool registerMode_ = false;

    // play
    std::string playlist_ = "solo";
    std::string mmTicket_;
    float mmElapsed_ = 0, mmPoll_ = 0;
    float offlineBots_ = 30;
    int offlineDiff_ = 1;
    int offlineMode_ = 0; // 0 solo, 1 duos, 2 squads
    std::string directHost_ = "127.0.0.1:7777";

    // locker
    int lockerSlot_ = 0; // 0 outfit,1 backbling,2 pickaxe,3 glider,4 contrail,5-10 emotes
    float spin_ = 0;
    float lockerScroll_ = 0;

    // offline server
    std::unique_ptr<si::ServerRunner> server_;
    std::thread serverThread_;
    std::atomic<bool> serverStop_{false};

    void loadSettings();
    void saveSettings();
    void setStatus(const std::string& s);
    void pollFutures();
    void refreshProfile();
    Loadout currentLoadout() const;
    std::string displayName() const;
    bool ownsItem(const std::string& id) const;
    void equip(const std::string& id, int emoteSlot);
    void startOffline();
    void stopOffline();
    void joinMatch(const std::string& host, uint16_t port, const std::string& ticket);
    void leaveMatch();

    // screens (menus.cpp)
    void drawLogin();
    void drawLobby();
    void drawPlayTab(Rectangle area);
    void drawLockerTab(Rectangle area);
    void drawShopTab(Rectangle area);
    void drawCareerTab(Rectangle area);
    void drawSettingsTab(Rectangle area);
    void drawMatchmaking();
    void drawPreview(Rectangle area, const Loadout& l);
    // STORM_SHOWROOM=1: renders every vehicle (with riders) from a few angles for screenshots.
    void drawShowroom(int view, float t);
    void drawStatus();
};

} // namespace client
