// Snapshot structure + codec (server -> client world state).
#pragma once
#include <vector>

#include "../game/items.h"
#include "protocol.h"

namespace si {

struct SnapSelf {
    uint16_t id = 0;
    uint16_t flags = 0;
    MoveMode mode = MoveMode::Ground;
    Vec3 pos, vel;
    uint8_t mflags = 0;       // 1 onGround, 2 canGlide, 4 crouched
    uint16_t prevButtons = 0;
    float fallStartY = 0;
    float health = 0, shield = 0, dbnoHealth = 0;
    uint8_t selected = 0;
    ItemStack inv[INVENTORY_SLOTS];
    uint16_t ammo[(int)AmmoType::Count] = {0};
    uint16_t mats[3] = {0, 0, 0};
    uint8_t buildMat = 0, buildMode = 0, buildPiece = 0, buildRot = 0;
    uint8_t action = 0;
    float actionTime = 0, actionTotal = 0;
    uint16_t kills = 0;
    uint16_t spectating = 0xFFFF;
    float regenLeft = 0;
    uint8_t placement = 0;
    float bloom = 0;
    uint8_t team = 0;
};

struct SnapGlobal {
    MatchPhase phase = MatchPhase::Warmup;
    float phaseTimer = 0;
    uint16_t alive = 0, total = 0;
    uint8_t teamSize = 1;
    int8_t stormPhase = -1;
    uint8_t stormShrinking = 0;
    float stormTimer = 0;
    Vec2 stormCur, stormNext;
    float stormCurR = 0, stormNextR = 0, stormDamage = 0;
    uint8_t busActive = 0;
    Vec3 busStart, busEnd;
    float busProgress = 0;
};

struct SnapPlayer {
    uint16_t id = 0;
    uint16_t flags = 0;
    MoveMode mode = MoveMode::Ground;
    Vec3 pos;
    float yaw = 0, pitch = 0;
    uint8_t heldType = 0, heldRarity = 0;
    uint8_t health = 0, shield = 0;
    uint8_t team = 0;
    uint8_t emote = 0;
    uint8_t buildPiece = 0xFF;
};

struct SnapProjectile { uint32_t id; uint8_t type; Vec3 pos; };
struct SnapDrop { uint32_t id; Vec3 pos; };
struct SnapEffect { uint8_t type; uint16_t player; Vec3 a, b; uint8_t extra; };
struct SnapEvent { uint32_t seq; std::vector<uint8_t> data; };

struct Snapshot {
    uint32_t tick = 0;
    float time = 0;
    uint32_t lastInputSeq = 0;
    uint16_t lastActionId = 0;
    bool hasSelf = false;
    SnapSelf self;
    SnapGlobal g;
    std::vector<SnapPlayer> players;
    std::vector<SnapProjectile> projectiles;
    std::vector<SnapDrop> drops;
    std::vector<SnapEffect> effects;
    std::vector<SnapEvent> events;
};

inline void writeStack(ByteWriter& w, const ItemStack& s) { w.u8((uint8_t)s.type); w.u8((uint8_t)s.rarity); w.u16(s.count); w.u16(s.clip); w.u8(s.extra); }
inline ItemStack readStack(ByteReader& r) {
    ItemStack s;
    s.type = (ItemType)r.u8(); s.rarity = (Rarity)r.u8(); s.count = r.u16(); s.clip = r.u16(); s.extra = r.u8();
    if ((int)s.type >= (int)ItemType::Count) s.type = ItemType::None;
    if ((int)s.rarity >= (int)Rarity::Count) s.rarity = Rarity::Common;
    return s;
}

inline void writeSnapshot(ByteWriter& w, const Snapshot& s) {
    w.u8(PKT_SNAPSHOT);
    w.u32(s.tick); w.f32(s.time); w.u32(s.lastInputSeq); w.u16(s.lastActionId);
    w.u8(s.hasSelf ? 1 : 0);
    if (s.hasSelf) {
        const SnapSelf& m = s.self;
        w.u16(m.id); w.u16(m.flags); w.u8((uint8_t)m.mode); w.vec3(m.pos); w.vec3(m.vel); w.u8(m.mflags); w.u16(m.prevButtons);
        w.f32(m.fallStartY); w.f32(m.health); w.f32(m.shield); w.f32(m.dbnoHealth); w.u8(m.selected);
        for (auto& it : m.inv) writeStack(w, it);
        for (auto a : m.ammo) w.u16(a);
        for (auto a : m.mats) w.u16(a);
        w.u8(m.buildMat); w.u8(m.buildMode); w.u8(m.buildPiece); w.u8(m.buildRot);
        w.u8(m.action); w.f32(m.actionTime); w.f32(m.actionTotal);
        w.u16(m.kills); w.u16(m.spectating); w.f32(m.regenLeft); w.u8(m.placement); w.f32(m.bloom); w.u8(m.team);
    }
    const SnapGlobal& g = s.g;
    w.u8((uint8_t)g.phase); w.f32(g.phaseTimer); w.u16(g.alive); w.u16(g.total); w.u8(g.teamSize);
    w.i8(g.stormPhase); w.u8(g.stormShrinking); w.f32(g.stormTimer);
    w.f32(g.stormCur.x); w.f32(g.stormCur.y); w.f32(g.stormCurR); w.f32(g.stormNext.x); w.f32(g.stormNext.y); w.f32(g.stormNextR);
    w.f32(g.stormDamage);
    w.u8(g.busActive); w.vec3(g.busStart); w.vec3(g.busEnd); w.f32(g.busProgress);
    w.u16((uint16_t)s.players.size());
    for (auto& p : s.players) {
        w.u16(p.id); w.u16(p.flags); w.u8((uint8_t)p.mode); w.vec3(p.pos); w.angle(p.yaw); w.angle(p.pitch);
        w.u8(p.heldType); w.u8(p.heldRarity); w.u8(p.health); w.u8(p.shield); w.u8(p.team); w.u8(p.emote); w.u8(p.buildPiece);
    }
    w.u16((uint16_t)s.projectiles.size());
    for (auto& p : s.projectiles) { w.u32(p.id); w.u8(p.type); w.vec3(p.pos); }
    w.u8((uint8_t)std::min<size_t>(255, s.drops.size()));
    for (size_t i = 0; i < s.drops.size() && i < 255; i++) { w.u32(s.drops[i].id); w.vec3(s.drops[i].pos); }
    w.u16((uint16_t)s.effects.size());
    for (auto& e : s.effects) { w.u8(e.type); w.u16(e.player); w.vec3(e.a); w.vec3(e.b); w.u8(e.extra); }
    w.u16((uint16_t)s.events.size());
    for (auto& e : s.events) { w.u32(e.seq); w.u16((uint16_t)e.data.size()); w.raw(e.data.data(), e.data.size()); }
}

inline bool readSnapshot(ByteReader& r, Snapshot& s) {
    s.tick = r.u32(); s.time = r.f32(); s.lastInputSeq = r.u32(); s.lastActionId = r.u16();
    s.hasSelf = r.u8() != 0;
    if (s.hasSelf) {
        SnapSelf& m = s.self;
        m.id = r.u16(); m.flags = r.u16(); m.mode = (MoveMode)r.u8(); m.pos = r.vec3(); m.vel = r.vec3(); m.mflags = r.u8(); m.prevButtons = r.u16();
        m.fallStartY = r.f32(); m.health = r.f32(); m.shield = r.f32(); m.dbnoHealth = r.f32(); m.selected = r.u8();
        for (auto& it : m.inv) it = readStack(r);
        for (auto& a : m.ammo) a = r.u16();
        for (auto& a : m.mats) a = r.u16();
        m.buildMat = r.u8(); m.buildMode = r.u8(); m.buildPiece = r.u8(); m.buildRot = r.u8();
        m.action = r.u8(); m.actionTime = r.f32(); m.actionTotal = r.f32();
        m.kills = r.u16(); m.spectating = r.u16(); m.regenLeft = r.f32(); m.placement = r.u8(); m.bloom = r.f32(); m.team = r.u8();
    }
    SnapGlobal& g = s.g;
    g.phase = (MatchPhase)r.u8(); g.phaseTimer = r.f32(); g.alive = r.u16(); g.total = r.u16(); g.teamSize = r.u8();
    g.stormPhase = r.i8(); g.stormShrinking = r.u8(); g.stormTimer = r.f32();
    g.stormCur.x = r.f32(); g.stormCur.y = r.f32(); g.stormCurR = r.f32(); g.stormNext.x = r.f32(); g.stormNext.y = r.f32(); g.stormNextR = r.f32();
    g.stormDamage = r.f32();
    g.busActive = r.u8(); g.busStart = r.vec3(); g.busEnd = r.vec3(); g.busProgress = r.f32();
    uint16_t np = r.u16();
    s.players.resize(r.ok() ? np : 0);
    for (auto& p : s.players) {
        p.id = r.u16(); p.flags = r.u16(); p.mode = (MoveMode)r.u8(); p.pos = r.vec3(); p.yaw = r.angle(); p.pitch = r.angle();
        p.heldType = r.u8(); p.heldRarity = r.u8(); p.health = r.u8(); p.shield = r.u8(); p.team = r.u8(); p.emote = r.u8(); p.buildPiece = r.u8();
    }
    uint16_t npr = r.u16();
    s.projectiles.resize(r.ok() ? npr : 0);
    for (auto& p : s.projectiles) { p.id = r.u32(); p.type = r.u8(); p.pos = r.vec3(); }
    uint8_t nd = r.u8();
    s.drops.resize(r.ok() ? nd : 0);
    for (auto& d : s.drops) { d.id = r.u32(); d.pos = r.vec3(); }
    uint16_t ne = r.u16();
    s.effects.resize(r.ok() ? ne : 0);
    for (auto& e : s.effects) { e.type = r.u8(); e.player = r.u16(); e.a = r.vec3(); e.b = r.vec3(); e.extra = r.u8(); }
    uint16_t nev = r.u16();
    s.events.clear();
    for (int i = 0; i < nev && r.ok(); i++) {
        SnapEvent e;
        e.seq = r.u32();
        uint16_t len = r.u16();
        if (r.remaining() < len) return false;
        e.data.assign(r.p + r.pos, r.p + r.pos + len);
        r.skip(len);
        s.events.push_back(std::move(e));
    }
    return r.ok();
}

} // namespace si
