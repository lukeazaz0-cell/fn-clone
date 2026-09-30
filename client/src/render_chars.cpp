// Procedural low-poly characters. Every outfit is an original design defined by
// the colors/shape knobs in shared/game/cosmetics.cpp.
#include <cmath>

#include "models.h"
#include "render.h"
#include "rlgl.h"
#include "shared/game/items.h"

namespace client {

namespace {

Color col(const Color4& c) { return {c.r, c.g, c.b, 255}; }
Color darker(Color c, float f) {
    auto ch = [f](unsigned char v) { return (unsigned char)clampf(v * f, 0.0f, 255.0f); };
    return {ch(c.r), ch(c.g), ch(c.b), c.a};
}

const CosmeticStyle& styleOf(const std::string& id, CosmeticType t) {
    const CosmeticDef* d = findCosmetic(id);
    if (!d || d->type != t) d = findCosmetic(defaultCosmetic(t));
    return d->style;
}

// Rotate a basis around its own right axis. Positive angles swing "down" (-u) toward +f.
Basis swingBasis(const Basis& parent, float angle) { return compose(parent, yawPitchBasis(0, angle)); }

struct LimbStyle {
    float upperLen, lowerLen, upperThick, lowerThick;
    Color upper, lower, joint, end;
    bool foot; // true = shoe pointing forward, false = hand
};

// Two-segment limb (thigh/shin or upper arm/forearm). `bend` bends the lower segment:
// negative for knees (backwards), positive for elbows (forwards). Returns the end point.
si::Vec3 limb2(const si::Vec3& pivot, const Basis& parent, float swing, float bend, const LimbStyle& s, Basis* endBasis = nullptr) {
    Basis ub = swingBasis(parent, swing);
    si::Vec3 joint = pivot - ub.u * s.upperLen;
    drawBox(pivot - ub.u * (s.upperLen * 0.5f), {s.upperThick, s.upperLen * 0.5f + 0.02f, s.upperThick}, ub, s.upper);
    drawBox(joint, {s.upperThick * 1.02f, s.upperThick * 0.9f, s.upperThick * 1.02f}, ub, s.joint);
    Basis lb = swingBasis(ub, bend);
    si::Vec3 end = joint - lb.u * s.lowerLen;
    drawBox(joint - lb.u * (s.lowerLen * 0.5f), {s.lowerThick, s.lowerLen * 0.5f, s.lowerThick}, lb, s.lower);
    if (s.foot) {
        // Shoe: sole + toe cap, always roughly level with the leg
        Basis fb = swingBasis(lb, -(swing + bend));
        drawBox(end + fb.f * 0.05f - fb.u * 0.03f, {s.lowerThick * 1.1f, 0.06f, 0.16f}, fb, s.end);
        drawBox(end + fb.f * 0.15f - fb.u * 0.06f, {s.lowerThick * 1.12f, 0.03f, 0.06f}, fb, darker(s.end, 0.6f));
    } else {
        drawBox(end - lb.u * 0.05f, {s.lowerThick * 1.1f, 0.07f, s.lowerThick * 1.15f}, lb, s.end);
        drawBox(end - lb.u * 0.07f + lb.f * 0.06f, {0.025f, 0.04f, 0.025f}, lb, s.end); // thumb
    }
    if (endBasis) *endBasis = lb;
    return end;
}

void gunModel(const si::Vec3& hand, const Basis& b, ItemType t, Color body, Color rc) {
    Color dark = darker(body, 0.6f), metal{60, 62, 70, 255}, wood{120, 80, 45, 255};
    auto part = [&](float x, float y, float z, float hx, float hy, float hz, Color c) {
        drawBox(hand + b.r * x + b.u * y + b.f * z, {hx, hy, hz}, b, c);
    };
    part(0, -0.09f, -0.02f, 0.035f, 0.08f, 0.04f, dark); // grip
    switch (t) {
        case ItemType::AssaultRifle:
        case ItemType::BurstRifle:
        case ItemType::ScopedRifle:
            part(0, 0.02f, 0.18f, 0.045f, 0.07f, 0.26f, body);      // receiver
            part(0, 0.04f, 0.58f, 0.018f, 0.018f, 0.16f, metal);    // barrel
            part(0, 0.0f, 0.46f, 0.04f, 0.045f, 0.07f, dark);       // handguard
            part(0, -0.12f, 0.2f, 0.03f, 0.09f, 0.045f, metal);     // magazine
            part(0, 0.0f, -0.18f, 0.035f, 0.06f, 0.12f, dark);      // stock
            if (t == ItemType::ScopedRifle) part(0, 0.13f, 0.18f, 0.03f, 0.03f, 0.12f, Color{25, 25, 30, 255});
            else part(0, 0.1f, 0.14f, 0.012f, 0.03f, 0.03f, metal);  // iron sight
            part(0, 0.095f, 0.25f, 0.047f, 0.012f, 0.1f, rc);       // rarity stripe
            break;
        case ItemType::PumpShotgun:
        case ItemType::TacticalShotgun:
            part(0, 0.02f, 0.14f, 0.045f, 0.06f, 0.2f, body);
            part(0, 0.05f, 0.5f, 0.025f, 0.025f, 0.22f, metal);     // barrel
            part(0, -0.01f, 0.45f, 0.035f, 0.035f, 0.1f, t == ItemType::PumpShotgun ? wood : dark); // pump
            part(0, -0.01f, -0.2f, 0.035f, 0.055f, 0.13f, t == ItemType::PumpShotgun ? wood : dark);
            part(0, 0.083f, 0.14f, 0.047f, 0.012f, 0.1f, rc);
            break;
        case ItemType::SMG:
            part(0, 0.02f, 0.1f, 0.04f, 0.07f, 0.17f, body);
            part(0, 0.03f, 0.33f, 0.016f, 0.016f, 0.07f, metal);
            part(0, -0.14f, 0.06f, 0.025f, 0.1f, 0.035f, metal);    // long mag
            part(0, 0.1f, 0.1f, 0.047f, 0.012f, 0.08f, rc);
            break;
        case ItemType::Pistol:
            part(0, 0.03f, 0.08f, 0.03f, 0.045f, 0.12f, body);
            part(0, 0.08f, 0.08f, 0.032f, 0.01f, 0.08f, rc);
            break;
        case ItemType::SniperRifle:
            part(0, 0.02f, 0.2f, 0.04f, 0.06f, 0.3f, wood);
            part(0, 0.04f, 0.75f, 0.018f, 0.018f, 0.28f, metal);
            part(0, 0.14f, 0.2f, 0.035f, 0.035f, 0.17f, Color{25, 25, 30, 255}); // scope
            part(0, 0.14f, 0.38f, 0.045f, 0.045f, 0.02f, Color{80, 150, 220, 255}); // lens
            part(0.05f, 0.06f, 0.08f, 0.03f, 0.012f, 0.012f, metal); // bolt
            part(0, 0.0f, -0.22f, 0.035f, 0.07f, 0.14f, wood);
            part(0, 0.083f, 0.0f, 0.042f, 0.012f, 0.1f, rc);
            break;
        case ItemType::RocketLauncher:
            part(0, 0.1f, 0.2f, 0.09f, 0.09f, 0.55f, body);
            part(0, 0.1f, 0.76f, 0.1f, 0.1f, 0.03f, Color{200, 60, 40, 255});
            part(0, 0.1f, -0.36f, 0.1f, 0.1f, 0.03f, dark);
            part(0.1f, 0.18f, 0.2f, 0.02f, 0.03f, 0.08f, metal);
            part(0, 0.195f, 0.2f, 0.092f, 0.012f, 0.3f, rc);
            break;
        case ItemType::Minigun:
            for (int i = 0; i < 6; i++) {
                float a = i * 2 * kPi / 6;
                part(std::cos(a) * 0.05f, 0.04f + std::sin(a) * 0.05f, 0.45f, 0.015f, 0.015f, 0.35f, metal);
            }
            part(0, 0.04f, 0.05f, 0.09f, 0.09f, 0.15f, body);
            part(0, 0.04f, 0.8f, 0.08f, 0.08f, 0.02f, dark);
            part(0, 0.14f, 0.05f, 0.092f, 0.012f, 0.1f, rc);
            break;
        case ItemType::HeavyRifle:
            part(0, 0.02f, 0.2f, 0.05f, 0.08f, 0.28f, body);
            part(0, 0.04f, 0.62f, 0.022f, 0.022f, 0.16f, metal);
            part(0, 0.04f, 0.8f, 0.03f, 0.03f, 0.03f, dark);              // muzzle brake
            part(0, -0.13f, 0.22f, 0.035f, 0.1f, 0.05f, metal);
            part(0, 0.0f, -0.2f, 0.04f, 0.07f, 0.13f, wood);
            part(0, 0.12f, 0.16f, 0.03f, 0.03f, 0.08f, Color{25, 25, 30, 255});
            part(0, 0.105f, 0.3f, 0.052f, 0.012f, 0.1f, rc);
            break;
        case ItemType::CompactSMG:
            part(0, 0.02f, 0.06f, 0.04f, 0.065f, 0.13f, body);
            part(0, 0.03f, 0.24f, 0.03f, 0.03f, 0.07f, dark);              // suppressor
            part(0, -0.12f, 0.04f, 0.025f, 0.08f, 0.03f, metal);
            part(0, -0.06f, 0.18f, 0.02f, 0.05f, 0.02f, dark);            // foregrip
            part(0, 0.09f, 0.06f, 0.042f, 0.012f, 0.07f, rc);
            break;
        case ItemType::DoubleBarrel:
            part(-0.025f, 0.05f, 0.45f, 0.022f, 0.022f, 0.3f, metal);
            part(0.025f, 0.05f, 0.45f, 0.022f, 0.022f, 0.3f, metal);
            part(0, 0.02f, 0.1f, 0.05f, 0.05f, 0.1f, metal);
            part(0, -0.01f, -0.16f, 0.04f, 0.06f, 0.16f, wood);
            part(0, 0.075f, 0.1f, 0.052f, 0.01f, 0.06f, rc);
            break;
        case ItemType::HeavyShotgun:
            part(0, 0.02f, 0.16f, 0.05f, 0.07f, 0.24f, body);
            part(0, 0.05f, 0.52f, 0.03f, 0.03f, 0.2f, metal);
            part(0, -0.13f, 0.18f, 0.035f, 0.08f, 0.06f, dark);            // box mag
            part(0, -0.01f, -0.2f, 0.035f, 0.06f, 0.12f, dark);
            part(0, 0.095f, 0.16f, 0.052f, 0.012f, 0.1f, rc);
            break;
        case ItemType::HuntingRifle:
            part(0, 0.02f, 0.25f, 0.035f, 0.05f, 0.35f, wood);
            part(0, 0.04f, 0.72f, 0.016f, 0.016f, 0.2f, metal);
            part(0, 0.1f, 0.15f, 0.012f, 0.03f, 0.03f, metal);
            part(0, 0.0f, -0.2f, 0.035f, 0.065f, 0.14f, wood);
            part(0, 0.075f, 0.3f, 0.037f, 0.01f, 0.08f, rc);
            break;
        case ItemType::HandCannon:
            part(0, 0.04f, 0.1f, 0.035f, 0.055f, 0.16f, body);
            part(0, 0.06f, 0.28f, 0.03f, 0.03f, 0.05f, metal);
            part(0, 0.105f, 0.1f, 0.037f, 0.01f, 0.1f, rc);
            break;
        case ItemType::GrenadeLauncher:
            part(0, 0.03f, 0.2f, 0.06f, 0.06f, 0.3f, body);
            part(0, 0.03f, 0.08f, 0.085f, 0.085f, 0.08f, metal);          // drum
            part(0, 0.03f, 0.52f, 0.045f, 0.045f, 0.04f, dark);
            part(0, 0.0f, -0.18f, 0.035f, 0.06f, 0.12f, dark);
            part(0, 0.095f, 0.25f, 0.062f, 0.012f, 0.1f, rc);
            break;
        case ItemType::Crossbow:
            part(0, 0.02f, 0.2f, 0.035f, 0.045f, 0.3f, wood);
            part(0, 0.04f, 0.46f, 0.32f, 0.02f, 0.025f, dark);            // limbs
            part(0, 0.06f, 0.3f, 0.01f, 0.01f, 0.2f, Color{210, 210, 200, 255}); // bolt
            part(0, 0.0f, -0.16f, 0.035f, 0.06f, 0.12f, wood);
            part(0, 0.07f, 0.12f, 0.037f, 0.01f, 0.08f, rc);
            break;
        default: part(0, 0.02f, 0.2f, 0.05f, 0.08f, 0.3f, body); break;
    }
}

} // namespace

void drawHeldItem(const si::Vec3& hand, const Basis& b, uint8_t type, uint8_t rarity, const Loadout& l, float swing) {
    ItemType t = (ItemType)type;
    if (t == ItemType::None) {
        // Pickaxe
        const CosmeticStyle& ps = styleOf(l.pickaxe, CosmeticType::Pickaxe);
        Basis pb = compose(b, yawPitchBasis(0, 0.4f - swing * 2.2f));
        drawBox(hand + pb.u * 0.35f, {0.035f, 0.45f, 0.035f}, pb, col(ps.secondary));
        drawBox(hand + pb.u * 0.05f, {0.042f, 0.1f, 0.042f}, pb, darker(col(ps.secondary), 0.6f)); // wrap
        si::Vec3 head = hand + pb.u * 0.8f;
        switch (ps.shape) {
            case 1:
                drawBox(head + pb.f * 0.12f, {0.025f, 0.15f, 0.16f}, pb, col(ps.primary));
                drawBox(head + pb.f * 0.27f, {0.02f, 0.16f, 0.02f}, pb, col(ps.accent));
                break;
            case 2:
                drawBox(head, {0.12f, 0.12f, 0.2f}, pb, col(ps.primary));
                drawBox(head, {0.13f, 0.03f, 0.21f}, pb, col(ps.accent));
                drawBox(head + pb.f * 0.21f, {0.1f, 0.1f, 0.02f}, pb, darker(col(ps.primary), 0.7f));
                break;
            case 3:
                drawBox(head + pb.f * 0.2f - pb.u * 0.02f, {0.025f, 0.05f, 0.2f}, compose(pb, yawPitchBasis(0, -0.3f)), col(ps.primary));
                drawBox(head + pb.f * 0.42f - pb.u * 0.14f, {0.02f, 0.04f, 0.12f}, compose(pb, yawPitchBasis(0, -0.9f)), col(ps.primary));
                drawBox(head, {0.05f, 0.05f, 0.05f}, pb, col(ps.accent));
                break;
            default:
                drawBox(head + pb.f * 0.12f, {0.035f, 0.045f, 0.15f}, compose(pb, yawPitchBasis(0, -0.2f)), col(ps.primary));
                drawBox(head - pb.f * 0.12f, {0.035f, 0.045f, 0.15f}, compose(pb, yawPitchBasis(0, 0.2f)), col(ps.primary));
                drawBox(head, {0.05f, 0.06f, 0.05f}, pb, darker(col(ps.primary), 0.7f));
                break;
        }
        return;
    }
    const ItemDef& d = itemDef(t);
    Color body = C(d.color);
    Color rc = C(rarityColor((Rarity)rarity));
    ModelLibrary* lib = models();
    HeldModel hm = t == ItemType::Pistol ? HeldModel::Blaster : HeldModel::BlasterRepeater;
    if ((t == ItemType::Pistol || t == ItemType::CompactSMG) && lib && lib->hasHeld(hm)) {
        // CC0 blaster models; the file's barrel points along -Z, so flip forward/right.
        Basis mb = b;
        mb.f = b.f * -1.0f;
        mb.r = b.r * -1.0f;
        lib->drawHeld(hm, hand + b.u * -0.06f + b.f * 0.08f, mb, t == ItemType::Pistol ? 0.2f : 0.24f, shadowOverride());
        drawBox(hand + b.u * 0.12f + b.f * 0.1f, {0.04f, 0.01f, 0.08f}, b, rc); // rarity marker
    } else if (d.cls == ItemClass::Gun) {
        gunModel(hand, b, t, body, rc);
    } else if (d.cls == ItemClass::Consumable) {
        switch (t) {
            case ItemType::Bandages:
                drawBox(hand + b.u * 0.06f, {0.07f, 0.06f, 0.07f}, b, body);
                drawBox(hand + b.u * 0.06f, {0.072f, 0.015f, 0.072f}, b, Color{200, 60, 60, 255});
                break;
            case ItemType::Medkit:
                drawBox(hand + b.u * 0.1f, {0.14f, 0.1f, 0.06f}, b, Color{240, 240, 240, 255});
                drawBox(hand + b.u * 0.1f + b.f * 0.062f, {0.03f, 0.07f, 0.005f}, b, body);
                drawBox(hand + b.u * 0.1f + b.f * 0.062f, {0.07f, 0.03f, 0.005f}, b, body);
                break;
            default:
                drawBox(hand + b.u * 0.11f, {0.07f, 0.11f, 0.07f}, b, body);
                drawBox(hand + b.u * 0.25f, {0.035f, 0.035f, 0.035f}, b, Color{230, 230, 230, 255});
                drawBox(hand + b.u * 0.12f, {0.072f, 0.03f, 0.072f}, b, darker(body, 1.4f));
                break;
        }
    } else if (d.cls == ItemClass::Throwable || d.cls == ItemClass::Utility) {
        drawBox(hand + b.u * 0.06f, {0.07f, 0.08f, 0.07f}, b, body);
        drawBox(hand + b.u * 0.15f, {0.025f, 0.02f, 0.025f}, b, Color{60, 60, 60, 255});
        drawBox(hand + b.u * 0.15f + b.r * 0.04f, {0.03f, 0.01f, 0.01f}, b, Color{200, 200, 200, 255});
    }
}

void drawGlider(const si::Vec3& pos, float yaw, const Loadout& l) {
    const CosmeticStyle& gs = styleOf(l.glider, CosmeticType::Glider);
    Basis b = yawBasis(yaw);
    si::Vec3 top = pos + si::Vec3{0, 3.3f, 0};
    // Curved canopy made of 9 panels with alternating colors, plus a leading edge
    for (int i = -4; i <= 4; i++) {
        float x = i * 0.45f;
        float y = -(i * i) * 0.035f;
        float tilt = i * 0.12f;
        Color c = (i % 2 == 0) ? col(gs.primary) : col(gs.secondary);
        Basis pb = compose(b, Basis{{std::cos(tilt), -std::sin(tilt), 0}, {std::sin(tilt), std::cos(tilt), 0}, {0, 0, 1}});
        drawBox(top + b.r * x + b.u * y, {0.23f, 0.04f, 0.85f}, pb, c);
        drawBox(top + b.r * x + b.u * (y + 0.03f) + b.f * 0.85f, {0.23f, 0.05f, 0.05f}, pb, col(gs.accent));
    }
    // Lines + handle bar
    Color lc = col(gs.accent);
    for (int s = -1; s <= 1; s += 2) {
        for (int k = 0; k < 2; k++) {
            si::Vec3 a = top + b.r * ((1.2f + k * 0.6f) * s) - b.u * (0.1f + k * 0.15f) + b.f * (k ? -0.5f : 0.5f);
            si::Vec3 d = pos + si::Vec3{0, 2.2f, 0} + b.r * (0.35f * s);
            si::Vec3 mid = (a + d) * 0.5f;
            si::Vec3 dir = (a - d);
            float len = dir.len();
            Basis lb;
            lb.u = dir / len;
            lb.r = b.f.cross(lb.u).norm();
            lb.f = lb.u.cross(lb.r).norm();
            drawBox(mid, {0.012f, len * 0.5f, 0.012f}, lb, lc);
        }
    }
    drawBox(pos + si::Vec3{0, 2.2f, 0}, {0.4f, 0.025f, 0.025f}, b, darker(lc, 0.6f));
}

static bool drawSoldierCharacter(const CharPose& p, const Loadout& l, float time) {
    ModelLibrary* lib = models();
    if (!lib || !lib->hasSoldier()) return false;
    bool sky = p.mode == MoveMode::Skydive, glide = p.mode == MoveMode::Glide;
    bool aiming = p.heldType != 0 && itemDef((ItemType)p.heldType).cls == ItemClass::Gun && !sky && !glide;
    SoldierAnim anim = SoldierAnim::Idle;
    if (p.flags & PF_DBNO) anim = SoldierAnim::Crouch;
    else if (sky || glide) anim = SoldierAnim::Fall;
    else if (p.mode == MoveMode::Air) anim = SoldierAnim::Jump;
    else if (p.emote) anim = SoldierAnim::Cheer;
    else if (p.flags & PF_CROUCH) anim = SoldierAnim::Crouch;
    else if (p.speed > 7.0f) anim = SoldierAnim::Sprint;
    else if (p.speed > 0.6f) anim = SoldierAnim::Walk;
    else if (aiming) anim = (p.flags & PF_FIRING) ? SoldierAnim::HoldShoot : SoldierAnim::Hold;
    if (p.seatPose == SEAT_SIT) anim = aiming && (p.flags & PF_FIRING) ? SoldierAnim::HoldShoot : SoldierAnim::Crouch;
    else if (p.seatPose == SEAT_STAND) anim = aiming ? ((p.flags & PF_FIRING) ? SoldierAnim::HoldShoot : SoldierAnim::Hold) : SoldierAnim::Idle;
    float scale = 2.9f;
    si::Vec3 feet = p.pos;
    if (sky) feet = feet + si::Vec3{0, 0.4f, 0};
    if (p.seatPose == SEAT_SIT) {
        // The chunky model sits lower and a bit smaller so it fits the seats
        scale = 2.25f;
        Basis ob = p.tilted ? p.tilt : yawBasis(p.yaw);
        feet = feet + ob.u * 0.12f - ob.f * 0.12f;
    }
    lib->drawSoldier(feet, p.yaw, anim, p.animTime, scale, shadowOverride(), p.tilted ? &p.tilt : nullptr);
    // Held item: only in the holding poses, where the right hand is extended in front.
    bool holding = anim == SoldierAnim::Hold || anim == SoldierAnim::HoldShoot || (p.flags & PF_HARVESTING);
    if (holding && !p.building) {
        Basis body = yawBasis(p.yaw);
        si::Vec3 hand = feet + si::Vec3{0, 1.2f, 0} + body.r * 0.36f + body.f * 0.5f;
        Basis itemB = compose(yawBasis(p.yaw), yawPitchBasis(0, aiming ? p.pitch : -0.3f));
        setDrawMaterial(M_METAL);
        drawHeldItem(hand, itemB, p.heldType, p.heldRarity, l, p.swing);
    }
    if (glide) { setDrawMaterial(M_FABRIC); drawGlider(p.pos, p.yaw, l); }
    setDrawMaterial(M_PLAIN);
    (void)time;
    return true;
}

void drawCharacter(const CharPose& p, const Loadout& l, float time) {
    const CosmeticStyle& os = styleOf(l.outfit, CosmeticType::Outfit);
    if (os.shape == 10 && drawSoldierCharacter(p, l, time)) return;
    Color primary = col(os.primary), secondary = col(os.secondary), accent = col(os.accent), skin = col(os.skin), hair = col(os.hair);
    bool dbno = p.flags & PF_DBNO;
    bool crouch = p.flags & PF_CROUCH;
    bool sky = p.mode == MoveMode::Skydive;
    bool glide = p.mode == MoveMode::Glide;
    bool swim = p.mode == MoveMode::Swim;
    bool air = p.mode == MoveMode::Air;

    // Body orientation
    float bodyPitch = 0;
    if (sky) bodyPitch = -1.25f;
    else if (dbno) bodyPitch = -1.35f;
    else if (swim) bodyPitch = -0.9f;
    else if (crouch) bodyPitch = -0.25f;
    Basis body = compose(p.tilted ? p.tilt : yawBasis(p.yaw), yawPitchBasis(0, bodyPitch));
    si::Vec3 root = p.pos;
    if (sky) root = root + si::Vec3{0, 1.0f, 0};
    if (dbno) root = root + si::Vec3{0, 0.35f, 0};
    float hipH = crouch ? 0.62f : 0.9f;

    // Walk cycle / emotes
    float phase = p.animTime * (p.speed > 7.0f ? 11.0f : 9.0f);
    float moving = clampf(p.speed / 4.0f, 0.0f, 1.0f);
    float cycle = std::sin(phase) * moving;
    float legSwingL = cycle * 0.7f, legSwingR = -cycle * 0.7f;
    float kneeL = -std::max(0.0f, std::sin(phase + 1.2f)) * 1.1f * moving, kneeR = -std::max(0.0f, std::sin(phase + 1.2f + kPi)) * 1.1f * moving;
    float armSwingL = -cycle * 0.5f, armSwingR = cycle * 0.5f;
    float elbowL = 0.25f + moving * 0.35f, elbowR = 0.25f + moving * 0.35f;
    float bob = std::fabs(std::sin(phase)) * 0.05f * moving;
    if (crouch) { legSwingL = legSwingR = 0.9f; kneeL = kneeR = -1.6f; }
    if (air && !crouch) { legSwingL = 0.5f; legSwingR = -0.1f; kneeL = -0.9f; kneeR = -0.4f; }
    float armAimR = 0, armAimL = 0;
    bool aiming = p.heldType != 0 && itemDef((ItemType)p.heldType).cls == ItemClass::Gun && !sky && !glide && !dbno;
    if (aiming) { armAimR = 1.45f + p.pitch; armAimL = 1.25f + p.pitch; elbowR = 0.1f; elbowL = 0.45f; }
    bool harvesting = p.flags & PF_HARVESTING;
    if (harvesting) { armAimR = 1.2f + std::sin(time * 11.0f) * 0.9f; elbowR = 0.3f; }
    float torsoTwist = aiming ? 0.15f : 0.0f;
    if (p.emote && !sky && !glide) {
        float t = p.animTime;
        elbowL = elbowR = 0.2f;
        kneeL = kneeR = 0;
        switch (p.emote) {
            case 1: // groove
                armSwingL = std::sin(t * 6) * 1.2f; armSwingR = -std::sin(t * 6) * 1.2f; bob = std::fabs(std::sin(t * 6)) * 0.15f;
                torsoTwist = std::sin(t * 3) * 0.4f; legSwingL = std::sin(t * 6) * 0.3f; legSwingR = -legSwingL;
                kneeL = kneeR = -std::fabs(std::sin(t * 6)) * 0.5f; break;
            case 2: armSwingR = 2.8f + std::sin(t * 8) * 0.3f; elbowR = 0.4f + std::sin(t * 8) * 0.3f; break; // wave
            case 3: torsoTwist = t * 8.0f; armSwingL = armSwingR = 1.5f; break; // spin
            case 4: { float j = std::fabs(std::sin(t * 5)); bob = j * 0.5f; armSwingL = armSwingR = 0.5f + j * 2.4f; legSwingL = j * 0.4f; legSwingR = -j * 0.4f; break; }
            case 5: armSwingL = std::sin(t * 7) > 0 ? 1.6f : 0.2f; armSwingR = std::sin(t * 7) > 0 ? 0.2f : 1.6f; elbowL = elbowR = 1.3f;
                    torsoTwist = std::sin(t * 7) > 0 ? 0.3f : -0.3f; break;
            case 6: armSwingL = armSwingR = 1.57f; elbowL = elbowR = 1.7f; bob = 0.05f; break; // flex
            default: break;
        }
        aiming = false;
        armAimL = armAimR = 0;
    }
    if (sky) { armSwingL = 2.4f; armSwingR = 2.4f; elbowL = elbowR = 0.6f; legSwingL = 0.3f; legSwingR = -0.3f; kneeL = kneeR = -0.4f; }
    if (glide) { armSwingL = armSwingR = 3.0f; elbowL = elbowR = 0.1f; legSwingL = legSwingR = 0.1f; kneeL = kneeR = -0.3f; }
    if (dbno) { armSwingL = 2.2f + cycle * 0.5f; armSwingR = 2.2f - cycle * 0.5f; kneeL = kneeR = -0.5f; }
    if (p.seatPose == SEAT_SIT) {
        // Thighs forward along the seat, shins hanging down
        hipH = 0.55f;
        legSwingL = legSwingR = 1.5f;
        kneeL = kneeR = -1.45f;
        bob = 0;
        if (!aiming && !p.emote) { armSwingL = armSwingR = 0.25f; elbowL = elbowR = 0.35f; }
    } else if (p.seatPose == SEAT_STAND) {
        // Surfer stance on a board
        hipH = 0.82f;
        legSwingL = 0.35f; legSwingR = -0.3f;
        kneeL = kneeR = -0.35f;
        bob = 0;
        torsoTwist += 0.35f;
        if (!aiming) { armSwingL = 0.6f; armSwingR = -0.4f; elbowL = elbowR = 0.4f; }
    }
    if (p.steering && !aiming && !p.emote) { armSwingL = armSwingR = 1.05f; elbowL = elbowR = 0.75f; }

    setDrawMaterial(M_FABRIC);
    Basis torsoB = compose(body, yawBasis(torsoTwist));
    si::Vec3 hips = root + body.u * (hipH + bob);

    // Legs
    LimbStyle leg{0.46f, 0.44f, 0.11f, 0.09f, secondary, secondary, darker(secondary, 0.85f), darker(secondary, 0.45f), true};
    if (crouch) { leg.upperLen = 0.36f; leg.lowerLen = 0.36f; }
    limb2(hips + body.r * -0.14f, body, legSwingL, kneeL, leg);
    limb2(hips + body.r * 0.14f, body, legSwingR, kneeR, leg);
    // Hips / belt
    drawBox(hips + torsoB.u * 0.04f, {0.28f, 0.1f, 0.17f}, torsoB, darker(secondary, 0.9f));
    drawBox(hips + torsoB.u * 0.12f, {0.285f, 0.035f, 0.175f}, torsoB, darker(secondary, 0.55f));
    drawBox(hips + torsoB.u * 0.12f + torsoB.f * 0.176f, {0.045f, 0.03f, 0.01f}, torsoB, Color{210, 190, 120, 255}); // buckle
    // Torso: abdomen + chest
    si::Vec3 abdomen = hips + torsoB.u * 0.26f;
    drawBox(abdomen, {0.25f, 0.12f, 0.15f}, torsoB, primary);
    si::Vec3 chest = hips + torsoB.u * 0.5f;
    drawBox(chest, {0.28f, 0.16f, 0.17f}, torsoB, primary);
    if (os.pattern == 1) drawBox(chest - torsoB.u * 0.02f, {0.285f, 0.05f, 0.175f}, torsoB, accent);
    if (os.pattern == 2) {
        drawBox(chest + torsoB.r * 0.14f, {0.142f, 0.162f, 0.172f}, torsoB, secondary);
        drawBox(abdomen + torsoB.r * 0.125f, {0.127f, 0.122f, 0.152f}, torsoB, secondary);
    }
    // Chest details: zipper line and a pocket
    drawBox(abdomen + torsoB.f * 0.152f, {0.012f, 0.12f, 0.004f}, torsoB, darker(primary, 0.6f));
    drawBox(chest + torsoB.f * 0.172f + torsoB.r * -0.12f + torsoB.u * 0.04f, {0.06f, 0.05f, 0.006f}, torsoB, darker(primary, 0.75f));
    // Collar + neck
    drawBox(chest + torsoB.u * 0.17f, {0.16f, 0.03f, 0.12f}, torsoB, darker(primary, 0.8f));
    drawBox(chest + torsoB.u * 0.22f, {0.07f, 0.05f, 0.07f}, torsoB, skin);
    // Shoulder pads for armored/visor outfits
    if (os.shape == 1 || os.shape == 4) {
        for (int s = -1; s <= 1; s += 2)
            drawBox(chest + torsoB.u * 0.14f + torsoB.r * (0.3f * s), {0.11f, 0.05f, 0.13f}, torsoB, accent);
    }
    // Arms
    si::Vec3 shL = chest + torsoB.u * 0.1f + torsoB.r * -0.36f, shR = chest + torsoB.u * 0.1f + torsoB.r * 0.36f;
    float aL = aiming ? armAimL : armSwingL, aR = (aiming || harvesting) ? armAimR : armSwingR;
    Color glove = os.shape == 1 || os.shape == 4 ? darker(accent, 0.8f) : skin;
    LimbStyle arm{0.33f, 0.31f, 0.085f, 0.075f, primary, os.pattern == 1 ? accent : primary, darker(primary, 0.85f), glove, false};
    limb2(shL, torsoB, aL, elbowL, arm);
    Basis handB;
    si::Vec3 handR = limb2(shR, torsoB, aR, elbowR, arm, &handB);

    // Head
    setDrawMaterial(M_PLAIN);
    si::Vec3 head = chest + torsoB.u * 0.47f;
    Basis headB = compose(torsoB, yawPitchBasis(0, sky || dbno ? 1.0f : clampf(p.pitch * 0.5f, -0.4f, 0.4f)));
    drawBox(head, {0.2f, 0.22f, 0.2f}, headB, skin);
    // Ears, nose, eyes, brows, mouth
    drawBox(head + headB.r * 0.205f, {0.02f, 0.05f, 0.04f}, headB, darker(skin, 0.9f));
    drawBox(head - headB.r * 0.205f, {0.02f, 0.05f, 0.04f}, headB, darker(skin, 0.9f));
    drawBox(head + headB.f * 0.21f - headB.u * 0.02f, {0.025f, 0.04f, 0.02f}, headB, darker(skin, 0.92f));
    for (int s = -1; s <= 1; s += 2) {
        drawBox(head + headB.f * 0.201f + headB.u * 0.04f + headB.r * (0.08f * s), {0.035f, 0.03f, 0.005f}, headB, Color{245, 245, 245, 255});
        drawBox(head + headB.f * 0.205f + headB.u * 0.04f + headB.r * (0.075f * s), {0.018f, 0.022f, 0.005f}, headB, Color{35, 30, 30, 255});
        drawBox(head + headB.f * 0.203f + headB.u * 0.1f + headB.r * (0.08f * s), {0.045f, 0.012f, 0.006f}, headB, darker(hair, 0.8f));
    }
    drawBox(head + headB.f * 0.202f - headB.u * 0.1f, {0.05f, 0.012f, 0.005f}, headB, Color{120, 60, 55, 255});
    switch (os.shape) {
        case 1: // helmet with visor slit and crest
            drawBox(head + headB.u * 0.05f, {0.235f, 0.22f, 0.235f}, headB, primary);
            drawBox(head + headB.f * 0.23f + headB.u * 0.04f, {0.19f, 0.05f, 0.012f}, headB, accent);
            drawBox(head + headB.u * 0.28f, {0.03f, 0.04f, 0.2f}, headB, accent);
            drawBox(head - headB.u * 0.12f + headB.f * 0.2f, {0.16f, 0.06f, 0.04f}, headB, darker(primary, 0.8f));
            break;
        case 2: // hood
            drawBox(head + headB.u * 0.06f - headB.f * 0.03f, {0.235f, 0.23f, 0.215f}, headB, darker(primary, 0.85f));
            drawBox(head + headB.f * 0.19f, {0.17f, 0.17f, 0.03f}, headB, skin);
            drawBox(head + headB.f * 0.21f + headB.u * 0.04f, {0.12f, 0.03f, 0.005f}, headB, Color{35, 30, 30, 255});
            drawBox(head - headB.f * 0.25f - headB.u * 0.15f, {0.14f, 0.1f, 0.04f}, headB, darker(primary, 0.75f));
            break;
        case 3: // cap with brim and hair at the back
            drawBox(head + headB.u * 0.2f, {0.215f, 0.07f, 0.215f}, headB, accent);
            drawBox(head + headB.u * 0.16f + headB.f * 0.25f, {0.19f, 0.018f, 0.1f}, headB, darker(accent, 0.8f));
            drawBox(head - headB.f * 0.19f - headB.u * 0.02f, {0.2f, 0.12f, 0.03f}, headB, hair);
            break;
        case 4: // visor + headset
            drawBox(head + headB.u * 0.18f, {0.215f, 0.07f, 0.215f}, headB, hair);
            drawBox(head + headB.f * 0.21f + headB.u * 0.04f, {0.2f, 0.055f, 0.03f}, headB, accent);
            drawBox(head + headB.r * 0.22f, {0.03f, 0.08f, 0.08f}, headB, darker(primary, 0.7f));
            drawBox(head - headB.r * 0.22f, {0.03f, 0.08f, 0.08f}, headB, darker(primary, 0.7f));
            break;
        default: // hair with a fringe
            drawBox(head + headB.u * 0.19f - headB.f * 0.02f, {0.215f, 0.07f, 0.215f}, headB, hair);
            drawBox(head - headB.f * 0.18f + headB.u * 0.03f, {0.215f, 0.17f, 0.04f}, headB, hair);
            drawBox(head + headB.f * 0.18f + headB.u * 0.15f + headB.r * 0.06f, {0.12f, 0.04f, 0.04f}, headB, hair);
            drawBox(head + headB.r * 0.2f + headB.u * 0.1f, {0.02f, 0.1f, 0.12f}, headB, hair);
            drawBox(head - headB.r * 0.2f + headB.u * 0.1f, {0.02f, 0.1f, 0.12f}, headB, hair);
            break;
    }
    // Back bling
    setDrawMaterial(M_FABRIC);
    const CosmeticStyle& bs = styleOf(l.backbling, CosmeticType::BackBling);
    si::Vec3 back = (chest + abdomen) * 0.5f - torsoB.f * 0.18f;
    switch (bs.shape) {
        case 1:
            drawBox(back - torsoB.f * 0.1f, {0.22f, 0.26f, 0.1f}, torsoB, col(bs.primary));
            drawBox(back - torsoB.f * 0.21f - torsoB.u * 0.08f, {0.16f, 0.1f, 0.02f}, torsoB, col(bs.accent));
            drawBox(back - torsoB.f * 0.1f + torsoB.u * 0.27f, {0.18f, 0.03f, 0.08f}, torsoB, col(bs.secondary));
            for (int s = -1; s <= 1; s += 2) drawBox(back + torsoB.r * (0.15f * s) + torsoB.f * 0.1f + torsoB.u * 0.12f, {0.03f, 0.18f, 0.01f}, torsoB, col(bs.secondary));
            break;
        case 2: {
            float flap = std::sin(time * 4.0f + p.pos.x) * 0.1f + std::min(0.6f, p.speed * 0.05f);
            Basis cb = compose(torsoB, yawPitchBasis(0, -flap));
            si::Vec3 top = chest + torsoB.u * 0.14f - torsoB.f * 0.19f;
            drawBox(top - cb.u * 0.4f, {0.3f, 0.4f, 0.015f}, cb, col(bs.primary));
            drawBox(top - cb.u * 0.82f, {0.3f, 0.03f, 0.016f}, cb, col(bs.accent));
            drawBox(top, {0.2f, 0.03f, 0.03f}, torsoB, col(bs.accent));
            break;
        }
        case 3:
            for (int s = -1; s <= 1; s += 2) {
                Basis wb = compose(torsoB, yawBasis(s * 0.5f));
                drawBox(back + torsoB.r * (0.28f * s) + torsoB.u * 0.18f - torsoB.f * 0.05f, {0.3f, 0.3f, 0.015f}, wb, col(bs.primary));
                drawBox(back + torsoB.r * (0.45f * s) + torsoB.u * -0.12f - torsoB.f * 0.1f, {0.18f, 0.16f, 0.015f}, wb, col(bs.secondary));
            }
            break;
        case 4:
            drawBox(back - torsoB.f * 0.1f, {0.12f, 0.3f, 0.12f}, torsoB, col(bs.primary));
            drawBox(back - torsoB.f * 0.1f, {0.125f, 0.08f, 0.125f}, torsoB, col(bs.secondary));
            drawBox(back - torsoB.f * 0.1f + torsoB.u * 0.33f, {0.05f, 0.04f, 0.05f}, torsoB, col(bs.accent));
            break;
        case 5:
            drawBox(back - torsoB.f * 0.06f, {0.3f, 0.3f, 0.04f}, torsoB, col(bs.primary));
            drawBox(back - torsoB.f * 0.1f, {0.12f, 0.12f, 0.02f}, torsoB, col(bs.accent));
            drawBox(back - torsoB.f * 0.1f, {0.31f, 0.03f, 0.045f}, torsoB, col(bs.secondary));
            drawBox(back - torsoB.f * 0.1f, {0.03f, 0.31f, 0.045f}, torsoB, col(bs.secondary));
            break;
        default: break;
    }
    // Held item in right hand
    if (!sky && !glide && !swim && !dbno && !p.building && !p.emote) {
        setDrawMaterial(M_METAL);
        si::Vec3 hand = handR - handB.u * 0.05f;
        Basis itemB = compose(yawBasis(p.yaw), yawPitchBasis(0, aiming ? p.pitch : -0.3f));
        drawHeldItem(hand, itemB, p.heldType, p.heldRarity, l, p.swing);
    }
    if (glide) { setDrawMaterial(M_FABRIC); drawGlider(p.pos, p.yaw, l); }
    setDrawMaterial(M_PLAIN);
}

} // namespace client
