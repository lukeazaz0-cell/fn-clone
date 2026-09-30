#include "netserver.h"

#include <chrono>

namespace si {

static constexpr size_t EVENT_BUDGET = 7000; // bytes of reliable events per snapshot
static constexpr double CLIENT_TIMEOUT = 12.0;

uint16_t playerFlags(const Player& p) {
    uint16_t f = 0;
    if (p.alive && !p.eliminated) f |= PF_ALIVE;
    if (p.dbno) f |= PF_DBNO;
    if (p.move.crouched) f |= PF_CROUCH;
    if (p.buttons & IN_ADS) f |= PF_ADS;
    if (p.buildMode) f |= PF_BUILDING;
    if ((p.buttons & IN_FIRE) && !p.buildMode) f |= PF_FIRING;
    if (p.action == ACT_RELOAD) f |= PF_RELOADING;
    if (p.action == ACT_CONSUME) f |= PF_CONSUMING;
    if (p.emote) f |= PF_EMOTING;
    if (p.bot) f |= PF_BOT;
    if (p.selected == 0 && (p.buttons & IN_FIRE) && !p.buildMode) f |= PF_HARVESTING;
    if (p.buttons & IN_SPRINT) f |= PF_SPRINT;
    if (p.action == ACT_REVIVE) f |= PF_REVIVING;
    if (p.eliminated) f |= PF_SPECTATOR;
    return f;
}

NetServer::NetServer(Game& g) : game_(g) {
    game_.emitEvent = [this](const std::vector<uint8_t>& ev, int target) {
        if (target < 0) {
            for (auto& [addr, c] : clients_) pushEvent(c, ev);
        } else if (Client* c = clientForPlayer((uint16_t)target)) {
            pushEvent(*c, ev);
        }
    };
}

double NetServer::now() const {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool NetServer::start(uint16_t port) { return sock_.open(port); }
void NetServer::stop() { sock_.close(); }

NetServer::Client* NetServer::clientForPlayer(uint16_t id) {
    for (auto& [addr, c] : clients_)
        if (c.playerId == id) return &c;
    return nullptr;
}

void NetServer::pushEvent(Client& c, const std::vector<uint8_t>& ev) {
    SnapEvent e;
    e.seq = c.nextSeq++;
    e.data = ev;
    c.pending.push_back(std::move(e));
}

void NetServer::sendReject(const NetAddress& to, const std::string& reason) {
    ByteWriter w;
    w.u8(PKT_REJECT);
    w.str(reason);
    sock_.send(to, w.buf.data(), w.buf.size());
}

void NetServer::kickPlayer(uint16_t playerId, const std::string& reason) {
    for (auto it = clients_.begin(); it != clients_.end(); ++it) {
        if (it->second.playerId == playerId) {
            ByteWriter w;
            w.u8(PKT_KICK);
            w.str(reason);
            sock_.send(it->first, w.buf.data(), w.buf.size());
            clients_.erase(it);
            break;
        }
    }
    game_.removePlayer(playerId);
}

void NetServer::poll() {
    uint8_t buf[65536];
    NetAddress from;
    for (int i = 0; i < 4096; i++) {
        int n = sock_.receive(from, buf, sizeof(buf));
        if (n <= 0) break;
        handlePacket(from, buf, (size_t)n);
    }
    // Finish ticket validations
    for (auto it = pending_.begin(); it != pending_.end();) {
        PendingHello& ph = **it;
        if (ph.fut.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            TicketInfo info = ph.fut.get();
            if (info.ok) {
                std::string name = info.displayName.empty() ? ph.name : info.displayName;
                acceptClient(ph.addr, name, info.accountId, info.loadout);
            } else {
                sendReject(ph.addr, info.reason.empty() ? "Invalid match ticket" : info.reason);
            }
            it = pending_.erase(it);
        } else {
            ++it;
        }
    }
    // Timeouts
    double t = now();
    for (auto it = clients_.begin(); it != clients_.end();) {
        if (t - it->second.lastRecv > CLIENT_TIMEOUT) {
            if (log) log("client timed out: " + it->first.toString());
            uint16_t pid = it->second.playerId;
            it = clients_.erase(it);
            game_.removePlayer(pid);
        } else {
            ++it;
        }
    }
}

void NetServer::handlePacket(const NetAddress& from, const uint8_t* data, size_t len) {
    if (len < 1) return;
    ByteReader r(data + 1, len - 1);
    switch (data[0]) {
        case PKT_HELLO: onHello(from, r); break;
        case PKT_INPUT: {
            auto it = clients_.find(from);
            if (it == clients_.end()) return;
            Client& c = it->second;
            c.lastRecv = now();
            uint32_t ack = r.u32();
            if (ack > c.acked && ack < c.nextSeq) {
                c.acked = ack;
                while (!c.pending.empty() && c.pending.front().seq <= ack) c.pending.pop_front();
            }
            auto pit = game_.players.find(c.playerId);
            if (pit == game_.players.end()) return;
            Player& p = pit->second;
            p.lastInputTime = (float)c.lastRecv;
            uint8_t n = r.u8();
            for (int i = 0; i < n && r.ok(); i++) {
                InputCmd cmd = readInput(r);
                if (r.ok()) game_.queueInput(p, cmd);
            }
            uint8_t na = r.u8();
            for (int i = 0; i < na && r.ok(); i++) {
                ClientAction a;
                a.id = r.u16();
                a.type = (ActionType)r.u8();
                a.a = r.u8();
                a.b = r.u8();
                if (r.ok()) game_.handleAction(p, a);
            }
            break;
        }
        case PKT_BYE: {
            auto it = clients_.find(from);
            if (it == clients_.end()) return;
            uint16_t pid = it->second.playerId;
            clients_.erase(it);
            game_.removePlayer(pid);
            if (log) log("client left: " + from.toString());
            break;
        }
        default: break;
    }
}

void NetServer::onHello(const NetAddress& from, ByteReader& r) {
    uint16_t version = r.u16();
    std::string ticket = r.str();
    std::string name = r.str();
    Loadout l = readLoadout(r);
    if (!r.ok()) return;
    auto existing = clients_.find(from);
    if (existing != clients_.end()) {
        // Duplicate hello (our welcome was lost): resend welcome.
        ByteWriter w;
        w.u8(PKT_WELCOME);
        w.u16(existing->second.playerId);
        w.u32(game_.cfg.mapSeed);
        w.u8((uint8_t)game_.cfg.teamSize);
        w.str(game_.cfg.playlist);
        sock_.send(from, w.buf.data(), w.buf.size());
        return;
    }
    for (auto& ph : pending_)
        if (ph->addr == from) return;
    if (version != PROTOCOL_VERSION) { sendReject(from, "Version mismatch - please update your client"); return; }
    if ((int)game_.players.size() >= game_.cfg.maxPlayers) { sendReject(from, "Server is full"); return; }
    if (name.empty()) name = "Player";
    if (name.size() > 20) name.resize(20);
    if (validateTicket) {
        auto ph = std::make_unique<PendingHello>();
        ph->addr = from;
        ph->name = name;
        ph->loadout = l;
        ph->started = now();
        auto fn = validateTicket;
        ph->fut = std::async(std::launch::async, [fn, ticket]() { return fn(ticket); });
        pending_.push_back(std::move(ph));
    } else {
        acceptClient(from, name, "", l);
    }
}

void NetServer::acceptClient(const NetAddress& from, const std::string& name, const std::string& accountId, const Loadout& l) {
    // Same account reconnecting from a new address: drop the old session.
    if (!accountId.empty()) {
        for (auto it = clients_.begin(); it != clients_.end(); ++it) {
            auto pit = game_.players.find(it->second.playerId);
            if (pit != game_.players.end() && pit->second.accountId == accountId) {
                uint16_t pid = it->second.playerId;
                clients_.erase(it);
                game_.removePlayer(pid);
                break;
            }
        }
    }
    Client c;
    c.addr = from;
    c.lastRecv = now();
    // Queue full world state before creating the player so the info event ordering is sane.
    std::vector<std::vector<uint8_t>> full;
    game_.writeFullStateEvents(full);
    Client& ref = clients_[from];
    ref = c;
    for (auto& ev : full) pushEvent(ref, ev);
    Player* p = game_.addPlayer(name, accountId, l, false);
    if (!p) {
        clients_.erase(from);
        sendReject(from, "Server is full");
        return;
    }
    clients_[from].playerId = p->id;
    ByteWriter w;
    w.u8(PKT_WELCOME);
    w.u16(p->id);
    w.u32(game_.cfg.mapSeed);
    w.u8((uint8_t)game_.cfg.teamSize);
    w.str(game_.cfg.playlist);
    sock_.send(from, w.buf.data(), w.buf.size());
    if (log) log("client connected: " + name + " from " + from.toString());
}

void NetServer::sendSnapshots() {
    tick_++;
    Snapshot base;
    base.tick = tick_;
    base.time = game_.time;
    SnapGlobal& g = base.g;
    g.phase = game_.phase;
    g.phaseTimer = game_.phaseTimer;
    g.alive = (uint16_t)game_.aliveCount();
    g.total = (uint16_t)game_.players.size();
    g.teamSize = (uint8_t)game_.cfg.teamSize;
    bool stormOn = game_.phase == MatchPhase::Bus || game_.phase == MatchPhase::Playing || game_.phase == MatchPhase::Ended;
    g.stormPhase = stormOn ? (int8_t)game_.storm.phase : -1;
    g.stormShrinking = game_.storm.shrinking ? 1 : 0;
    g.stormTimer = game_.storm.timer / std::max(0.01f, game_.cfg.stormSpeed);
    g.stormCur = game_.storm.curCenter;
    g.stormCurR = stormOn ? game_.storm.curRadius : 5000;
    g.stormNext = game_.storm.nextCenter;
    g.stormNextR = game_.storm.nextRadius;
    g.stormDamage = game_.storm.damage;
    g.busActive = game_.busActive ? 1 : 0;
    g.busStart = game_.busStart;
    g.busEnd = game_.busEnd;
    g.busProgress = game_.busProgress;
    for (auto& [id, p] : game_.players) {
        if (p.eliminated && !p.alive) continue;
        SnapPlayer sp;
        sp.id = id;
        sp.flags = playerFlags(p);
        sp.mode = p.move.mode;
        sp.pos = p.move.pos;
        sp.yaw = p.yaw;
        sp.pitch = p.pitch;
        const ItemStack* h = p.held();
        sp.heldType = h ? (uint8_t)h->type : 0;
        sp.heldRarity = h ? (uint8_t)h->rarity : 0;
        sp.health = (uint8_t)clampf(p.dbno ? p.dbnoHealth : p.health, 0, 255);
        sp.shield = (uint8_t)clampf(p.shield, 0, 255);
        sp.team = p.team;
        sp.emote = p.emote;
        sp.buildPiece = p.buildMode ? (uint8_t)p.buildPiece : 0xFF;
        sp.vehicle = p.vehicle;
        sp.seat = p.seat;
        base.players.push_back(sp);
    }
    for (auto& pr : game_.projectiles) base.projectiles.push_back({pr.id, (uint8_t)pr.type, pr.pos});
    for (auto& d : game_.supplyDrops)
        if (!d.opened) base.drops.push_back({d.id, d.pos});
    for (auto& e : game_.effects) base.effects.push_back({(uint8_t)e.type, e.player, e.a, e.b, e.extra});
    std::vector<SnapVehicle> allVehicles;
    for (auto& v : game_.vehicles) {
        if (!v.alive) continue;
        SnapVehicle sv;
        sv.id = v.id;
        sv.type = (uint8_t)v.type;
        sv.pos = v.st.pos;
        sv.yaw = v.st.yaw;
        sv.pitch = v.st.pitch;
        sv.roll = v.st.roll;
        sv.hp = (uint8_t)clampf(v.hp / std::max(1.0f, v.maxHp) * 255.0f, 1, 255);
        sv.flags = (v.st.boosting ? VF_BOOST : 0) | (v.st.onGround ? VF_GROUND : 0) | (v.hasDriver() ? VF_DRIVER : 0) | (v.st.inWater ? VF_WATER : 0);
        allVehicles.push_back(sv);
    }
    game_.effects.clear();

    ByteWriter w;
    for (auto& [addr, c] : clients_) {
        Snapshot s = base; // copy (players list shared per tick is small)
        auto pit = game_.players.find(c.playerId);
        // Vehicles near the viewer (or the player they spectate)
        {
            Vec3 view{WORLD_SIZE / 2, 0, WORLD_SIZE / 2};
            bool whole = true;
            if (pit != game_.players.end()) {
                const Player& vp = pit->second;
                auto sit = vp.eliminated ? game_.players.find(vp.spectating) : pit;
                const Player& focus = sit != game_.players.end() ? sit->second : vp;
                if (focus.move.mode != MoveMode::OnBus && focus.move.mode != MoveMode::Skydive) { view = focus.move.pos; whole = false; }
                else if (focus.move.mode == MoveMode::Skydive) { view = focus.move.pos; whole = false; }
            }
            for (auto& sv : allVehicles) {
                bool mine = pit != game_.players.end() && pit->second.vehicle == sv.id;
                if (whole || mine || distXZ(sv.pos, view) < 420.0f) s.vehicles.push_back(sv);
            }
            if (whole && s.vehicles.size() > 160) s.vehicles.resize(160);
        }
        if (pit != game_.players.end()) {
            const Player& p = pit->second;
            s.hasSelf = true;
            s.lastInputSeq = p.lastInputSeq;
            s.lastActionId = p.lastActionId;
            SnapSelf& m = s.self;
            m.id = p.id;
            m.flags = playerFlags(p);
            m.mode = p.move.mode;
            m.pos = p.move.pos;
            m.vel = p.move.vel;
            m.mflags = (p.move.onGround ? 1 : 0) | (p.move.canGlide ? 2 : 0) | (p.move.crouched ? 4 : 0);
            m.prevButtons = p.move.prevButtons;
            m.fallStartY = p.move.fallStartY;
            m.health = p.health;
            m.shield = p.shield;
            m.dbnoHealth = p.dbnoHealth;
            m.selected = (uint8_t)p.selected;
            for (int i = 0; i < INVENTORY_SLOTS; i++) m.inv[i] = p.inv[i];
            for (int i = 0; i < (int)AmmoType::Count; i++) m.ammo[i] = (uint16_t)p.ammo[i];
            for (int i = 0; i < 3; i++) m.mats[i] = (uint16_t)p.mats[i];
            m.buildMat = (uint8_t)p.buildMat;
            m.buildMode = p.buildMode ? 1 : 0;
            m.buildPiece = (uint8_t)p.buildPiece;
            m.buildRot = p.buildRot;
            m.action = (uint8_t)p.action;
            m.actionTime = p.actionTime;
            m.actionTotal = p.actionTotal;
            m.kills = (uint16_t)p.kills;
            m.spectating = p.spectating;
            if (p.eliminated) {
                // Keep spectating someone alive.
                auto sit = game_.players.find(p.spectating);
                if (sit == game_.players.end() || !sit->second.active()) {
                    for (auto& [oid, o] : game_.players)
                        if (o.active()) { m.spectating = oid; const_cast<Player&>(p).spectating = oid; break; }
                }
            }
            m.regenLeft = p.regenLeft;
            m.placement = (uint8_t)std::min(255, p.placement);
            m.bloom = p.bloom;
            m.team = p.team;
            m.vehicle = p.vehicle;
            m.seat = p.seat;
            if (const Vehicle* v = game_.findVehicle(p.vehicle)) {
                m.vehicleType = (uint8_t)v->type;
                m.veh = v->st;
            } else {
                m.vehicle = 0xFFFF;
                m.seat = NO_SEAT;
            }
        }
        size_t budget = 0;
        for (auto& e : c.pending) {
            if (budget + e.data.size() + 6 > EVENT_BUDGET && !s.events.empty()) break;
            budget += e.data.size() + 6;
            s.events.push_back(e);
        }
        w.buf.clear();
        writeSnapshot(w, s);
        sock_.send(addr, w.buf.data(), w.buf.size());
    }
}

} // namespace si
