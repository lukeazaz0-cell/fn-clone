// UDP connection to a game server.
#pragma once
#include <deque>
#include <string>
#include <vector>

#include "shared/net/snapshot.h"
#include "shared/net/udp.h"

namespace client {

using namespace si;

class NetClient {
public:
    enum class State { Idle, Connecting, Connected, Rejected, TimedOut };

    bool connect(const std::string& host, uint16_t port, const std::string& ticket, const std::string& name, const Loadout& l);
    void disconnect();
    // Pumps the socket; fills `snapshots` with any complete snapshots received.
    void update(double now, std::vector<Snapshot>& snapshots);
    void sendInput(uint32_t eventAck, const std::vector<InputCmd>& inputs, const std::vector<ClientAction>& actions);

    State state = State::Idle;
    std::string rejectReason;
    uint16_t playerId = 0;
    uint32_t mapSeed = 0;
    uint8_t teamSize = 1;
    std::string playlist;
    double lastRecv = 0;
    float rttEstimate = 0.05f;

private:
    UdpSocket sock_;
    NetAddress server_;
    std::string ticket_, name_;
    Loadout loadout_;
    double lastHello_ = 0, connectStart_ = 0;
    void sendHello();
};

} // namespace client
