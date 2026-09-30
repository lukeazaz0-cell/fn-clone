#include "game_client.h"

#include "models.h"

#include <algorithm>
#include <cmath>

#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include "shared/game/items.h"
#include "ui.h"

namespace client {

namespace {
Color darker4(Color c) { return {(unsigned char)(c.r * 0.6f), (unsigned char)(c.g * 0.6f), (unsigned char)(c.b * 0.6f), c.a}; }
bool newer16(uint16_t a, uint16_t b) { return (int16_t)(a - b) > 0; }
float angleLerp(float a, float b, float t) { return a + wrapAngle(b - a) * t; }
si::Vec3 rotateAround(const si::Vec3& v, const si::Vec3& axis, float ang) {
    float c = std::cos(ang), sn = std::sin(ang);
    return v * c + axis.cross(v) * sn + axis * (axis.dot(v) * (1 - c));
}
void rollBasis(Basis& b, const si::Vec3& delta, float radius) {
    si::Vec3 flat{delta.x, 0, delta.z};
    float d = flat.len();
    if (d < 1e-4f) return;
    si::Vec3 axis = si::Vec3{0, 1, 0}.cross(flat / d).norm();
    float ang = d / radius;
    b.r = rotateAround(b.r, axis, ang);
    b.u = rotateAround(b.u, axis, ang);
    b.f = rotateAround(b.f, axis, ang);
    // Gram-Schmidt, keeping the basis handedness
    b.f = b.f.norm();
    b.u = (b.u - b.f * b.u.dot(b.f)).norm();
    si::Vec3 rc = b.u.cross(b.f);
    b.r = rc * (b.r.dot(rc) < 0 ? -1.0f : 1.0f);
}
}

GameClient::GameClient(NetClient& net, Settings& settings, AudioSystem& audio) : net_(net), settings_(settings), audio_(audio) {
    light_.load();
    if (models()) models()->useShader(light_.model);
    cam_.up = {0, 1, 0};
    cam_.fovy = settings_.fov;
    cam_.projection = CAMERA_PERSPECTIVE;
}

GameClient::~GameClient() {
    audio_.loop(Loop::Engine, 0);
    audio_.loop(Loop::Ambience, 0);
    worldR_.unload();
    light_.unload();
    ui::setMouseCaptured(false);
}

void GameClient::initMap(uint32_t seed) {
    map_.generate(seed);
    world_.setMap(&map_);
    worldR_.build(map_, world_, light_);
    chestOpened_.assign(map_.chests.size(), 0);
    ammoBoxOpened_.assign(map_.ammoBoxes.size(), 0);
    mapReady_ = true;
    // Drifting clouds (CC0 cloud model), deterministic per map
    Rng cr(seed ^ 0xC10D, 9);
    clouds_.clear();
    for (int i = 0; i < 46; i++) {
        Cloud c;
        c.pos = {cr.range(-300, WORLD_SIZE + 300), cr.range(190, 270), cr.range(-300, WORLD_SIZE + 300)};
        c.size = {cr.range(40, 90), cr.range(14, 26), cr.range(30, 70)};
        c.speed = cr.range(1.5f, 4.0f);
        clouds_.push_back(c);
    }
}

std::string GameClient::playerName(uint16_t id) const {
    if (id == 0xFFFF) return "The storm";
    auto it = players_.find(id);
    return it == players_.end() ? "Someone" : it->second.name;
}

bool GameClient::localControllable() const {
    if (!haveSelf_) return false;
    if (!(self_.flags & PF_ALIVE)) return false;
    return self_.mode != MoveMode::OnBus && self_.mode != MoveMode::Dead && self_.mode != MoveMode::Spectate;
}

const RemotePlayer* GameClient::spectateTarget() const {
    if (!haveSelf_ || (self_.flags & PF_ALIVE)) return nullptr;
    auto it = players_.find(self_.spectating);
    if (it == players_.end() || !it->second.present) return nullptr;
    return &it->second;
}

void GameClient::pushAction(ActionType t, uint8_t a, uint8_t b) {
    ClientAction ca;
    ca.id = ++actionSeq_;
    ca.type = t;
    ca.a = a;
    ca.b = b;
    pendingActions_.push_back(ca);
    if (pendingActions_.size() > 32) pendingActions_.erase(pendingActions_.begin());
}

// ======================================================================= snapshots

void GameClient::applySnapshot(const Snapshot& s) {
    // Reliable events, strictly in order
    for (auto& e : s.events) {
        if (e.seq <= eventAck_) continue;
        if (e.seq != eventAck_ + 1) break;
        handleEvent(e.data);
        eventAck_ = e.seq;
    }
    serverTime_ = s.time;
    if (!timeInit_) { renderTime_ = s.time - 0.1f; timeInit_ = true; }
    uint8_t prevPhase = (uint8_t)g_.phase;
    g_ = s.g;
    if (prevPhase != (uint8_t)g_.phase && g_.phase == MatchPhase::Bus) audio_.play(Sfx::Bus, 0.6f);

    // Acked actions
    pendingActions_.erase(std::remove_if(pendingActions_.begin(), pendingActions_.end(),
                                         [&](const ClientAction& a) { return !newer16(a.id, s.lastActionId); }),
                          pendingActions_.end());
    if (s.hasSelf) {
        bool wasAlive = haveSelf_ && (self_.flags & PF_ALIVE);
        self_ = s.self;
        haveSelf_ = true;
        reconcile(s);
        if (wasAlive && !(self_.flags & PF_ALIVE)) { inventoryOpen_ = false; emoteWheel_ = false; }
    }
    for (auto& [id, p] : players_) p.present = false;
    for (auto& sp : s.players) {
        RemotePlayer& rp = players_[sp.id];
        rp.id = sp.id;
        rp.present = true;
        if (!rp.buf.empty() && s.time <= rp.buf.back().t) continue;
        rp.buf.push_back({s.time, sp});
        while (rp.buf.size() > 40) rp.buf.pop_front();
    }
    for (auto& [id, v] : vehicles_) v.present = false;
    for (auto& sv : s.vehicles) {
        ClientVehicle& cv = vehicles_[sv.id];
        cv.id = sv.id;
        cv.present = true;
        if (!cv.buf.empty() && s.time <= cv.buf.back().t) continue;
        cv.buf.push_back({s.time, sv});
        while (cv.buf.size() > 40) cv.buf.pop_front();
    }
    for (auto it = vehicles_.begin(); it != vehicles_.end();) {
        if (!it->second.present) it = vehicles_.erase(it);
        else ++it;
    }
    projectiles_ = s.projectiles;
    for (auto& d : s.drops) {
        auto it = drops_.find(d.id);
        if (it != drops_.end()) it->second.pos = d.pos;
    }
    si::Vec3 me = viewPos();
    for (auto& e : s.effects) {
        float dist = (e.a - me).len();
        float vol = clampf(1.0f - dist / 250.0f, 0.0f, 1.0f);
        switch ((EffectType)e.type) {
            case FX_SHOT: {
                Color c = e.player == net_.playerId ? Color{255, 240, 180, 255} : Color{255, 220, 140, 255};
                tracers_.push_back({e.a, e.b, 0.07f, c});
                ItemType t = (ItemType)e.extra;
                Sfx sfx = Sfx::Rifle;
                float pitch = 1.0f;
                if (t == ItemType::PumpShotgun || t == ItemType::TacticalShotgun || t == ItemType::DoubleBarrel || t == ItemType::HeavyShotgun) sfx = Sfx::Shotgun;
                else if (t == ItemType::SMG || t == ItemType::Minigun || t == ItemType::CompactSMG) sfx = Sfx::Smg;
                else if (t == ItemType::SniperRifle || t == ItemType::HuntingRifle) sfx = Sfx::Sniper;
                else if (t == ItemType::Pistol) sfx = Sfx::Pistol;
                else if (t == ItemType::HandCannon) { sfx = Sfx::Sniper; pitch = 1.3f; }
                if (t == ItemType::HeavyRifle) pitch = 0.85f;
                if (models() && models()->hasHeld(HeldModel::Blaster)) {
                    if (t == ItemType::Pistol) { sfx = Sfx::Blaster; pitch = 1.0f; }
                    if (t == ItemType::CompactSMG) { sfx = Sfx::BlasterRepeater; pitch = 1.0f; }
                }
                if (t == ItemType::CompactSMG) pitch = 1.2f;
                // Muzzle flash and a brief flash light cue
                si::Vec3 mdir = (e.b - e.a).norm();
                flashes_.push_back({e.a + mdir * 0.9f, 0.05f});
                // Only one sound per shot (shotguns emit several tracer effects)
                static uint16_t lastPlayer = 0xFFFF;
                static float lastTime = -1;
                if (!(lastPlayer == e.player && time_ - lastTime < 0.03f)) audio_.play(sfx, vol * (e.player == net_.playerId ? 0.8f : 1.0f), pitch);
                lastPlayer = e.player;
                lastTime = time_;
                break;
            }
            case FX_EXPLOSION:
                blasts_.push_back({e.a, 0, e.extra == 1 ? 6.0f : 5.0f});
                shake_ = std::max(shake_, clampf(1.0f - dist / 60.0f, 0.0f, 1.0f) * 0.8f);
                audio_.play(Sfx::Explosion, vol);
                for (int i = 0; i < 24; i++) {
                    si::Vec3 v{GetRandomValue(-100, 100) / 20.0f, GetRandomValue(20, 160) / 20.0f, GetRandomValue(-100, 100) / 20.0f};
                    particles_.push_back({e.a, v, 0, 1.2f, Color{90, 85, 80, 255}, 0.35f});
                }
                break;
            case FX_IMPACT:
            case FX_HARVEST: {
                Color c = e.extra == (uint8_t)si::Material::Wood ? Color{160, 120, 70, 255} : e.extra == (uint8_t)si::Material::Brick ? Color{170, 100, 80, 255} : Color{190, 195, 205, 255};
                for (int i = 0; i < (e.type == FX_HARVEST ? 8 : 4); i++) {
                    si::Vec3 v{GetRandomValue(-100, 100) / 30.0f, GetRandomValue(20, 120) / 30.0f, GetRandomValue(-100, 100) / 30.0f};
                    particles_.push_back({e.a, v, 0, 0.6f, c, 0.12f});
                }
                if (e.type == FX_HARVEST) audio_.play(Sfx::Pickaxe, vol, 0.9f + GetRandomValue(0, 20) / 100.0f);
                else if (vol > 0.5f) audio_.play(Sfx::Impact, vol * 0.25f, 1.4f);
                break;
            }
            case FX_BUILD: audio_.play(Sfx::Build, vol * 0.7f); break;
            case FX_HONK: {
                float hv = clampf(1.0f - dist / 120.0f, 0.0f, 1.0f);
                audio_.play(Sfx::Horn, hv * 0.8f, e.extra == (uint8_t)VehicleType::CrashQuad ? 0.8f : e.extra == (uint8_t)VehicleType::Trolley ? 1.4f : 1.0f);
                break;
            }
            case FX_BOOST: audio_.play(Sfx::Boost, vol * 0.8f, e.extra == (uint8_t)VehicleType::CrashQuad ? 0.85f : 1.1f); break;
            case FX_CRASH: {
                audio_.play(Sfx::Crash, vol * clampf(e.extra / 120.0f, 0.3f, 1.0f));
                shake_ = std::max(shake_, clampf(1.0f - dist / 25.0f, 0.0f, 1.0f) * 0.45f);
                for (int i = 0; i < 10; i++) {
                    si::Vec3 v{GetRandomValue(-100, 100) / 25.0f, GetRandomValue(20, 120) / 25.0f, GetRandomValue(-100, 100) / 25.0f};
                    particles_.push_back({e.a, v, 0, 0.7f, Color{120, 110, 100, 255}, 0.15f});
                }
                break;
            }
            default: break;
        }
    }
}

void GameClient::reconcile(const Snapshot& s) {
    bool predictable = self_.mode == MoveMode::Ground || self_.mode == MoveMode::Air || self_.mode == MoveMode::Skydive ||
                       self_.mode == MoveMode::Glide || self_.mode == MoveMode::Swim;
    si::Vec3 before = pred_.pos;
    while (!pendingInputs_.empty() && pendingInputs_.front().seq <= s.lastInputSeq) pendingInputs_.pop_front();
    pred_.pos = self_.pos;
    pred_.vel = self_.vel;
    pred_.mode = self_.mode;
    pred_.onGround = self_.mflags & 1;
    pred_.canGlide = self_.mflags & 2;
    pred_.crouched = self_.mflags & 4;
    pred_.prevButtons = self_.prevButtons;
    pred_.fallStartY = self_.fallStartY;
    if (self_.vehicle != 0xFFFF && self_.seat == 0 && (self_.flags & PF_ALIVE)) {
        // Driving: predict the vehicle itself and replay unacknowledged inputs through it.
        bool continuing = driving_ && drivingId_ == self_.vehicle;
        si::Vec3 beforeVeh = predVeh_.pos;
        float beforeYaw = predVeh_.yaw;
        predVeh_ = self_.veh;
        drivingType_ = (VehicleType)self_.vehicleType;
        drivingId_ = self_.vehicle;
        driving_ = true;
        for (auto& in : pendingInputs_) stepPrediction(in);
        if (continuing) {
            vehSmooth_ += beforeVeh - predVeh_.pos;
            vehYawSmooth_ = wrapAngle(vehYawSmooth_ + wrapAngle(beforeYaw - predVeh_.yaw));
            if (vehSmooth_.len() > 4.0f) vehSmooth_ = {};
            if (std::fabs(vehYawSmooth_) > 0.8f) vehYawSmooth_ = 0;
        } else {
            vehSmooth_ = {};
            vehYawSmooth_ = 0;
            smoothOffset_ = {};
        }
        pred_.pos = vehicleSeatPos(predVeh_, drivingType_, 0);
        pred_.mode = MoveMode::Vehicle;
        predInit_ = true;
        return;
    }
    driving_ = false;
    if (self_.mode == MoveMode::Vehicle) {
        // Passenger: the view follows the interpolated vehicle
        pendingInputs_.clear();
        smoothOffset_ = {};
        predInit_ = false;
        return;
    }
    if (!predictable || !(self_.flags & PF_ALIVE)) {
        pendingInputs_.clear();
        smoothOffset_ = {};
        predInit_ = false;
        return;
    }
    for (auto& in : pendingInputs_) stepPrediction(in);
    if (predInit_) {
        smoothOffset_ += before - pred_.pos;
        if (smoothOffset_.len() > 2.5f) smoothOffset_ = {};
    }
    predInit_ = true;
}

MoveEvents GameClient::stepPrediction(const InputCmd& in) {
    MoveInput mi;
    mi.yaw = in.yaw;
    mi.pitch = in.pitch;
    mi.fwd = in.fwd;
    mi.right = in.right;
    mi.buttons = in.buttons;
    if (driving_) {
        VehicleEvents ve = stepVehicle(predVeh_, drivingType_, mi, true, world_, SIM_DT, &launchPads_);
        pred_.pos = vehicleSeatPos(predVeh_, drivingType_, 0);
        pred_.vel = predVeh_.vel;
        MoveEvents me;
        me.jumped = ve.jumped;
        me.landed = ve.landed;
        return me;
    }
    if (self_.mode == MoveMode::Vehicle) return MoveEvents();
    MoveParams mp;
    mp.launchPads = &launchPads_;
    mp.dbno = self_.flags & PF_DBNO;
    if (self_.action == ACT_CONSUME && self_.selected >= 1 && self_.selected <= 5) {
        const ItemStack& h = self_.inv[self_.selected - 1];
        if (!h.empty() && !itemDef(h.type).consumable.movementWhileUsing) mp.rooted = true;
    }
    if (self_.action == ACT_REVIVE) mp.rooted = true;
    if (self_.selected >= 1 && self_.selected <= 5 && self_.inv[self_.selected - 1].type == ItemType::Minigun) mp.speedMul = 0.85f;
    if ((self_.flags & PF_EMOTING) && !mi.fwd && !mi.right && !(mi.buttons & (IN_FIRE | IN_JUMP))) { mi.fwd = mi.right = 0; }
    return stepMovement(pred_, mi, world_, map_, SIM_DT, mp);
}

void GameClient::handleEvent(const std::vector<uint8_t>& ev) {
    if (ev.empty()) return;
    ByteReader r(ev.data() + 1, ev.size() - 1);
    si::Vec3 me = viewPos();
    switch ((EventType)ev[0]) {
        case EV_PLAYER_INFO: {
            uint16_t id = r.u16();
            RemotePlayer& p = players_[id];
            p.id = id;
            p.name = r.str();
            p.team = r.u8();
            p.bot = r.u8() != 0;
            p.loadout = readLoadout(r);
            break;
        }
        case EV_PLAYER_LEFT: players_.erase(r.u16()); break;
        case EV_STRUCT_ADD: {
            Structure s;
            s.id = r.u32();
            s.piece = (PieceType)r.u8();
            s.gx = r.i16(); s.gy = r.i16(); s.gz = r.i16();
            s.rot = r.u8();
            s.mat = (si::Material)r.u8();
            s.edit = r.u8();
            s.hp = r.f32();
            s.maxHp = r.f32();
            s.buildTime = r.f32();
            s.age = r.f32();
            s.owner = r.u16();
            s.team = r.u8();
            if (!r.ok()) break;
            if (structures_.find(s.id)) structures_.remove(s.id, world_);
            structures_.add(s, world_);
            break;
        }
        case EV_STRUCT_REMOVE: {
            uint32_t id = r.u32();
            if (Structure* s = structures_.find(id)) {
                AABB b = pieceBounds(s->piece, s->gx, s->gy, s->gz, s->rot);
                float d = (b.center() - me).len();
                if (d < 60) audio_.play(Sfx::StructureBreak, clampf(1.0f - d / 60.0f, 0, 1) * 0.8f);
                Color c = materialColor(s->mat);
                for (int i = 0; i < 10; i++) {
                    si::Vec3 p{GetRandomValue((int)(b.min.x * 10), (int)(b.max.x * 10)) / 10.0f, GetRandomValue((int)(b.min.y * 10), (int)(b.max.y * 10)) / 10.0f,
                               GetRandomValue((int)(b.min.z * 10), (int)(b.max.z * 10)) / 10.0f};
                    particles_.push_back({p, {GetRandomValue(-20, 20) / 10.0f, 1.0f, GetRandomValue(-20, 20) / 10.0f}, 0, 0.9f, c, 0.3f});
                }
            }
            structures_.remove(id, world_);
            break;
        }
        case EV_STRUCT_EDIT: {
            audio_.play(Sfx::Click, 0.4f);
            uint32_t id = r.u32();
            uint8_t e = r.u8();
            structures_.setEdit(id, e, world_);
            break;
        }
        case EV_STRUCT_HP: {
            uint32_t id = r.u32();
            float hp = r.f32();
            if (Structure* s = structures_.find(id)) s->hp = hp;
            break;
        }
        case EV_SHAPE_REMOVE: {
            uint32_t id = r.u32();
            if (const Shape* s = world_.shape(id)) {
                Color c = C(s->color);
                si::Vec3 ctr = s->box.center();
                for (int i = 0; i < 8; i++)
                    particles_.push_back({ctr, {GetRandomValue(-30, 30) / 10.0f, GetRandomValue(5, 40) / 10.0f, GetRandomValue(-30, 30) / 10.0f}, 0, 1.0f, c, 0.35f});
            }
            world_.removeShape(id);
            worldR_.markShapeRemoved(id);
            break;
        }
        case EV_ITEM_SPAWN: {
            ClientItem it;
            it.id = r.u32();
            it.stack.type = (ItemType)r.u8();
            it.stack.rarity = (Rarity)r.u8();
            it.stack.count = r.u16();
            it.stack.clip = r.u16();
            it.stack.extra = r.u8();
            it.pos = r.vec3();
            it.spawnTime = time_;
            if (r.ok()) items_[it.id] = it;
            break;
        }
        case EV_ITEM_REMOVE: {
            uint32_t id = r.u32();
            auto it = items_.find(id);
            if (it != items_.end() && (it->second.pos - me).len() < 4) audio_.play(Sfx::Pickup, 0.6f);
            items_.erase(id);
            break;
        }
        case EV_CHEST_OPEN: {
            uint32_t i = r.u32();
            if (i < chestOpened_.size()) {
                chestOpened_[i] = 1;
                float d = (map_.chests[i].pos - me).len();
                if (d < 60) audio_.play(Sfx::Chest, clampf(1 - d / 60, 0, 1));
            }
            break;
        }
        case EV_AMMOBOX_OPEN: { uint32_t i = r.u32(); if (i < ammoBoxOpened_.size()) ammoBoxOpened_[i] = 1; break; }
        case EV_KILLFEED: {
            uint16_t killer = r.u16(), victim = r.u16();
            ItemType w = (ItemType)r.u8();
            uint8_t f = r.u8();
            std::string vn = playerName(victim), kn = playerName(killer);
            std::string txt;
            if (f & KF_KNOCKED) txt = kn + " knocked down " + vn;
            else if (f & KF_STORM) txt = killer != 0xFFFF && killer != victim ? kn + " finished off " + vn + " (storm)" : vn + " was lost in the storm";
            else if (f & KF_FALL) txt = killer != 0xFFFF && killer != victim ? kn + " sent " + vn + " off a ledge" : vn + " took a fall";
            else if (killer == 0xFFFF || killer == victim) txt = vn + " was eliminated";
            else {
                std::string weapon = (f & KF_PICKAXE) ? "a pickaxe" : (f & KF_EXPLOSION) ? "an explosion" : (w == ItemType::None ? "" : itemDef(w).name);
                txt = kn + " eliminated " + vn + (weapon.empty() ? "" : " with " + weapon) + ((f & KF_HEADSHOT) ? " (headshot)" : "");
            }
            bool mine = killer == net_.playerId;
            killfeed_.push_back({txt, 6.0f, mine || victim == net_.playerId});
            if (killfeed_.size() > 6) killfeed_.erase(killfeed_.begin());
            if (mine && killer != victim && !(f & KF_KNOCKED)) {
                audio_.play(Sfx::Eliminated, 0.8f);
                messages_.push_back({"You eliminated " + vn, 0, 3.0f});
            }
            break;
        }
        case EV_DAMAGE: {
            si::Vec3 p = r.vec3();
            uint16_t amount = r.u16();
            uint8_t f = r.u8();
            dmgNumbers_.push_back({p, amount, f, 0});
            hitMarker_ = 0.25f;
            hitMarkerHead_ = f & DF_HEADSHOT;
            audio_.play(hitMarkerHead_ ? Sfx::HeadHit : Sfx::Hit, 0.6f);
            break;
        }
        case EV_MESSAGE: {
            std::string t = r.str();
            uint8_t kind = r.u8();
            messages_.push_back({t, kind, 5.0f});
            if (messages_.size() > 4) messages_.erase(messages_.begin());
            break;
        }
        case EV_MATCH_RESULT: {
            result_.has = true;
            result_.placement = r.u8();
            result_.kills = r.u16();
            uint16_t winnerTeam = r.u16();
            result_.damage = r.f32();
            result_.players = r.u16();
            result_.won = result_.placement == 1 && winnerTeam != 0xFFFF;
            result_.final = winnerTeam != 0xFFFF || g_.phase == MatchPhase::Ended;
            if (result_.won) audio_.play(Sfx::Victory, 0.9f);
            break;
        }
        case EV_LAUNCHPAD_ADD: launchPads_.push_back(r.vec3()); break;
        case EV_SUPPLY_DROP: {
            uint32_t id = r.u32();
            Drop d;
            d.pos = r.vec3();
            d.groundY = r.f32();
            d.opened = false;
            drops_[id] = d;
            break;
        }
        case EV_SUPPLY_OPEN: { auto it = drops_.find(r.u32()); if (it != drops_.end()) it->second.opened = true; break; }
        case EV_RESET_WORLD: {
            structures_.clear(world_);
            world_.setMap(&map_);
            worldR_.markAllDirty();
            items_.clear();
            std::fill(chestOpened_.begin(), chestOpened_.end(), 0);
            std::fill(ammoBoxOpened_.begin(), ammoBoxOpened_.end(), 0);
            launchPads_.clear();
            drops_.clear();
            result_ = MatchResult();
            break;
        }
        case EV_TRICK: {
            TrickPopup tp;
            tp.score = r.u16();
            int halves = r.u8();
            int flips = r.i8();
            float air = r.u16() / 1000.0f;
            tp.combo = r.u8();
            tp.bailed = r.u8() != 0;
            char nb[64];
            tp.name = tp.bailed ? "BAILED!" : trickName(halves, flips, air, nb, sizeof(nb));
            if (settings_.autotest) TraceLog(LOG_INFO, "AUTOTEST trick %s +%d combo %d", tp.name.c_str(), tp.score, tp.combo);
            tp.t = 0;
            if (!tp.bailed) {
                trickTotal_ += tp.score;
                trickBest_ = std::max(trickBest_, tp.score);
                audio_.play(Sfx::Trick, 0.6f, 1.0f + std::min(4, tp.combo - 1) * 0.08f);
            }
            tricks_.push_back(tp);
            if (tricks_.size() > 4) tricks_.erase(tricks_.begin());
            break;
        }
        case EV_TAKE_DAMAGE: {
            si::Vec3 from = r.vec3();
            r.u16();
            uint8_t hasAttacker = r.u8();
            damageFlash_ = 0.35f;
            if (hasAttacker) dmgIndicators_.push_back({from, 1.2f});
            audio_.play(Sfx::Hurt, 0.5f);
            break;
        }
        default: break;
    }
}

// ======================================================================= input

void GameClient::sampleInput(float) {
    InputCmd in;
    in.seq = ++inputSeq_;
    in.yaw = camYaw_;
    in.pitch = camPitch_;
    bool overlay = mapOpen_ || inventoryOpen_ || paused_;
    if (!overlay) {
        if (IsKeyDown(KEY_W)) in.fwd += 1;
        if (settings_.autotest && !settings_.autotestVehicles && (pred_.mode == MoveMode::Glide || (pred_.mode == MoveMode::Ground && std::fmod(time_, 6.0f) < 2.0f))) in.fwd = 1;
        if (IsKeyDown(KEY_S)) in.fwd -= 1;
        if (IsKeyDown(KEY_D)) in.right += 1;
        if (IsKeyDown(KEY_A)) in.right -= 1;
        if (IsKeyDown(KEY_SPACE)) in.buttons |= IN_JUMP;
        if (IsKeyDown(KEY_LEFT_CONTROL)) in.buttons |= IN_CROUCH;
        if (IsKeyDown(KEY_LEFT_SHIFT)) in.buttons |= IN_SPRINT;
        if (IsKeyDown(KEY_E)) in.buttons |= IN_INTERACT;
        if (!emoteWheel_ && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) in.buttons |= IN_FIRE;
        if (!self_.buildMode) {
            if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) in.buttons |= IN_ADS;
            if (IsKeyDown(KEY_R)) in.buttons |= IN_RELOAD;
        }
    }
    if (settings_.autotestVehicles) {
        autopilotVehicles(in);
        in.yaw = camYaw_;
        in.pitch = camPitch_;
    }
    if (localControllable()) {
        MoveEvents ev = stepPrediction(in);
        if (ev.jumped) audio_.play(Sfx::Jump, 0.45f);
        if (ev.landed) audio_.play(Sfx::Land, 0.6f);
        // Footsteps from the locally predicted movement
        float spd = pred_.vel.lenXZ();
        if (pred_.onGround && spd > 1.0f) {
            stepTimer_ -= SIM_DT * spd;
            if (stepTimer_ <= 0) {
                audio_.play(Sfx::Footstep, pred_.crouched ? 0.15f : 0.32f, spd > 7 ? 1.1f : 1.0f);
                stepTimer_ = 2.1f;
            }
        }
        pendingInputs_.push_back(in);
        while (pendingInputs_.size() > 240) pendingInputs_.pop_front();
    }
    recentInputs_.push_back(in);
    if (recentInputs_.size() > 8) recentInputs_.erase(recentInputs_.begin());
    net_.sendInput(eventAck_, recentInputs_, pendingActions_);
}

// Scripted driver for STORM_AUTOTEST=vehicles: walk to the nearest vehicle of a type not yet
// tried, get in, drive (with some steering and boost), get out, repeat.
void GameClient::autopilotVehicles(InputCmd& in) {
    if (!localControllable()) return;
    if (inVehicle()) {
        apDrive_ += SIM_DT;
        VehicleVisual vv;
        bool have = vehicleVisual(self_.vehicle, vv);
        in.fwd = 1;
        if (have) {
            // Seek the nearest stunt ramp: drive to a run-up point, then line up and hit it on boost.
            if (apRamp_ < 0 || apDrive_ < SIM_DT * 1.5f) {
                float best = 120;
                apRamp_ = -1;
                apPhase_ = 0;
                for (size_t i = 0; i < map_.shapes.size(); i++) {
                    const Shape& sh = map_.shapes[i];
                    if (sh.kind != ShapeKind::Ramp || sh.maxHp != 600 || sh.box.max.y - sh.box.min.y < 2.0f) continue;
                    const Shape* ws = world_.shape(sh.id);
                    if (!ws || !ws->alive) continue;
                    float d = distXZ(sh.box.center(), vv.pos);
                    if (d < best) { best = d; apRamp_ = (int)i; }
                }
            }
            bool air = driving_ && !predVeh_.onGround;
            // Unstick: if we barely move for a while, reverse away and pick the run-up again
            apVehStuck_ = vv.speed < 1.5f && !air ? apVehStuck_ + SIM_DT : 0;
            if (apVehStuck_ > 1.2f) { apReverse_ = 1.4f; apVehStuck_ = 0; apPhase_ = 0; apRevYaw_ = wrapAngle(vv.yaw + kPi); }
            if (apReverse_ > 0) {
                apReverse_ -= SIM_DT;
                in.fwd = -1;
                in.right = 1;
                if (vehicleDef(vv.type).cameraSteer) camYaw_ = apRevYaw_;
                in.fwd = vehicleDef(vv.type).cameraSteer ? 1 : -1;
            } else if (apRamp_ >= 0 && !air) {
                const Shape& sh = map_.shapes[apRamp_];
                si::Vec3 dir = sh.rampDir == RAMP_PX ? si::Vec3{1, 0, 0} : sh.rampDir == RAMP_NX ? si::Vec3{-1, 0, 0} : sh.rampDir == RAMP_PZ ? si::Vec3{0, 0, 1} : si::Vec3{0, 0, -1};
                si::Vec3 c = sh.box.center();
                float halfLen = std::fabs(dir.x) > 0 ? (sh.box.max.x - sh.box.min.x) / 2 : (sh.box.max.z - sh.box.min.z) / 2;
                si::Vec3 low = c - dir * halfLen;
                si::Vec3 target = apPhase_ == 0 ? low - dir * 22.0f : c + dir * 30.0f;
                if (apPhase_ == 0 && distXZ(vv.pos, target) < 5.0f) apPhase_ = 1;
                if (apPhase_ == 1 && (vv.pos - c).dot(dir) > halfLen + 20) { apPhase_ = 0; apRamp_ = -1; }
                float want = yawFromDir(target - vv.pos);
                float diff = wrapAngle(want - vv.yaw);
                const VehicleDef& d = vehicleDef(vv.type);
                if (d.cameraSteer) {
                    camYaw_ = angleLerp(camYaw_, want, 0.15f);
                } else {
                    in.right = diff > 0.06f ? -1 : diff < -0.06f ? 1 : 0;
                    if (std::fabs(diff) > 1.8f) in.fwd = -1; // back up and swing round
                }
                if (apPhase_ == 1 && std::fabs(diff) < 0.35f && d.boostSpeed > 0) in.buttons |= IN_SPRINT;
                if (apPhase_ == 0 && distXZ(vv.pos, target) < 14.0f) in.fwd = std::fabs(diff) < 0.5f ? 1 : 0;
            }
            if (air) {
                in.right = 1; // spin for points
                if (predVeh_.airTime > 0.15f && predVeh_.airTime < 1.35f && vv.type == VehicleType::CrashQuad) {
                    in.buttons |= IN_CROUCH;
                    in.fwd = -1; // backflip
                }
            }
            float trail = vv.type == VehicleType::Hoverboard || vv.type == VehicleType::RollerBall ? 0.0f : 0.5f;
            if (!vehicleDef(vv.type).cameraSteer) camYaw_ = angleLerp(camYaw_, vv.yaw + trail, 0.05f);
            camPitch_ = lerpf(camPitch_, -0.18f, 0.05f);
        }
        if (apDrive_ > 22.0f) {
            apDoneTypes_ |= 1u << self_.vehicleType;
            in.buttons |= IN_INTERACT;
            apDrive_ = 0;
            apTarget_ = 0xFFFF;
        }
        return;
    }
    apDrive_ = 0;
    si::Vec3 me = pred_.pos;
    auto it = vehicles_.find(apTarget_);
    apTargetTime_ += SIM_DT;
    bool giveUp = apTargetTime_ > 20.0f || apStuck_ > 2.5f;
    if (giveUp && apTarget_ != 0xFFFF) { apSkip_.push_back(apTarget_); apTarget_ = 0xFFFF; apStuck_ = 0; }
    if (it == vehicles_.end() || apTarget_ == 0xFFFF || (it->second.cur.flags & VF_DRIVER)) {
        apTarget_ = 0xFFFF;
        apTargetTime_ = 0;
        float best = 1e9f;
        si::Vec3 eye = me + si::Vec3{0, 1.6f, 0};
        // Prefer untried types in plain sight, then anything in sight, then anything
        for (int pass = 0; pass < 3 && apTarget_ == 0xFFFF; pass++)
            for (auto& [id, v] : vehicles_) {
                if (!v.present || (v.cur.flags & VF_DRIVER)) continue;
                if (std::find(apSkip_.begin(), apSkip_.end(), id) != apSkip_.end()) continue;
                if (pass == 0 && (apDoneTypes_ & (1u << v.cur.type))) continue;
                if (pass < 2 && !world_.lineOfSight(eye, v.cur.pos + si::Vec3{0, 1.0f, 0})) continue;
                if (v.cur.pos.y < WATER_LEVEL + 0.3f) continue;
                float d = distXZ(v.cur.pos, me);
                if (d < best) { best = d; apTarget_ = id; }
            }
        it = vehicles_.find(apTarget_);
        if (it == vehicles_.end()) return;
    }
    const ClientVehicle& v = it->second;
    float d = distXZ(v.cur.pos, me);
    camYaw_ = angleLerp(camYaw_, yawFromDir(v.cur.pos - me), 0.2f);
    camPitch_ = lerpf(camPitch_, -0.15f, 0.1f);
    float reach = vehicleDef((VehicleType)v.cur.type).radius + 1.4f;
    if (d > reach) {
        in.fwd = 1;
        in.buttons |= IN_SPRINT;
        apStuck_ = distXZ(me, apLastPos_) < 0.02f ? apStuck_ + SIM_DT : 0;
        if (apStuck_ > 0.4f) { in.buttons |= IN_JUMP; in.right = 1; }
    } else if (std::fmod(time_, 0.4f) < 0.2f) {
        in.buttons |= IN_INTERACT;
    }
    apLastPos_ = me;
}

si::Vec3 GameClient::viewPos() const {
    if (localControllable() && inVehicle()) {
        VehicleVisual vv;
        if (vehicleVisual(self_.vehicle, vv)) {
            VehicleState st;
            st.pos = vv.pos;
            st.yaw = vv.yaw;
            return vehicleSeatPos(st, vv.type, self_.seat);
        }
        return self_.pos;
    }
    if (localControllable()) return pred_.pos + smoothOffset_;
    if (const RemotePlayer* t = spectateTarget()) return t->cur.pos;
    return haveSelf_ ? self_.pos : si::Vec3{WORLD_SIZE / 2, 50, WORLD_SIZE / 2};
}

// ======================================================================= per-frame

void GameClient::updateInterpolation(float dt) {
    float target = serverTime_ - 0.1f;
    renderTime_ += dt;
    if (std::fabs(renderTime_ - target) > 0.5f) renderTime_ = target;
    else renderTime_ += (target - renderTime_) * 0.1f;
    for (auto& [id, p] : players_) {
        if (p.buf.empty()) continue;
        while (p.buf.size() > 2 && p.buf[1].t <= renderTime_) p.buf.pop_front();
        const auto& a = p.buf.front();
        if (p.buf.size() >= 2 && renderTime_ >= a.t) {
            const auto& b = p.buf[1];
            float t = clampf((renderTime_ - a.t) / std::max(0.001f, b.t - a.t), 0, 1);
            p.cur = b.s;
            p.cur.pos = lerp3(a.s.pos, b.s.pos, t);
            if ((b.s.pos - a.s.pos).len() > 20) p.cur.pos = b.s.pos; // teleports
            p.cur.yaw = angleLerp(a.s.yaw, b.s.yaw, t);
            p.cur.pitch = lerpf(a.s.pitch, b.s.pitch, t);
        } else {
            p.cur = a.s;
        }
        float moved = distXZ(p.cur.pos, p.lastPos);
        p.speed = lerpf(p.speed, dt > 0 ? moved / dt : 0, 0.3f);
        p.lastPos = p.cur.pos;
        p.animTime += dt;
        if (id != net_.playerId && p.present && p.cur.mode == MoveMode::Ground && p.speed > 1.0f && (p.cur.flags & PF_ALIVE)) {
            p.stepTimer -= dt * p.speed;
            if (p.stepTimer <= 0) {
                float d = (p.cur.pos - viewPos()).len();
                if (d < 30) audio_.play(Sfx::Footstep, 0.35f * (1.0f - d / 30.0f));
                p.stepTimer = 2.1f;
            }
        }
        if (p.cur.flags & PF_HARVESTING) p.swing = std::fmod(p.swing + dt * 2.0f, 1.0f);
        else p.swing = 0;
        if (p.cur.mode == MoveMode::Skydive || p.cur.mode == MoveMode::Glide) {
            p.trail.push_back(p.cur.pos + si::Vec3{0, 1.2f, 0});
            if (p.trail.size() > 40) p.trail.erase(p.trail.begin());
        } else if (!p.trail.empty()) {
            p.trail.erase(p.trail.begin());
        }
    }
    for (auto& [id, v] : vehicles_) {
        if (v.buf.empty()) continue;
        while (v.buf.size() > 2 && v.buf[1].t <= renderTime_) v.buf.pop_front();
        const auto& a = v.buf.front();
        if (v.buf.size() >= 2 && renderTime_ >= a.t) {
            const auto& b = v.buf[1];
            float t = clampf((renderTime_ - a.t) / std::max(0.001f, b.t - a.t), 0, 1);
            v.cur = b.s;
            v.cur.pos = lerp3(a.s.pos, b.s.pos, t);
            if ((b.s.pos - a.s.pos).len() > 25) v.cur.pos = b.s.pos;
            v.cur.yaw = angleLerp(a.s.yaw, b.s.yaw, t);
            v.cur.pitch = angleLerp(a.s.pitch, b.s.pitch, t);
            v.cur.roll = angleLerp(a.s.roll, b.s.roll, t);
            float dyaw = wrapAngle(b.s.yaw - a.s.yaw) / std::max(0.001f, b.t - a.t);
            v.steer = lerpf(v.steer, clampf(-dyaw * 0.6f, -1, 1), clampf(dt * 8, 0, 1));
        } else {
            v.cur = a.s;
        }
        si::Vec3 delta = v.cur.pos - v.lastPos;
        if (!v.init || delta.len() > 25) { delta = {}; v.init = true; v.ballSpin = Basis(); }
        float moved = delta.lenXZ();
        float fwdSign = (delta.x * std::sin(v.cur.yaw) + delta.z * std::cos(v.cur.yaw)) >= 0 ? 1.0f : -1.0f;
        v.speed = lerpf(v.speed, dt > 0 ? moved / dt : 0, 0.3f);
        v.wheelSpin += fwdSign * moved / 0.35f;
        if ((VehicleType)v.cur.type == VehicleType::RollerBall) rollBasis(v.ballSpin, delta, 1.2f);
        v.lastPos = v.cur.pos;
    }
    if (driving_) {
        static si::Vec3 lastDrivePos;
        si::Vec3 p = predVeh_.pos + vehSmooth_;
        si::Vec3 delta = p - lastDrivePos;
        if (delta.len() > 25) delta = {};
        float fwdSign = (delta.x * std::sin(predVeh_.yaw) + delta.z * std::cos(predVeh_.yaw)) >= 0 ? 1.0f : -1.0f;
        vehWheelSpin_ += fwdSign * delta.lenXZ() / 0.35f;
        if (drivingType_ == VehicleType::RollerBall) rollBasis(vehBallSpin_, delta, 1.2f);
        float steerIn = 0;
        if (!(mapOpen_ || inventoryOpen_ || paused_)) steerIn = (IsKeyDown(KEY_D) ? 1.0f : 0.0f) - (IsKeyDown(KEY_A) ? 1.0f : 0.0f);
        vehSteerVis_ = lerpf(vehSteerVis_, steerIn, clampf(dt * 10, 0, 1));
        lastDrivePos = p;
    }
    float decay = std::exp(-dt * 12.0f);
    smoothOffset_ = smoothOffset_ * decay;
    vehSmooth_ = vehSmooth_ * std::exp(-dt * 8.0f);
    vehYawSmooth_ *= std::exp(-dt * 10.0f);
}

void GameClient::updateEffects(float dt) {
    for (auto& t : tracers_) t.t -= dt;
    tracers_.erase(std::remove_if(tracers_.begin(), tracers_.end(), [](const Tracer& t) { return t.t <= 0; }), tracers_.end());
    for (auto& f : flashes_) f.t -= dt;
    flashes_.erase(std::remove_if(flashes_.begin(), flashes_.end(), [](const Flash& f) { return f.t <= 0; }), flashes_.end());
    shake_ = std::max(0.0f, shake_ - dt * 2.5f);
    for (auto& b : blasts_) b.t += dt;
    blasts_.erase(std::remove_if(blasts_.begin(), blasts_.end(), [](const Blast& b) { return b.t > 0.6f; }), blasts_.end());
    for (auto& p : particles_) {
        p.t += dt;
        p.v.y -= 9.0f * dt;
        p.p += p.v * dt;
    }
    particles_.erase(std::remove_if(particles_.begin(), particles_.end(), [](const Particle& p) { return p.t > p.life; }), particles_.end());
    if (particles_.size() > 1500) particles_.erase(particles_.begin(), particles_.begin() + (particles_.size() - 1500));
    for (auto& d : dmgNumbers_) { d.t += dt; d.p.y += dt * 1.2f; }
    dmgNumbers_.erase(std::remove_if(dmgNumbers_.begin(), dmgNumbers_.end(), [](const DamageNumber& d) { return d.t > 1.0f; }), dmgNumbers_.end());
    for (auto& k : killfeed_) k.t -= dt;
    killfeed_.erase(std::remove_if(killfeed_.begin(), killfeed_.end(), [](const KillFeedEntry& k) { return k.t <= 0; }), killfeed_.end());
    for (auto& m : messages_) m.t -= dt;
    messages_.erase(std::remove_if(messages_.begin(), messages_.end(), [](const Message& m) { return m.t <= 0; }), messages_.end());
    for (auto& d : dmgIndicators_) d.t -= dt;
    for (auto& t : tricks_) t.t += dt;
    tricks_.erase(std::remove_if(tricks_.begin(), tricks_.end(), [](const TrickPopup& t) { return t.t > 2.6f; }), tricks_.end());
    dmgIndicators_.erase(std::remove_if(dmgIndicators_.begin(), dmgIndicators_.end(), [](const DamageIndicator& d) { return d.t <= 0; }), dmgIndicators_.end());
    hitMarker_ = std::max(0.0f, hitMarker_ - dt);
    damageFlash_ = std::max(0.0f, damageFlash_ - dt);
}

void GameClient::computeInteractPrompt() {
    interactPrompt_.clear();
    if (!localControllable()) return;
    if (inVehicle()) {
        interactPrompt_ = "E to get out";
        return;
    }
    si::Vec3 origin = pred_.pos + si::Vec3{0, 1.0f, 0};
    si::Vec3 look = dirFromAngles(camYaw_, camPitch_);
    float best = 1e9f;
    auto consider = [&](const si::Vec3& pos, float range, const std::string& text) {
        si::Vec3 d = pos - origin;
        float dist = d.len();
        if (dist > range) return;
        float facing = dist > 0.01f ? d.norm().dot(look) : 1.0f;
        float score = dist - facing * 1.5f;
        if (score < best) { best = score; interactPrompt_ = text; }
    };
    if (self_.flags & PF_DBNO) return;
    for (auto& [id, p] : players_)
        if (id != net_.playerId && p.team == self_.team && (p.cur.flags & PF_DBNO) && p.present) consider(p.cur.pos + si::Vec3{0, 0.5f, 0}, INTERACT_RANGE, "Hold E to revive " + p.name);
    for (size_t i = 0; i < map_.chests.size(); i++)
        if (!chestOpened_[i] && std::fabs(map_.chests[i].pos.x - origin.x) < 4 && std::fabs(map_.chests[i].pos.z - origin.z) < 4)
            consider(map_.chests[i].pos + si::Vec3{0, 0.5f, 0}, INTERACT_RANGE, "Hold E to open chest");
    for (size_t i = 0; i < map_.ammoBoxes.size(); i++)
        if (!ammoBoxOpened_[i] && std::fabs(map_.ammoBoxes[i].x - origin.x) < 4 && std::fabs(map_.ammoBoxes[i].z - origin.z) < 4)
            consider(map_.ammoBoxes[i] + si::Vec3{0, 0.4f, 0}, INTERACT_RANGE, "Hold E to open ammo box");
    for (auto& [id, d] : drops_)
        if (!d.opened && d.pos.y <= d.groundY + 0.1f) consider(d.pos + si::Vec3{0, 0.6f, 0}, INTERACT_RANGE + 0.5f, "Hold E to open supply drop");
    for (auto& [id, v] : vehicles_) {
        if (!v.present || std::fabs(v.cur.pos.x - origin.x) > 6 || std::fabs(v.cur.pos.z - origin.z) > 6) continue;
        const VehicleDef& d = vehicleDef((VehicleType)v.cur.type);
        int taken = 0;
        bool driverTaken = v.cur.flags & VF_DRIVER;
        for (auto& [pid, p] : players_) if (p.present && p.cur.vehicle == id) taken++;
        if (taken >= d.seats) continue;
        std::string verb = driverTaken ? "ride in " : (d.seats > 1 ? "drive " : "ride ");
        consider(v.cur.pos + si::Vec3{0, 0.8f, 0}, d.radius + 1.8f, std::string("E to ") + verb + d.name);
    }
    for (auto& [id, it] : items_)
        if (std::fabs(it.pos.x - origin.x) < 4 && std::fabs(it.pos.z - origin.z) < 4) {
            std::string name = itemDisplayName(it.stack);
            if (it.stack.count > 1) name += " x" + std::to_string(it.stack.count);
            consider(it.pos + si::Vec3{0, 0.3f, 0}, INTERACT_RANGE, "E to pick up " + name);
        }
}

bool GameClient::frame(float dt) {
    time_ += dt;
    std::vector<Snapshot> snaps;
    net_.update(GetTime(), snaps);
    if (!mapReady_ && net_.state == NetClient::State::Connected) initMap(net_.mapSeed);
    if (mapReady_) for (auto& s : snaps) applySnapshot(s);

    if (net_.state == NetClient::State::Rejected || net_.state == NetClient::State::TimedOut) {
        ui::setMouseCaptured(false);
        ClearBackground(ui::BG);
        ui::beginFrame();
        float w = (float)GetScreenWidth(), h = (float)GetScreenHeight();
        ui::textCentered("Disconnected", w / 2, h * 0.38f, 40, ui::TEXT);
        ui::textCentered(net_.rejectReason, w / 2, h * 0.38f + ui::px(56), 22, ui::MUTED);
        if (ui::button({w / 2 - ui::px(130), h * 0.55f, (float)ui::px(260), (float)ui::px(52)}, "Back to lobby", true)) leave_ = true;
        return !leave_;
    }
    if (!mapReady_ || !haveSelf_) {
        ClearBackground(ui::BG);
        ui::beginFrame();
        float w = (float)GetScreenWidth(), h = (float)GetScreenHeight();
        ui::textCentered(mapReady_ ? "Joining match..." : "Connecting to game server...", w / 2, h * 0.45f, 32, ui::TEXT);
        if (ui::button({w / 2 - ui::px(90), h * 0.58f, (float)ui::px(180), (float)ui::px(46)}, "Cancel")) leave_ = true;
        return !leave_;
    }

    // Autotest: open the inventory, map and pause menu near the end for screenshots
    if (settings_.autotest && !settings_.autotestVehicles && haveSelf_) {
        inventoryOpen_ = time_ > 55.5f && time_ < 58.5f && localControllable();
        mapOpen_ = time_ > 59.5f && time_ < 62.5f;
        paused_ = time_ > 63.5f && time_ < 66.5f;
    }
    // --- overlays & discrete keys
    bool overlay = mapOpen_ || inventoryOpen_ || paused_;
    if (IsKeyPressed(KEY_ESCAPE)) {
        if (mapOpen_ || inventoryOpen_) { mapOpen_ = inventoryOpen_ = false; }
        else paused_ = !paused_;
    }
    if (!paused_) {
        if (IsKeyPressed(KEY_M)) { mapOpen_ = !mapOpen_; inventoryOpen_ = false; }
        if (IsKeyPressed(KEY_TAB) && localControllable()) { inventoryOpen_ = !inventoryOpen_; mapOpen_ = false; }
    }
    overlay = mapOpen_ || inventoryOpen_ || paused_;
    bool wantCapture = !(overlay || (result_.has && !(self_.flags & PF_ALIVE))) && IsWindowFocused();
    // Ignore the mouse delta on the frame capture toggles (the pointer was just warped).
    bool captureChanged = ui::setMouseCaptured(wantCapture);

    if (!overlay) {
        Vector2 md = GetMouseDelta();
        float sens = settings_.sensitivity;
        const ItemStack* held = self_.selected >= 1 && self_.selected <= 5 ? &self_.inv[self_.selected - 1] : nullptr;
        if ((IsMouseButtonDown(MOUSE_BUTTON_RIGHT) && !self_.buildMode) && held && itemDef(held->type).cls == ItemClass::Gun)
            sens /= std::max(1.0f, weaponStats(held->type, held->rarity).adsZoom * 0.8f);
        if (IsCursorHidden() && !captureChanged) {
            camYaw_ = wrapAngle(camYaw_ - md.x * sens);
            camPitch_ = clampf(camPitch_ - md.y * sens * (settings_.invertY ? -1.0f : 1.0f), -1.45f, 1.45f);
        }
        if (localControllable()) {
            for (int k = 0; k < 5; k++)
                if (IsKeyPressed(KEY_ONE + k)) pushAction(ActionType::SelectSlot, (uint8_t)(k + 1));
            if (IsKeyPressed(KEY_F)) pushAction(ActionType::SelectSlot, 0);
            float wheel = GetMouseWheelMove();
            if (wheel != 0 && !self_.buildMode) {
                int s = self_.selected + (wheel < 0 ? 1 : -1);
                if (s < 0) s = 5;
                if (s > 5) s = 0;
                pushAction(ActionType::SelectSlot, (uint8_t)s);
            }
            if (inVehicle()) {
                if (IsKeyPressed(KEY_C)) pushAction(ActionType::ChangeSeat);
            } else {
                if (IsKeyPressed(KEY_Q)) pushAction(ActionType::SetBuildMode, self_.buildMode ? 0 : 1, self_.buildPiece);
                if (IsKeyPressed(KEY_Z)) pushAction(ActionType::SetBuildMode, 1, (uint8_t)PieceType::Wall);
                if (IsKeyPressed(KEY_X)) pushAction(ActionType::SetBuildMode, 1, (uint8_t)PieceType::Floor);
                if (IsKeyPressed(KEY_C)) pushAction(ActionType::SetBuildMode, 1, (uint8_t)PieceType::Ramp);
                if (IsKeyPressed(KEY_V)) pushAction(ActionType::SetBuildMode, 1, (uint8_t)PieceType::Cone);
            }
            if (self_.buildMode) {
                if (IsKeyPressed(KEY_R)) pushAction(ActionType::RotatePiece);
                if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) pushAction(ActionType::SetBuildMat, (uint8_t)((self_.buildMat + 1) % 3));
            }
            if (IsKeyPressed(KEY_G)) pushAction(ActionType::EditPiece);
            if (IsKeyPressed(KEY_SPACE) && pred_.mode == MoveMode::Air && pred_.canGlide) pushAction(ActionType::ToggleGlide);
            emoteWheel_ = IsKeyDown(KEY_B) && pred_.mode == MoveMode::Ground;
        }
        if (haveSelf_ && self_.mode == MoveMode::OnBus && IsKeyPressed(KEY_SPACE)) pushAction(ActionType::JumpFromBus);
        if (settings_.autotest && haveSelf_ && self_.mode == MoveMode::OnBus && g_.busProgress > 0.35f && pendingActions_.empty())
            pushAction(ActionType::JumpFromBus);
        if (haveSelf_ && !(self_.flags & PF_ALIVE) && (self_.flags & PF_SPECTATOR) && (IsKeyPressed(KEY_SPACE) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT)))
            pushAction(ActionType::Spectate);
    }

    if (settings_.autotest) {
        static float nextLog = 0;
        if (time_ > nextLog) { TraceLog(LOG_INFO, "AUTOTEST yaw=%.3f pitch=%.3f captured=%d", camYaw_, camPitch_, (int)IsCursorHidden()); nextLog = time_ + 2.0f; }
    }

    // --- fixed-step input + prediction
    accum_ += dt;
    int steps = 0;
    while (accum_ >= SIM_DT && steps < 8) {
        sampleInput(SIM_DT);
        accum_ -= SIM_DT;
        steps++;
    }
    if (steps == 8) accum_ = 0;

    updateInterpolation(dt);
    updateEffects(dt);
    worldR_.rebuildDirty(world_, 4);
    computeInteractPrompt();

    // Storm hum when outside
    if (localControllable() && g_.stormPhase >= 0) {
        float d = dist2d(pred_.pos.xz(), g_.stormCur);
        if (d > g_.stormCurR) {
            stormTickSound_ -= dt;
            if (stormTickSound_ <= 0) { audio_.play(Sfx::Storm, 0.4f); stormTickSound_ = 1.0f; }
        }
    }

    // Looping ambience: engine of the drop ship, outdoor ambience
    if (g_.busActive) {
        si::Vec3 bus = lerp3(g_.busStart, g_.busEnd, clampf(g_.busProgress, 0, 1));
        float d = (bus - S(cam_.position)).len();
        audio_.loop(Loop::Engine, clampf(1.0f - d / 400.0f, 0, 1) * 0.55f, 0.7f);
    } else {
        audio_.loop(Loop::Engine, 0);
    }
    audio_.loop(Loop::Ambience, 0.22f);
    updateVehicleAudio(dt);
    // Weapon switch click
    if (haveSelf_ && (int)self_.selected != lastSlotSound_) {
        if (lastSlotSound_ >= 0) audio_.play(Sfx::WeaponSwitch, 0.5f);
        lastSlotSound_ = self_.selected;
    }

    render3D();
    renderHud();
    return !leave_;
}

// ======================================================================= vehicles

bool GameClient::vehicleVisual(uint16_t id, VehicleVisual& out) const {
    if (driving_ && id == drivingId_) {
        out.type = drivingType_;
        out.id = id;
        out.pos = predVeh_.pos + vehSmooth_;
        out.yaw = predVeh_.yaw + vehYawSmooth_;
        out.pitch = predVeh_.pitch;
        out.roll = predVeh_.roll;
        out.speed = predVeh_.vel.len();
        out.boosting = predVeh_.boosting;
        out.wheelSpin = vehWheelSpin_;
        out.steer = vehSteerVis_;
        out.ballSpin = vehBallSpin_;
        out.occupied = true;
        auto it = vehicles_.find(id);
        out.hp = it != vehicles_.end() ? it->second.cur.hp / 255.0f : 1.0f;
        return true;
    }
    auto it = vehicles_.find(id);
    if (it == vehicles_.end() || !it->second.present || it->second.buf.empty()) return false;
    const ClientVehicle& v = it->second;
    out.type = (VehicleType)v.cur.type;
    out.id = id;
    out.pos = v.cur.pos;
    out.yaw = v.cur.yaw;
    out.pitch = v.cur.pitch;
    out.roll = v.cur.roll;
    out.speed = v.speed;
    out.boosting = v.cur.flags & VF_BOOST;
    out.wheelSpin = v.wheelSpin;
    out.steer = v.steer;
    out.ballSpin = v.ballSpin;
    out.occupied = v.cur.flags & VF_DRIVER;
    out.hp = v.cur.hp / 255.0f;
    return true;
}

void GameClient::renderVehicles(bool shellPass) {
    si::Vec3 cp = S(cam_.position);
    std::vector<uint16_t> ids;
    for (auto& [id, v] : vehicles_) ids.push_back(id);
    if (driving_ && !vehicles_.count(drivingId_)) ids.push_back(drivingId_);
    for (uint16_t id : ids) {
        VehicleVisual vv;
        if (!vehicleVisual(id, vv)) continue;
        if ((vv.pos - cp).len() > settings_.viewDistance * 0.9f) continue;
        if (shellPass) drawVehicleShell(vv, time_);
        else drawVehicle(vv, time_);
    }
}

void GameClient::updateVehicleAudio(float dt) {
    (void)dt;
    // Loudest nearby running vehicle drives the engine / hover / rolling loops.
    si::Vec3 me = viewPos();
    float bestEngine = 0, bestHover = 0, bestRoll = 0;
    float pEngine = 1, pHover = 1, pRoll = 1;
    auto consider = [&](const VehicleVisual& vv, bool mine) {
        float d = (vv.pos - me).len();
        float near = mine ? 1.0f : clampf(1.0f - d / 70.0f, 0.0f, 1.0f);
        if (near <= 0) return;
        float sp = vv.speed;
        switch (vv.type) {
            case VehicleType::GolfCart:
            case VehicleType::CrashQuad: {
                if (!vv.occupied) return;
                float vol = near * (0.25f + clampf(sp / 25.0f, 0, 1) * 0.35f) * (mine ? 1.0f : 0.8f);
                if (vol > bestEngine) {
                    bestEngine = vol;
                    pEngine = (vv.type == VehicleType::GolfCart ? 1.35f : 0.8f) + sp / 30.0f + (vv.boosting ? 0.25f : 0.0f);
                }
                break;
            }
            case VehicleType::Hoverboard: {
                float vol = near * (0.15f + clampf(sp / 25.0f, 0, 1) * 0.3f);
                if (vol > bestHover) { bestHover = vol; pHover = 0.9f + sp / 30.0f + (vv.boosting ? 0.3f : 0.0f); }
                break;
            }
            default: {
                if (sp < 1.0f) return;
                float vol = near * clampf(sp / 20.0f, 0, 1) * 0.5f;
                if (vol > bestRoll) { bestRoll = vol; pRoll = 0.7f + sp / 35.0f; }
                break;
            }
        }
    };
    for (auto& [id, v] : vehicles_) {
        VehicleVisual vv;
        if (vehicleVisual(id, vv)) consider(vv, inVehicle() && id == self_.vehicle);
    }
    audio_.loop(Loop::Vehicle, bestEngine, pEngine);
    audio_.loop(Loop::Hover, bestHover, pHover);
    audio_.loop(Loop::Roll, bestRoll, pRoll);
}

// ======================================================================= rendering

void GameClient::render3D() {
    // Camera
    bool ads = !self_.buildMode && IsMouseButtonDown(MOUSE_BUTTON_RIGHT) && !(mapOpen_ || inventoryOpen_ || paused_);
    float fov = settings_.fov;
    si::Vec3 focus;
    float yaw = camYaw_, pitch = camPitch_;
    bool crouch = false;
    if (haveSelf_ && self_.mode == MoveMode::OnBus && g_.busActive) {
        si::Vec3 bus = lerp3(g_.busStart, g_.busEnd, clampf(g_.busProgress, 0, 1));
        si::Vec3 dir = dirFromAngles(camYaw_, camPitch_);
        cam_.position = V(bus - dir * 28.0f + si::Vec3{0, 6, 0});
        cam_.target = V(bus);
    } else {
        const RemotePlayer* spec = spectateTarget();
        if (spec) {
            focus = spec->cur.pos;
            yaw = spec->cur.yaw;
            pitch = spec->cur.pitch;
            crouch = spec->cur.flags & PF_CROUCH;
            ads = false;
        } else {
            focus = viewPos();
            crouch = pred_.crouched;
        }
        if (ads && self_.selected >= 1 && self_.selected <= 5) {
            const ItemStack& h = self_.inv[self_.selected - 1];
            if (!h.empty() && itemDef(h.type).cls == ItemClass::Gun) fov = settings_.fov / weaponStats(h.type, h.rarity).adsZoom;
        }
        CameraRig rig = thirdPersonCamera(focus, yaw, pitch, ads, crouch);
        bool sky = haveSelf_ && (self_.mode == MoveMode::Skydive || self_.mode == MoveMode::Glide) && !spec;
        if (sky) rig.pos = focus + si::Vec3{0, 2.2f, 0} - rig.dir * 7.0f;
        // Vehicles: pull the camera back so the whole vehicle is in view
        VehicleVisual camVeh;
        if (!spec && inVehicle() && vehicleVisual(self_.vehicle, camVeh) && !ads) {
            float back = camVeh.type == VehicleType::Hoverboard ? 4.8f : camVeh.type == VehicleType::Trolley ? 5.0f : 7.0f;
            si::Vec3 pivot = camVeh.pos + si::Vec3{0, camVeh.type == VehicleType::RollerBall ? 1.4f : 1.6f, 0};
            rig.pos = pivot - rig.dir * back + si::Vec3{0, 1.1f, 0};
            focus = camVeh.pos;
        }
        // Pull the camera in front of walls
        si::Vec3 head = focus + si::Vec3{0, crouch ? 1.3f : 1.8f, 0};
        si::Vec3 toCam = rig.pos - head;
        float len = toCam.len();
        if (len > 0.1f) {
            RayHit h = world_.raycast(head, toCam / len, len);
            if (h.hit) rig.pos = head + toCam / len * std::max(0.3f, h.t - 0.25f);
        }
        cam_.position = V(rig.pos);
        cam_.target = V(rig.pos + rig.dir * 10.0f);
    }
    cam_.fovy = lerpf(cam_.fovy, fov, 0.25f);
    if (shake_ > 0) {
        float a = shake_ * shake_ * 0.35f;
        si::Vec3 j{std::sin(time_ * 61.0f) * a, std::sin(time_ * 47.0f + 1.0f) * a, std::cos(time_ * 53.0f) * a};
        cam_.position = V(S(cam_.position) + j);
        cam_.target = V(S(cam_.target) + j);
    }

    // --- Shadow pass (sun depth map around the view focus)
    light_.shadows = settings_.shadows;
    si::Vec3 shadowFocus = viewPos();
    if (haveSelf_ && self_.mode == MoveMode::OnBus) shadowFocus = lerp3(g_.busStart, g_.busEnd, clampf(g_.busProgress, 0, 1));
    if (light_.shadows) {
        light_.beginShadowPass(V(shadowFocus));
        worldR_.drawNear(V(shadowFocus), light_.shadowRadius * 1.4f, light_.depth);
        BeginShaderMode(light_.depth);
        renderStructures();
        renderPlayers();
        renderVehicles(false);
        renderBus();
        EndShaderMode();
        light_.endObjects();
        light_.endShadowPass();
    }

    // --- Main pass
    light_.drawSky(cam_, time_);
    BeginMode3D(cam_);
    rlSetClipPlanes(0.1, 3000.0);
    light_.begin(cam_, time_);
    worldR_.draw(cam_, settings_.viewDistance, time_);
    if (ModelLibrary* lib = models()) {
        if (lib->hasHeld(HeldModel::Cloud)) {
            for (auto& c : clouds_) {
                si::Vec3 p = c.pos + si::Vec3{std::fmod(time_ * c.speed, WORLD_SIZE + 600), 0, 0};
                if (p.x > WORLD_SIZE + 300) p.x -= WORLD_SIZE + 600;
                Basis b;
                b.r = {c.size.x, 0, 0};
                b.u = {0, c.size.y, 0};
                b.f = {0, 0, c.size.z};
                lib->drawHeld(HeldModel::Cloud, p, b, 1.0f);
            }
        }
        // Victory trophy floating in front of the camera
        if (result_.has && result_.won && lib->hasHeld(HeldModel::Trophy)) {
            si::Vec3 fwd = (S(cam_.target) - S(cam_.position)).norm();
            lib->drawHeld(HeldModel::Trophy, S(cam_.position) + fwd * 3.0f + si::Vec3{0, -0.55f, 0}, yawBasis(time_ * 1.5f), 1.6f);
        }
    }
    BeginShaderMode(light_.shader);
    renderStructures();
    renderItems();
    renderVehicles(false);
    renderPlayers();
    renderBus();
    renderVehicles(true);
    EndShaderMode();
    light_.endObjects();
    worldR_.drawWater(cam_, time_);
    renderSlipstreams();
    renderEffects();
    renderBuildPreview();
    renderStorm();
    EndMode3D();
}

void GameClient::renderStructures() {
    si::Vec3 cp = S(cam_.position);
    std::vector<Shape> tmp;
    for (auto& [id, s] : structures_.byId) {
        AABB b = pieceBounds(s.piece, s.gx, s.gy, s.gz, s.rot);
        if ((b.center() - cp).lenXZ() > settings_.viewDistance) continue;
        setDrawMaterial(s.mat == si::Material::Wood ? M_WOOD : s.mat == si::Material::Brick ? M_BRICK : M_METAL);
        Color c = materialColor(s.mat);
        float hpF = s.maxHp > 0 ? clampf(s.hp / s.maxHp, 0.25f, 1.0f) : 1.0f;
        float f = 0.65f + 0.35f * hpF;
        c = {(unsigned char)(c.r * f), (unsigned char)(c.g * f), (unsigned char)(c.b * f), 255};
        for (uint32_t sid : s.shapes) {
            const Shape* sh = world_.shape(sid);
            if (!sh || !sh->alive) continue;
            if (sh->kind == ShapeKind::Box) {
                drawAABB(sh->box, c);
                // Frame lines for the classic "build panel" look
                si::Vec3 sz = sh->box.size();
                Color fc = {(unsigned char)(c.r * 0.7f), (unsigned char)(c.g * 0.7f), (unsigned char)(c.b * 0.7f), 255};
                if (s.piece == PieceType::Wall && sz.y > 3.0f) {
                    si::Vec3 ctr = sh->box.center();
                    si::Vec3 half = sz * 0.5f;
                    if (s.rot == 0) drawBox(ctr, {half.x, 0.06f, half.z + 0.01f}, Basis(), fc);
                    else drawBox(ctr, {half.x + 0.01f, 0.06f, half.z}, Basis(), fc);
                }
            } else if (sh->kind == ShapeKind::Ramp) {
                drawRampShape(sh->box, sh->rampDir, c);
            } else {
                drawPyramid(sh->box, c);
            }
        }
    }
    // Launch pads
    setDrawMaterial(M_METAL);
    for (auto& lp : launchPads_) {
        drawAABB(AABB(lp + si::Vec3{-1.2f, 0, -1.2f}, lp + si::Vec3{1.2f, 0.2f, 1.2f}), Color{60, 60, 70, 255});
        drawAABB(AABB(lp + si::Vec3{-0.9f, 0.2f, -0.9f}, lp + si::Vec3{0.9f, 0.28f, 0.9f}), Color{240, 180, 40, 255});
    }
}

void GameClient::renderItems() {
    si::Vec3 cp = viewPos();
    // Chests
    for (size_t i = 0; i < map_.chests.size(); i++) {
        if (chestOpened_[i]) continue;
        const si::Vec3& p = map_.chests[i].pos;
        if ((p - cp).lenXZ() > 150) continue;
        Basis cb = yawBasis(map_.chests[i].yaw);
        setDrawMaterial(M_WOOD);
        float glow = 0.5f + 0.5f * std::sin(time_ * 3.0f + i);
        drawBox(p + si::Vec3{0, 0.32f, 0}, {0.55f, 0.32f, 0.35f}, cb, Color{150, 95, 40, 255});                     // wooden body
        drawBox(p + si::Vec3{0, 0.72f, 0}, {0.56f, 0.1f, 0.36f}, cb, Color{165, 105, 45, 255});                     // lid
        drawBox(p + si::Vec3{0, 0.83f, 0}, {0.5f, 0.04f, 0.3f}, cb, Color{175, 115, 50, 255});
        for (int k = -1; k <= 1; k += 2) {
            drawBox(p + si::Vec3{0, 0.45f, 0} + cb.r * (0.42f * k), {0.05f, 0.44f, 0.37f}, cb, Color{235, 190, 60, 255}); // gold bands
        }
        drawBox(p + si::Vec3{0, 0.64f, 0}, {0.57f, 0.035f, 0.37f}, cb, Color{235, 190, 60, 255});
        drawBox(p + si::Vec3{0, 0.55f, 0} + cb.f * 0.36f, {0.08f, 0.1f, 0.02f}, cb, Color{(unsigned char)(200 + 55 * glow), 200, 90, 255}); // lock
    }
    for (size_t i = 0; i < map_.ammoBoxes.size(); i++) {
        if (ammoBoxOpened_[i]) continue;
        const si::Vec3& p = map_.ammoBoxes[i];
        if ((p - cp).lenXZ() > 100) continue;
        drawBox(p + si::Vec3{0, 0.25f, 0}, {0.45f, 0.25f, 0.28f}, Basis(), Color{90, 110, 80, 255});
        drawBox(p + si::Vec3{0, 0.51f, 0}, {0.46f, 0.02f, 0.29f}, Basis(), Color{200, 190, 90, 255});
        drawBox(p + si::Vec3{0, 0.3f, 0.285f}, {0.3f, 0.08f, 0.01f}, Basis(), Color{220, 210, 120, 255});
        for (int k = -1; k <= 1; k += 2) drawBox(p + si::Vec3{0.47f * k, 0.3f, 0}, {0.02f, 0.05f, 0.12f}, Basis(), Color{50, 60, 45, 255});
    }
    // Floor items
    setDrawMaterial(M_PLAIN);
    for (auto& [id, it] : items_) {
        if ((it.pos - cp).lenXZ() > 90) continue;
        float bob = 0.3f + std::sin(time_ * 2.0f + id) * 0.08f;
        float spin = time_ * 1.2f + id;
        Basis b = yawBasis(spin);
        si::Vec3 p = it.pos + si::Vec3{0, bob, 0};
        const ItemDef& d = itemDef(it.stack.type);
        if (d.cls == ItemClass::Ammo) {
            drawBox(p, {0.2f, 0.12f, 0.12f}, b, Color{140, 140, 90, 255});
            drawBox(p + si::Vec3{0, 0.13f, 0}, {0.21f, 0.02f, 0.13f}, b, Color{230, 210, 120, 255});
        } else if (d.cls == ItemClass::Mats) {
            Color mc = materialColor((si::Material)it.stack.extra);
            for (int k = 0; k < 3; k++) drawBox(p + si::Vec3{0, k * 0.12f - 0.1f, 0}, {0.3f, 0.05f, 0.14f}, yawBasis(spin + k * 0.4f), mc);
        } else {
            drawHeldItem(p, b, (uint8_t)it.stack.type, (uint8_t)it.stack.rarity, Loadout(), 0);
            drawBox(it.pos + si::Vec3{0, 0.02f, 0}, {0.4f, 0.01f, 0.4f}, b, C(rarityColor(it.stack.rarity)));
        }
    }
    // Supply drops
    for (auto& [id, d] : drops_) {
        if (d.opened) continue;
        drawBox(d.pos + si::Vec3{0, 0.6f, 0}, {0.8f, 0.6f, 0.8f}, Basis(), Color{60, 110, 200, 255});
        drawBox(d.pos + si::Vec3{0, 0.6f, 0}, {0.82f, 0.1f, 0.82f}, Basis(), Color{240, 240, 250, 255});
        if (d.pos.y > d.groundY + 0.2f) {
            drawBox(d.pos + si::Vec3{0, 4.2f, 0}, {1.4f, 1.4f, 1.4f}, yawBasis(time_), Color{230, 70, 70, 255});
        }
    }
    // Projectiles
    for (auto& pr : projectiles_) {
        Color c = pr.type == (uint8_t)ItemType::RocketLauncher ? Color{220, 220, 220, 255} : C(itemDef((ItemType)pr.type).color);
        if (pr.type == (uint8_t)ItemType::Crossbow) {
            drawBox(pr.pos, {0.02f, 0.02f, 0.35f}, yawBasis(time_), Color{210, 200, 180, 255});
        } else if (pr.type == (uint8_t)ItemType::StickyCharge) {
            drawBox(pr.pos, {0.14f, 0.08f, 0.14f}, Basis(), c);
            if (std::fmod(time_, 0.4f) < 0.2f) drawBox(pr.pos + si::Vec3{0, 0.1f, 0}, {0.04f, 0.03f, 0.04f}, Basis(), Color{255, 60, 40, 255});
        } else {
            drawBox(pr.pos, {0.15f, 0.15f, 0.15f}, yawBasis(time_ * 8), c);
        }
        if (pr.type == (uint8_t)ItemType::RocketLauncher && GetRandomValue(0, 1))
            particles_.push_back({pr.pos, {0, 0.5f, 0}, 0, 0.8f, Color{120, 120, 120, 255}, 0.25f});
    }
}

void GameClient::renderPlayers() {
    for (auto& [id, p] : players_) {
        if (!p.present || !(p.cur.flags & PF_ALIVE)) continue;
        if (p.cur.mode == MoveMode::OnBus) continue;
        CharPose pose;
        bool local = id == net_.playerId && localControllable();
        if (local) {
            pose.pos = pred_.pos + smoothOffset_;
            pose.yaw = camYaw_;
            pose.pitch = camPitch_;
            pose.mode = pred_.mode;
            pose.flags = self_.flags;
            if (pred_.crouched) pose.flags |= PF_CROUCH;
            pose.speed = pred_.vel.lenXZ();
        } else {
            pose.pos = p.cur.pos;
            pose.yaw = p.cur.yaw;
            pose.pitch = p.cur.pitch;
            pose.mode = p.cur.mode;
            pose.flags = p.cur.flags;
            pose.speed = p.speed;
        }
        uint16_t vehId = local ? self_.vehicle : p.cur.vehicle;
        uint8_t seat = local ? self_.seat : p.cur.seat;
        if (vehId != 0xFFFF) {
            VehicleVisual vv;
            if (vehicleVisual(vehId, vv)) {
                const VehicleDef& d = vehicleDef(vv.type);
                if (seat >= d.seats) seat = 0;
                // Seat position through the vehicle's full orientation so riders follow flips and slopes
                Basis vb = vehicleBasis(vv);
                const si::Vec3& o = d.seatPos[seat];
                pose.pos = vv.pos + vb.r * o.x + vb.u * o.y + vb.f * o.z;
                if (vv.type == VehicleType::Hoverboard) pose.pos.y += 0.06f * std::sin(time_ * 3.0f + vv.id);
                pose.seatPose = d.seatPose[seat];
                pose.steering = seat == 0;
                pose.mode = MoveMode::Ground;
                pose.speed = 0;
                bool aimingSeat = d.seatShoot[seat] && pose.flags & (PF_FIRING | PF_ADS);
                if (!aimingSeat) {
                    pose.yaw = vv.yaw;
                    if (vv.type != VehicleType::RollerBall) { pose.tilted = true; pose.tilt = vb; }
                }
                if (aimingSeat) pose.steering = false;
                if (!d.seatShoot[seat]) pose.steering = seat == 0;
            }
        }
        if ((S(cam_.position) - pose.pos).len() > settings_.viewDistance * 0.8f) continue;
        pose.animTime = p.animTime;
        pose.heldType = local ? (self_.selected >= 1 && self_.selected <= 5 ? (uint8_t)self_.inv[self_.selected - 1].type : 0) : p.cur.heldType;
        pose.heldRarity = local ? (self_.selected >= 1 && self_.selected <= 5 ? (uint8_t)self_.inv[self_.selected - 1].rarity : 0) : p.cur.heldRarity;
        pose.building = p.cur.buildPiece != 0xFF || (local && self_.buildMode);
        if (pose.seatPose && vehId != 0xFFFF) {
            // Hands are busy driving / holding on unless this seat can shoot
            VehicleVisual vv;
            if (vehicleVisual(vehId, vv) && !vehicleDef(vv.type).seatShoot[seat < MAX_SEATS ? seat : 0]) { pose.heldType = 0; pose.building = true; }
        }
        pose.swing = p.swing;
        if (p.cur.emote) {
            const CosmeticDef* e = findCosmetic(p.loadout.emotes[(p.cur.emote - 1) % 6]);
            pose.emote = e ? e->style.shape : 1;
        }
        drawCharacter(pose, p.loadout, time_);
    }
}

void GameClient::renderBus() {
    if (!g_.busActive) return;
    si::Vec3 p = lerp3(g_.busStart, g_.busEnd, clampf(g_.busProgress, 0, 1));
    si::Vec3 dir = (g_.busEnd - g_.busStart).norm();
    Basis b = yawBasis(yawFromDir(dir));
    // Airship (original design): rounded envelope, striped band, gondola with windows,
    // four tail fins and two engine pods with spinning propellers.
    Color env{70, 130, 220, 255}, env2{84, 146, 234, 255}, stripe{245, 245, 250, 255}, trim{230, 200, 90, 255};
    setDrawMaterial(M_FABRIC);
    si::Vec3 c = p + si::Vec3{0, 6, 0};
    const int N = 13;
    for (int i = 0; i < N; i++) {
        float t = (i - (N - 1) / 2.0f) / ((N - 1) / 2.0f);   // -1..1 along the length
        float r = 3.6f * std::sqrt(std::max(0.05f, 1.0f - t * t));
        si::Vec3 slice = c + b.f * (t * 9.5f);
        drawBox(slice, {r, r * 0.92f, 0.76f}, b, i % 2 ? env : env2);
        drawBox(slice, {r * 0.72f, r * 0.98f, 0.77f}, b, i % 2 ? env : env2); // rounder profile
        if (r > 1.5f) drawBox(slice, {r + 0.02f, 0.3f, 0.77f}, b, stripe);
    }
    // Nose cap and tail cone
    drawBox(c + b.f * 10.2f, {0.9f, 0.8f, 0.4f}, b, trim);
    drawBox(c - b.f * 10.1f, {0.6f, 0.55f, 0.4f}, b, trim);
    // Tail fins
    Color fin{230, 80, 60, 255};
    drawBox(c - b.f * 8.3f + b.u * 2.6f, {0.12f, 1.5f, 1.4f}, b, fin);
    drawBox(c - b.f * 8.3f - b.u * 2.6f, {0.12f, 1.5f, 1.4f}, b, fin);
    drawBox(c - b.f * 8.3f + b.r * 2.6f, {1.5f, 0.12f, 1.4f}, b, fin);
    drawBox(c - b.f * 8.3f - b.r * 2.6f, {1.5f, 0.12f, 1.4f}, b, fin);
    // Gondola with windows and a railing deck
    setDrawMaterial(M_METAL);
    si::Vec3 g = p + si::Vec3{0, 1.7f, 0};
    drawBox(g, {1.6f, 1.1f, 4.4f}, b, trim);
    drawBox(g - b.u * 1.15f, {1.7f, 0.1f, 4.6f}, b, darker4(trim));
    for (int k = -3; k <= 3; k++) {
        for (int sd = -1; sd <= 1; sd += 2) drawBox(g + b.r * (1.61f * sd) + b.u * 0.25f + b.f * (k * 1.15f), {0.02f, 0.35f, 0.4f}, b, Color{150, 215, 250, 255});
    }
    drawBox(g + b.f * 4.41f + b.u * 0.25f, {1.2f, 0.4f, 0.02f}, b, Color{150, 215, 250, 255});
    // Struts connecting gondola to envelope
    for (int sd = -1; sd <= 1; sd += 2)
        for (int k = -1; k <= 1; k += 2) drawBox(p + si::Vec3{0, 3.4f, 0} + b.r * (1.2f * sd) + b.f * (2.8f * k), {0.06f, 0.7f, 0.06f}, b, Color{80, 80, 90, 255});
    // Engine pods + propellers
    for (int sd = -1; sd <= 1; sd += 2) {
        si::Vec3 pod = p + b.r * (2.9f * sd) + si::Vec3{0, 2.0f, 0} - b.f * 2.4f;
        drawBox(pod, {0.45f, 0.45f, 1.0f}, b, Color{200, 205, 215, 255});
        drawBox(pod + b.r * (-1.2f * sd), {0.8f, 0.08f, 0.2f}, b, Color{80, 80, 90, 255});
        float ang = time_ * 22.0f;
        Basis pb = compose(b, Basis{{std::cos(ang), std::sin(ang), 0}, {-std::sin(ang), std::cos(ang), 0}, {0, 0, 1}});
        drawBox(pod - b.f * 1.1f, {1.3f, 0.12f, 0.04f}, pb, Color{45, 45, 50, 255});
        drawBox(pod - b.f * 1.1f, {0.12f, 1.3f, 0.04f}, pb, Color{45, 45, 50, 255});
        drawBox(pod - b.f * 1.15f, {0.18f, 0.18f, 0.1f}, b, trim);
    }
}

void GameClient::renderSlipstreams() {
    si::Vec3 cp = S(cam_.position);
    for (auto& s : map_.slipstreams) {
        for (size_t i = 0; i + 1 < s.points.size(); i++) {
            const si::Vec3& a = s.points[i];
            const si::Vec3& b = s.points[i + 1];
            if ((a - cp).lenXZ() > 450) continue;
            si::Vec3 d = (b - a);
            float len = d.len();
            si::Vec3 dn = d / len;
            // Moving rings along the stream
            for (float t = std::fmod(time_ * 20.0f, 10.0f); t < len; t += 10.0f) {
                si::Vec3 c = a + dn * t;
                float yawA = yawFromDir(dn) * RAD2DEG;
                DrawCircle3D(V(c), s.radius, {0, 1, 0}, yawA, Color{130, 220, 255, 110});
                DrawCircle3D(V(c), s.radius * 0.6f, {0, 1, 0}, yawA, Color{200, 245, 255, 70});
            }
            DrawLine3D(V(a), V(b), Color{150, 230, 255, 160});
        }
    }
    for (auto& v : map_.vents) {
        if ((v.pos - cp).lenXZ() > 300) continue;
        DrawCylinder(V(v.pos), v.radius, v.radius, 0.2f, 16, Color{255, 120, 40, 200});
        if (GetRandomValue(0, 3) == 0) particles_.push_back({v.pos + si::Vec3{GetRandomValue(-20, 20) / 10.0f, 0, GetRandomValue(-20, 20) / 10.0f}, {0, 6, 0}, 0, 1.5f, Color{255, 160, 80, 255}, 0.2f});
    }
}

void GameClient::renderEffects() {
    for (auto& t : tracers_) {
        Color c = t.c;
        c.a = (unsigned char)(255 * clampf(t.t / 0.07f, 0, 1));
        DrawLine3D(V(t.a), V(t.b), c);
        DrawLine3D(V(t.a + si::Vec3{0, 0.02f, 0}), V(t.b + si::Vec3{0, 0.02f, 0}), c);
    }
    for (auto& p : particles_) {
        Color c = p.c;
        c.a = (unsigned char)(255 * clampf(1 - p.t / p.life, 0, 1));
        DrawCube(V(p.p), p.size, p.size, p.size, c);
    }
    for (auto& f : flashes_) {
        float k = f.t / 0.05f;
        DrawSphere(V(f.p), 0.12f + 0.12f * k, Color{255, 230, 150, (unsigned char)(230 * k)});
        DrawSphere(V(f.p), 0.28f * k, Color{255, 170, 60, (unsigned char)(90 * k)});
    }
    for (auto& b : blasts_) {
        float k = b.t / 0.6f;
        DrawSphere(V(b.p), b.r * (0.3f + k), Color{255, (unsigned char)(200 - 150 * k), 60, (unsigned char)(220 * (1 - k))});
    }
    // Contrails
    for (auto& [id, p] : players_) {
        if (p.trail.size() < 2) continue;
        const CosmeticDef* ct = findCosmetic(p.loadout.contrail);
        Color c = ct ? C(ct->style.primary) : WHITE;
        for (size_t i = 1; i < p.trail.size(); i++) {
            c.a = (unsigned char)(200 * i / p.trail.size());
            DrawLine3D(V(p.trail[i - 1] + si::Vec3{0.4f, 0, 0}), V(p.trail[i] + si::Vec3{0.4f, 0, 0}), c);
            DrawLine3D(V(p.trail[i - 1] - si::Vec3{0.4f, 0, 0}), V(p.trail[i] - si::Vec3{0.4f, 0, 0}), c);
        }
    }
    // Loot beams for rare+ items
    si::Vec3 cp = viewPos();
    for (auto& [id, it] : items_) {
        if ((int)it.stack.rarity < (int)Rarity::Rare || itemDef(it.stack.type).cls != ItemClass::Gun) continue;
        if ((it.pos - cp).lenXZ() > 90) continue;
        Color c = C(rarityColor(it.stack.rarity));
        c.a = 90;
        DrawCylinder(V(it.pos), 0.05f, 0.05f, 6.0f, 6, c);
    }
}

void GameClient::renderBuildPreview() {
    if (!localControllable() || !self_.buildMode) return;
    BuildTarget t = computeBuildTarget(pred_.pos, camYaw_, camPitch_, (PieceType)self_.buildPiece, self_.buildRot);
    bool freeBuild = g_.phase == MatchPhase::Warmup || g_.phase == MatchPhase::Countdown;
    bool enough = freeBuild || self_.mats[self_.buildMat] >= BUILD_COST || self_.mats[0] + self_.mats[1] + self_.mats[2] >= BUILD_COST;
    bool valid = structures_.canPlace(t, world_) && enough;
    Structure s;
    s.piece = t.piece; s.gx = t.gx; s.gy = t.gy; s.gz = t.gz; s.rot = t.rot; s.id = 0;
    std::vector<Shape> shapes;
    pieceShapes(s, shapes);
    Color c = valid ? Color{80, 170, 255, 110} : Color{255, 80, 80, 90};
    rlDisableDepthMask();
    for (auto& sh : shapes) {
        if (sh.kind == ShapeKind::Box) {
            DrawCube(V(sh.box.center()), sh.box.size().x, sh.box.size().y, sh.box.size().z, c);
            DrawCubeWires(V(sh.box.center()), sh.box.size().x, sh.box.size().y, sh.box.size().z, Color{200, 230, 255, 200});
        } else if (sh.kind == ShapeKind::Ramp) {
            rlDisableBackfaceCulling();
            drawRampShape(sh.box, sh.rampDir, c);
            rlEnableBackfaceCulling();
        } else {
            rlDisableBackfaceCulling();
            drawPyramid(sh.box, c);
            rlEnableBackfaceCulling();
        }
    }
    rlEnableDepthMask();
}

void GameClient::renderStorm() {
    if (g_.stormPhase < 0 || g_.stormCurR > 3000) return;
    rlDisableBackfaceCulling();
    rlDisableDepthMask();
    float pulse = 0.5f + 0.5f * std::sin(time_ * 1.5f);
    DrawCylinder({g_.stormCur.x, -40, g_.stormCur.y}, g_.stormCurR, g_.stormCurR, 700, 96,
                 Color{150, 60, 220, (unsigned char)(55 + 20 * pulse)});
    // Next safe zone outline on the ground level
    DrawCircle3D({g_.stormNext.x, 2.0f, g_.stormNext.y}, g_.stormNextR, {1, 0, 0}, 90, Color{255, 255, 255, 120});
    rlEnableDepthMask();
    rlEnableBackfaceCulling();
}

} // namespace client
