#include "net_client.h"

namespace client {

bool NetClient::connect(const std::string& host, uint16_t port, const std::string& ticket, const std::string& name, const Loadout& l) {
    disconnect();
    if (!NetAddress::resolve(host, port, server_)) {
        state = State::Rejected;
        rejectReason = "Could not resolve " + host;
        return false;
    }
    if (!sock_.open(0)) {
        state = State::Rejected;
        rejectReason = "Could not open a UDP socket";
        return false;
    }
    ticket_ = ticket;
    name_ = name;
    loadout_ = l;
    state = State::Connecting;
    lastHello_ = 0;
    connectStart_ = -1;
    return true;
}

void NetClient::disconnect() {
    if (sock_.isOpen() && state == State::Connected) {
        uint8_t bye = PKT_BYE;
        for (int i = 0; i < 3; i++) sock_.send(server_, &bye, 1);
    }
    sock_.close();
    state = State::Idle;
}

void NetClient::sendHello() {
    ByteWriter w;
    w.u8(PKT_HELLO);
    w.u16(PROTOCOL_VERSION);
    w.str(ticket_);
    w.str(name_);
    writeLoadout(w, loadout_);
    sock_.send(server_, w.buf.data(), w.buf.size());
}

void NetClient::update(double now, std::vector<Snapshot>& snapshots) {
    if (!sock_.isOpen()) return;
    if (state == State::Connecting) {
        if (connectStart_ < 0) connectStart_ = now;
        if (now - lastHello_ > 0.5) { sendHello(); lastHello_ = now; }
        if (now - connectStart_ > 15.0) { state = State::TimedOut; rejectReason = "Connection timed out"; }
    }
    static uint8_t buf[65536];
    NetAddress from;
    for (int i = 0; i < 512; i++) {
        int n = sock_.receive(from, buf, sizeof(buf));
        if (n <= 0) break;
        if (from != server_) continue;
        lastRecv = now;
        ByteReader r(buf + 1, (size_t)n - 1);
        switch (buf[0]) {
            case PKT_WELCOME:
                if (state == State::Connecting) {
                    playerId = r.u16();
                    mapSeed = r.u32();
                    teamSize = r.u8();
                    playlist = r.str();
                    if (r.ok()) state = State::Connected;
                }
                break;
            case PKT_REJECT:
            case PKT_KICK:
                rejectReason = r.str();
                state = State::Rejected;
                break;
            case PKT_SNAPSHOT: {
                if (state != State::Connected) break;
                Snapshot s;
                if (readSnapshot(r, s)) snapshots.push_back(std::move(s));
                break;
            }
            default: break;
        }
    }
    if (state == State::Connected && now - lastRecv > 10.0) {
        state = State::TimedOut;
        rejectReason = "Lost connection to the server";
    }
}

void NetClient::sendInput(uint32_t eventAck, const std::vector<InputCmd>& inputs, const std::vector<ClientAction>& actions) {
    if (state != State::Connected) return;
    ByteWriter w;
    w.u8(PKT_INPUT);
    w.u32(eventAck);
    size_t n = std::min<size_t>(inputs.size(), 8);
    w.u8((uint8_t)n);
    for (size_t i = inputs.size() - n; i < inputs.size(); i++) writeInput(w, inputs[i]);
    size_t na = std::min<size_t>(actions.size(), 16);
    w.u8((uint8_t)na);
    for (size_t i = 0; i < na; i++) {
        w.u16(actions[i].id);
        w.u8((uint8_t)actions[i].type);
        w.u8(actions[i].a);
        w.u8(actions[i].b);
    }
    sock_.send(server_, w.buf.data(), w.buf.size());
}

} // namespace client
