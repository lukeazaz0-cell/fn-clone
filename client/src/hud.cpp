#include <algorithm>
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

std::string shortName(const ItemStack& s) {
    switch (s.type) {
        case ItemType::AssaultRifle: return "AR";
        case ItemType::BurstRifle: return "BURST";
        case ItemType::ScopedRifle: return "SCOPED";
        case ItemType::PumpShotgun: return "PUMP";
        case ItemType::TacticalShotgun: return "TAC";
        case ItemType::SMG: return "SMG";
        case ItemType::Pistol: return "PISTOL";
        case ItemType::SniperRifle: return "SNIPER";
        case ItemType::RocketLauncher: return "ROCKET";
        case ItemType::Minigun: return "MINIGUN";
        case ItemType::Grenade: return "GRENADE";
        case ItemType::ImpulseGrenade: return "IMPULSE";
        case ItemType::Bandages: return "BANDAGE";
        case ItemType::Medkit: return "MEDKIT";
        case ItemType::SmallShield: return "SM SHIELD";
        case ItemType::ShieldPotion: return "SHIELD";
        case ItemType::RegenSoda: return "SODA";
        case ItemType::MegaFlask: return "FLASK";
        case ItemType::LaunchPad: return "LAUNCH";
        case ItemType::HeavyRifle: return "HEAVY AR";
        case ItemType::CompactSMG: return "C-SMG";
        case ItemType::DoubleBarrel: return "DOUBLE";
        case ItemType::HeavyShotgun: return "HEAVY SG";
        case ItemType::HuntingRifle: return "HUNTING";
        case ItemType::HandCannon: return "CANNON";
        case ItemType::GrenadeLauncher: return "GL";
        case ItemType::Crossbow: return "XBOW";
        case ItemType::StickyCharge: return "STICKY";
        case ItemType::FieldKit: return "FIELD KIT";
        default: return "";
    }
}

} // namespace

Vector2 GameClient::worldToMap(const si::Vec3& p, Rectangle r, si::Vec3 center, float meters, bool whole) const {
    if (whole) return {r.x + p.x / WORLD_SIZE * r.width, r.y + p.z / WORLD_SIZE * r.height};
    return {r.x + r.width / 2 + (p.x - center.x) / meters * r.width, r.y + r.height / 2 + (p.z - center.z) / meters * r.height};
}

void GameClient::hudMinimap(Rectangle r, float meters) {
    si::Vec3 me = viewPos();
    // Source rect in texture space
    float texPerMeter = worldR_.minimapSize / WORLD_SIZE;
    Rectangle src{(me.x - meters / 2) * texPerMeter, (me.z - meters / 2) * texPerMeter, meters * texPerMeter, meters * texPerMeter};
    DrawRectangleRec(r, Color{40, 110, 170, 255});
    BeginScissorMode((int)r.x, (int)r.y, (int)r.width, (int)r.height);
    DrawTexturePro(worldR_.minimap, src, r, {0, 0}, 0, WHITE);
    float scale = r.width / meters;
    if (g_.stormPhase >= 0 && g_.stormCurR < 3000) {
        Vector2 c = worldToMap({g_.stormCur.x, 0, g_.stormCur.y}, r, me, meters, false);
        DrawCircleLinesV(c, g_.stormCurR * scale, Color{190, 90, 255, 255});
        Vector2 n = worldToMap({g_.stormNext.x, 0, g_.stormNext.y}, r, me, meters, false);
        DrawCircleLinesV(n, g_.stormNextR * scale, WHITE);
    }
    for (auto& [id, p] : players_) {
        if (!p.present || id == net_.playerId || p.team != self_.team || !(p.cur.flags & PF_ALIVE)) continue;
        Vector2 v = worldToMap(p.cur.pos, r, me, meters, false);
        DrawCircleV(v, 4 * ui::scale(), ui::GOOD);
    }
    // Vehicles: small rounded markers (occupied ones are dimmed)
    for (auto& [id, v] : vehicles_) {
        if (!v.present) continue;
        Vector2 m = worldToMap(v.cur.pos, r, me, meters, false);
        float vs = 3.5f * ui::scale();
        Color vc = (v.cur.flags & VF_DRIVER) ? Color{200, 200, 200, 140} : Color{255, 214, 90, 230};
        DrawRectangleRounded({m.x - vs, m.y - vs * 0.7f, vs * 2, vs * 1.4f}, 0.5f, 4, Color{20, 20, 26, 200});
        DrawRectangleRounded({m.x - vs + 1, m.y - vs * 0.7f + 1, vs * 2 - 2, vs * 1.4f - 2}, 0.5f, 4, vc);
    }
    for (auto& poi : map_.pois) {
        if (!poi.major) continue;
        Vector2 v = worldToMap({poi.center.x, 0, poi.center.y}, r, me, meters, false);
        ui::textCentered(poi.name, v.x, v.y - 6 * ui::scale(), 12, ui::withAlpha(WHITE, 0.85f));
    }
    // Player arrow
    Vector2 c{r.x + r.width / 2, r.y + r.height / 2};
    float yaw = camYaw_;
    Vector2 f{std::sin(yaw), std::cos(yaw)}, rt{-std::cos(yaw), std::sin(yaw)};
    float s = 8 * ui::scale();
    DrawTriangle({c.x + f.x * s * 1.4f, c.y + f.y * s * 1.4f}, {c.x - f.x * s - rt.x * s * 0.8f, c.y - f.y * s - rt.y * s * 0.8f},
                 {c.x - f.x * s + rt.x * s * 0.8f, c.y - f.y * s + rt.y * s * 0.8f}, ui::GOLDEN);
    DrawTriangle({c.x + f.x * s * 1.4f, c.y + f.y * s * 1.4f}, {c.x - f.x * s + rt.x * s * 0.8f, c.y - f.y * s + rt.y * s * 0.8f},
                 {c.x - f.x * s - rt.x * s * 0.8f, c.y - f.y * s - rt.y * s * 0.8f}, ui::GOLDEN);
    EndScissorMode();
    DrawRectangleLinesEx(r, 2, ui::withAlpha(WHITE, 0.6f));
}

void GameClient::hudFullMap() {
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    DrawRectangle(0, 0, (int)W, (int)H, Color{0, 0, 0, 170});
    float size = std::min(W, H) * 0.88f;
    Rectangle r{W / 2 - size / 2, H / 2 - size / 2, size, size};
    DrawTexturePro(worldR_.minimap, {0, 0, (float)worldR_.minimapSize, (float)worldR_.minimapSize}, r, {0, 0}, 0, WHITE);
    // Grid
    for (int i = 1; i < 10; i++) {
        DrawLineV({r.x + r.width * i / 10, r.y}, {r.x + r.width * i / 10, r.y + r.height}, Color{255, 255, 255, 30});
        DrawLineV({r.x, r.y + r.height * i / 10}, {r.x + r.width, r.y + r.height * i / 10}, Color{255, 255, 255, 30});
    }
    for (int i = 0; i < 10; i++) {
        ui::text(std::string(1, (char)('A' + i)), r.x + r.width * (i + 0.5f) / 10 - 4, r.y - 18 * ui::scale(), 14, ui::MUTED);
        ui::text(std::to_string(i + 1), r.x - 18 * ui::scale(), r.y + r.height * (i + 0.5f) / 10 - 7, 14, ui::MUTED);
    }
    float scale = r.width / WORLD_SIZE;
    if (g_.busActive) {
        Vector2 a = worldToMap(g_.busStart, r, {}, 0, true), b = worldToMap(g_.busEnd, r, {}, 0, true);
        DrawLineEx(a, b, 3, Color{255, 255, 255, 180});
        Vector2 cur = worldToMap(lerp3(g_.busStart, g_.busEnd, clampf(g_.busProgress, 0, 1)), r, {}, 0, true);
        DrawCircleV(cur, 7, Color{80, 150, 240, 255});
    }
    if (g_.stormPhase >= 0 && g_.stormCurR < 3000) {
        Vector2 c = worldToMap({g_.stormCur.x, 0, g_.stormCur.y}, r, {}, 0, true);
        DrawRing(c, g_.stormCurR * scale, g_.stormCurR * scale + 3, 0, 360, 96, Color{190, 90, 255, 255});
        Vector2 n = worldToMap({g_.stormNext.x, 0, g_.stormNext.y}, r, {}, 0, true);
        DrawRing(n, g_.stormNextR * scale, g_.stormNextR * scale + 2, 0, 360, 96, WHITE);
    }
    for (auto& poi : map_.pois) {
        Vector2 v = worldToMap({poi.center.x, 0, poi.center.y}, r, {}, 0, true);
        float sz = poi.major ? 17 : 12;
        ui::textShadow(poi.name, v.x - ui::textWidth(poi.name, sz) / 2, v.y - 8, sz, poi.major ? WHITE : ui::withAlpha(WHITE, 0.75f));
    }
    for (auto& [id, p] : players_) {
        if (!p.present || id == net_.playerId || p.team != self_.team || !(p.cur.flags & PF_ALIVE)) continue;
        DrawCircleV(worldToMap(p.cur.pos, r, {}, 0, true), 6, ui::GOOD);
    }
    Vector2 me = worldToMap(viewPos(), r, {}, 0, true);
    DrawCircleV(me, 8, ui::GOLDEN);
    DrawCircleLinesV(me, 8, BLACK);
    ui::textCentered("M or Esc to close", W / 2, r.y + r.height + 8 * ui::scale(), 16, ui::MUTED);
}

void GameClient::hudKillfeed() {
    float y = GetScreenHeight() * 0.36f;
    for (auto& k : killfeed_) {
        float a = clampf(k.t, 0, 1);
        float w = ui::textWidth(k.text, 16) + 16 * ui::scale();
        DrawRectangle(ui::px(12), (int)y, (int)w, ui::px(24), ui::withAlpha(k.mine ? Color{60, 40, 20, 255} : Color{0, 0, 0, 255}, 0.45f * a));
        ui::text(k.text, ui::px(20), y + ui::px(4), 16, ui::withAlpha(k.mine ? ui::GOLDEN : WHITE, a));
        y += ui::px(28);
    }
}

void GameClient::hudBottom() {
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    float s = ui::scale();
    // Health / shield
    float bx = 24 * s, by = H - 96 * s, bw = 300 * s, bh = 20 * s;
    float shield = self_.shield, health = self_.flags & PF_DBNO ? self_.dbnoHealth : self_.health;
    DrawRectangleRec({bx, by, bw, bh}, Color{0, 0, 0, 140});
    DrawRectangleRec({bx, by, bw * shield / 100.0f, bh}, Color{70, 150, 255, 255});
    ui::text(std::to_string((int)std::round(shield)), bx + bw + 8 * s, by + 1 * s, 18, Color{150, 200, 255, 255});
    DrawRectangleRec({bx, by + bh + 6 * s, bw, bh}, Color{0, 0, 0, 140});
    Color hc = self_.flags & PF_DBNO ? ui::BAD : Color{80, 210, 90, 255};
    DrawRectangleRec({bx, by + bh + 6 * s, bw * health / 100.0f, bh}, hc);
    ui::text(std::to_string((int)std::round(health)), bx + bw + 8 * s, by + bh + 7 * s, 18, Color{160, 240, 160, 255});
    if (self_.regenLeft > 0) ui::text("Regenerating", bx, by - 22 * s, 14, ui::MUTED);
    if (self_.flags & PF_DBNO) ui::text("KNOCKED - crawl to a teammate!", bx, by - 22 * s, 16, ui::BAD);

    // Hotbar: pickaxe + 5 slots
    float slot = 64 * s, gap = 8 * s;
    float total = 6 * slot + 5 * gap;
    float x0 = W / 2 - total / 2 + 60 * s, y0 = H - slot - 20 * s;
    for (int i = 0; i < 6; i++) {
        Rectangle r{x0 + i * (slot + gap), y0, slot, slot};
        bool sel = self_.selected == i && !self_.buildMode;
        Color bg = Color{20, 26, 38, 200};
        const ItemStack* st = i == 0 ? nullptr : &self_.inv[i - 1];
        if (st && !st->empty()) bg = ui::withAlpha(C(rarityColor(st->rarity)), 0.75f);
        if (i == 0) bg = Color{90, 90, 100, 200};
        DrawRectangleRounded(r, 0.12f, 4, bg);
        if (sel) DrawRectangleRoundedLinesEx(r, 0.12f, 4, 3 * s, WHITE);
        ui::text(slotKey(i), r.x + 4 * s, r.y + 3 * s, 12, ui::withAlpha(WHITE, 0.8f));
        if (i == 0) {
            ui::textCentered("PICK", r.x + slot / 2, r.y + slot / 2 - 8 * s, 14, WHITE);
        } else if (st && !st->empty()) {
            ui::textCentered(shortName(*st), r.x + slot / 2, r.y + slot / 2 - 8 * s, 13, WHITE);
            const ItemDef& d = itemDef(st->type);
            if (d.cls == ItemClass::Gun) {
                std::string a = st->type == ItemType::Minigun ? std::to_string(self_.ammo[(int)AmmoType::Light]) : std::to_string(st->clip);
                ui::textRight(a, r.x + slot - 4 * s, r.y + slot - 18 * s, 14, WHITE);
            } else if (st->count > 1) {
                ui::textRight("x" + std::to_string(st->count), r.x + slot - 4 * s, r.y + slot - 18 * s, 14, WHITE);
            }
        }
    }
    // Selected weapon name + reserve ammo
    if (self_.selected >= 1 && self_.selected <= 5 && !self_.buildMode) {
        const ItemStack& st = self_.inv[self_.selected - 1];
        if (!st.empty()) {
            std::string name = itemDisplayName(st);
            ui::textCentered(name, x0 + total / 2, y0 - 26 * s, 18, C(rarityColor(st.rarity)));
            const ItemDef& d = itemDef(st.type);
            if (d.cls == ItemClass::Gun && st.type != ItemType::Minigun) {
                std::string ammo = std::to_string(st.clip) + " / " + std::to_string(self_.ammo[(int)d.ammo]);
                ui::textCentered(ammo, x0 + total / 2, y0 - 48 * s, 22, WHITE);
            }
        }
    }
    // Build bar
    if (self_.buildMode) {
        const char* keys[] = {"Z", "X", "C", "V"};
        float bw2 = 70 * s;
        float bx2 = W / 2 - 2 * (bw2 + gap) + 60 * s;
        for (int i = 0; i < 4; i++) {
            Rectangle r{bx2 + i * (bw2 + gap), y0 - 80 * s, bw2, 56 * s};
            bool sel = self_.buildPiece == i;
            DrawRectangleRounded(r, 0.15f, 4, sel ? ui::withAlpha(ui::ACCENT, 0.8f) : Color{20, 26, 38, 200});
            ui::textCentered(PIECE_NAMES[i], r.x + r.width / 2, r.y + 12 * s, 14, WHITE);
            ui::textCentered(keys[i], r.x + r.width / 2, r.y + 32 * s, 14, ui::MUTED);
        }
        ui::textCentered(std::string("Building with ") + MATERIAL_NAMES[self_.buildMat] + "  (right-click: material, R: rotate, G: edit)", W / 2 + 60 * s,
                         y0 - 104 * s, 14, ui::TEXT);
    }
    // Materials & ammo (right side)
    float rx = W - 24 * s;
    float ry = H - 150 * s;
    for (int m = 0; m < 3; m++) {
        bool sel = self_.buildMat == m;
        Rectangle r{rx - 150 * s, ry + m * 32 * s, 150 * s, 28 * s};
        DrawRectangleRounded(r, 0.2f, 4, sel && self_.buildMode ? ui::withAlpha(ui::ACCENT, 0.6f) : Color{0, 0, 0, 120});
        DrawRectangleRec({r.x + 6 * s, r.y + 6 * s, 16 * s, 16 * s}, materialColor((si::Material)m));
        ui::text(MATERIAL_NAMES[m], r.x + 30 * s, r.y + 6 * s, 15, WHITE);
        ui::textRight(std::to_string(self_.mats[m]), r.x + r.width - 8 * s, r.y + 6 * s, 16, WHITE);
    }
    float ay = ry - 26 * s;
    for (int a = (int)AmmoType::Count - 1; a >= 1; a--) {
        if (self_.ammo[a] == 0) continue;
        ui::textRight(std::string(AMMO_NAMES[a]) + "  " + std::to_string(self_.ammo[a]), rx, ay, 14, ui::withAlpha(WHITE, 0.85f));
        ay -= 20 * s;
    }
}

void GameClient::hudCenter() {
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    float s = ui::scale();
    Vector2 c{W / 2, H / 2};
    // Crosshair (the camera looks exactly down the aim ray)
    if (localControllable() && !mapOpen_ && !inventoryOpen_) {
        const ItemStack* h = self_.selected >= 1 && self_.selected <= 5 ? &self_.inv[self_.selected - 1] : nullptr;
        float spread = 0.0f;
        if (h && !h->empty() && itemDef(h->type).cls == ItemClass::Gun) {
            WeaponStats ws = weaponStats(h->type, h->rarity);
            bool ads = IsMouseButtonDown(MOUSE_BUTTON_RIGHT);
            spread = (ads ? ws.spreadAds : ws.spreadHip) + self_.bloom;
            if (pred_.vel.lenXZ() > 1 && !ads) spread += 0.02f;
            if (pred_.mode == MoveMode::Air) spread += 0.035f;
        }
        float gap = 6 * s + spread * H * 1.2f;
        float len = 8 * s;
        Color cc = Color{255, 255, 255, 230};
        DrawRectangleV({c.x - 1, c.y - gap - len}, {2, len}, cc);
        DrawRectangleV({c.x - 1, c.y + gap}, {2, len}, cc);
        DrawRectangleV({c.x - gap - len, c.y - 1}, {len, 2}, cc);
        DrawRectangleV({c.x + gap, c.y - 1}, {len, 2}, cc);
        DrawCircleV(c, 1.5f, cc);
        if (hitMarker_ > 0) {
            Color hm = hitMarkerHead_ ? Color{255, 210, 60, 255} : WHITE;
            float o = 8 * s, l = 7 * s;
            DrawLineEx({c.x - o, c.y - o}, {c.x - o - l, c.y - o - l}, 2.5f, hm);
            DrawLineEx({c.x + o, c.y - o}, {c.x + o + l, c.y - o - l}, 2.5f, hm);
            DrawLineEx({c.x - o, c.y + o}, {c.x - o - l, c.y + o + l}, 2.5f, hm);
            DrawLineEx({c.x + o, c.y + o}, {c.x + o + l, c.y + o + l}, 2.5f, hm);
        }
    }
    // Damage numbers
    for (auto& d : dmgNumbers_) {
        Vector2 sp = GetWorldToScreen(V(d.p), cam_);
        if (sp.x < 0 || sp.y < 0 || sp.x > W || sp.y > H) continue;
        Color col = (d.flags & DF_HEADSHOT) ? Color{255, 210, 60, 255} : (d.flags & DF_SHIELD) ? Color{120, 190, 255, 255} : WHITE;
        col.a = (unsigned char)(255 * clampf(1.2f - d.t, 0, 1));
        ui::textShadow(std::to_string(d.amount), sp.x, sp.y, (d.flags & DF_HEADSHOT) ? 30 : 24, col);
    }
    // Teammate names
    for (auto& [id, p] : players_) {
        if (!p.present || id == net_.playerId || p.team != self_.team || !(p.cur.flags & PF_ALIVE)) continue;
        si::Vec3 head = p.cur.pos + si::Vec3{0, 2.3f, 0};
        si::Vec3 toP = head - S(cam_.position);
        if (toP.dot(S(cam_.target) - S(cam_.position)) < 0) continue;
        Vector2 sp = GetWorldToScreen(V(head), cam_);
        ui::textCentered(p.name, sp.x, sp.y - 18 * s, 14, (p.cur.flags & PF_DBNO) ? ui::BAD : ui::GOOD);
        DrawRectangle((int)(sp.x - 25 * s), (int)(sp.y), (int)(50 * s), (int)(4 * s), Color{0, 0, 0, 150});
        DrawRectangle((int)(sp.x - 25 * s), (int)(sp.y), (int)(50 * s * p.cur.health / 100.0f), (int)(4 * s), ui::GOOD);
    }
    // Damage direction indicators
    for (auto& d : dmgIndicators_) {
        si::Vec3 to = d.from - viewPos();
        float ang = std::atan2(to.x, to.z) - camYaw_;
        float rad = 120 * s;
        Vector2 p{c.x - std::sin(ang) * rad, c.y - std::cos(ang) * rad};
        DrawCircleV(p, 10 * s, Color{255, 60, 60, (unsigned char)(200 * clampf(d.t, 0, 1))});
    }
    // Interaction / action progress
    if (!interactPrompt_.empty() && self_.action == ACT_NONE) {
        float w = ui::textWidth(interactPrompt_, 18) + 24 * s;
        DrawRectangleRounded({c.x + 40 * s, c.y + 30 * s, w, 32 * s}, 0.3f, 4, Color{0, 0, 0, 150});
        ui::text(interactPrompt_, c.x + 52 * s, c.y + 37 * s, 18, WHITE);
    }
    if (self_.action != ACT_NONE && self_.actionTotal > 0) {
        const char* label = self_.action == ACT_RELOAD ? "Reloading" : self_.action == ACT_CONSUME ? "Using" : self_.action == ACT_REVIVE ? "Reviving" : "Opening";
        float t = clampf(self_.actionTime / self_.actionTotal, 0, 1);
        Rectangle bar{c.x - 110 * s, c.y + 70 * s, 220 * s, 10 * s};
        DrawRectangleRec(bar, Color{0, 0, 0, 150});
        DrawRectangleRec({bar.x, bar.y, bar.width * t, bar.height}, WHITE);
        ui::textCentered(std::string(label) + "  " + std::to_string(std::max(0.0f, self_.actionTotal - self_.actionTime)).substr(0, 3) + "s",
                         c.x, bar.y - 22 * s, 16, WHITE);
    }
    // Messages
    float my = H * 0.16f;
    for (auto& m : messages_) {
        Color col = m.kind == 2 ? Color{210, 150, 255, 255} : m.kind == 3 ? ui::WARN : WHITE;
        col.a = (unsigned char)(255 * clampf(m.t, 0, 1));
        ui::textShadow(m.text, W / 2 - ui::textWidth(m.text, 22) / 2, my, 22, col);
        my += 30 * s;
    }
    // Phase banner
    std::string banner;
    if (g_.phase == MatchPhase::Warmup) banner = g_.phaseTimer < 999 ? "WARMUP - match starts in " + fmtTime(g_.phaseTimer) : "WARMUP - waiting for players";
    if (g_.phase == MatchPhase::Countdown) banner = "DROP SHIP BOARDING IN " + fmtTime(g_.phaseTimer);
    if (haveSelf_ && self_.mode == MoveMode::OnBus) banner = "Press SPACE to jump from the drop ship";
    if (haveSelf_ && self_.mode == MoveMode::Skydive) banner = "SPACE to open glider  |  W + look down to dive";
    if (!banner.empty()) ui::textShadow(banner, W / 2 - ui::textWidth(banner, 26) / 2, H * 0.1f, 26, WHITE);
    // Spectating
    if (const RemotePlayer* t = spectateTarget()) {
        std::string sp = "Spectating " + t->name + "   (SPACE: next player)";
        ui::textShadow(sp, W / 2 - ui::textWidth(sp, 20) / 2, H - 150 * s, 20, WHITE);
    }
}

void GameClient::hudInventory() {
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    float s = ui::scale();
    DrawRectangle(0, 0, (int)W, (int)H, Color{0, 0, 0, 160});
    ui::textCentered("INVENTORY", W / 2, H * 0.18f, 34, WHITE);
    ui::textCentered("Click a slot, then another to swap. Right-click to drop.", W / 2, H * 0.18f + 44 * s, 16, ui::MUTED);
    float slot = 110 * s, gap = 16 * s;
    float x0 = W / 2 - (5 * slot + 4 * gap) / 2, y0 = H * 0.32f;
    for (int i = 0; i < 5; i++) {
        Rectangle r{x0 + i * (slot + gap), y0, slot, slot};
        const ItemStack& st = self_.inv[i];
        DrawRectangleRounded(r, 0.1f, 4, st.empty() ? Color{30, 36, 50, 230} : ui::withAlpha(C(rarityColor(st.rarity)), 0.85f));
        if (invDragSlot_ == i + 1) DrawRectangleRoundedLinesEx(r, 0.1f, 4, 3, WHITE);
        else if (ui::hovered(r)) DrawRectangleRoundedLinesEx(r, 0.1f, 4, 2, ui::ACCENT);
        if (!st.empty()) {
            std::string n = itemDisplayName(st);
            ui::textCentered(shortName(st), r.x + slot / 2, r.y + slot / 2 - 10 * s, 18, WHITE);
            ui::textCentered(st.count > 1 ? "x" + std::to_string(st.count) : "", r.x + slot / 2, r.y + slot - 24 * s, 14, WHITE);
            if (ui::hovered(r)) ui::textCentered(n, W / 2, y0 + slot + 20 * s, 20, C(rarityColor(st.rarity)));
        }
        if (ui::hovered(r) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (invDragSlot_ < 0) invDragSlot_ = i + 1;
            else {
                if (invDragSlot_ != i + 1) pushAction(ActionType::SwapSlots, (uint8_t)invDragSlot_, (uint8_t)(i + 1));
                invDragSlot_ = -1;
            }
        }
        if (ui::hovered(r) && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) && !st.empty()) pushAction(ActionType::DropSlot, (uint8_t)(i + 1));
    }
    // Materials drop buttons
    float my = y0 + slot + 70 * s;
    for (int m = 0; m < 3; m++) {
        Rectangle r{W / 2 - 240 * s + m * 165 * s, my, 150 * s, 44 * s};
        if (ui::button(r, std::string("Drop 30 ") + MATERIAL_NAMES[m], false, self_.mats[m] > 0)) pushAction(ActionType::DropMats, (uint8_t)m, 3);
    }
    ui::textCentered("Tab to close", W / 2, my + 70 * s, 16, ui::MUTED);
}

void GameClient::hudEmoteWheel() {
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    float s = ui::scale();
    Vector2 c{W / 2, H / 2};
    DrawCircleV(c, 190 * s, Color{0, 0, 0, 140});
    auto it = players_.find(net_.playerId);
    Loadout l = it != players_.end() ? it->second.loadout : Loadout();
    for (int i = 0; i < 6; i++) {
        float a = i * 2 * kPi / 6 - kPi / 2;
        Vector2 p{c.x + std::cos(a) * 120 * s, c.y + std::sin(a) * 120 * s};
        const CosmeticDef* e = findCosmetic(l.emotes[i]);
        std::string name = e ? e->name : "Empty";
        DrawCircleV(p, 44 * s, Color{30, 40, 60, 230});
        ui::textCentered(std::to_string(i + 1), p.x, p.y - 26 * s, 14, ui::MUTED);
        ui::textCentered(name, p.x, p.y - 6 * s, 14, WHITE);
        if (IsKeyPressed(KEY_ONE + i) && e && e->id != "emote_none") pushAction(ActionType::Emote, (uint8_t)i);
    }
    ui::textCentered("Press 1-6 to emote", c.x, c.y - 8 * s, 16, WHITE);
}

void GameClient::hudPause() {
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    float s = ui::scale();
    DrawRectangle(0, 0, (int)W, (int)H, Color{0, 0, 0, 170});
    Rectangle p{W / 2 - 220 * s, H / 2 - 190 * s, 440 * s, 380 * s};
    ui::panel(p);
    ui::textCentered("PAUSED", W / 2, p.y + 24 * s, 30, WHITE);
    ui::text("Mouse sensitivity", p.x + 30 * s, p.y + 86 * s, 18, ui::MUTED);
    float sens = settings_.sensitivity * 1000;
    if (ui::slider({p.x + 30 * s, p.y + 112 * s, p.width - 60 * s, 24 * s}, sens, 0.5f, 8.0f, 901)) settings_.sensitivity = sens / 1000;
    ui::text("Field of view: " + std::to_string((int)settings_.fov), p.x + 30 * s, p.y + 146 * s, 18, ui::MUTED);
    ui::slider({p.x + 30 * s, p.y + 172 * s, p.width - 60 * s, 24 * s}, settings_.fov, 60, 110, 902);
    ui::text("Volume", p.x + 30 * s, p.y + 206 * s, 18, ui::MUTED);
    if (ui::slider({p.x + 30 * s, p.y + 232 * s, p.width - 60 * s, 24 * s}, settings_.volume, 0, 1, 903)) audio_.masterVolume = settings_.volume;
    if (ui::button({p.x + 30 * s, p.y + p.height - 76 * s, 180 * s, 50 * s}, "Resume", true)) paused_ = false;
    if (ui::button({p.x + p.width - 210 * s, p.y + p.height - 76 * s, 180 * s, 50 * s}, "Leave match")) leave_ = true;
}

void GameClient::hudResults() {
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    float s = ui::scale();
    if (result_.won) {
        DrawRectangle(0, 0, (int)W, (int)H, Color{0, 0, 0, 90});
        ui::textShadow("#1 VICTORY!", W / 2 - ui::textWidth("#1 VICTORY!", 72) / 2, H * 0.22f, 72, ui::GOLDEN);
    } else {
        DrawRectangle(0, 0, (int)W, (int)H, Color{0, 0, 0, 120});
        std::string t = "You placed #" + std::to_string(result_.placement);
        ui::textShadow(t, W / 2 - ui::textWidth(t, 56) / 2, H * 0.22f, 56, WHITE);
    }
    std::string stats = std::to_string(result_.kills) + " eliminations   |   " + std::to_string((int)result_.damage) + " damage dealt";
    ui::textCentered(stats, W / 2, H * 0.22f + 90 * s, 22, ui::TEXT);
    if (ui::button({W / 2 - 200 * s, H * 0.42f, 190 * s, 52 * s}, "Return to lobby", true)) leave_ = true;
    if (!result_.won && g_.phase != MatchPhase::Ended) {
        if (ui::button({W / 2 + 10 * s, H * 0.42f, 190 * s, 52 * s}, "Spectate")) { result_.has = false; }
    }
}

void GameClient::hudVehicle() {
    if (!inVehicle()) return;
    VehicleVisual vv;
    if (!vehicleVisual(self_.vehicle, vv)) return;
    const VehicleDef& d = vehicleDef(vv.type);
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    float s = ui::scale();
    float pw = 320 * s, ph = 92 * s;
    Rectangle r{W / 2 - pw / 2 + 60 * s, H - 64 * s - 20 * s - ph - 70 * s, pw, ph};
    if (!d.seatShoot[self_.seat < MAX_SEATS ? self_.seat : 0]) r.y = H - ph - 28 * s;
    ui::panel(r, Color{14, 18, 28, 200}, 0.14f);
    ui::text(d.name, r.x + 14 * s, r.y + 10 * s, 18, WHITE);
    const char* role = self_.seat == 0 ? (d.seatPose[0] == SEAT_STAND ? "RIDER" : "DRIVER") : "PASSENGER";
    ui::textRight(role, r.x + r.width - 14 * s, r.y + 12 * s, 13, ui::ACCENT);
    // Speed readout
    float kmh = (driving_ ? predVeh_.vel.lenXZ() : vv.speed) * 3.6f;
    ui::textRight(std::to_string((int)std::round(kmh)) + " km/h", r.x + r.width - 14 * s, r.y + 32 * s, 15, ui::TEXT);
    // Hull bar
    float bx = r.x + 14 * s, bw = r.width * 0.55f, bh = 10 * s, by = r.y + 38 * s;
    DrawRectangleRounded({bx, by, bw, bh}, 0.5f, 4, Color{0, 0, 0, 150});
    Color hc = vv.hp > 0.5f ? Color{120, 220, 120, 255} : vv.hp > 0.25f ? ui::WARN : ui::BAD;
    DrawRectangleRounded({bx, by, bw * clampf(vv.hp, 0, 1), bh}, 0.5f, 4, hc);
    ui::text("HULL", bx, by - 1 * s + bh + 2 * s, 11, ui::MUTED);
    // Boost meter (driver only, vehicles that have boost)
    if (self_.seat == 0 && d.boostSpeed > 0 && driving_) {
        float bx2 = bx + bw + 14 * s, bw2 = r.x + r.width - 14 * s - bx2;
        DrawRectangleRounded({bx2, by, bw2, bh}, 0.5f, 4, Color{0, 0, 0, 150});
        Color bc = predVeh_.boosting ? Color{255, 170, 60, 255} : Color{90, 190, 255, 255};
        DrawRectangleRounded({bx2, by, bw2 * clampf(predVeh_.boost, 0, 1), bh}, 0.5f, 4, bc);
        ui::text("BOOST", bx2, by + bh + 1 * s, 11, ui::MUTED);
    }
    // Controls
    std::string hint = "E exit";
    if (d.seats > 1) hint += "   C switch seat";
    if (self_.seat == 0) {
        if (d.boostSpeed > 0) hint += "   Shift boost";
        if (d.jumpVel > 0) hint += "   Space jump";
        if (!d.seatShoot[0]) hint += "   LMB horn";
    }
    ui::text(hint, r.x + 14 * s, r.y + r.height - 22 * s, 12, ui::withAlpha(WHITE, 0.7f));
}

void GameClient::renderHud() {
    ui::beginFrame();
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    float s = ui::scale();
    // Storm / damage vignettes
    if (localControllable() && g_.stormPhase >= 0 && dist2d(pred_.pos.xz(), g_.stormCur) > g_.stormCurR)
        DrawRectangle(0, 0, (int)W, (int)H, Color{120, 40, 180, 70});
    if (damageFlash_ > 0) DrawRectangleLinesEx({0, 0, W, H}, 30 * s, Color{220, 30, 30, (unsigned char)(200 * damageFlash_ / 0.35f)});

    hudCenter();
    hudKillfeed();
    if (localControllable() || (haveSelf_ && self_.mode == MoveMode::OnBus)) hudBottom();
    if (localControllable()) hudVehicle();

    // Top-right: minimap + stats
    float mm = 220 * s;
    Rectangle mr{W - mm - 20 * s, 20 * s, mm, mm};
    hudMinimap(mr, 360);
    float ty = mr.y + mr.height + 10 * s;
    std::string alive = std::to_string(g_.alive) + " alive";
    std::string kills = std::to_string(self_.kills) + " eliminations";
    ui::textRight(alive + "   " + kills, mr.x + mr.width, ty, 16, WHITE);
    ty += 22 * s;
    if (g_.stormPhase >= 0 && g_.phase != MatchPhase::Ended) {
        std::string st = g_.stormShrinking ? "Storm closing: " + fmtTime(g_.stormTimer) : "Storm moves in: " + fmtTime(g_.stormTimer);
        ui::textRight(st, mr.x + mr.width, ty, 16, g_.stormShrinking ? Color{210, 150, 255, 255} : WHITE);
        ty += 22 * s;
        if (localControllable()) {
            float d = dist2d(pred_.pos.xz(), g_.stormNext) - g_.stormNextR;
            if (d > 0) ui::textRight(std::to_string((int)d) + "m to the safe zone", mr.x + mr.width, ty, 14, ui::WARN);
        }
    }
    // Location name
    if (const POI* poi = map_.nearestPoi(viewPos().x, viewPos().z)) {
        if (dist2d(poi->center, viewPos().xz()) < poi->radius * 1.3f) ui::textRight(poi->name, mr.x + mr.width, mr.y - 2 * s + mr.height - 20 * s, 14, WHITE);
    }
    if (settings_.showFps) ui::text(std::to_string(GetFPS()) + " FPS", 10 * s, 10 * s, 14, ui::MUTED);

    if (emoteWheel_) hudEmoteWheel();
    if (inventoryOpen_) hudInventory();
    if (mapOpen_) hudFullMap();
    if (result_.has && !(self_.flags & PF_ALIVE) && !paused_) hudResults();
    else if (result_.has && result_.won && !paused_) hudResults();
    if (paused_) hudPause();
}

} // namespace client
