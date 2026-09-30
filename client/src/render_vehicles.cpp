// Procedural vehicle models (original designs): golf cart, crash quad, hoverboard, trolley
// and roller ball. Drawn with the lit object shader like the rest of the props.
#include <cmath>
#include <initializer_list>

#include "render.h"

namespace client {

namespace {

Color shade(Color c, float f) {
    return {(unsigned char)clampf(c.r * f, 0, 255), (unsigned char)clampf(c.g * f, 0, 255), (unsigned char)clampf(c.b * f, 0, 255), c.a};
}

// Local-space helper: positions are (x right, y up, z forward) relative to the body origin.
struct Body {
    si::Vec3 origin;
    Basis b;
    si::Vec3 at(float x, float y, float z) const { return origin + b.r * x + b.u * y + b.f * z; }
    void box(float x, float y, float z, float hx, float hy, float hz, Color c) const { drawBox(at(x, y, z), {hx, hy, hz}, b, c); }
    void boxRot(float x, float y, float z, float hx, float hy, float hz, const Basis& local, Color c) const {
        drawBox(at(x, y, z), {hx, hy, hz}, compose(b, local), c);
    }
};

Basis pitchLocal(float a) { return Basis{{1, 0, 0}, {0, std::cos(a), -std::sin(a)}, {0, std::sin(a), std::cos(a)}}; }
Basis yawLocal(float a) { return Basis{{std::cos(a), 0, -std::sin(a)}, {0, 1, 0}, {std::sin(a), 0, std::cos(a)}}; }

// A chunky wheel: tyre cylinder + hub + spokes that show rotation.
void wheel(const Body& body, float x, float y, float z, float radius, float width, float spin, float steer, Color tyre, Color hub) {
    Basis wb = compose(body.b, yawLocal(steer));
    si::Vec3 c = body.at(x, y, z);
    setDrawMaterial(M_FABRIC);
    drawCylinder(c, wb.r, radius, width * 0.5f, tyre, 14, spin);
    setDrawMaterial(M_METAL);
    float side = x >= 0 ? 1.0f : -1.0f;
    si::Vec3 capC = c + wb.r * (side * width * 0.52f);
    drawCylinder(capC, wb.r, radius * 0.55f, 0.02f, hub, 10, spin);
    // spokes rotate with the wheel
    Basis sb = compose(wb, pitchLocal(spin));
    for (int k = 0; k < 2; k++) {
        Basis kb = compose(sb, pitchLocal(k * kPi / 2));
        drawBox(capC + wb.r * (side * 0.02f), {0.012f, radius * 0.5f, 0.04f}, kb, shade(hub, 0.7f));
    }
}

void flame(const Body& body, float x, float y, float z, float len, float width, float time, Color core, Color outer) {
    float flick = 0.75f + 0.25f * std::sin(time * 57.0f + x * 13.0f);
    setDrawMaterial(M_PLAIN);
    body.box(x, y, z - len * 0.5f * flick, width, width, len * 0.5f * flick, outer);
    body.box(x, y, z - len * 0.3f * flick, width * 0.55f, width * 0.55f, len * 0.3f * flick, core);
}

void golfCart(const VehicleVisual& v, const Body& body, float time) {
    static const Color bodies[] = {{236, 238, 240, 255}, {150, 220, 190, 255}, {140, 190, 240, 255}, {245, 214, 120, 255}};
    Color paint = bodies[v.id % 4];
    Color canopy = v.id % 2 ? Color{40, 120, 80, 255} : Color{200, 60, 60, 255};
    Color dark{45, 48, 55, 255}, seat{70, 60, 55, 255}, chrome{190, 195, 205, 255};
    setDrawMaterial(M_METAL);
    // Chassis + floor
    body.box(0, 0.42f, -0.1f, 0.6f, 0.1f, 1.2f, shade(paint, 0.85f));
    body.box(0, 0.54f, 0.05f, 0.56f, 0.03f, 0.95f, dark);
    // Front cowl with rounded nose and headlights
    body.box(0, 0.66f, 0.95f, 0.6f, 0.18f, 0.32f, paint);
    body.box(0, 0.78f, 0.78f, 0.58f, 0.07f, 0.18f, paint);
    body.boxRot(0, 0.82f, 1.12f, 0.56f, 0.12f, 0.1f, pitchLocal(0.5f), paint);
    setDrawMaterial(M_GLASS);
    for (int s = -1; s <= 1; s += 2) body.box(0.4f * s, 0.7f, 1.28f, 0.1f, 0.06f, 0.02f, Color{255, 246, 200, 255});
    setDrawMaterial(M_METAL);
    body.box(0, 0.4f, 1.3f, 0.64f, 0.06f, 0.05f, dark);  // bumper
    body.box(0, 0.4f, -1.33f, 0.64f, 0.06f, 0.05f, dark);
    // Rear body under the back seat
    body.box(0, 0.6f, -0.95f, 0.6f, 0.16f, 0.38f, paint);
    // Seats
    setDrawMaterial(M_FABRIC);
    body.box(0, 0.74f, 0.12f, 0.58f, 0.07f, 0.3f, seat);
    body.box(0, 1.02f, -0.2f, 0.58f, 0.24f, 0.06f, seat);
    body.box(0, 0.84f, -0.95f, 0.58f, 0.07f, 0.26f, seat);
    body.box(0, 1.12f, -1.24f, 0.58f, 0.24f, 0.06f, seat);
    // Steering column + wheel in front of the driver seat
    setDrawMaterial(M_METAL);
    body.boxRot(0.36f, 0.95f, 0.62f, 0.025f, 0.25f, 0.025f, pitchLocal(0.5f), dark);
    body.boxRot(0.36f, 1.12f, 0.52f, 0.17f, 0.02f, 0.17f, compose(pitchLocal(-0.9f), yawLocal(v.steer * 0.6f)), dark);
    // Canopy posts and striped roof
    for (int sx = -1; sx <= 1; sx += 2) {
        body.boxRot(0.56f * sx, 1.5f, 0.78f, 0.03f, 0.72f, 0.03f, pitchLocal(-0.12f), chrome);
        body.box(0.56f * sx, 1.55f, -1.2f, 0.03f, 0.7f, 0.03f, chrome);
    }
    setDrawMaterial(M_FABRIC);
    body.box(0, 2.26f, -0.2f, 0.7f, 0.05f, 1.28f, canopy);
    for (int k = -2; k <= 2; k++) body.box(0, 2.265f, -0.2f + k * 0.5f, 0.705f, 0.052f, 0.12f, Color{245, 245, 240, 255});
    // Golf bag strapped to the side
    body.box(-0.68f, 0.95f, -0.95f, 0.1f, 0.35f, 0.1f, Color{60, 90, 160, 255});
    for (int k = 0; k < 3; k++) body.box(-0.68f + (k - 1) * 0.04f, 1.38f, -0.95f, 0.015f, 0.12f, 0.015f, chrome);
    // Wheels
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2)
            wheel(body, 0.6f * sx, 0.28f, 0.85f * sz, 0.28f, 0.2f, v.wheelSpin, sz > 0 ? v.steer * 0.4f : 0, Color{35, 35, 38, 255}, chrome);
    (void)time;
}

void crashQuad(const VehicleVisual& v, const Body& body, float time) {
    static const Color paints[] = {{235, 120, 30, 255}, {210, 45, 45, 255}, {120, 200, 60, 255}};
    Color paint = paints[v.id % 3];
    Color frame{50, 52, 58, 255}, seat{30, 30, 34, 255}, chrome{180, 185, 195, 255};
    setDrawMaterial(M_METAL);
    // Frame and engine block
    body.box(0, 0.55f, 0, 0.3f, 0.14f, 0.8f, frame);
    body.box(0, 0.6f, 0.05f, 0.24f, 0.2f, 0.3f, Color{90, 92, 100, 255});
    for (int k = 0; k < 3; k++) body.box(0, 0.62f + k * 0.08f, 0.05f, 0.27f, 0.015f, 0.27f, chrome); // cooling fins
    // Fenders
    body.box(0, 0.86f, 0.6f, 0.62f, 0.06f, 0.36f, paint);
    body.boxRot(0, 0.78f, 0.98f, 0.6f, 0.05f, 0.14f, pitchLocal(0.7f), paint);
    body.box(0, 0.9f, -0.58f, 0.62f, 0.06f, 0.42f, paint);
    body.boxRot(0, 0.8f, -1.02f, 0.6f, 0.05f, 0.12f, pitchLocal(-0.7f), paint);
    body.box(0, 0.96f, -0.58f, 0.4f, 0.02f, 0.3f, shade(paint, 0.7f)); // rack
    // Seat + tank
    setDrawMaterial(M_FABRIC);
    body.box(0, 0.98f, -0.2f, 0.22f, 0.09f, 0.42f, seat);
    setDrawMaterial(M_METAL);
    body.box(0, 1.0f, 0.3f, 0.24f, 0.12f, 0.2f, paint);
    // Handlebars
    body.boxRot(0, 1.08f, 0.55f, 0.035f, 0.2f, 0.035f, pitchLocal(-0.35f), chrome);
    body.boxRot(0, 1.27f, 0.5f, 0.45f, 0.025f, 0.025f, yawLocal(v.steer * 0.35f), chrome);
    for (int s = -1; s <= 1; s += 2) {
        si::Vec3 grip = body.at(0, 1.27f, 0.5f) + compose(body.b, yawLocal(v.steer * 0.35f)).r * (0.42f * s);
        drawBox(grip, {0.06f, 0.035f, 0.035f}, compose(body.b, yawLocal(v.steer * 0.35f)), seat);
    }
    // Ram plate with spikes (the "crash" part)
    body.box(0, 0.62f, 1.12f, 0.55f, 0.2f, 0.05f, frame);
    for (int k = -2; k <= 2; k++) body.boxRot(k * 0.22f, 0.62f, 1.22f, 0.04f, 0.04f, 0.1f, Basis(), chrome);
    setDrawMaterial(M_GLASS);
    body.box(0, 0.92f, 1.0f, 0.12f, 0.06f, 0.03f, Color{255, 250, 210, 255});
    // Exhausts
    setDrawMaterial(M_METAL);
    for (int s = -1; s <= 1; s += 2) drawCylinder(body.at(0.22f * s, 0.72f, -1.02f), body.b.f, 0.06f, 0.14f, chrome, 8);
    if (v.boosting)
        for (int s = -1; s <= 1; s += 2) flame(body, 0.22f * s, 0.72f, -1.16f, 0.9f, 0.08f, time, Color{255, 250, 200, 255}, Color{255, 140, 40, 220});
    // Big knobbly wheels
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2)
            wheel(body, 0.66f * sx, 0.4f, 0.62f * sz, 0.4f, 0.32f, v.wheelSpin, sz > 0 ? v.steer * 0.45f : 0, Color{28, 28, 30, 255}, paint);
}

void hoverboard(const VehicleVisual& v, const Body& body0, float time) {
    static const Color paints[] = {{40, 200, 200, 255}, {230, 70, 170, 255}, {250, 200, 50, 255}};
    Color paint = paints[v.id % 3];
    Body body = body0;
    body.origin = body.origin + body.b.u * (0.06f * std::sin(time * 3.0f + v.id));
    setDrawMaterial(M_METAL);
    // Deck with kicked-up nose and tail
    body.box(0, 0.34f, 0, 0.3f, 0.045f, 0.62f, paint);
    body.boxRot(0, 0.4f, 0.74f, 0.27f, 0.04f, 0.16f, pitchLocal(-0.35f), paint);
    body.boxRot(0, 0.4f, -0.74f, 0.27f, 0.04f, 0.16f, pitchLocal(0.35f), paint);
    setDrawMaterial(M_FABRIC);
    body.box(0, 0.39f, 0, 0.25f, 0.008f, 0.55f, Color{35, 35, 40, 255}); // grip tape
    body.box(0, 0.395f, 0.25f, 0.2f, 0.005f, 0.05f, shade(paint, 0.6f));
    body.box(0, 0.395f, -0.25f, 0.2f, 0.005f, 0.05f, shade(paint, 0.6f));
    // Glowing hover pads underneath
    setDrawMaterial(M_PLAIN);
    Color glow = v.boosting ? Color{255, 190, 90, 255} : Color{140, 245, 255, 255};
    float pulse = 0.85f + 0.15f * std::sin(time * 9.0f + v.id);
    for (int s = -1; s <= 1; s += 2) {
        body.box(0, 0.27f, 0.42f * s, 0.2f, 0.025f, 0.14f, shade(glow, pulse));
        body.box(0, 0.24f, 0.42f * s, 0.14f, 0.01f, 0.09f, Color{255, 255, 255, 255});
    }
    // Tail fins + thruster
    setDrawMaterial(M_METAL);
    for (int s = -1; s <= 1; s += 2) body.boxRot(0.22f * s, 0.5f, -0.78f, 0.015f, 0.12f, 0.1f, pitchLocal(0.4f), shade(paint, 0.7f));
    drawCylinder(body.at(0, 0.31f, -0.86f), body.b.f, 0.07f, 0.08f, Color{80, 85, 95, 255}, 8);
    if (v.boosting) flame(body, 0, 0.31f, -0.95f, 1.2f, 0.07f, time, Color{255, 255, 230, 255}, Color{120, 200, 255, 220});
}

void trolley(const VehicleVisual& v, const Body& body, float time) {
    Color metal{185, 190, 198, 255}, handle = v.id % 2 ? Color{210, 40, 40, 255} : Color{40, 90, 200, 255};
    setDrawMaterial(M_METAL);
    // Chassis rails and lower tray
    for (int s = -1; s <= 1; s += 2) body.box(0.3f * s, 0.2f, -0.05f, 0.02f, 0.02f, 0.62f, metal);
    body.box(0, 0.24f, -0.1f, 0.3f, 0.01f, 0.4f, shade(metal, 0.85f));
    // Basket: floor, corner posts, bars and rails
    body.box(0, 0.55f, 0.08f, 0.4f, 0.015f, 0.58f, shade(metal, 0.9f));
    const float bx = 0.42f, bz0 = -0.52f, bz1 = 0.68f, by0 = 0.55f, by1 = 1.08f;
    for (float y : {0.7f, 0.88f, by1}) {
        for (int s = -1; s <= 1; s += 2) body.box(bx * s, y, (bz0 + bz1) / 2, 0.012f, 0.012f, (bz1 - bz0) / 2, metal);
        body.box(0, y, bz1, bx, 0.012f, 0.012f, metal);
        body.box(0, y, bz0, bx, 0.012f, 0.012f, metal);
    }
    for (float z = bz0; z <= bz1 + 0.01f; z += 0.15f)
        for (int s = -1; s <= 1; s += 2) body.box(bx * s, (by0 + by1) / 2, z, 0.008f, (by1 - by0) / 2, 0.008f, metal);
    for (float x = -bx; x <= bx + 0.01f; x += 0.14f) {
        body.box(x, (by0 + by1) / 2, bz1, 0.008f, (by1 - by0) / 2, 0.008f, metal);
        body.box(x, (by0 + by1) / 2, bz0, 0.008f, (by1 - by0) / 2, 0.008f, metal);
    }
    // Handle
    for (int s = -1; s <= 1; s += 2) body.boxRot(0.38f * s, 1.12f, -0.62f, 0.015f, 0.1f, 0.015f, pitchLocal(0.5f), metal);
    setDrawMaterial(M_FABRIC);
    body.box(0, 1.19f, -0.7f, 0.42f, 0.03f, 0.03f, handle);
    body.box(0, 1.08f, -0.54f, 0.25f, 0.06f, 0.01f, handle); // child-seat flap
    // Legs + caster wheels
    setDrawMaterial(M_METAL);
    for (int sx = -1; sx <= 1; sx += 2)
        for (int sz = -1; sz <= 1; sz += 2) {
            body.box(0.3f * sx, 0.14f, 0.5f * sz, 0.015f, 0.05f, 0.015f, metal);
            wheel(body, 0.3f * sx, 0.07f, 0.5f * sz, 0.07f, 0.04f, v.wheelSpin * 3.5f, sz > 0 ? v.steer * 0.6f : 0, Color{40, 40, 44, 255}, metal);
        }
    (void)time;
}

void rollerBallCage(const VehicleVisual& v, float time) {
    const float R = 1.18f;
    si::Vec3 c = v.pos + si::Vec3{0, R + 0.02f, 0};
    Color cage = v.id % 2 ? Color{245, 150, 40, 255} : Color{60, 170, 240, 255};
    Basis s = v.ballSpin;
    setDrawMaterial(M_METAL);
    // Three perpendicular rings of short segments, rotating with the ball
    const int N = 18;
    for (int ring = 0; ring < 3; ring++) {
        Basis rb = ring == 0 ? s : ring == 1 ? compose(s, yawLocal(kPi / 2)) : compose(s, pitchLocal(kPi / 2));
        for (int k = 0; k < N; k++) {
            float a = 2 * kPi * (k + 0.5f) / N;
            si::Vec3 p = c + rb.r * (std::cos(a) * R) + rb.u * (std::sin(a) * R);
            Basis seg;
            seg.f = rb.f;
            seg.u = (rb.r * -std::sin(a) + rb.u * std::cos(a)).norm();
            seg.r = seg.u.cross(seg.f).norm();
            drawBox(p, {0.07f, R * kPi / N, 0.07f}, seg, ring == 0 ? cage : shade(cage, 0.85f));
        }
    }
    // Hub rings on the sides + inner seat frame (stays upright)
    for (int sd = -1; sd <= 1; sd += 2) drawCylinder(c + s.f * (R * 0.98f * sd), s.f, 0.22f, 0.04f, Color{70, 70, 78, 255}, 10);
    Basis up = yawBasis(v.yaw);
    drawBox(v.pos + si::Vec3{0, 0.42f, 0}, {0.3f, 0.05f, 0.3f}, up, Color{55, 55, 62, 255});
    drawBox(v.pos + si::Vec3{0, 0.2f, 0} , {0.05f, 0.2f, 0.05f}, up, Color{55, 55, 62, 255});
    (void)time;
}

} // namespace

Basis vehicleBasis(const VehicleVisual& v) {
    Basis yb = yawPitchBasis(v.yaw, v.pitch);
    float cr = std::cos(v.roll), sr = std::sin(v.roll);
    return compose(yb, Basis{{cr, sr, 0}, {-sr, cr, 0}, {0, 0, 1}});
}

void drawVehicle(const VehicleVisual& v, float time) {
    Body body;
    body.origin = v.pos;
    body.b = vehicleBasis(v);
    switch (v.type) {
        case VehicleType::GolfCart: golfCart(v, body, time); break;
        case VehicleType::CrashQuad: crashQuad(v, body, time); break;
        case VehicleType::Hoverboard: hoverboard(v, body, time); break;
        case VehicleType::Trolley: trolley(v, body, time); break;
        case VehicleType::RollerBall: rollerBallCage(v, time); break;
        default: break;
    }
    // Smoke when badly damaged
    if (v.hp < 0.3f && v.type != VehicleType::RollerBall) {
        setDrawMaterial(M_PLAIN);
        for (int k = 0; k < 3; k++) {
            float t = std::fmod(time * 0.8f + k * 0.33f, 1.0f);
            si::Vec3 p = body.at(0, 0.9f, 0.6f) + si::Vec3{std::sin(k * 2.1f + time) * 0.2f, t * 1.6f, 0};
            unsigned char a = (unsigned char)(160 * (1 - t));
            drawBox(p, {0.12f + t * 0.25f, 0.12f + t * 0.25f, 0.12f + t * 0.25f}, yawBasis(t * 3), Color{60, 60, 60, a});
        }
    }
    setDrawMaterial(M_PLAIN);
}

void drawVehicleShell(const VehicleVisual& v, float time) {
    if (v.type != VehicleType::RollerBall) return;
    const float R = 1.16f;
    si::Vec3 c = v.pos + si::Vec3{0, R + 0.04f, 0};
    setDrawMaterial(M_GLASS);
    float hurt = 1.0f - v.hp;
    Color shell{(unsigned char)(170 + 80 * hurt), (unsigned char)(215 - 120 * hurt), (unsigned char)(255 - 150 * hurt), 70};
    if (v.boosting) shell = Color{255, 200, 120, 90};
    drawSphere(c, {R, R, R}, v.ballSpin, shell, 12, 20);
    setDrawMaterial(M_PLAIN);
    (void)time;
}

} // namespace client
