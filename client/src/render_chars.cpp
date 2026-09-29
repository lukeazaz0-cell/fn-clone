// Procedural low-poly characters. Every outfit is an original design defined by
// the colors/shape knobs in shared/game/cosmetics.cpp.
#include <cmath>

#include "render.h"
#include "rlgl.h"
#include "shared/game/items.h"

namespace client {

namespace {

Color col(const Color4& c) { return {c.r, c.g, c.b, 255}; }
Color darker(Color c, float f) { return {(unsigned char)(c.r * f), (unsigned char)(c.g * f), (unsigned char)(c.b * f), c.a}; }

const CosmeticStyle& styleOf(const std::string& id, CosmeticType t) {
    const CosmeticDef* d = findCosmetic(id);
    if (!d || d->type != t) d = findCosmetic(defaultCosmetic(t));
    return d->style;
}

// A limb hanging from a pivot, rotated by `swing` around the character's right axis.
void limb(const si::Vec3& pivot, const Basis& body, float swing, float len, float thick, Color c, Color endColor) {
    Basis rot = compose(body, yawPitchBasis(0, swing));
    // After pitching, "down" along the limb is -u in the rotated basis.
    si::Vec3 center = pivot - rot.u * (len * 0.5f);
    drawBox(center, {thick, len * 0.5f, thick}, rot, c);
    drawBox(pivot - rot.u * (len - 0.06f), {thick * 1.05f, 0.07f, thick * 1.05f}, rot, endColor);
}

} // namespace

void drawHeldItem(const si::Vec3& hand, const Basis& b, uint8_t type, uint8_t rarity, const Loadout& l, float swing) {
    ItemType t = (ItemType)type;
    if (t == ItemType::None) {
        // Pickaxe
        const CosmeticStyle& ps = styleOf(l.pickaxe, CosmeticType::Pickaxe);
        Basis pb = compose(b, yawPitchBasis(0, 0.4f - swing * 2.2f));
        si::Vec3 handleC = hand + pb.u * 0.35f;
        drawBox(handleC, {0.04f, 0.45f, 0.04f}, pb, col(ps.secondary));
        si::Vec3 head = hand + pb.u * 0.8f;
        switch (ps.shape) {
            case 1: drawBox(head + pb.f * 0.12f, {0.03f, 0.14f, 0.16f}, pb, col(ps.primary)); break;
            case 2: drawBox(head, {0.12f, 0.12f, 0.2f}, pb, col(ps.primary)); drawBox(head, {0.13f, 0.03f, 0.21f}, pb, col(ps.accent)); break;
            case 3: drawBox(head + pb.f * 0.25f - pb.u * 0.1f, {0.03f, 0.06f, 0.3f}, compose(pb, yawPitchBasis(0, -0.5f)), col(ps.primary)); break;
            default: drawBox(head, {0.04f, 0.05f, 0.32f}, compose(pb, yawPitchBasis(0, 0.15f)), col(ps.primary)); break;
        }
        return;
    }
    const ItemDef& d = itemDef(t);
    Color body = C(d.color);
    Color rc = C(rarityColor((Rarity)rarity));
    if (d.cls == ItemClass::Gun) {
        float len = 0.6f, h = 0.09f;
        switch (t) {
            case ItemType::SniperRifle: len = 1.1f; break;
            case ItemType::ScopedRifle: len = 0.85f; break;
            case ItemType::RocketLauncher: len = 1.0f; h = 0.14f; break;
            case ItemType::Pistol: len = 0.28f; break;
            case ItemType::SMG: len = 0.45f; break;
            case ItemType::PumpShotgun:
            case ItemType::TacticalShotgun: len = 0.75f; break;
            case ItemType::Minigun: len = 0.9f; h = 0.16f; break;
            default: break;
        }
        si::Vec3 c = hand + b.f * (len * 0.4f);
        drawBox(c, {0.05f, h, len * 0.5f}, b, body);
        drawBox(c + b.u * (h + 0.02f), {0.052f, 0.02f, len * 0.3f}, b, rc); // rarity stripe
        drawBox(hand - b.u * 0.1f, {0.04f, 0.1f, 0.05f}, b, darker(body, 0.8f)); // grip
        if (t == ItemType::SniperRifle || t == ItemType::ScopedRifle) drawBox(c + b.u * (h + 0.07f), {0.035f, 0.035f, 0.15f}, b, Color{30, 30, 30, 255});
        if (t == ItemType::RocketLauncher) drawBox(c + b.f * (len * 0.5f), {0.1f, 0.1f, 0.04f}, b, Color{200, 60, 40, 255});
    } else if (d.cls == ItemClass::Consumable) {
        drawBox(hand + b.u * 0.1f, {0.08f, 0.12f, 0.08f}, b, body);
        drawBox(hand + b.u * 0.24f, {0.04f, 0.03f, 0.04f}, b, Color{230, 230, 230, 255});
    } else if (d.cls == ItemClass::Throwable || d.cls == ItemClass::Utility) {
        drawBox(hand + b.u * 0.05f, {0.09f, 0.09f, 0.09f}, b, body);
    }
}

void drawGlider(const si::Vec3& pos, float yaw, const Loadout& l) {
    const CosmeticStyle& gs = styleOf(l.glider, CosmeticType::Glider);
    Basis b = yawBasis(yaw);
    si::Vec3 top = pos + si::Vec3{0, 3.3f, 0};
    // Canopy made of panels with alternating colors
    for (int i = -2; i <= 2; i++) {
        float x = i * 0.9f;
        float y = -std::fabs((float)i) * 0.18f;
        Color c = (i % 2 == 0) ? col(gs.primary) : col(gs.secondary);
        drawBox(top + b.r * x + b.u * y, {0.45f, 0.06f, 0.9f}, compose(b, yawPitchBasis(0, 0)), c);
    }
    // Lines
    Color lc = col(gs.accent);
    for (int s = -1; s <= 1; s += 2) {
        si::Vec3 a = top + b.r * (1.8f * s) - b.u * 0.3f;
        si::Vec3 d = pos + si::Vec3{0, 1.6f, 0} + b.r * (0.3f * s);
        si::Vec3 mid = (a + d) * 0.5f;
        si::Vec3 dir = (a - d);
        float len = dir.len();
        Basis lb;
        lb.u = dir / len;
        lb.r = b.f.cross(lb.u).norm();
        lb.f = lb.u.cross(lb.r).norm();
        drawBox(mid, {0.015f, len * 0.5f, 0.015f}, lb, lc);
    }
}

void drawCharacter(const CharPose& p, const Loadout& l, float time) {
    const CosmeticStyle& os = styleOf(l.outfit, CosmeticType::Outfit);
    Color primary = col(os.primary), secondary = col(os.secondary), accent = col(os.accent), skin = col(os.skin), hair = col(os.hair);
    bool dbno = p.flags & PF_DBNO;
    bool crouch = p.flags & PF_CROUCH;
    bool sky = p.mode == MoveMode::Skydive;
    bool glide = p.mode == MoveMode::Glide;
    bool swim = p.mode == MoveMode::Swim;

    // Body orientation
    float bodyPitch = 0;
    if (sky) bodyPitch = -1.25f;
    else if (dbno) bodyPitch = -1.35f;
    else if (swim) bodyPitch = -0.9f;
    Basis body = compose(yawBasis(p.yaw), yawPitchBasis(0, bodyPitch));
    si::Vec3 root = p.pos;
    if (sky) root = root + si::Vec3{0, 1.0f, 0};
    if (dbno) root = root + si::Vec3{0, 0.35f, 0};
    float hipH = crouch ? 0.65f : 0.9f;

    // Walk cycle / emotes
    float cycle = p.speed > 0.5f ? std::sin(p.animTime * 9.0f) : 0.0f;
    float legSwingL = cycle * 0.7f, legSwingR = -cycle * 0.7f;
    float armSwingL = -cycle * 0.5f, armSwingR = cycle * 0.5f;
    float bob = std::fabs(cycle) * 0.05f;
    float armAimR = 0, armAimL = 0;
    bool aiming = p.heldType != 0 && itemDef((ItemType)p.heldType).cls == ItemClass::Gun && !sky && !glide && !dbno;
    if (aiming) { armAimR = 1.45f + p.pitch; armAimL = 1.35f + p.pitch; }
    if (p.flags & PF_HARVESTING) armAimR = 1.2f + std::sin(time * 11.0f) * 0.9f;
    float torsoTwist = 0;
    if (p.emote && !sky && !glide) {
        float t = p.animTime;
        switch (p.emote) {
            case 1: // groove
                armSwingL = std::sin(t * 6) * 1.2f; armSwingR = -std::sin(t * 6) * 1.2f; bob = std::fabs(std::sin(t * 6)) * 0.15f;
                torsoTwist = std::sin(t * 3) * 0.4f; legSwingL = std::sin(t * 6) * 0.3f; legSwingR = -legSwingL; break;
            case 2: armSwingR = 2.8f + std::sin(t * 8) * 0.3f; break; // wave
            case 3: torsoTwist = t * 8.0f; armSwingL = armSwingR = 1.5f; break; // spin
            case 4: { float j = std::fabs(std::sin(t * 5)); bob = j * 0.5f; armSwingL = armSwingR = 0.5f + j * 2.4f; legSwingL = j * 0.4f; legSwingR = -j * 0.4f; break; }
            case 5: armSwingL = std::sin(t * 7) > 0 ? 1.6f : 0.2f; armSwingR = std::sin(t * 7) > 0 ? 0.2f : 1.6f; torsoTwist = std::sin(t * 7) > 0 ? 0.3f : -0.3f; break;
            case 6: armSwingL = armSwingR = 1.57f; bob = 0.05f; break; // flex
            default: break;
        }
        aiming = false;
        armAimL = armAimR = 0;
    }
    if (sky) { armSwingL = 2.6f; armSwingR = 2.6f; legSwingL = 0.3f; legSwingR = -0.3f; }
    if (glide) { armSwingL = armSwingR = 3.0f; legSwingL = legSwingR = 0.1f; }
    if (dbno) { armSwingL = 2.2f + cycle * 0.5f; armSwingR = 2.2f - cycle * 0.5f; }

    Basis torsoB = compose(body, yawBasis(torsoTwist));
    si::Vec3 hips = root + body.u * (hipH + bob);
    // Legs
    si::Vec3 legL = hips + body.r * -0.14f, legR = hips + body.r * 0.14f;
    limb(legL, body, legSwingL, hipH, 0.11f, secondary, darker(secondary, 0.6f));
    limb(legR, body, legSwingR, hipH, 0.11f, secondary, darker(secondary, 0.6f));
    // Torso
    si::Vec3 chest = hips + torsoB.u * 0.35f;
    drawBox(chest, {0.27f, 0.36f, 0.16f}, torsoB, primary);
    if (os.pattern == 1) drawBox(chest + torsoB.u * 0.05f, {0.275f, 0.07f, 0.165f}, torsoB, accent);
    if (os.pattern == 2) drawBox(chest + torsoB.r * 0.135f, {0.14f, 0.365f, 0.165f}, torsoB, secondary);
    drawBox(hips + torsoB.u * 0.02f, {0.28f, 0.05f, 0.17f}, torsoB, darker(secondary, 0.7f)); // belt
    // Arms
    si::Vec3 shL = chest + torsoB.u * 0.28f + torsoB.r * -0.36f, shR = chest + torsoB.u * 0.28f + torsoB.r * 0.36f;
    float aL = aiming ? armAimL : armSwingL, aR = aiming || (p.flags & PF_HARVESTING) ? armAimR : armSwingR;
    limb(shL, torsoB, aL, 0.68f, 0.085f, primary, skin);
    limb(shR, torsoB, aR, 0.68f, 0.085f, primary, skin);
    // Head
    si::Vec3 head = chest + torsoB.u * 0.62f;
    Basis headB = compose(torsoB, yawPitchBasis(0, sky || dbno ? 1.0f : clampf(p.pitch * 0.5f, -0.4f, 0.4f)));
    drawBox(head, {0.2f, 0.22f, 0.2f}, headB, skin);
    // Face (eyes)
    drawBox(head + headB.f * 0.2f + headB.u * 0.04f + headB.r * 0.08f, {0.03f, 0.03f, 0.01f}, headB, Color{25, 25, 30, 255});
    drawBox(head + headB.f * 0.2f + headB.u * 0.04f - headB.r * 0.08f, {0.03f, 0.03f, 0.01f}, headB, Color{25, 25, 30, 255});
    switch (os.shape) {
        case 1: // helmet
            drawBox(head + headB.u * 0.04f, {0.23f, 0.23f, 0.23f}, headB, primary);
            drawBox(head + headB.f * 0.2f + headB.u * 0.02f, {0.2f, 0.07f, 0.04f}, headB, accent);
            break;
        case 2: // hood
            drawBox(head + headB.u * 0.06f - headB.f * 0.03f, {0.23f, 0.22f, 0.21f}, headB, darker(primary, 0.85f));
            drawBox(head + headB.f * 0.19f, {0.17f, 0.17f, 0.03f}, headB, skin);
            break;
        case 3: // cap
            drawBox(head + headB.u * 0.2f, {0.21f, 0.06f, 0.21f}, headB, accent);
            drawBox(head + headB.u * 0.17f + headB.f * 0.22f, {0.19f, 0.02f, 0.1f}, headB, accent);
            break;
        case 4: // visor
            drawBox(head + headB.u * 0.18f, {0.21f, 0.06f, 0.21f}, headB, hair);
            drawBox(head + headB.f * 0.19f + headB.u * 0.04f, {0.19f, 0.05f, 0.03f}, headB, accent);
            break;
        default: // hair
            drawBox(head + headB.u * 0.18f - headB.f * 0.02f, {0.21f, 0.07f, 0.21f}, headB, hair);
            drawBox(head - headB.f * 0.17f + headB.u * 0.02f, {0.21f, 0.16f, 0.04f}, headB, hair);
            break;
    }
    // Back bling
    const CosmeticStyle& bs = styleOf(l.backbling, CosmeticType::BackBling);
    si::Vec3 back = chest - torsoB.f * 0.2f;
    switch (bs.shape) {
        case 1: drawBox(back - torsoB.f * 0.1f, {0.22f, 0.26f, 0.1f}, torsoB, col(bs.primary));
                drawBox(back - torsoB.f * 0.21f - torsoB.u * 0.05f, {0.16f, 0.1f, 0.02f}, torsoB, col(bs.accent)); break;
        case 2: {
            float flap = std::sin(time * 4.0f + p.pos.x) * 0.1f + std::min(0.6f, p.speed * 0.05f);
            Basis cb = compose(torsoB, yawPitchBasis(0, -flap));
            drawBox(back + torsoB.u * 0.3f - cb.u * 0.55f - torsoB.f * 0.03f, {0.3f, 0.55f, 0.02f}, cb, col(bs.primary));
            break;
        }
        case 3:
            for (int s = -1; s <= 1; s += 2) {
                Basis wb = compose(torsoB, yawBasis(s * 0.5f));
                drawBox(back + torsoB.r * (0.28f * s) + torsoB.u * 0.12f - torsoB.f * 0.05f, {0.3f, 0.34f, 0.02f}, wb, col(bs.primary));
            }
            break;
        case 4: drawBox(back - torsoB.f * 0.1f, {0.12f, 0.3f, 0.12f}, torsoB, col(bs.primary));
                drawBox(back - torsoB.f * 0.1f, {0.125f, 0.08f, 0.125f}, torsoB, col(bs.secondary)); break;
        case 5: drawBox(back - torsoB.f * 0.06f, {0.3f, 0.3f, 0.04f}, torsoB, col(bs.primary));
                drawBox(back - torsoB.f * 0.1f, {0.12f, 0.12f, 0.02f}, torsoB, col(bs.accent)); break;
        default: break;
    }
    // Held item in right hand
    if (!sky && !glide && !swim && !dbno && !p.building && !p.emote) {
        Basis armB = compose(torsoB, yawPitchBasis(0, aR));
        si::Vec3 hand = shR - armB.u * 0.66f;
        Basis itemB = compose(yawBasis(p.yaw), yawPitchBasis(0, aiming ? p.pitch : -0.3f));
        drawHeldItem(hand, itemB, p.heldType, p.heldRarity, l, p.swing);
    }
    if (glide) drawGlider(p.pos, p.yaw, l);
}

} // namespace client
