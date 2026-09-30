// Vector icons and item pictograms drawn from raylib 2D primitives, so the UI needs no
// image assets. All shapes are laid out in a unit box centred on (cx, cy).
#include <cmath>
#include <initializer_list>

#include "shared/common/math.h"
#include "shared/game/defs.h"
#include "ui.h"

namespace ui {

namespace {

struct Pen {
    float cx, cy, s;
    Color c;
    Vector2 P(float x, float y) const { return {cx + x * s, cy + y * s}; }
    void rect(float x, float y, float w, float h, Color col) const { DrawRectangleV(P(x, y), {w * s, h * s}, col); }
    void rect(float x, float y, float w, float h) const { rect(x, y, w, h, c); }
    void rrect(float x, float y, float w, float h, float round, Color col) const {
        DrawRectangleRounded({cx + x * s, cy + y * s, w * s, h * s}, round, 6, col);
    }
    void rrect(float x, float y, float w, float h, float round) const { rrect(x, y, w, h, round, c); }
    void rot(float x, float y, float w, float h, float deg, Color col) const {
        DrawRectanglePro({cx + x * s, cy + y * s, w * s, h * s}, {w * s / 2, h * s / 2}, deg, col);
    }
    void rot(float x, float y, float w, float h, float deg) const { rot(x, y, w, h, deg, c); }
    void circle(float x, float y, float r, Color col) const { DrawCircleV(P(x, y), r * s, col); }
    void circle(float x, float y, float r) const { circle(x, y, r, c); }
    void ringArc(float x, float y, float r0, float r1, float a0, float a1, Color col) const { DrawRing(P(x, y), r0 * s, r1 * s, a0, a1, 32, col); }
    void ringArc(float x, float y, float r0, float r1, float a0, float a1) const { ringArc(x, y, r0, r1, a0, a1, c); }
    void line(float x0, float y0, float x1, float y1, float w, Color col) const { DrawLineEx(P(x0, y0), P(x1, y1), std::max(1.0f, w * s), col); }
    void line(float x0, float y0, float x1, float y1, float w) const { line(x0, y0, x1, y1, w, c); }
    // Triangle in either winding (raylib culls one of them).
    void tri(Vector2 a, Vector2 b, Vector2 d, Color col) const {
        Vector2 A = P(a.x, a.y), B = P(b.x, b.y), D = P(d.x, d.y);
        float cr = (B.x - A.x) * (D.y - A.y) - (B.y - A.y) * (D.x - A.x);
        if (cr > 0) DrawTriangle(A, D, B, col);
        else DrawTriangle(A, B, D, col);
    }
    void tri(Vector2 a, Vector2 b, Vector2 d) const { tri(a, b, d, c); }
    void poly(std::initializer_list<Vector2> pts, Color col) const {
        const Vector2* p = pts.begin();
        size_t n = pts.size();
        for (size_t i = 1; i + 1 < n; i++) tri(p[0], p[i], p[i + 1], col);
    }
    void poly(std::initializer_list<Vector2> pts) const { poly(pts, c); }
};

Color shade(Color c, float f) {
    auto ch = [&](unsigned char v) { return (unsigned char)std::fmin(255.0f, std::fmax(0.0f, v * f)); };
    return {ch(c.r), ch(c.g), ch(c.b), c.a};
}

} // namespace

void icon(Icon i, float cx, float cy, float size, Color c) {
    Pen p{cx, cy, size, c};
    Color dark = shade(c, 0.55f);
    switch (i) {
        case Icon::Shield:
            p.poly({{-0.4f, -0.42f}, {0.4f, -0.42f}, {0.4f, 0.02f}, {0.0f, 0.46f}, {-0.4f, 0.02f}});
            p.poly({{0.0f, -0.42f}, {0.4f, -0.42f}, {0.4f, 0.02f}, {0.0f, 0.46f}}, withAlpha(WHITE, 0.25f));
            break;
        case Icon::Health:
            p.rrect(-0.14f, -0.42f, 0.28f, 0.84f, 0.3f);
            p.rrect(-0.42f, -0.14f, 0.84f, 0.28f, 0.3f);
            break;
        case Icon::Person:
            p.circle(0, -0.22f, 0.2f);
            p.rrect(-0.32f, 0.04f, 0.64f, 0.42f, 0.7f);
            break;
        case Icon::Users:
            p.circle(0.16f, -0.2f, 0.16f, dark);
            p.rrect(-0.06f, 0.02f, 0.46f, 0.36f, 0.7f, dark);
            p.circle(-0.12f, -0.16f, 0.18f);
            p.rrect(-0.4f, 0.08f, 0.56f, 0.38f, 0.7f);
            break;
        case Icon::Crosshair:
            p.ringArc(0, 0, 0.26f, 0.34f, 0, 360);
            p.rect(-0.04f, -0.5f, 0.08f, 0.28f);
            p.rect(-0.04f, 0.22f, 0.08f, 0.28f);
            p.rect(-0.5f, -0.04f, 0.28f, 0.08f);
            p.rect(0.22f, -0.04f, 0.28f, 0.08f);
            p.circle(0, 0, 0.06f);
            break;
        case Icon::Storm:
            p.circle(-0.16f, -0.08f, 0.2f);
            p.circle(0.12f, -0.14f, 0.24f);
            p.circle(0.28f, 0.0f, 0.16f);
            p.rrect(-0.36f, -0.02f, 0.78f, 0.18f, 0.8f);
            p.poly({{0.02f, 0.14f}, {-0.12f, 0.36f}, {0.0f, 0.34f}, {-0.08f, 0.5f}, {0.14f, 0.26f}, {0.02f, 0.28f}, {0.1f, 0.14f}}, PLAY);
            break;
        case Icon::Clock:
            p.ringArc(0, 0, 0.34f, 0.44f, 0, 360);
            p.line(0, 0, 0, -0.24f, 0.08f);
            p.line(0, 0, 0.18f, 0.08f, 0.08f);
            break;
        case Icon::Coin:
            p.circle(0, 0, 0.44f, shade(c, 0.75f));
            p.circle(0, -0.02f, 0.4f);
            p.ringArc(0, -0.02f, 0.26f, 0.32f, 0, 360, shade(c, 0.8f));
            p.poly({{0, -0.2f}, {0.06f, -0.06f}, {0.2f, -0.04f}, {0.08f, 0.06f}, {0.12f, 0.2f}, {0, 0.12f}, {-0.12f, 0.2f}, {-0.08f, 0.06f}, {-0.2f, -0.04f}, {-0.06f, -0.06f}},
                   shade(c, 0.8f));
            break;
        case Icon::Gear:
            for (int k = 0; k < 6; k++) p.rot(0, 0, 0.16f, 0.94f, k * 30.0f);
            p.circle(0, 0, 0.34f);
            p.circle(0, 0, 0.14f, INK);
            break;
        case Icon::Lock:
            p.ringArc(0, -0.12f, 0.16f, 0.26f, 180, 360);
            p.rect(-0.26f, -0.14f, 0.1f, 0.12f);
            p.rect(0.16f, -0.14f, 0.1f, 0.12f);
            p.rrect(-0.34f, -0.04f, 0.68f, 0.5f, 0.25f);
            p.circle(0, 0.18f, 0.07f, INK);
            break;
        case Icon::Star: {
            Vector2 pts[10];
            for (int k = 0; k < 10; k++) {
                float a = -si::kPi / 2 + k * si::kPi / 5;
                float r = k % 2 ? 0.2f : 0.48f;
                pts[k] = {std::cos(a) * r, std::sin(a) * r + 0.03f};
            }
            for (int k = 0; k < 10; k++) p.tri({0, 0.03f}, pts[k], pts[(k + 1) % 10]);
            break;
        }
        case Icon::Check:
            p.line(-0.34f, 0.02f, -0.1f, 0.28f, 0.14f);
            p.line(-0.12f, 0.28f, 0.36f, -0.26f, 0.14f);
            break;
        case Icon::Trophy:
            p.poly({{-0.3f, -0.42f}, {0.3f, -0.42f}, {0.22f, 0.02f}, {0, 0.12f}, {-0.22f, 0.02f}});
            p.ringArc(-0.3f, -0.2f, 0.1f, 0.16f, 90, 270);
            p.ringArc(0.3f, -0.2f, 0.1f, 0.16f, -90, 90);
            p.rect(-0.06f, 0.1f, 0.12f, 0.18f);
            p.rrect(-0.24f, 0.26f, 0.48f, 0.16f, 0.4f);
            break;
        case Icon::Skull:
            p.circle(0, -0.08f, 0.36f);
            p.rrect(-0.2f, 0.12f, 0.4f, 0.3f, 0.4f);
            p.circle(-0.14f, -0.06f, 0.1f, INK);
            p.circle(0.14f, -0.06f, 0.1f, INK);
            p.rect(-0.1f, 0.3f, 0.04f, 0.12f, INK);
            p.rect(0.06f, 0.3f, 0.04f, 0.12f, INK);
            break;
        case Icon::Wood:
            p.rot(0, 0, 0.9f, 0.3f, -30, Color{176, 128, 78, 255});
            p.rot(0.04f, -0.04f, 0.8f, 0.05f, -30, Color{140, 96, 56, 255});
            p.circle(0.34f, -0.22f, 0.13f, Color{220, 180, 120, 255});
            p.ringArc(0.34f, -0.22f, 0.05f, 0.08f, 0, 360, Color{160, 110, 60, 255});
            break;
        case Icon::Brick: {
            Color b{190, 95, 70, 255}, m{230, 200, 180, 255};
            p.rect(-0.46f, -0.34f, 0.92f, 0.68f, m);
            p.rect(-0.42f, -0.3f, 0.4f, 0.18f, b); p.rect(0.02f, -0.3f, 0.4f, 0.18f, b);
            p.rect(-0.42f, -0.08f, 0.18f, 0.18f, b); p.rect(-0.2f, -0.08f, 0.4f, 0.18f, b); p.rect(0.24f, -0.08f, 0.18f, 0.18f, b);
            p.rect(-0.42f, 0.14f, 0.4f, 0.16f, b); p.rect(0.02f, 0.14f, 0.4f, 0.16f, b);
            break;
        }
        case Icon::Metal: {
            Color m{170, 180, 195, 255};
            p.rrect(-0.44f, -0.36f, 0.88f, 0.72f, 0.15f, m);
            p.rect(-0.44f, -0.04f, 0.88f, 0.08f, shade(m, 0.7f));
            for (int k = 0; k < 4; k++) p.circle(k % 2 ? 0.32f : -0.32f, k < 2 ? -0.24f : 0.24f, 0.05f, shade(m, 0.55f));
            break;
        }
        case Icon::Bullet:
            p.rrect(-0.12f, -0.1f, 0.24f, 0.52f, 0.2f, Color{210, 170, 70, 255});
            p.poly({{-0.12f, -0.1f}, {0.12f, -0.1f}, {0.1f, -0.3f}, {0, -0.44f}, {-0.1f, -0.3f}}, Color{200, 120, 70, 255});
            break;
        case Icon::Car:
            p.rrect(-0.46f, -0.06f, 0.92f, 0.28f, 0.4f);
            p.poly({{-0.26f, -0.06f}, {-0.14f, -0.3f}, {0.16f, -0.3f}, {0.3f, -0.06f}});
            p.circle(-0.24f, 0.24f, 0.13f, INK);
            p.circle(0.24f, 0.24f, 0.13f, INK);
            break;
        case Icon::Map:
            p.poly({{-0.44f, -0.3f}, {-0.16f, -0.4f}, {-0.16f, 0.34f}, {-0.44f, 0.44f}});
            p.poly({{-0.14f, -0.4f}, {0.14f, -0.3f}, {0.14f, 0.44f}, {-0.14f, 0.34f}}, dark);
            p.poly({{0.16f, -0.3f}, {0.44f, -0.4f}, {0.44f, 0.34f}, {0.16f, 0.44f}});
            break;
        case Icon::Backpack:
        case Icon::Bag:
            p.ringArc(0, -0.28f, 0.1f, 0.16f, 180, 360);
            p.rrect(-0.32f, -0.28f, 0.64f, 0.72f, 0.35f);
            p.rrect(-0.2f, 0.08f, 0.4f, 0.22f, 0.3f, dark);
            break;
        case Icon::Bolt:
            p.poly({{0.12f, -0.48f}, {-0.3f, 0.06f}, {-0.02f, 0.06f}, {-0.12f, 0.48f}, {0.3f, -0.08f}, {0.02f, -0.08f}});
            break;
        case Icon::Play:
            p.poly({{-0.24f, -0.36f}, {0.38f, 0.0f}, {-0.24f, 0.36f}});
            break;
        case Icon::Globe:
            p.ringArc(0, 0, 0.36f, 0.44f, 0, 360);
            p.line(-0.4f, 0, 0.4f, 0, 0.07f);
            p.ringArc(0, 0, 0.14f, 0.2f, 0, 360);
            p.line(0, -0.4f, 0, 0.4f, 0.07f);
            break;
        case Icon::Flag:
            p.rect(-0.3f, -0.44f, 0.08f, 0.88f);
            p.poly({{-0.22f, -0.42f}, {0.38f, -0.26f}, {-0.22f, -0.06f}});
            break;
        case Icon::Parachute:
            p.ringArc(0, 0.02f, 0.0f, 0.44f, 180, 360);
            p.line(-0.42f, 0.02f, -0.02f, 0.4f, 0.05f);
            p.line(0.42f, 0.02f, 0.02f, 0.4f, 0.05f);
            p.line(0, 0.02f, 0, 0.4f, 0.05f);
            break;
        case Icon::Chart:
            p.rect(-0.38f, 0.06f, 0.18f, 0.36f);
            p.rect(-0.09f, -0.14f, 0.18f, 0.56f);
            p.rect(0.2f, -0.38f, 0.18f, 0.8f);
            break;
        case Icon::Sparkle:
            p.poly({{0, -0.46f}, {0.1f, -0.1f}, {0.46f, 0}, {0.1f, 0.1f}, {0, 0.46f}, {-0.1f, 0.1f}, {-0.46f, 0}, {-0.1f, -0.1f}});
            break;
        case Icon::Pickaxe:
            p.rot(0.02f, 0.06f, 0.1f, 0.86f, 40, Color{150, 110, 70, 255});
            p.ringArc(-0.02f, 0.24f, 0.4f, 0.52f, 215, 325);
            break;
        case Icon::Hammer:
            p.rot(0.06f, 0.1f, 0.1f, 0.76f, 40, Color{150, 110, 70, 255});
            p.rot(-0.14f, -0.16f, 0.52f, 0.22f, 40);
            break;
        case Icon::Close:
            p.line(-0.3f, -0.3f, 0.3f, 0.3f, 0.12f);
            p.line(0.3f, -0.3f, -0.3f, 0.3f, 0.12f);
            break;
    }
}

// Side-on silhouettes of every item, pointing right.
void itemIcon(unsigned char type, float cx, float cy, float size, Color c) {
    using si::ItemType;
    Pen p{cx, cy, size, c};
    Color dark = shade(c, 0.6f);
    ItemType t = (ItemType)type;
    auto rifle = [&](float len, float magLen, bool scope, bool stockThick) {
        p.rrect(-0.42f, -0.1f, 0.6f * len, 0.16f, 0.3f);                 // receiver
        p.rect(-0.42f + 0.6f * len - 0.02f, -0.07f, 0.34f * len, 0.07f);  // barrel
        p.poly({{-0.42f, -0.08f}, {-0.5f, -0.04f}, {-0.5f, stockThick ? 0.14f : 0.1f}, {-0.36f, 0.06f}}); // stock
        p.rot(-0.08f, 0.14f, 0.09f, magLen, -12);                         // magazine
        p.rot(-0.24f, 0.13f, 0.07f, 0.16f, 15, dark);                     // grip
        if (scope) { p.rrect(-0.2f, -0.2f, 0.26f, 0.08f, 0.5f, dark); p.rect(-0.12f, -0.13f, 0.03f, 0.04f, dark); }
    };
    switch (t) {
        case ItemType::AssaultRifle: rifle(1.0f, 0.2f, false, false); break;
        case ItemType::HeavyRifle: rifle(1.05f, 0.22f, false, true); p.rect(0.2f, -0.12f, 0.12f, 0.04f, dark); break;
        case ItemType::BurstRifle: rifle(1.0f, 0.2f, false, false); p.rect(0.02f, -0.14f, 0.12f, 0.04f, dark); break;
        case ItemType::ScopedRifle: rifle(1.0f, 0.18f, true, false); break;
        case ItemType::SniperRifle:
        case ItemType::HuntingRifle:
            p.rrect(-0.46f, -0.06f, 0.5f, 0.12f, 0.4f);
            p.rect(0.02f, -0.04f, 0.46f, 0.05f);
            p.poly({{-0.46f, -0.04f}, {-0.5f, 0.12f}, {-0.3f, 0.06f}});
            if (t == ItemType::SniperRifle) { p.rrect(-0.24f, -0.17f, 0.3f, 0.08f, 0.6f, dark); p.rot(0.02f, 0.1f, 0.05f, 0.14f, 20); }
            else p.rot(-0.2f, 0.1f, 0.06f, 0.1f, 12, dark);
            break;
        case ItemType::PumpShotgun:
        case ItemType::TacticalShotgun:
        case ItemType::HeavyShotgun:
            p.rrect(-0.4f, -0.1f, 0.42f, 0.14f, 0.3f);
            p.rect(0.0f, -0.08f, 0.44f, 0.08f);
            p.rrect(0.06f, 0.0f, 0.2f, 0.07f, 0.4f, dark); // pump
            p.poly({{-0.4f, -0.08f}, {-0.5f, -0.02f}, {-0.5f, 0.12f}, {-0.34f, 0.05f}});
            p.rot(-0.24f, 0.12f, 0.07f, 0.14f, 15, dark);
            if (t == ItemType::TacticalShotgun) p.rect(-0.1f, 0.03f, 0.08f, 0.1f, dark);
            if (t == ItemType::HeavyShotgun) p.rect(0.02f, -0.12f, 0.38f, 0.04f, dark);
            break;
        case ItemType::DoubleBarrel:
            p.rect(-0.08f, -0.09f, 0.52f, 0.06f);
            p.rect(-0.08f, -0.02f, 0.52f, 0.06f, dark);
            p.poly({{-0.08f, -0.1f}, {-0.46f, 0.0f}, {-0.46f, 0.16f}, {-0.08f, 0.05f}});
            break;
        case ItemType::SMG:
        case ItemType::CompactSMG:
            p.rrect(-0.3f, -0.1f, 0.46f, 0.16f, 0.3f);
            p.rect(0.16f, -0.06f, 0.18f, 0.06f);
            p.rot(0.0f, 0.18f, 0.08f, t == ItemType::SMG ? 0.26f : 0.18f, 0);
            p.rot(-0.2f, 0.12f, 0.07f, 0.16f, 12, dark);
            if (t == ItemType::SMG) p.rect(-0.46f, -0.06f, 0.16f, 0.05f, dark);
            break;
        case ItemType::Pistol:
        case ItemType::HandCannon:
            p.rrect(-0.24f, -0.14f, t == ItemType::HandCannon ? 0.56f : 0.46f, 0.14f, 0.3f);
            p.rot(-0.16f, 0.08f, 0.13f, 0.28f, 14, dark);
            break;
        case ItemType::RocketLauncher:
            p.rrect(-0.48f, -0.12f, 0.96f, 0.2f, 0.5f);
            p.rect(0.36f, -0.15f, 0.12f, 0.26f, dark);
            p.rot(-0.02f, 0.14f, 0.07f, 0.14f, 10, dark);
            p.rrect(-0.2f, -0.24f, 0.2f, 0.08f, 0.6f, dark);
            break;
        case ItemType::GrenadeLauncher:
            p.rrect(-0.4f, -0.1f, 0.5f, 0.16f, 0.3f);
            p.rect(0.08f, -0.12f, 0.34f, 0.12f);
            p.circle(-0.08f, 0.06f, 0.14f, dark);
            break;
        case ItemType::Minigun:
            for (int k = 0; k < 3; k++) p.rect(0.0f, -0.12f + k * 0.07f, 0.46f, 0.05f, k == 1 ? dark : c);
            p.rrect(-0.44f, -0.16f, 0.46f, 0.3f, 0.4f);
            p.rect(-0.3f, 0.12f, 0.2f, 0.16f, dark);
            break;
        case ItemType::Crossbow:
            p.rect(-0.4f, -0.03f, 0.8f, 0.07f);
            p.ringArc(0.12f, 0.0f, 0.3f, 0.36f, -80, 80);
            p.line(0.18f, -0.34f, 0.18f, 0.34f, 0.02f, dark);
            p.poly({{0.4f, 0.0f}, {0.48f, -0.04f}, {0.48f, 0.04f}}, dark);
            break;
        case ItemType::Grenade:
        case ItemType::ImpulseGrenade:
        case ItemType::StickyCharge:
            if (t == ItemType::StickyCharge) {
                p.rrect(-0.3f, -0.2f, 0.6f, 0.4f, 0.4f);
                p.circle(0.0f, 0.0f, 0.1f, Color{255, 80, 60, 255});
            } else {
                p.circle(0, 0.06f, 0.3f);
                p.rect(-0.08f, -0.32f, 0.16f, 0.12f, dark);
                p.ringArc(0.14f, -0.28f, 0.06f, 0.1f, 0, 360, dark);
                if (t == ItemType::ImpulseGrenade) p.ringArc(0, 0.06f, 0.14f, 0.2f, 0, 360, Color{120, 220, 255, 255});
            }
            break;
        case ItemType::Bandages:
            p.circle(-0.08f, 0.0f, 0.28f, Color{240, 236, 225, 255});
            p.circle(-0.08f, 0.0f, 0.1f, Color{200, 190, 175, 255});
            p.rect(-0.08f, 0.18f, 0.5f, 0.1f, Color{240, 236, 225, 255});
            break;
        case ItemType::Medkit:
        case ItemType::FieldKit:
            p.rrect(-0.4f, -0.26f, 0.8f, 0.56f, 0.2f, t == ItemType::Medkit ? Color{235, 235, 235, 255} : Color{90, 140, 90, 255});
            p.rect(-0.08f, -0.18f, 0.16f, 0.4f, Color{220, 50, 50, 255});
            p.rect(-0.2f, -0.06f, 0.4f, 0.16f, Color{220, 50, 50, 255});
            p.rect(-0.12f, -0.34f, 0.24f, 0.08f, dark);
            break;
        case ItemType::SmallShield:
            p.rrect(-0.16f, -0.12f, 0.32f, 0.44f, 0.4f, Color{80, 170, 255, 255});
            p.rect(-0.08f, -0.26f, 0.16f, 0.14f, Color{200, 220, 240, 255});
            break;
        case ItemType::ShieldPotion:
            p.circle(0, 0.12f, 0.3f, Color{70, 150, 255, 255});
            p.rect(-0.08f, -0.36f, 0.16f, 0.26f, Color{200, 220, 240, 255});
            p.circle(-0.1f, 0.04f, 0.07f, withAlpha(WHITE, 0.6f));
            break;
        case ItemType::RegenSoda:
            p.rrect(-0.2f, -0.34f, 0.4f, 0.7f, 0.3f, Color{230, 80, 120, 255});
            p.rect(-0.2f, -0.06f, 0.4f, 0.12f, WHITE);
            break;
        case ItemType::MegaFlask:
            p.circle(0, 0.1f, 0.34f, Color{120, 200, 255, 255});
            p.rect(-0.1f, -0.44f, 0.2f, 0.26f, Color{200, 220, 240, 255});
            p.circle(0, 0.1f, 0.2f, Color{60, 130, 240, 255});
            break;
        case ItemType::LaunchPad:
            p.rrect(-0.44f, 0.06f, 0.88f, 0.2f, 0.4f);
            p.poly({{-0.2f, 0.02f}, {0.0f, -0.36f}, {0.2f, 0.02f}}, Color{255, 220, 70, 255});
            break;
        default:
            p.circle(0, 0, 0.3f);
            break;
    }
}

} // namespace ui
