// UDP game protocol shared by the game server and the client.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "../game/cosmetics.h"
#include "../game/defs.h"
#include "bytes.h"

namespace si {

enum PacketType : uint8_t {
    // client -> server
    PKT_HELLO = 1,
    PKT_INPUT = 2,
    PKT_BYE = 3,
    // server -> client
    PKT_WELCOME = 10,
    PKT_REJECT = 11,
    PKT_SNAPSHOT = 12,
    PKT_KICK = 13,
};

enum EventType : uint8_t {
    EV_PLAYER_INFO = 1,
    EV_PLAYER_LEFT,
    EV_STRUCT_ADD,
    EV_STRUCT_REMOVE,
    EV_STRUCT_EDIT,
    EV_STRUCT_HP,
    EV_SHAPE_REMOVE,
    EV_ITEM_SPAWN,
    EV_ITEM_REMOVE,
    EV_CHEST_OPEN,
    EV_AMMOBOX_OPEN,
    EV_KILLFEED,
    EV_DAMAGE,
    EV_MESSAGE,
    EV_MATCH_RESULT,
    EV_LAUNCHPAD_ADD,
    EV_SUPPLY_DROP,
    EV_SUPPLY_OPEN,
    EV_RESET_WORLD,   // clears structures/items/chests (warmup -> match)
    EV_TAKE_DAMAGE,   // to victim: direction indicator
};

enum KillFlags : uint8_t { KF_HEADSHOT = 1, KF_KNOCKED = 2, KF_STORM = 4, KF_FALL = 8, KF_EXPLOSION = 16, KF_PICKAXE = 32 };
enum DamageFlags : uint8_t { DF_SHIELD = 1, DF_HEADSHOT = 2, DF_STRUCTURE = 4, DF_KILL = 8 };

enum EffectType : uint8_t { FX_SHOT = 1, FX_EXPLOSION = 2, FX_IMPACT = 3, FX_HARVEST = 4, FX_BUILD = 5 };

enum ActionKind : uint8_t { ACT_NONE = 0, ACT_RELOAD, ACT_CONSUME, ACT_INTERACT, ACT_REVIVE, ACT_EQUIP };

struct InputCmd {
    uint32_t seq = 0;
    int8_t fwd = 0, right = 0;
    float yaw = 0, pitch = 0;
    uint16_t buttons = 0;
};

struct ClientAction {
    uint16_t id = 0;
    ActionType type = ActionType::None;
    uint8_t a = 0, b = 0;
};

inline void writeLoadout(ByteWriter& w, const Loadout& l) {
    w.str(l.outfit); w.str(l.backbling); w.str(l.pickaxe); w.str(l.glider); w.str(l.contrail);
    for (auto& e : l.emotes) w.str(e);
}
inline Loadout readLoadout(ByteReader& r) {
    Loadout l;
    l.outfit = r.str(); l.backbling = r.str(); l.pickaxe = r.str(); l.glider = r.str(); l.contrail = r.str();
    for (auto& e : l.emotes) e = r.str();
    return l;
}

inline void writeInput(ByteWriter& w, const InputCmd& c) {
    w.u32(c.seq); w.i8(c.fwd); w.i8(c.right); w.f32(c.yaw); w.f32(c.pitch); w.u16(c.buttons);
}
inline InputCmd readInput(ByteReader& r) {
    InputCmd c;
    c.seq = r.u32(); c.fwd = r.i8(); c.right = r.i8(); c.yaw = r.f32(); c.pitch = r.f32(); c.buttons = r.u16();
    return c;
}

// Third-person camera shared by client (rendering) and server (hit-scan origin).
struct CameraRig {
    Vec3 pos;
    Vec3 dir;
};
inline CameraRig thirdPersonCamera(const Vec3& feet, float yaw, float pitch, bool ads, bool crouched) {
    float eye = crouched ? 1.15f : EYE_HEIGHT;
    Vec3 head = feet + Vec3{0, eye + 0.15f, 0};
    Vec3 dir = dirFromAngles(yaw, pitch);
    Vec3 right{-std::cos(yaw), 0, std::sin(yaw)};
    float back = ads ? 1.6f : 3.2f;
    float shoulder = ads ? 0.55f : 0.75f;
    CameraRig c;
    c.pos = head - dir * back + right * shoulder + Vec3{0, 0.25f, 0};
    c.dir = dir;
    return c;
}

} // namespace si
