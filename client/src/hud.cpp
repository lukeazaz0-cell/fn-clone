#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

#include "game_client.h"
#include "raylib.h"
#include "raymath.h"
#include "shared/game/items.h"
#include "ui.h"

namespace client {

namespace {

std::string fmtTime(float s) {
    int t = std::max(0, (int)std::ceil(s));
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d:%02d", t / 60, t % 60);
    return buf;
}

const char* slotKey(int i) {
    static const char* k[] = {"F", "1", "2", "3", "4", "5"};
    return k[i];
}

Color mixc(Color a, Color b, float t) {
    return {(unsigned char)(a.r + (b.r - a.r) * t), (unsigned char)(a.g + (b.g - a.g) * t), (unsigned char)(a.b + (b.b - a.b) * t),
            (unsigned char)(a.a + (b.a - a.a) * t)};
}

// Bearing in degrees (0 = north = -Z on the map, clockwise).
float bearingOf(float dx, float dz) {
    float b = std::atan2(dx, -dz) * RAD2DEG;
    return b < 0 ? b + 360 : b;
}

const Color kHudPanel{10, 14, 24, 170};

} // namespace

Vector2 GameClient::worldToMap(const si::Vec3& p, Rectangle r, si::Vec3 center, float meters, bool whole) const {
    if (whole) return {r.x + p.x / WORLD_SIZE * r.width, r.y + p.z / WORLD_SIZE * r.height};
    return {r.x + r.width / 2 + (p.x - center.x) / meters * r.width, r.y + r.height / 2 + (p.z - center.z) / meters * r.height};
}

// ------------------------------------------------------------------------------ minimap

void GameClient::hudMinimap(Rectangle r, float meters) {
    float s = ui::scale();
    si::Vec3 me = viewPos();
    float texPerMeter = worldR_.minimapSize / WORLD_SIZE;
    Rectangle src{(me.x - meters / 2) * texPerMeter, (me.z - meters / 2) * texPerMeter, meters * texPerMeter, meters * texPerMeter};
    ui::shadow(r, 14, 10, 0.45f);
    ui::rrect(r, 14, Color{40, 110, 170, 255});
    BeginScissorMode((int)r.x, (int)r.y, (int)r.width, (int)r.height);
    DrawTexturePro(worldR_.minimap, src, r, {0, 0}, 0, WHITE);
    float scale = r.width / meters;
    if (g_.stormPhase >= 0 && g_.stormCurR < 3000) {
        Vector2 c = worldToMap({g_.stormCur.x, 0, g_.stormCur.y}, r, me, meters, false);
        DrawRing(c, g_.stormCurR * scale, g_.stormCurR * scale + 2.5f * s, 0, 360, 96, ui::STORM);
        Vector2 n = worldToMap({g_.stormNext.x, 0, g_.stormNext.y}, r, me, meters, false);
        DrawRing(n, g_.stormNextR * scale, g_.stormNextR * scale + 2 * s, 0, 360, 96, WHITE);
        // Line toward the safe zone when outside it
        float d = dist2d(me.xz(), g_.stormNext);
        if (d > g_.stormNextR) {
            Vector2 c0{r.x + r.width / 2, r.y + r.height / 2};
            Vector2 dir{n.x - c0.x, n.y - c0.y};
            float l = std::sqrt(dir.x * dir.x + dir.y * dir.y);
            if (l > 1) {
                for (float t = 12 * s; t < std::min(l - g_.stormNextR * scale, r.width); t += 10 * s)
                    DrawCircleV({c0.x + dir.x / l * t, c0.y + dir.y / l * t}, 1.8f * s, Color{255, 255, 255, 200});
            }
        }
    }
    // Vehicles
    for (auto& [id, v] : vehicles_) {
        if (!v.present) continue;
        Vector2 m = worldToMap(v.cur.pos, r, me, meters, false);
        bool taken = v.cur.flags & VF_DRIVER;
        DrawCircleV(m, 6.5f * s, Color{16, 18, 26, 220});
        ui::icon(ui::Icon::Car, m.x, m.y, 10 * s, taken ? Color{170, 170, 180, 255} : ui::PLAY);
    }
    for (auto& poi : map_.pois) {
        if (!poi.major) continue;
        Vector2 v = worldToMap({poi.center.x, 0, poi.center.y}, r, me, meters, false);
        ui::textShadow(poi.name, v.x - ui::textWidth(poi.name, 11) / 2, v.y - 6 * s, 11, ui::withAlpha(WHITE, 0.9f));
    }
    for (auto& [id, p] : players_) {
        if (!p.present || id == net_.playerId || p.team != self_.team || !(p.cur.flags & PF_ALIVE)) continue;
        Vector2 v = worldToMap(p.cur.pos, r, me, meters, false);
        DrawCircleV(v, 5 * s, Color{10, 20, 12, 255});
        DrawCircleV(v, 3.8f * s, ui::GOOD);
    }
    // Player arrow
    Vector2 c{r.x + r.width / 2, r.y + r.height / 2};
    float yaw = camYaw_;
    Vector2 f{std::sin(yaw), std::cos(yaw)}, rt{-std::cos(yaw), std::sin(yaw)};
    float as = 9 * s;
    Vector2 tip{c.x + f.x * as * 1.4f, c.y + f.y * as * 1.4f};
    Vector2 l{c.x - f.x * as - rt.x * as * 0.85f, c.y - f.y * as - rt.y * as * 0.85f};
    Vector2 rr{c.x - f.x * as + rt.x * as * 0.85f, c.y - f.y * as + rt.y * as * 0.85f};
    DrawTriangle(tip, l, rr, Color{0, 0, 0, 160});
    DrawTriangle(tip, rr, l, Color{0, 0, 0, 160});
    DrawTriangle(tip, l, c, ui::PLAY);
    DrawTriangle(tip, c, l, ui::PLAY);
    DrawTriangle(tip, rr, c, Color{230, 180, 30, 255});
    DrawTriangle(tip, c, rr, Color{230, 180, 30, 255});
    EndScissorMode();
    ui::rrectLine(r, 14, 2.0f, Color{255, 255, 255, 110});
    // North marker
    ui::rrect({r.x + r.width / 2 - 9 * s, r.y - 9 * s, 18 * s, 18 * s}, 9, Color{16, 20, 32, 255});
    ui::textCentered("N", r.x + r.width / 2, r.y - 7.5f * s, 12, ui::PLAY);
}

void GameClient::hudFullMap() {
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    float s = ui::scale();
    DrawRectangle(0, 0, (int)W, (int)H, Color{4, 6, 12, 190});
    float size = std::min(W, H) * 0.86f;
    Rectangle r{W / 2 - size / 2, H / 2 - size / 2 + 10 * s, size, size};
    ui::shadow(r, 10, 16, 0.6f);
    DrawTexturePro(worldR_.minimap, {0, 0, (float)worldR_.minimapSize, (float)worldR_.minimapSize}, r, {0, 0}, 0, WHITE);
    for (int i = 1; i < 10; i++) {
        DrawLineV({r.x + r.width * i / 10, r.y}, {r.x + r.width * i / 10, r.y + r.height}, Color{255, 255, 255, 26});
        DrawLineV({r.x, r.y + r.height * i / 10}, {r.x + r.width, r.y + r.height * i / 10}, Color{255, 255, 255, 26});
    }
    for (int i = 0; i < 10; i++) {
        ui::textCentered(std::string(1, (char)('A' + i)), r.x + r.width * (i + 0.5f) / 10, r.y - 22 * s, 14, ui::MUTED);
        ui::textCentered(std::to_string(i + 1), r.x - 16 * s, r.y + r.height * (i + 0.5f) / 10 - 8 * s, 14, ui::MUTED);
    }
    float scale = r.width / WORLD_SIZE;
    if (g_.busActive) {
        Vector2 a = worldToMap(g_.busStart, r, {}, 0, true), b = worldToMap(g_.busEnd, r, {}, 0, true);
        DrawLineEx(a, b, 3 * s, Color{255, 255, 255, 170});
        Vector2 cur = worldToMap(lerp3(g_.busStart, g_.busEnd, clampf(g_.busProgress, 0, 1)), r, {}, 0, true);
        DrawCircleV(cur, 9 * s, Color{16, 20, 32, 255});
        ui::icon(ui::Icon::Parachute, cur.x, cur.y, 14 * s, ui::ACCENT);
    }
    if (g_.stormPhase >= 0 && g_.stormCurR < 3000) {
        Vector2 c = worldToMap({g_.stormCur.x, 0, g_.stormCur.y}, r, {}, 0, true);
        DrawRing(c, g_.stormCurR * scale, g_.stormCurR * scale + 3 * s, 0, 360, 128, ui::STORM);
        Vector2 n = worldToMap({g_.stormNext.x, 0, g_.stormNext.y}, r, {}, 0, true);
        DrawRing(n, g_.stormNextR * scale, g_.stormNextR * scale + 2 * s, 0, 360, 128, WHITE);
    }
    for (auto& [id, v] : vehicles_) {
        if (!v.present) continue;
        Vector2 m = worldToMap(v.cur.pos, r, {}, 0, true);
        DrawCircleV(m, 6 * s, Color{16, 18, 26, 220});
        ui::icon(ui::Icon::Car, m.x, m.y, 9 * s, (v.cur.flags & VF_DRIVER) ? Color{170, 170, 180, 255} : ui::PLAY);
    }
    for (auto& poi : map_.pois) {
        Vector2 v = worldToMap({poi.center.x, 0, poi.center.y}, r, {}, 0, true);
        float sz = poi.major ? 16 : 11;
        ui::textShadow(poi.name, v.x - ui::textWidth(poi.name, sz) / 2, v.y - 8 * s, sz, poi.major ? WHITE : ui::withAlpha(WHITE, 0.7f));
    }
    for (auto& [id, p] : players_) {
        if (!p.present || id == net_.playerId || p.team != self_.team || !(p.cur.flags & PF_ALIVE)) continue;
        DrawCircleV(worldToMap(p.cur.pos, r, {}, 0, true), 6 * s, ui::GOOD);
    }
    Vector2 me = worldToMap(viewPos(), r, {}, 0, true);
    float pulse = 0.5f + 0.5f * std::sin(time_ * 5);
    DrawCircleV(me, (10 + 6 * pulse) * s, ui::withAlpha(ui::PLAY, 0.25f));
    DrawCircleV(me, 8 * s, ui::PLAY);
    DrawCircleLinesV(me, 8 * s, BLACK);
    ui::rrectLine(r, 10, 2.0f, Color{255, 255, 255, 90});
    // Legend
    float lx = r.x + r.width + 24 * s, ly = r.y;
    if (lx + 180 * s < W) {
        Rectangle lg{lx, ly, 180 * s, 170 * s};
        ui::card(lg, 12, Color{16, 21, 34, 235});
        float yy = ly + 16 * s;
        ui::text("MAP", lx + 16 * s, yy, 16, ui::PLAY);
        yy += 30 * s;
        DrawCircleV({lx + 26 * s, yy + 8 * s}, 7 * s, ui::PLAY);
        ui::text("You", lx + 44 * s, yy, 15, WHITE);
        yy += 26 * s;
        DrawRing({lx + 26 * s, yy + 8 * s}, 5 * s, 8 * s, 0, 360, 24, ui::STORM);
        ui::text("Storm", lx + 44 * s, yy, 15, WHITE);
        yy += 26 * s;
        DrawRing({lx + 26 * s, yy + 8 * s}, 5 * s, 8 * s, 0, 360, 24, WHITE);
        ui::text("Next safe zone", lx + 44 * s, yy, 15, WHITE);
        yy += 26 * s;
        ui::icon(ui::Icon::Car, lx + 26 * s, yy + 8 * s, 14 * s, ui::PLAY);
        ui::text("Vehicle", lx + 44 * s, yy, 15, WHITE);
    }
    float kw = ui::keycap("M", W / 2 - 60 * s, r.y + r.height + 14 * s, 28 * s);
    ui::text("or", W / 2 - 60 * s + kw + 8 * s, r.y + r.height + 18 * s, 15, ui::MUTED);
    ui::keycap("Esc", W / 2 - 60 * s + kw + 30 * s, r.y + r.height + 14 * s, 28 * s);
    ui::text("close", W / 2 + 30 * s + kw, r.y + r.height + 18 * s, 15, ui::MUTED);
}

// ------------------------------------------------------------------------------ top of screen

void GameClient::hudCompass() {
    if (!haveSelf_ || self_.mode == MoveMode::OnBus) return;
    float W = (float)GetScreenWidth();
    float s = ui::scale();
    float cw = 520 * s, ch = 34 * s;
    Rectangle r{W / 2 - cw / 2, 12 * s, cw, ch};
    DrawRectangleGradientH((int)r.x, (int)r.y, (int)(cw / 2), (int)ch, Color{0, 0, 0, 0}, Color{0, 0, 0, 120});
    DrawRectangleGradientH((int)(r.x + cw / 2), (int)r.y, (int)(cw / 2), (int)ch, Color{0, 0, 0, 120}, Color{0, 0, 0, 0});
    si::Vec3 look = dirFromAngles(camYaw_, 0);
    float heading = bearingOf(look.x, look.z);
    const float span = 75.0f; // degrees each side
    auto xFor = [&](float deg) {
        float d = deg - heading;
        while (d > 180) d -= 360;
        while (d < -180) d += 360;
        return std::fabs(d) > span ? -1.0f : r.x + cw / 2 + d / span * (cw / 2);
    };
    const char* card[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    for (int deg = 0; deg < 360; deg += 5) {
        float x = xFor((float)deg);
        if (x < 0) continue;
        float edge = 1.0f - std::fabs(x - (r.x + cw / 2)) / (cw / 2);
        unsigned char a = (unsigned char)(255 * clampf(edge * 1.6f, 0, 1));
        if (deg % 45 == 0) {
            bool main = deg % 90 == 0;
            ui::textCentered(card[deg / 45], x, r.y + 4 * s, main ? 17 : 14, deg == 0 ? Color{255, 214, 58, a} : Color{255, 255, 255, a});
        } else if (deg % 15 == 0) {
            DrawRectangle((int)x, (int)(r.y + 8 * s), std::max(1, (int)(2 * s)), (int)(8 * s), Color{255, 255, 255, (unsigned char)(a * 0.8f)});
            ui::textCentered(std::to_string(deg), x, r.y + 18 * s, 10, Color{255, 255, 255, (unsigned char)(a * 0.6f)});
        } else {
            DrawRectangle((int)x, (int)(r.y + 10 * s), 1, (int)(5 * s), Color{255, 255, 255, (unsigned char)(a * 0.4f)});
        }
    }
    // Safe zone marker
    if (g_.stormPhase >= 0) {
        si::Vec3 me = viewPos();
        float d = dist2d(me.xz(), g_.stormNext);
        if (d > g_.stormNextR * 0.5f) {
            float x = xFor(bearingOf(g_.stormNext.x - me.x, g_.stormNext.y - me.z));
            if (x >= 0) {
                DrawTriangle({x, r.y + ch + 6 * s}, {x + 6 * s, r.y + ch - 2 * s}, {x - 6 * s, r.y + ch - 2 * s}, WHITE);
            }
        }
    }
    // Centre notch and heading readout
    DrawTriangle({W / 2, r.y + ch - 6 * s}, {W / 2 - 6 * s, r.y + ch + 2 * s}, {W / 2 + 6 * s, r.y + ch + 2 * s}, ui::PLAY);
    ui::textCentered(std::to_string((int)std::round(heading) % 360), W / 2, r.y + ch + 4 * s, 13, ui::PLAY);
}

void GameClient::hudTopRight() {
    float W = (float)GetScreenWidth();
    float s = ui::scale();
    float mm = 232 * s;
    Rectangle mr{W - mm - 22 * s, 24 * s, mm, mm};
    hudMinimap(mr, 360);
    float y = mr.y + mr.height + 12 * s;
    // Chips row: alive, eliminations
    float cx = mr.x;
    cx += ui::chip(std::to_string(g_.alive), cx, y, 17, WHITE, kHudPanel, ui::Icon::Person, true) + 8 * s;
    cx += ui::chip(std::to_string(self_.kills), cx, y, 17, WHITE, kHudPanel, ui::Icon::Crosshair, true) + 8 * s;
    if (g_.teamSize > 1) ui::chip(g_.teamSize == 2 ? "DUOS" : "SQUADS", cx, y, 13, ui::MUTED, kHudPanel);
    y += 38 * s;
    // Storm timer card
    if (g_.stormPhase >= 0 && g_.phase != MatchPhase::Ended) {
        Rectangle sr{mr.x, y, mr.width, 52 * s};
        ui::rrect(sr, 12, kHudPanel);
        Color sc = g_.stormShrinking ? ui::STORM : WHITE;
        ui::icon(ui::Icon::Storm, sr.x + 24 * s, sr.y + 26 * s, 28 * s, sc);
        ui::text(g_.stormShrinking ? "STORM CLOSING" : "STORM MOVES IN", sr.x + 48 * s, sr.y + 8 * s, 12, ui::MUTED);
        ui::text(fmtTime(g_.stormTimer), sr.x + 48 * s, sr.y + 22 * s, 21, sc);
        ui::textRight("Phase " + std::to_string(g_.stormPhase + 1), sr.x + sr.width - 12 * s, sr.y + 8 * s, 12, ui::MUTED);
        if (localControllable()) {
            float d = dist2d(pred_.pos.xz(), g_.stormNext) - g_.stormNextR;
            if (d > 0) ui::textRight(std::to_string((int)d) + " m", sr.x + sr.width - 12 * s, sr.y + 26 * s, 17, ui::WARN);
            else ui::textRight("SAFE", sr.x + sr.width - 12 * s, sr.y + 26 * s, 15, ui::GOOD);
        }
        y += 60 * s;
    }
}

void GameClient::hudLocationBanner(float dt) {
    if (!haveSelf_ || !localControllable() || self_.mode == MoveMode::Skydive || self_.mode == MoveMode::Glide) { poiBanner_ = std::max(0.0f, poiBanner_ - dt); }
    else if (const POI* poi = map_.nearestPoi(viewPos().x, viewPos().z)) {
        bool inside = dist2d(poi->center, viewPos().xz()) < poi->radius;
        if (inside && poi->name != poiName_) { poiName_ = poi->name; poiBanner_ = 3.5f; }
        if (!inside && dist2d(poi->center, viewPos().xz()) > poi->radius * 1.4f) poiName_.clear();
    }
    poiBanner_ = std::max(0.0f, poiBanner_ - dt);
    if (poiBanner_ <= 0 || poiName_.empty()) return;
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    float s = ui::scale();
    float a = std::min(1.0f, std::min(poiBanner_, (3.5f - poiBanner_) * 3.0f));
    std::string up;
    for (char c : poiName_) up.push_back((char)std::toupper((unsigned char)c));
    float y = H * 0.2f - (1 - a) * 10 * s;
    float tw = ui::textWidth(up, 40);
    DrawRectangleGradientH((int)(W / 2 - tw), (int)(y - 6 * s), (int)tw, (int)(66 * s), Color{0, 0, 0, 0}, Color{0, 0, 0, (unsigned char)(110 * a)});
    DrawRectangleGradientH((int)(W / 2), (int)(y - 6 * s), (int)tw, (int)(66 * s), Color{0, 0, 0, (unsigned char)(110 * a)}, Color{0, 0, 0, 0});
    ui::textShadow(up, W / 2 - tw / 2, y, 40, ui::withAlpha(WHITE, a));
    ui::textCentered("NOW ENTERING", W / 2, y - 18 * s, 13, ui::withAlpha(ui::PLAY, a));
}

// ------------------------------------------------------------------------------ kill feed

void GameClient::hudKillfeed() {
    float s = ui::scale();
    float y = GetScreenHeight() * 0.34f;
    for (auto& k : killfeed_) {
        float a = clampf(k.t, 0, 1);
        float slide = (1 - clampf((6.0f - k.t) * 4, 0, 1)) * 30 * s;
        float w = ui::textWidth(k.text, 15) + 44 * s;
        Rectangle r{16 * s - slide, y, w, 28 * s};
        ui::rrect(r, 8, ui::withAlpha(k.mine ? Color{70, 50, 10, 255} : Color{8, 10, 18, 255}, 0.62f * a));
        if (k.mine) DrawRectangleRounded({r.x, r.y, 4 * s, r.height}, 1.0f, 4, ui::withAlpha(ui::PLAY, a));
        ui::icon(ui::Icon::Skull, r.x + 18 * s, r.y + r.height / 2, 16 * s, ui::withAlpha(k.mine ? ui::PLAY : WHITE, 0.9f * a));
        ui::text(k.text, r.x + 32 * s, r.y + 5 * s, 15, ui::withAlpha(k.mine ? ui::PLAY : WHITE, a));
        y += 32 * s;
    }
}

// ------------------------------------------------------------------------------ bottom HUD

void GameClient::hudBottom() {
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    float s = ui::scale();
    // --- Health / shield (bottom left)
    float bx = 26 * s, bw = 330 * s, bh = 24 * s;
    float by = H - 30 * s - bh * 2 - 8 * s;
    float shield = self_.shield, health = self_.flags & PF_DBNO ? self_.dbnoHealth : self_.health;
    Rectangle back{bx - 12 * s, by - 14 * s, bw + 80 * s, bh * 2 + 36 * s};
    DrawRectangleGradientH((int)back.x, (int)back.y, (int)back.width, (int)back.height, Color{0, 0, 0, 120}, Color{0, 0, 0, 0});
    auto barRow = [&](float y, float v, Color fill, ui::Icon ic, Color icCol) {
        ui::icon(ic, bx + 11 * s, y + bh / 2, 22 * s, icCol);
        Rectangle r{bx + 30 * s, y, bw, bh};
        ui::meter(r, v / 100.0f, fill, Color{0, 0, 0, 150}, 4);
        std::string num = std::to_string((int)std::round(v));
        ui::textShadow(num, r.x + 10 * s, y + 1 * s, 19, WHITE);
        ui::text("/100", r.x + 14 * s + ui::textWidth(num, 19), y + 5 * s, 13, ui::withAlpha(WHITE, 0.7f));
    };
    barRow(by, shield, ui::SHIELD, ui::Icon::Shield, Color{120, 190, 255, 255});
    bool low = health < 30;
    Color hc = self_.flags & PF_DBNO ? ui::BAD : (low ? mixc(ui::BAD, ui::HEALTH, 0.5f + 0.5f * std::sin(time_ * 8)) : ui::HEALTH);
    barRow(by + bh + 8 * s, health, hc, ui::Icon::Health, hc);
    if (self_.regenLeft > 0) ui::chip("Regenerating", bx, by - 36 * s, 13, ui::GOOD, kHudPanel, ui::Icon::Sparkle, true);
    if (self_.flags & PF_DBNO) ui::chip("KNOCKED - crawl to a teammate", bx, by - 36 * s, 14, WHITE, Color{160, 30, 30, 220}, ui::Icon::Skull, true);

    // --- Hotbar (bottom centre)
    float slot = 70 * s, gap = 8 * s;
    float total = 6 * slot + 5 * gap;
    float x0 = W / 2 - total / 2, y0 = H - slot - 22 * s;
    for (int i = 0; i < 6; i++) {
        bool sel = self_.selected == i && !self_.buildMode;
        float grow = sel ? 8 * s : 0;
        Rectangle r{x0 + i * (slot + gap) - grow / 2, y0 - grow - (sel ? 6 * s : 0), slot + grow, slot + grow};
        const ItemStack* st = i == 0 ? nullptr : &self_.inv[i - 1];
        bool has = st && !st->empty();
        Color rc = has ? C(rarityColor(st->rarity)) : Color{60, 66, 80, 255};
        if (sel) ui::shadow(r, 12, 10, 0.5f);
        if (i == 0) ui::rrectGrad(r, 12, Color{70, 76, 92, 225}, Color{40, 44, 56, 225});
        else if (has) ui::rrectGrad(r, 12, mixc(Color{20, 22, 30, 230}, rc, 0.35f), mixc(Color{20, 22, 30, 230}, rc, 0.95f));
        else ui::rrect(r, 12, Color{12, 16, 26, 150});
        ui::rrectLine(r, 12, sel ? 2.5f : 1.0f, sel ? WHITE : Color{255, 255, 255, (unsigned char)(has || i == 0 ? 50 : 22)});
        ui::text(slotKey(i), r.x + 7 * s, r.y + 4 * s, 12, ui::withAlpha(WHITE, 0.75f));
        if (i == 0) ui::icon(ui::Icon::Pickaxe, r.x + r.width / 2, r.y + r.height / 2 + 2 * s, r.width * 0.55f, WHITE);
        else if (has) {
            ui::itemIcon((unsigned char)st->type, r.x + r.width / 2, r.y + r.height / 2 - 2 * s, r.width * 0.78f, WHITE);
            const ItemDef& d = itemDef(st->type);
            std::string label;
            if (d.cls == ItemClass::Gun) label = st->type == ItemType::Minigun ? std::to_string(self_.ammo[(int)AmmoType::Light]) : std::to_string(st->clip);
            else if (st->count > 1) label = "x" + std::to_string(st->count);
            if (!label.empty()) ui::textRight(label, r.x + r.width - 6 * s, r.y + r.height - 20 * s, 14, WHITE);
        }
    }
    // Selected item name + ammo (hidden while driving a vehicle whose seat can't shoot)
    bool handsBusy = false;
    if (inVehicle()) {
        VehicleVisual vv;
        if (vehicleVisual(self_.vehicle, vv)) handsBusy = !vehicleDef(vv.type).seatShoot[self_.seat < MAX_SEATS ? self_.seat : 0];
    }
    if (self_.selected >= 1 && self_.selected <= 5 && !self_.buildMode && !handsBusy) {
        const ItemStack& st = self_.inv[self_.selected - 1];
        if (!st.empty()) {
            Color rc = C(rarityColor(st.rarity));
            std::string name = itemDisplayName(st);
            float ny = y0 - 44 * s;
            ui::textShadow(name, W / 2 - ui::textWidth(name, 17) / 2, ny, 17, rc);
            const ItemDef& d = itemDef(st.type);
            if (d.cls == ItemClass::Gun && st.type != ItemType::Minigun) {
                std::string clip = std::to_string(st.clip), reserve = " / " + std::to_string(self_.ammo[(int)d.ammo]);
                float cw = ui::textWidth(clip, 26) + ui::textWidth(reserve, 16);
                float ax = W / 2 - cw / 2 + 12 * s;
                ui::icon(ui::Icon::Bullet, ax - 16 * s, ny - 18 * s, 20 * s, WHITE);
                ui::textShadow(clip, ax, ny - 34 * s, 26, st.clip == 0 ? ui::BAD : WHITE);
                ui::text(reserve, ax + ui::textWidth(clip, 26), ny - 25 * s, 16, ui::withAlpha(WHITE, 0.75f));
            }
        }
    }
    // Build bar
    if (self_.buildMode) {
        const char* keys[] = {"Z", "X", "C", "V"};
        float bw2 = 84 * s;
        float bx2 = W / 2 - 2 * (bw2 + gap) + gap / 2;
        float by2 = y0 - 96 * s;
        for (int i = 0; i < 4; i++) {
            Rectangle r{bx2 + i * (bw2 + gap), by2, bw2, 66 * s};
            bool sel = self_.buildPiece == i;
            ui::rrectGrad(r, 12, sel ? Color{95, 176, 255, 235} : Color{16, 20, 32, 190}, sel ? Color{58, 136, 235, 235} : Color{10, 14, 24, 190});
            ui::rrectLine(r, 12, sel ? 2.0f : 1.0f, sel ? WHITE : Color{255, 255, 255, 30});
            // piece pictograms
            float cx = r.x + r.width / 2, cy = r.y + 26 * s;
            Color pc = sel ? Color{8, 24, 48, 255} : WHITE;
            if (i == 0) DrawRectangleRec({cx - 14 * s, cy - 12 * s, 28 * s, 22 * s}, pc);
            if (i == 1) DrawRectangleRec({cx - 18 * s, cy + 2 * s, 36 * s, 7 * s}, pc);
            if (i == 2) { DrawTriangle({cx - 18 * s, cy + 10 * s}, {cx + 18 * s, cy + 10 * s}, {cx + 18 * s, cy - 12 * s}, pc); }
            if (i == 3) { DrawTriangle({cx - 18 * s, cy + 10 * s}, {cx + 18 * s, cy + 10 * s}, {cx, cy - 12 * s}, pc); }
            ui::textCentered(PIECE_NAMES[i], cx, r.y + 42 * s, 13, sel ? Color{8, 24, 48, 255} : ui::withAlpha(WHITE, 0.85f));
            ui::keycap(keys[i], r.x + 4 * s, r.y + 4 * s, 18 * s);
        }
        std::string hint = std::string("Building with ") + MATERIAL_NAMES[self_.buildMat];
        float hy = by2 - 34 * s;
        float hw = ui::textWidth(hint, 15) + 40 * s;
        ui::rrect({W / 2 - hw / 2 - 150 * s, hy, hw + 300 * s, 28 * s}, 14, kHudPanel);
        ui::text(hint, W / 2 - hw / 2 - 136 * s, hy + 5 * s, 15, WHITE);
        float kx = W / 2 + hw / 2 - 110 * s;
        kx += ui::keycap("RMB", kx, hy + 3 * s, 22 * s) + 6 * s;
        ui::text("material", kx, hy + 5 * s, 13, ui::MUTED);
        kx += 62 * s;
        kx += ui::keycap("R", kx, hy + 3 * s, 22 * s) + 6 * s;
        ui::text("rotate", kx, hy + 5 * s, 13, ui::MUTED);
        kx += 50 * s;
        kx += ui::keycap("G", kx, hy + 3 * s, 22 * s) + 6 * s;
        ui::text("edit", kx, hy + 5 * s, 13, ui::MUTED);
    }
    // --- Materials & ammo (bottom right)
    float rx = W - 26 * s;
    float ry = H - 30 * s - 3 * 36 * s;
    ui::Icon matIcons[] = {ui::Icon::Wood, ui::Icon::Brick, ui::Icon::Metal};
    for (int m = 0; m < 3; m++) {
        bool sel = self_.buildMat == m;
        Rectangle r{rx - 150 * s, ry + m * 36 * s, 150 * s, 30 * s};
        ui::rrect(r, 10, sel && self_.buildMode ? ui::withAlpha(ui::ACCENT, 0.55f) : kHudPanel);
        if (sel && self_.buildMode) ui::rrectLine(r, 10, 1.5f, WHITE);
        ui::icon(matIcons[m], r.x + 18 * s, r.y + r.height / 2, 22 * s, WHITE);
        ui::text(MATERIAL_NAMES[m], r.x + 36 * s, r.y + 6 * s, 14, ui::withAlpha(WHITE, 0.85f));
        ui::textRight(std::to_string(self_.mats[m]), r.x + r.width - 10 * s, r.y + 4 * s, 18, WHITE);
    }
    float ay = ry - 28 * s;
    for (int a = (int)AmmoType::Count - 1; a >= 1; a--) {
        if (self_.ammo[a] == 0) continue;
        std::string txt = std::string(AMMO_NAMES[a]) + "  " + std::to_string(self_.ammo[a]);
        float tw = ui::textWidth(txt, 13) + 34 * s;
        Rectangle r{rx - tw, ay, tw, 22 * s};
        ui::rrect(r, 11, Color{10, 14, 24, 130});
        ui::icon(ui::Icon::Bullet, r.x + 12 * s, r.y + r.height / 2, 14 * s, WHITE);
        ui::text(txt, r.x + 24 * s, r.y + 3 * s, 13, ui::withAlpha(WHITE, 0.85f));
        ay -= 26 * s;
    }
}

// ------------------------------------------------------------------------------ centre HUD

void GameClient::hudCenter() {
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    float s = ui::scale();
    Vector2 c{W / 2, H / 2};
    // Crosshair with a dark outline for contrast
    if (localControllable() && !mapOpen_ && !inventoryOpen_) {
        const ItemStack* h = self_.selected >= 1 && self_.selected <= 5 ? &self_.inv[self_.selected - 1] : nullptr;
        float spread = 0.0f;
        bool gun = h && !h->empty() && itemDef(h->type).cls == ItemClass::Gun;
        if (gun) {
            WeaponStats ws = weaponStats(h->type, h->rarity);
            bool ads = IsMouseButtonDown(MOUSE_BUTTON_RIGHT);
            spread = (ads ? ws.spreadAds : ws.spreadHip) + self_.bloom;
            if (pred_.vel.lenXZ() > 1 && !ads) spread += 0.02f;
            if (pred_.mode == MoveMode::Air) spread += 0.035f;
        }
        float gap = 6 * s + spread * H * 1.2f;
        float len = 9 * s, th = std::max(2.0f, 2 * s);
        auto tick = [&](Rectangle q) {
            DrawRectangleRec({q.x - 1, q.y - 1, q.width + 2, q.height + 2}, Color{0, 0, 0, 120});
            DrawRectangleRec(q, Color{255, 255, 255, 235});
        };
        tick({c.x - th / 2, c.y - gap - len, th, len});
        tick({c.x - th / 2, c.y + gap, th, len});
        tick({c.x - gap - len, c.y - th / 2, len, th});
        tick({c.x + gap, c.y - th / 2, len, th});
        DrawCircleV(c, 2.2f * s, Color{0, 0, 0, 120});
        DrawCircleV(c, 1.4f * s, WHITE);
        if (hitMarker_ > 0) {
            Color hm = hitMarkerHead_ ? Color{255, 210, 60, 255} : WHITE;
            float o = 8 * s + (0.2f - std::min(0.2f, hitMarker_)) * 20 * s, l = 8 * s;
            for (int k = 0; k < 4; k++) {
                float sx = k % 2 ? 1.0f : -1.0f, sy = k < 2 ? -1.0f : 1.0f;
                DrawLineEx({c.x + sx * o, c.y + sy * o}, {c.x + sx * (o + l), c.y + sy * (o + l)}, 3.0f * s, hm);
            }
        }
    }
    // Damage numbers
    for (auto& d : dmgNumbers_) {
        Vector2 sp = GetWorldToScreen(V(d.p), cam_);
        if (sp.x < 0 || sp.y < 0 || sp.x > W || sp.y > H) continue;
        bool structure = d.flags & DF_STRUCTURE;
        Color col = (d.flags & DF_HEADSHOT) ? Color{255, 210, 60, 255} : (d.flags & DF_SHIELD) ? Color{120, 190, 255, 255} : structure ? Color{200, 200, 210, 255} : WHITE;
        float a = clampf(1.2f - d.t, 0, 1);
        col.a = (unsigned char)(255 * a);
        float pop = 1.0f + 0.35f * std::max(0.0f, 0.15f - d.t) / 0.15f;
        float sz = ((d.flags & DF_HEADSHOT) ? 30 : structure ? 18 : 24) * pop;
        ui::textShadow(std::to_string(d.amount), sp.x - ui::textWidth(std::to_string(d.amount), sz) / 2, sp.y, sz, col);
    }
    // Teammate names + health
    for (auto& [id, p] : players_) {
        if (!p.present || id == net_.playerId || p.team != self_.team || !(p.cur.flags & PF_ALIVE)) continue;
        si::Vec3 head = p.cur.pos + si::Vec3{0, 2.3f, 0};
        si::Vec3 toP = head - S(cam_.position);
        if (toP.dot(S(cam_.target) - S(cam_.position)) < 0) continue;
        Vector2 sp = GetWorldToScreen(V(head), cam_);
        bool knocked = p.cur.flags & PF_DBNO;
        float nw = ui::textWidth(p.name, 14) + 20 * s;
        ui::rrect({sp.x - nw / 2, sp.y - 24 * s, nw, 20 * s}, 10, Color{0, 0, 0, 140});
        ui::textCentered(p.name, sp.x, sp.y - 22 * s, 14, knocked ? ui::BAD : ui::GOOD);
        ui::meter({sp.x - 28 * s, sp.y, 56 * s, 5 * s}, p.cur.health / 100.0f, knocked ? ui::BAD : ui::HEALTH, Color{0, 0, 0, 150});
    }
    // Damage direction: red arcs around the crosshair
    for (auto& d : dmgIndicators_) {
        si::Vec3 to = d.from - viewPos();
        float ang = std::atan2(to.x, to.z) - camYaw_;
        float deg = -ang * RAD2DEG - 90;
        unsigned char a = (unsigned char)(220 * clampf(d.t, 0, 1));
        DrawRing(c, 110 * s, 122 * s, deg - 22, deg + 22, 24, Color{255, 50, 50, a});
        DrawRing(c, 122 * s, 124 * s, deg - 24, deg + 24, 24, Color{255, 180, 180, (unsigned char)(a / 2)});
    }
    // Interaction prompt: [E] keycap + label
    if (!interactPrompt_.empty() && self_.action == ACT_NONE) {
        std::string label = interactPrompt_;
        std::string key = "E";
        // Prompts are written as "E to ..." / "Hold E to ..." - show the key as a keycap
        bool hold = label.rfind("Hold E ", 0) == 0;
        if (label.rfind("E to ", 0) == 0) label = label.substr(5);
        else if (hold) label = "Hold to " + label.substr(10);
        float kh = 30 * s;
        float w = ui::textWidth(label, 17) + kh + 34 * s;
        Rectangle r{c.x + 44 * s, c.y + 34 * s, w, kh + 12 * s};
        ui::rrect(r, 12, Color{8, 12, 22, 190});
        ui::keycap(key, r.x + 6 * s, r.y + 6 * s, kh);
        ui::text(label, r.x + kh + 20 * s, r.y + r.height / 2 - 10 * s, 17, WHITE);
    }
    // Action progress ring
    if (self_.action != ACT_NONE && self_.actionTotal > 0) {
        const char* label = self_.action == ACT_RELOAD ? "Reloading" : self_.action == ACT_CONSUME ? "Using" : self_.action == ACT_REVIVE ? "Reviving" : "Opening";
        float t = clampf(self_.actionTime / self_.actionTotal, 0, 1);
        Vector2 rc{c.x, c.y + 88 * s};
        DrawCircleV(rc, 26 * s, Color{0, 0, 0, 120});
        ui::ring(rc, 26 * s, 5 * s, t, WHITE, Color{255, 255, 255, 40});
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%.1f", std::max(0.0f, self_.actionTotal - self_.actionTime));
        ui::textCentered(buf, rc.x, rc.y - 9 * s, 15, WHITE);
        ui::textCentered(label, rc.x, rc.y + 32 * s, 15, ui::withAlpha(WHITE, 0.9f));
    }
    // Messages (banner under the compass)
    float my = H * 0.13f + 40 * s;
    for (auto& m : messages_) {
        Color col = m.kind == 2 ? ui::STORM : m.kind == 3 ? ui::WARN : WHITE;
        float a = clampf(m.t, 0, 1);
        float tw = ui::textWidth(m.text, 19) + 40 * s;
        Rectangle r{W / 2 - tw / 2, my, tw, 34 * s};
        ui::rrect(r, 10, Color{8, 12, 22, (unsigned char)(170 * a)});
        DrawRectangleRounded({r.x, r.y, 4 * s, r.height}, 1.0f, 4, ui::withAlpha(col, a));
        ui::text(m.text, r.x + 22 * s, r.y + 6 * s, 19, ui::withAlpha(col, a));
        my += 40 * s;
    }
    // Phase banner / hints
    std::string banner, hintKey, hintText;
    if (g_.phase == MatchPhase::Warmup) banner = g_.phaseTimer < 999 ? "WARMUP  -  MATCH STARTS IN " + fmtTime(g_.phaseTimer) : "WARMUP  -  WAITING FOR PLAYERS";
    if (g_.phase == MatchPhase::Countdown) banner = "DROP SHIP BOARDING IN " + fmtTime(g_.phaseTimer);
    if (haveSelf_ && self_.mode == MoveMode::OnBus) { hintKey = "Space"; hintText = "Jump from the drop ship"; }
    if (haveSelf_ && self_.mode == MoveMode::Skydive) { hintKey = "Space"; hintText = "Open glider    W + look down to dive"; }
    float by = H * 0.085f;
    if (!banner.empty()) {
        float tw = ui::textWidth(banner, 17) + 44 * s;
        Rectangle r{W / 2 - tw / 2, by, tw, 32 * s};
        ui::rrect(r, 16, Color{8, 12, 22, 170});
        ui::rrectLine(r, 16, 1.0f, ui::withAlpha(ui::PLAY, 0.6f));
        ui::textCentered(banner, W / 2, r.y + 6 * s, 17, ui::PLAY);
        by += 40 * s;
    }
    if (!hintKey.empty()) {
        float kh = 32 * s;
        float tw = ui::textWidth(hintText, 19) + 100 * s;
        Rectangle r{W / 2 - tw / 2, H * 0.72f, tw, kh + 14 * s};
        ui::rrect(r, 14, Color{8, 12, 22, 180});
        float kw = ui::keycap(hintKey, r.x + 8 * s, r.y + 7 * s, kh);
        ui::text(hintText, r.x + kw + 22 * s, r.y + r.height / 2 - 11 * s, 19, WHITE);
    }
    // Spectating bar
    if (const RemotePlayer* t = spectateTarget()) {
        float bw = 420 * s;
        Rectangle r{W / 2 - bw / 2, H - 120 * s, bw, 70 * s};
        ui::card(r, 14, Color{12, 16, 28, 220});
        ui::text("SPECTATING", r.x + 18 * s, r.y + 10 * s, 12, ui::MUTED);
        ui::text(t->name, r.x + 18 * s, r.y + 26 * s, 22, WHITE);
        ui::meter({r.x + 220 * s, r.y + 16 * s, 180 * s, 12 * s}, t->cur.shield / 100.0f, ui::SHIELD, Color{0, 0, 0, 150});
        ui::meter({r.x + 220 * s, r.y + 34 * s, 180 * s, 12 * s}, t->cur.health / 100.0f, ui::HEALTH, Color{0, 0, 0, 150});
        float kw = ui::keycap("Space", r.x + 220 * s, r.y + 50 * s, 18 * s);
        ui::text("next player", r.x + 226 * s + kw, r.y + 50 * s, 12, ui::MUTED);
    }
}

// ------------------------------------------------------------------------------ overlays

void GameClient::hudInventory() {
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    float s = ui::scale();
    DrawRectangle(0, 0, (int)W, (int)H, Color{4, 6, 12, 180});
    ui::icon(ui::Icon::Backpack, W / 2, H * 0.17f, 40 * s, ui::PLAY);
    ui::textCentered("INVENTORY", W / 2, H * 0.17f + 30 * s, 32, WHITE);
    ui::textCentered("Click a slot, then another to swap them. Right-click to drop.", W / 2, H * 0.17f + 76 * s, 16, ui::MUTED);
    float slot = 132 * s, gap = 18 * s;
    float x0 = W / 2 - (5 * slot + 4 * gap) / 2, y0 = H * 0.34f;
    for (int i = 0; i < 5; i++) {
        Rectangle r{x0 + i * (slot + gap), y0, slot, slot * 1.18f};
        const ItemStack& st = self_.inv[i];
        bool h = ui::hovered(r);
        Color rc = st.empty() ? Color{60, 66, 80, 255} : C(rarityColor(st.rarity));
        if (h) ui::shadow(r, 14, 12, 0.5f);
        if (st.empty()) ui::rrect(r, 14, Color{20, 24, 36, 220});
        else ui::rrectGrad(r, 14, mixc(Color{20, 22, 30, 240}, rc, 0.3f), mixc(Color{20, 22, 30, 240}, rc, 0.95f));
        bool picked = invDragSlot_ == i + 1;
        ui::rrectLine(r, 14, picked ? 3.0f : (h ? 2.0f : 1.0f), picked ? ui::PLAY : (h ? WHITE : Color{255, 255, 255, 40}));
        ui::keycap(std::to_string(i + 1), r.x + 8 * s, r.y + 8 * s, 22 * s);
        if (!st.empty()) {
            ui::itemIcon((unsigned char)st.type, r.x + r.width / 2, r.y + r.height * 0.42f, slot * 0.8f, WHITE);
            std::string n = itemDisplayName(st);
            auto lines = ui::wrap(n, r.width - 16 * s, 14);
            for (size_t li = 0; li < lines.size() && li < 2; li++) ui::textCentered(lines[li], r.x + r.width / 2, r.y + r.height - (44 - li * 18.0f) * s, 14, WHITE);
            if (st.count > 1) ui::textRight("x" + std::to_string(st.count), r.x + r.width - 10 * s, r.y + 10 * s, 15, WHITE);
        } else {
            ui::textCentered("Empty", r.x + r.width / 2, r.y + r.height / 2 - 8 * s, 15, ui::MUTED);
        }
        if (h && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (invDragSlot_ < 0) invDragSlot_ = i + 1;
            else {
                if (invDragSlot_ != i + 1) pushAction(ActionType::SwapSlots, (uint8_t)invDragSlot_, (uint8_t)(i + 1));
                invDragSlot_ = -1;
            }
        }
        if (h && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) && !st.empty()) pushAction(ActionType::DropSlot, (uint8_t)(i + 1));
    }
    // Materials: drop buttons with icons
    float my = y0 + slot * 1.18f + 40 * s;
    ui::Icon matIcons[] = {ui::Icon::Wood, ui::Icon::Brick, ui::Icon::Metal};
    for (int m = 0; m < 3; m++) {
        Rectangle r{W / 2 - 270 * s + m * 185 * s, my, 170 * s, 50 * s};
        bool en = self_.mats[m] > 0;
        if (ui::button(r, "", false, en)) pushAction(ActionType::DropMats, (uint8_t)m, 3);
        ui::icon(matIcons[m], r.x + 24 * s, r.y + r.height / 2, 24 * s, en ? WHITE : ui::MUTED);
        ui::text("Drop 30", r.x + 46 * s, r.y + 6 * s, 15, en ? WHITE : ui::MUTED);
        ui::text(std::to_string(self_.mats[m]) + " " + MATERIAL_NAMES[m], r.x + 46 * s, r.y + 26 * s, 12, ui::MUTED);
    }
    float kw = ui::keycap("Tab", W / 2 - 40 * s, my + 80 * s, 26 * s);
    ui::text("close", W / 2 - 32 * s + kw, my + 83 * s, 15, ui::MUTED);
}

void GameClient::hudEmoteWheel() {
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    float s = ui::scale();
    Vector2 c{W / 2, H / 2};
    DrawCircleV(c, 210 * s, Color{6, 9, 18, 170});
    DrawRing(c, 206 * s, 210 * s, 0, 360, 64, Color{255, 255, 255, 30});
    auto it = players_.find(net_.playerId);
    Loadout l = it != players_.end() ? it->second.loadout : Loadout();
    Vector2 mouse = GetMousePosition();
    float mang = std::atan2(mouse.y - c.y, mouse.x - c.x);
    for (int i = 0; i < 6; i++) {
        float a = i * 2 * kPi / 6 - kPi / 2;
        Vector2 p{c.x + std::cos(a) * 132 * s, c.y + std::sin(a) * 132 * s};
        float da = std::fabs(wrapAngle(mang - a));
        bool hl = da < kPi / 6 && !IsCursorHidden();
        const CosmeticDef* e = findCosmetic(l.emotes[i]);
        std::string name = e ? e->name : "Empty";
        DrawRing(c, 80 * s, 200 * s, a * RAD2DEG - 28, a * RAD2DEG + 28, 16, hl ? Color{95, 176, 255, 120} : Color{255, 255, 255, 14});
        ui::keycap(std::to_string(i + 1), p.x - 12 * s, p.y - 38 * s, 24 * s);
        ui::icon(ui::Icon::Star, p.x, p.y + 2 * s, 22 * s, e && e->id != "emote_none" ? ui::PLAY : ui::MUTED);
        ui::textCentered(name, p.x, p.y + 18 * s, 14, WHITE);
        if (IsKeyPressed(KEY_ONE + i) && e && e->id != "emote_none") pushAction(ActionType::Emote, (uint8_t)i);
    }
    DrawCircleV(c, 74 * s, Color{12, 16, 28, 240});
    ui::textCentered("EMOTE", c.x, c.y - 18 * s, 18, WHITE);
    ui::textCentered("press 1-6", c.x, c.y + 4 * s, 13, ui::MUTED);
}

void GameClient::hudPause() {
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    float s = ui::scale();
    DrawRectangle(0, 0, (int)W, (int)H, Color{4, 6, 12, 190});
    Rectangle p{W / 2 - 240 * s, H / 2 - 230 * s, 480 * s, 460 * s};
    ui::card(p, 18, Color{18, 23, 38, 250});
    ui::icon(ui::Icon::Gear, p.x + 34 * s, p.y + 36 * s, 26 * s, ui::PLAY);
    ui::text("PAUSED", p.x + 60 * s, p.y + 20 * s, 28, WHITE);
    ui::text("The match keeps running", p.x + 60 * s, p.y + 54 * s, 13, ui::MUTED);
    float x = p.x + 32 * s, w = p.width - 64 * s, y = p.y + 96 * s;
    float sens = settings_.sensitivity * 1000;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f", sens);
    ui::text("Mouse sensitivity", x, y, 16, ui::MUTED);
    ui::textRight(buf, x + w, y, 16, WHITE);
    if (ui::slider({x, y + 26 * s, w, 24 * s}, sens, 0.5f, 8.0f, 901)) settings_.sensitivity = sens / 1000;
    y += 66 * s;
    ui::text("Field of view", x, y, 16, ui::MUTED);
    ui::textRight(std::to_string((int)settings_.fov), x + w, y, 16, WHITE);
    ui::slider({x, y + 26 * s, w, 24 * s}, settings_.fov, 60, 110, 902);
    y += 66 * s;
    ui::text("Volume", x, y, 16, ui::MUTED);
    ui::textRight(std::to_string((int)std::round(settings_.volume * 100)) + "%", x + w, y, 16, WHITE);
    if (ui::slider({x, y + 26 * s, w, 24 * s}, settings_.volume, 0, 1, 903)) audio_.masterVolume = settings_.volume;
    y += 72 * s;
    ui::checkbox({x, y, w / 2, 28 * s}, settings_.shadows, "Shadows");
    ui::checkbox({x + w / 2, y, w / 2, 28 * s}, settings_.showFps, "Show FPS");
    if (ui::button({x, p.y + p.height - 78 * s, w * 0.48f, 52 * s}, "Resume", true)) paused_ = false;
    if (ui::button({x + w * 0.52f, p.y + p.height - 78 * s, w * 0.48f, 52 * s}, "Leave match")) leave_ = true;
}

void GameClient::hudResults() {
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    float s = ui::scale();
    DrawRectangleGradientV(0, 0, (int)W, (int)H, Color{0, 0, 0, (unsigned char)(result_.won ? 40 : 90)}, Color{0, 0, 0, 200});
    float y = H * 0.16f;
    if (result_.won) {
        // Ribbon with rays
        float t = time_;
        for (int i = 0; i < 16; i++) {
            float a = t * 10 + i * 22.5f;
            DrawCircleSector({W / 2, y + 60 * s}, H * 0.5f, a, a + 8, 4, Color{255, 214, 58, 16});
        }
        ui::icon(ui::Icon::Trophy, W / 2, y, 70 * s, ui::PLAY);
        ui::textShadow("#1", W / 2 - ui::textWidth("#1", 40) / 2, y + 44 * s, 40, WHITE);
        std::string v = "STORM SURVIVOR";
        ui::textShadow(v, W / 2 - ui::textWidth(v, 64) / 2, y + 90 * s, 64, ui::PLAY);
    } else {
        std::string t = "#" + std::to_string(result_.placement);
        ui::textShadow(t, W / 2 - ui::textWidth(t, 84) / 2, y + 10 * s, 84, WHITE);
        std::string sub = result_.players > 0 ? "OUT OF " + std::to_string(result_.players) : "ELIMINATED";
        ui::textCentered(sub, W / 2, y + 112 * s, 20, ui::MUTED);
    }
    // Stat tiles
    struct Tile { ui::Icon ic; const char* label; std::string value; Color c; } tiles[] = {
        {ui::Icon::Trophy, "Placement", "#" + std::to_string(std::max(1, result_.placement)), ui::PLAY},
        {ui::Icon::Crosshair, "Eliminations", std::to_string(result_.kills), ui::BAD},
        {ui::Icon::Bolt, "Damage dealt", std::to_string((int)result_.damage), ui::ACCENT}};
    float tw = 210 * s, gap = 16 * s;
    float tx = W / 2 - (3 * tw + 2 * gap) / 2, ty = y + 190 * s;
    for (int i = 0; i < 3; i++) {
        Rectangle r{tx + i * (tw + gap), ty, tw, 96 * s};
        ui::card(r, 14, Color{16, 21, 34, 235});
        ui::icon(tiles[i].ic, r.x + 30 * s, r.y + 30 * s, 26 * s, tiles[i].c);
        ui::text(tiles[i].label, r.x + 54 * s, r.y + 20 * s, 14, ui::MUTED);
        ui::text(tiles[i].value, r.x + 20 * s, r.y + 50 * s, 30, WHITE);
    }
    float byy = ty + 130 * s;
    if (ui::button({W / 2 - 210 * s, byy, 200 * s, 54 * s}, "Return to lobby", true)) leave_ = true;
    if (!result_.won && g_.phase != MatchPhase::Ended) {
        if (ui::button({W / 2 + 10 * s, byy, 200 * s, 54 * s}, "Spectate")) { result_.has = false; }
    }
}

void GameClient::hudVehicle() {
    if (!inVehicle()) return;
    VehicleVisual vv;
    if (!vehicleVisual(self_.vehicle, vv)) return;
    const VehicleDef& d = vehicleDef(vv.type);
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    float s = ui::scale();
    float pw = 340 * s, ph = 104 * s;
    bool canShoot = d.seatShoot[self_.seat < MAX_SEATS ? self_.seat : 0];
    Rectangle r{W / 2 - pw / 2, H - 70 * s - 22 * s - ph - (canShoot ? 90 : 14) * s, pw, ph};
    ui::card(r, 16, Color{12, 16, 28, 215});
    ui::icon(ui::Icon::Car, r.x + 28 * s, r.y + 26 * s, 26 * s, ui::PLAY);
    ui::text(d.name, r.x + 50 * s, r.y + 12 * s, 19, WHITE);
    const char* role = self_.seat == 0 ? (d.seatPose[0] == SEAT_STAND ? "RIDER" : "DRIVER") : "PASSENGER";
    ui::chip(role, r.x + 58 * s + ui::textWidth(d.name, 19), r.y + 13 * s, 11, ui::INK, ui::PLAY);
    // Speed readout
    float kmh = (driving_ ? predVeh_.vel.lenXZ() : vv.speed) * 3.6f;
    std::string sp = std::to_string((int)std::round(kmh));
    ui::textRight(sp, r.x + r.width - 52 * s, r.y + 8 * s, 30, WHITE);
    ui::text("km/h", r.x + r.width - 46 * s, r.y + 20 * s, 13, ui::MUTED);
    if (trickTotal_ > 0) ui::textRight("TRICKS " + std::to_string(trickTotal_), r.x + r.width - 18 * s, r.y + r.height - 24 * s, 12, ui::PLAY);
    // Hull + boost meters
    float bx = r.x + 18 * s, by = r.y + 52 * s, bh = 10 * s;
    bool boost = self_.seat == 0 && d.boostSpeed > 0 && driving_;
    float bw = boost ? r.width * 0.5f - 24 * s : r.width - 36 * s;
    Color hc = vv.hp > 0.5f ? ui::HEALTH : vv.hp > 0.25f ? ui::WARN : ui::BAD;
    ui::meter({bx, by, bw, bh}, vv.hp, hc, Color{0, 0, 0, 150}, 5);
    ui::text("HULL", bx, by + bh + 3 * s, 11, ui::MUTED);
    if (boost) {
        float bx2 = r.x + r.width * 0.5f + 6 * s, bw2 = r.width * 0.5f - 24 * s;
        Color bc = predVeh_.boosting ? Color{255, 170, 60, 255} : ui::SHIELD;
        ui::meter({bx2, by, bw2, bh}, predVeh_.boost, bc, Color{0, 0, 0, 150});
        ui::icon(ui::Icon::Bolt, bx2 - 2 * s + bw2, by + bh + 10 * s, 12 * s, bc);
        ui::text("BOOST", bx2, by + bh + 3 * s, 11, ui::MUTED);
    }
    // Controls as keycaps
    float kx = r.x + 18 * s, ky = r.y + r.height - 26 * s, kh = 18 * s;
    auto hint = [&](const char* key, const char* what) {
        kx += ui::keycap(key, kx, ky, kh) + 5 * s;
        ui::text(what, kx, ky + 1 * s, 12, ui::withAlpha(WHITE, 0.75f));
        kx += ui::textWidth(what, 12) + 12 * s;
    };
    hint("E", "exit");
    if (d.seats > 1) hint("C", "seat");
    if (self_.seat == 0) {
        if (d.boostSpeed > 0) hint("Shift", "boost");
        if (d.jumpVel > 0) hint("Space", "jump");
        if (!d.seatShoot[0]) hint("LMB", "horn");
    }
}

// Live air readout while driving, combo timer, and landed-trick popups.
void GameClient::hudTricks() {
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    float s = ui::scale();
    if (driving_ && !predVeh_.onGround && predVeh_.airTime > 0.3f) {
        float air = predVeh_.airTime;
        int halves = (int)((predVeh_.airSpin + 0.45f) / kPi);
        int flips = (int)std::round(predVeh_.flip / (2 * kPi));
        float rem = predVeh_.flip - std::round(predVeh_.flip / (2 * kPi)) * 2 * kPi;
        char nb[64];
        std::string name = trickName(halves, flips, air, nb, sizeof(nb));
        char ab[32];
        std::snprintf(ab, sizeof(ab), "%.1fs", air);
        float y = H * 0.6f;
        float tw = std::max(ui::textWidth(name, 26), 200 * s) + 60 * s;
        Rectangle r{W / 2 - tw / 2, y, tw, 74 * s};
        ui::rrect(r, 14, Color{8, 12, 22, 170});
        ui::textCentered(name, W / 2, r.y + 8 * s, 26, ui::PLAY);
        ui::textCentered(std::string("AIR ") + ab, W / 2, r.y + 44 * s, 15, WHITE);
        bool danger = std::fabs(rem) > 0.95f && predVeh_.vel.y < 0;
        float hy = r.y + r.height + 8 * s;
        if (danger) {
            ui::textCentered("LEVEL OUT!", W / 2, hy, 20, ui::BAD);
        } else {
            float kx = W / 2 - 150 * s;
            kx += ui::keycap("A", kx, hy, 22 * s) + 4 * s;
            kx += ui::keycap("D", kx, hy, 22 * s) + 6 * s;
            ui::text("spin", kx, hy + 3 * s, 13, ui::withAlpha(WHITE, 0.8f));
            kx += 44 * s;
            kx += ui::keycap("Ctrl", kx, hy, 22 * s) + 4 * s;
            ui::text("+", kx, hy + 2 * s, 14, WHITE);
            kx += 14 * s;
            kx += ui::keycap("W", kx, hy, 22 * s) + 4 * s;
            kx += ui::keycap("S", kx, hy, 22 * s) + 6 * s;
            ui::text("flip", kx, hy + 3 * s, 13, ui::withAlpha(WHITE, 0.8f));
        }
    } else if (driving_ && predVeh_.comboTimer > 0 && predVeh_.combo > 0) {
        float y = H * 0.66f;
        std::string c = "COMBO x" + std::to_string(predVeh_.combo + 1) + " - land another trick!";
        ui::textCentered(c, W / 2, y, 16, ui::PLAY);
        ui::meter({W / 2 - 110 * s, y + 24 * s, 220 * s, 6 * s}, predVeh_.comboTimer / 3.0f, ui::PLAY, Color{0, 0, 0, 140});
    }
    // Popups (newest at the bottom of the stack)
    float py = H * 0.3f;
    for (auto& t : tricks_) {
        float a = clampf(1.0f - (t.t - 1.8f) / 0.8f, 0, 1);
        float pop = 1.0f + 0.5f * std::max(0.0f, 0.18f - t.t) / 0.18f;
        Color col = t.bailed ? ui::BAD : ui::PLAY;
        float sz = 34 * pop;
        ui::textShadow(t.name, W / 2 - ui::textWidth(t.name, sz) / 2, py - t.t * 12 * s, sz, ui::withAlpha(col, a));
        if (!t.bailed) {
            std::string sc = "+" + std::to_string(t.score) + (t.combo > 1 ? "   COMBO x" + std::to_string(t.combo) : "");
            ui::textShadow(sc, W / 2 - ui::textWidth(sc, 22) / 2, py + 44 * s * pop - t.t * 12 * s, 22, ui::withAlpha(WHITE, a));
        }
        py += 86 * s;
    }
}

void GameClient::renderHud() {
    ui::beginFrame();
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    float s = ui::scale();
    // Storm / damage / low-health vignettes
    if (localControllable() && g_.stormPhase >= 0 && dist2d(pred_.pos.xz(), g_.stormCur) > g_.stormCurR) {
        DrawRectangle(0, 0, (int)W, (int)H, Color{110, 40, 180, 60});
        DrawRectangleGradientV(0, 0, (int)W, (int)(H * 0.25f), Color{120, 50, 200, 90}, Color{0, 0, 0, 0});
    }
    if (damageFlash_ > 0) {
        unsigned char a = (unsigned char)(170 * damageFlash_ / 0.35f);
        DrawRectangleGradientH(0, 0, (int)(W * 0.15f), (int)H, Color{200, 20, 20, a}, Color{0, 0, 0, 0});
        DrawRectangleGradientH((int)(W * 0.85f), 0, (int)(W * 0.15f), (int)H, Color{0, 0, 0, 0}, Color{200, 20, 20, a});
    }
    if (localControllable() && self_.health < 30 && !(self_.flags & PF_DBNO)) {
        unsigned char a = (unsigned char)(60 + 40 * std::sin(time_ * 5));
        DrawRectangleGradientV(0, (int)(H * 0.75f), (int)W, (int)(H * 0.25f), Color{0, 0, 0, 0}, Color{180, 20, 20, a});
    }

    hudCenter();
    hudKillfeed();
    hudCompass();
    hudLocationBanner(GetFrameTime());
    if (localControllable() || (haveSelf_ && self_.mode == MoveMode::OnBus)) hudBottom();
    if (localControllable()) hudVehicle();
    if (localControllable()) hudTricks();
    hudTopRight();
    if (settings_.showFps) ui::text(std::to_string(GetFPS()) + " FPS", 10 * s, 8 * s, 13, ui::withAlpha(WHITE, 0.6f));

    if (emoteWheel_) hudEmoteWheel();
    if (inventoryOpen_) hudInventory();
    if (mapOpen_) hudFullMap();
    if (result_.has && !(self_.flags & PF_ALIVE) && !paused_) hudResults();
    else if (result_.has && result_.won && !paused_) hudResults();
    if (paused_) hudPause();
}

} // namespace client
