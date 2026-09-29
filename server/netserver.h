// UDP transport for the game server: handshakes, input intake, snapshots and
// reliable event delivery (events are re-sent until the client acknowledges them).
#pragma once
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <unordered_map>

#include "../shared/net/snapshot.h"
#include "../shared/net/udp.h"
#include "game.h"

namespace si {

struct TicketInfo {
    bool ok = false;
    std::string accountId;
    std::string displayName;
    Loadout loadout;
    std::string reason;
};

class NetServer {
public:
    explicit NetServer(Game& g);
    bool start(uint16_t port);
    void stop();
    // Called every server tick.
    void poll();
    void sendSnapshots();
    uint16_t port() const { return sock_.localPort(); }
    size_t clientCount() const { return clients_.size(); }
    void kickPlayer(uint16_t playerId, const std::string& reason);

    // When set, HELLO tickets are validated through this (runs on a worker thread).
    std::function<TicketInfo(const std::string& ticket)> validateTicket;
    std::function<void(const std::string&)> log;

private:
    struct Client {
        NetAddress addr;
        uint16_t playerId = 0;
        uint32_t nextSeq = 1;
        uint32_t acked = 0;
        std::deque<SnapEvent> pending;
        double lastRecv = 0;
    };
    struct PendingHello {
        NetAddress addr;
        std::string name;
        Loadout loadout;
        std::future<TicketInfo> fut;
        double started;
    };

    Game& game_;
    UdpSocket sock_;
    std::unordered_map<NetAddress, Client, NetAddressHash> clients_;
    std::vector<std::unique_ptr<PendingHello>> pending_;
    uint32_t tick_ = 0;
    double now() const;

    void handlePacket(const NetAddress& from, const uint8_t* data, size_t len);
    void onHello(const NetAddress& from, ByteReader& r);
    void acceptClient(const NetAddress& from, const std::string& name, const std::string& accountId, const Loadout& l);
    void sendReject(const NetAddress& to, const std::string& reason);
    void pushEvent(Client& c, const std::vector<uint8_t>& ev);
    Client* clientForPlayer(uint16_t id);
};

uint16_t playerFlags(const Player& p);

} // namespace si
