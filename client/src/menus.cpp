// Lobby / login / matchmaking screens.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "app.h"
#include "rlgl.h"
#include "ui.h"

namespace client {

namespace {

const char* kSlotNames[] = {"Outfit", "Back Bling", "Pickaxe", "Glider", "Contrail", "Emote 1", "Emote 2", "Emote 3", "Emote 4", "Emote 5", "Emote 6"};

CosmeticType slotType(int slot) {
    switch (slot) {
        case 0: return CosmeticType::Outfit;
        case 1: return CosmeticType::BackBling;
        case 2: return CosmeticType::Pickaxe;
        case 3: return CosmeticType::Glider;
        case 4: return CosmeticType::Contrail;
        default: return CosmeticType::Emote;
    }
}

ui::Icon slotIcon(int slot) {
    switch (slot) {
        case 0: return ui::Icon::Person;
        case 1: return ui::Icon::Backpack;
        case 2: return ui::Icon::Pickaxe;
        case 3: return ui::Icon::Parachute;
        case 4: return ui::Icon::Sparkle;
        default: return ui::Icon::Star;
    }
}

std::string equippedFor(const Loadout& l, int slot) {
    switch (slot) {
        case 0: return l.outfit;
        case 1: return l.backbling;
        case 2: return l.pickaxe;
        case 3: return l.glider;
        case 4: return l.contrail;
        default: return l.emotes[std::min(5, std::max(0, slot - 5))];
    }
}

Color rarityByName(const std::string& r) {
    for (int i = 0; i < 5; i++)
        if (r == RARITY_NAMES[i]) return ui::rarityColor(i);
    return ui::MUTED;
}

Color mix(Color a, Color b, float t) {
    return {(unsigned char)(a.r + (b.r - a.r) * t), (unsigned char)(a.g + (b.g - a.g) * t), (unsigned char)(a.b + (b.b - a.b) * t),
            (unsigned char)(a.a + (b.a - a.a) * t)};
}

// Layered animated backdrop: gradient, glow behind the character, a slow storm swirl,
// drifting motes and a vignette.
void background(float glowX = 0.72f) {
    int W = GetScreenWidth(), H = GetScreenHeight();
    float t = (float)GetTime();
    DrawRectangleGradientV(0, 0, W, H, Color{28, 46, 92, 255}, Color{9, 12, 22, 255});
    // Glow behind the hero area
    Vector2 g{W * glowX, H * 0.55f};
    for (int i = 6; i >= 1; i--) DrawCircleV(g, H * (0.12f + i * 0.09f), Color{70, 120, 255, (unsigned char)(7 + i)});
    // Storm swirl (rotating arcs) in the lower left
    Vector2 sc{W * 0.18f, H * 1.05f};
    for (int i = 0; i < 7; i++) {
        float r0 = H * (0.25f + i * 0.09f);
        float a0 = t * (6.0f + i * 2.0f) + i * 40.0f;
        DrawRing(sc, r0, r0 + H * 0.018f, a0, a0 + 110 + i * 12, 48, Color{150, 80, 255, (unsigned char)(16 + i * 2)});
        DrawRing(sc, r0, r0 + H * 0.010f, a0 + 180, a0 + 250, 48, Color{90, 140, 255, 14});
    }
    // Light beams from the top
    for (int i = 0; i < 3; i++) {
        float x = W * (0.55f + i * 0.14f) + std::sin(t * 0.2f + i) * W * 0.02f;
        DrawTriangle({x, 0}, {x - W * 0.12f, (float)H}, {x - W * 0.04f, (float)H}, Color{255, 255, 255, 6});
    }
    // Drifting motes
    for (int i = 0; i < 60; i++) {
        float sx = std::fmod(i * 0.6180339f, 1.0f);
        float speed = 0.01f + std::fmod(i * 0.371f, 1.0f) * 0.02f;
        float y = 1.0f - std::fmod(t * speed + i * 0.137f, 1.0f);
        float x = sx + std::sin(t * 0.3f + i) * 0.01f;
        unsigned char a = (unsigned char)(40 + 60 * std::fabs(std::sin(t + i)));
        DrawCircleV({x * W, y * H}, 1.0f + (i % 3), Color{190, 210, 255, a});
    }
    // Vignette
    DrawRectangleGradientV(0, H * 2 / 3, W, H / 3, Color{0, 0, 0, 0}, Color{0, 0, 0, 120});
    DrawRectangleGradientH(0, 0, W / 6, H, Color{0, 0, 0, 90}, Color{0, 0, 0, 0});
}

// Pill-shaped segmented control; returns true when the selection changed.
bool segmented(Rectangle r, const char* const* labels, int n, int& sel) {
    float s = ui::scale();
    ui::rrect(r, r.height / s / 2, Color{10, 14, 24, 200});
    ui::rrectLine(r, r.height / s / 2, 1.0f, Color{255, 255, 255, 20});
    float w = (r.width - 8 * s) / n;
    bool changed = false;
    for (int i = 0; i < n; i++) {
        Rectangle q{r.x + 4 * s + i * w, r.y + 4 * s, w, r.height - 8 * s};
        bool h = ui::hovered(q);
        if (i == sel) ui::rrectGrad(q, q.height / s / 2, Color{95, 176, 255, 255}, Color{58, 136, 235, 255});
        else if (h) ui::rrect(q, q.height / s / 2, Color{255, 255, 255, 14});
        ui::textCentered(labels[i], q.x + q.width / 2, q.y + q.height / 2 - 9.5f * s, 16, i == sel ? Color{6, 22, 44, 255} : (h ? ui::TEXT : ui::MUTED));
        if (h && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && sel != i) { sel = i; changed = true; }
    }
    return changed;
}

// Three diagonal color bands in a rounded square: the "art" for a cosmetic card.
void swatchArt(Rectangle r, const CosmeticStyle& st, float radius) {
    ui::rrect(r, radius, C(st.primary));
    BeginScissorMode((int)r.x, (int)r.y, (int)r.width, (int)r.height);
    float w = r.width, h = r.height;
    DrawTriangle({r.x + w * 0.55f, r.y + h}, {r.x + w, r.y + h}, {r.x + w, r.y + h * 0.2f}, C(st.secondary));
    DrawTriangle({r.x + w * 0.82f, r.y + h}, {r.x + w, r.y + h}, {r.x + w, r.y + h * 0.66f}, C(st.accent));
    DrawCircleV({r.x + w * 0.3f, r.y + h * 0.35f}, w * 0.16f, Color{255, 255, 255, 40});
    EndScissorMode();
    ui::rrectLine(r, radius, 1.0f, Color{255, 255, 255, 40});
}

void sectionTitle(const std::string& t, float x, float y, Color accent = ui::PLAY) {
    float s = ui::scale();
    DrawRectangleRounded({x, y + 3 * s, 4 * s, 20 * s}, 1.0f, 4, accent);
    ui::text(t, x + 14 * s, y, 22, WHITE);
}

const char* kTips[] = {
    "Tip: boosting a Crash Quad smashes straight through player builds.",
    "Tip: the Roller Ball shields its rider - bounce your way out of trouble.",
    "Tip: Skyboards float over water and let you shoot while you ride.",
    "Tip: hold E on a knocked teammate to revive them.",
    "Tip: slipstreams launch you across the island - press Space to glide.",
    "Tip: trolleys are slow on flat ground but fly downhill.",
    "Tip: right-click in build mode to switch between wood, brick and metal.",
};

} // namespace

void App::drawStatus() {
    if (statusTimer_ <= 0 || status_.empty()) return;
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    float s = ui::scale();
    float a = std::min(1.0f, statusTimer_);
    float w = ui::textWidth(status_, 19) + 64 * s;
    Rectangle r{W / 2 - w / 2, H - 96 * s + (1 - a) * 20 * s, w, 48 * s};
    ui::shadow(r, 24, 10, 0.4f * a);
    ui::rrect(r, 24, ui::withAlpha(Color{24, 30, 48, 255}, a));
    ui::rrectLine(r, 24, 1.0f, ui::withAlpha(ui::ACCENT, 0.6f * a));
    ui::icon(ui::Icon::Sparkle, r.x + 26 * s, r.y + r.height / 2, 20 * s, ui::withAlpha(ui::PLAY, a));
    ui::text(status_, r.x + 44 * s, r.y + r.height / 2 - 11 * s, 19, ui::withAlpha(ui::TEXT, a));
}

void App::drawShowroom(int view, float t) {
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    Camera3D cam{};
    cam.up = {0, 1, 0};
    cam.fovy = 38;
    cam.projection = CAMERA_PERSPECTIVE;
    const float spacing = 4.2f;
    const si::Vec3 camPos[] = {{-6, 4.5f, 12}, {9, 3.5f, -9}, {-2, 1.8f, 5.5f}, {10.5f, 2.2f, 4.5f}};
    const si::Vec3 camTgt[] = {{8, 0.6f, 0}, {8, 0.6f, 0}, {0.5f, 0.9f, 0}, {12.6f, 0.9f, 0}};
    int v = view % 4;
    cam.position = V(camPos[v]);
    cam.target = V(camTgt[v]);
    previewLight_.shadows = false;
    previewLight_.drawSky(cam, t);
    BeginMode3D(cam);
    previewLight_.begin(cam, t);
    BeginShaderMode(previewLight_.shader);
    setDrawMaterial(M_GRASS);
    drawBox({8, -0.5f, 0}, {40, 0.5f, 20}, Basis(), Color{110, 170, 80, 255});
    setDrawMaterial(M_ASPHALT);
    drawBox({8, 0.005f, 0}, {14, 0.01f, 3.2f}, Basis(), Color{90, 92, 98, 255});
    Loadout riders[] = {Loadout(), Loadout(), Loadout(), Loadout(), Loadout()};
    riders[1].outfit = "outfit_arena_trooper";
    if (std::getenv("STORM_SHOWROOM_TROOPER")) for (auto& r : riders) r.outfit = "outfit_arena_trooper";
    for (int i = 0; i < (int)VehicleType::Count; i++) {
        VehicleVisual vv;
        vv.type = (VehicleType)i;
        vv.id = (uint16_t)i;
        vv.pos = {i * spacing, 0, 0};
        vv.yaw = 0.9f + (view == 1 ? kPi : 0);
        vv.wheelSpin = t * 3;
        vv.steer = std::sin(t) * 0.5f;
        vv.boosting = i == 1 || i == 2;
        vv.speed = 8;
        vv.occupied = true;
        vv.hp = i == 3 ? 0.2f : 1.0f;
        drawVehicle(vv, t);
        const VehicleDef& d = vehicleDef(vv.type);
        VehicleState st;
        st.pos = vv.pos;
        st.yaw = vv.yaw;
        for (int seat = 0; seat < d.seats; seat++) {
            if (i == 0 && seat == 3) continue;
            CharPose p;
            p.pos = vehicleSeatPos(st, vv.type, seat);
            p.yaw = vv.yaw;
            p.seatPose = d.seatPose[seat];
            p.steering = seat == 0 && !d.seatShoot[0];
            p.building = !d.seatShoot[seat];
            p.heldType = d.seatShoot[seat] ? (uint8_t)ItemType::AssaultRifle : 0;
            p.animTime = t;
            drawCharacter(p, riders[(i + seat) % 5], t);
        }
    }
    for (int i = 0; i < (int)VehicleType::Count; i++) {
        VehicleVisual vv;
        vv.type = (VehicleType)i;
        vv.id = (uint16_t)i;
        vv.pos = {i * spacing, 0, 0};
        vv.yaw = 0.9f + (view == 1 ? kPi : 0);
        drawVehicleShell(vv, t);
    }
    EndShaderMode();
    previewLight_.endObjects();
    EndMode3D();
    ui::textCentered("Vehicle showroom", W / 2, 16, 20, WHITE);
    (void)H;
}

void App::drawPreview(Rectangle area, const Loadout& l) {
    spin_ += GetFrameTime() * 0.6f;
    int w = std::max(16, (int)area.width), h = std::max(16, (int)area.height);
    if (previewRT_.id == 0 || previewRT_.texture.width != w || previewRT_.texture.height != h) {
        if (previewRT_.id) UnloadRenderTexture(previewRT_);
        previewRT_ = LoadRenderTexture(w, h);
    }
    Camera3D cam{};
    cam.position = {0, 1.4f, 4.6f};
    cam.target = {0, 1.0f, 0};
    cam.up = {0, 1, 0};
    cam.fovy = 36;
    cam.projection = CAMERA_PERSPECTIVE;
    float t = (float)GetTime();
    BeginTextureMode(previewRT_);
    ClearBackground(BLANK);
    BeginMode3D(cam);
    previewLight_.shadows = false;
    previewLight_.begin(cam, t);
    BeginShaderMode(previewLight_.shader);
    // Pedestal: stepped disc with a glowing rim
    setDrawMaterial(M_METAL);
    drawCylinder({0, -0.16f, 0}, {0, 1, 0}, 0.82f, 0.13f, Color{36, 46, 76, 255}, 40);
    drawCylinder({0, -0.02f, 0}, {0, 1, 0}, 0.7f, 0.03f, Color{58, 74, 116, 255}, 40);
    setDrawMaterial(M_PLAIN);
    float pulse = 0.75f + 0.25f * std::sin(t * 2.0f);
    drawCylinder({0, -0.05f, 0}, {0, 1, 0}, 0.835f, 0.012f, Color{(unsigned char)(90 * pulse), (unsigned char)(170 * pulse), 255, 255}, 40);
    CharPose p;
    p.pos = {0, 0.02f, 0};
    p.yaw = spin_;
    p.animTime = t;
    p.heldType = 0;
    if (lockerSlot_ >= 5 && tab_ == LobbyTab::Locker) {
        const CosmeticDef* e = findCosmetic(equippedFor(l, lockerSlot_));
        if (e) p.emote = e->style.shape;
    }
    if (lockerSlot_ == 3 && tab_ == LobbyTab::Locker) {
        p.mode = MoveMode::Glide;
        p.pos = {0, -1.4f, 0};
    }
    drawCharacter(p, l, t);
    EndShaderMode();
    previewLight_.endObjects();
    EndMode3D();
    EndTextureMode();
    // Floor shadow + spotlight, then the character (render textures are stored upside down)
    float s = ui::scale();
    DrawEllipse((int)(area.x + area.width / 2), (int)(area.y + area.height * 0.86f), area.width * 0.3f, 18 * s, Color{0, 0, 0, 70});
    DrawTextureRec(previewRT_.texture, {0, 0, (float)w, (float)-h}, {area.x, area.y}, WHITE);
}

void App::drawLogin() {
    background(0.7f);
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    float s = ui::scale();
    // Left: brand + pitch
    float lx = W * 0.08f, ly = H * 0.2f;
    ui::rrectGrad({lx, ly, 86 * s, 86 * s}, 22, Color{255, 226, 84, 255}, Color{245, 170, 20, 255});
    ui::icon(ui::Icon::Storm, lx + 43 * s, ly + 45 * s, 60 * s, Color{40, 24, 0, 255});
    ui::textShadow("STORM", lx, ly + 110 * s, 76, WHITE);
    ui::textShadow("ISLAND", lx, ly + 190 * s, 76, ui::PLAY);
    ui::text("Drop in. Loot up. Build. Drive. Be the last one standing.", lx, ly + 285 * s, 21, ui::TEXT);
    struct Feat { ui::Icon ic; const char* t; } feats[] = {
        {ui::Icon::Parachute, "Skydive onto a 1.5 km island with 18 named locations"},
        {ui::Icon::Hammer, "Build walls, floors, ramps and roofs in three materials"},
        {ui::Icon::Car, "Carts, quads, hoverboards, trolleys and roller balls"},
        {ui::Icon::Users, "Solo, Duos and Squads - online or against bots"},
    };
    float fy = ly + 340 * s;
    for (auto& f : feats) {
        ui::rrect({lx, fy, 36 * s, 36 * s}, 10, Color{255, 255, 255, 18});
        ui::icon(f.ic, lx + 18 * s, fy + 18 * s, 22 * s, ui::ACCENT);
        ui::text(f.t, lx + 50 * s, fy + 7 * s, 18, ui::TEXT);
        fy += 46 * s;
    }
    // Right: form card
    float cw = 440 * s, chh = (registerMode_ ? 520 : 468) * s;
    Rectangle p{W * 0.62f, H / 2 - chh / 2, cw, chh};
    ui::card(p, 18, Color{20, 26, 42, 245});
    float x = p.x + 32 * s, w = p.width - 64 * s, y = p.y + 28 * s;
    const char* modes[] = {"Sign in", "Create account"};
    int mode = registerMode_ ? 1 : 0;
    if (segmented({x, y, w, 44 * s}, modes, 2, mode)) registerMode_ = mode == 1;
    y += 66 * s;
    bool enter = false;
    ui::text("Username", x, y, 15, ui::MUTED);
    y += 22 * s;
    enter |= ui::textBox({x, y, w, 46 * s}, user_, "Your username", 1, false, 16);
    y += 60 * s;
    ui::text("Password", x, y, 15, ui::MUTED);
    y += 22 * s;
    enter |= ui::textBox({x, y, w, 46 * s}, pass_, "At least 6 characters", 2, true, 64);
    y += 60 * s;
    if (registerMode_) {
        ui::text("Display name (optional)", x, y, 15, ui::MUTED);
        y += 22 * s;
        enter |= ui::textBox({x, y, w, 46 * s}, display_, "Shown to other players", 3, false, 20);
        y += 60 * s;
    }
    bool busy = fLogin_.valid();
    if ((ui::button({x, y, w, 52 * s}, busy ? "Please wait..." : (registerMode_ ? "Create account" : "Sign in"), true, !busy) || enter) && !busy) {
        api_.baseUrl = settings_.backendUrl;
        if (registerMode_) fLogin_ = api_.post("/api/register", {{"username", user_}, {"password", pass_}, {"display_name", display_}});
        else fLogin_ = api_.post("/api/login", {{"username", user_}, {"password", pass_}});
    }
    y += 70 * s;
    DrawRectangle((int)x, (int)(y + 10 * s), (int)(w * 0.42f), 1, ui::LINE);
    DrawRectangle((int)(x + w * 0.58f), (int)(y + 10 * s), (int)(w * 0.42f), 1, ui::LINE);
    ui::textCentered("or", x + w / 2, y, 15, ui::MUTED);
    y += 30 * s;
    if (ui::button({x, y, w, 48 * s}, "Play offline against bots")) {
        offline_ = true;
        account_ = json();
        screen_ = Screen::Lobby;
        tab_ = LobbyTab::Play;
    }
    // Backend URL under the card
    float by = p.y + p.height + 16 * s;
    ui::icon(ui::Icon::Globe, p.x + 14 * s, by + 19 * s, 20 * s, ui::MUTED);
    ui::textBox({p.x + 34 * s, by, p.width - 34 * s, 38 * s}, settings_.backendUrl, "http://host:8080", 4, false, 100);
    if (ui::button({W - 124 * s, 18 * s, 104 * s, 40 * s}, "Quit")) quit_ = true;
    ui::text("v1.0  -  original game, not affiliated with any other battle royale", 20 * s, H - 30 * s, 13, ui::withAlpha(ui::MUTED, 0.7f));
}

void App::drawLobby() {
    background();
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    float s = ui::scale();
    // Top bar
    float bh = 72 * s;
    DrawRectangleGradientV(0, 0, (int)W, (int)bh, Color{6, 9, 18, 245}, Color{10, 14, 26, 200});
    DrawRectangle(0, (int)bh - 1, (int)W, 1, Color{255, 255, 255, 18});
    ui::rrectGrad({20 * s, 16 * s, 40 * s, 40 * s}, 11, Color{255, 226, 84, 255}, Color{245, 170, 20, 255});
    ui::icon(ui::Icon::Storm, 40 * s, 37 * s, 28 * s, Color{40, 24, 0, 255});
    ui::text("STORM ISLAND", 72 * s, 22 * s, 25, WHITE);
    const char* tabs[] = {"PLAY", "LOCKER", "ITEM SHOP", "CAREER", "SETTINGS"};
    float tx = 280 * s;
    Rectangle activeTab{};
    for (int i = 0; i < 5; i++) {
        if (offline_ && (i == 2 || i == 3)) continue;
        float tw = std::max(110 * s, ui::textWidth(tabs[i], 21) + 44 * s);
        Rectangle r{tx, 0, tw, bh};
        if (ui::tab(r, tabs[i], (int)tab_ == i)) { tab_ = (LobbyTab)i; audio_.play(Sfx::Click, 0.4f); }
        if ((int)tab_ == i) activeTab = r;
        tx += tw;
    }
    ui::tabUnderline(activeTab);
    // Account area
    float rx = W - 20 * s;
    Rectangle out{rx - 104 * s, 16 * s, 104 * s, 40 * s};
    if (ui::button(out, offline_ ? "Sign in" : "Sign out")) {
        if (!offline_) api_.post("/api/logout", json::object());
        api_.token.clear();
        offline_ = false;
        screen_ = Screen::Login;
        saveSettings();
        return;
    }
    rx = out.x - 16 * s;
    std::string who = displayName();
    if (!offline_ && account_.is_object()) {
        int level = account_.value("level", 1);
        int64_t coins = account_.value("coins", 0);
        float prog = (float)account_.value("level_progress", 0.0);
        // Level badge with XP ring
        Vector2 bc{rx - 22 * s, 36 * s};
        ui::ring(bc, 21 * s, 4 * s, prog, ui::PLAY, Color{255, 255, 255, 30});
        DrawCircleV(bc, 16 * s, Color{24, 32, 54, 255});
        ui::textCentered(std::to_string(level), bc.x, bc.y - 11 * s, 18, WHITE);
        rx -= 54 * s;
        ui::textRight(who, rx, 14 * s, 18, WHITE);
        ui::textRight("Level " + std::to_string(level) + "  -  " + std::to_string((int)(prog * 100)) + "% to next", rx, 38 * s, 13, ui::MUTED);
        rx -= std::max(ui::textWidth(who, 18), ui::textWidth("Level 00  -  00% to next", 13)) + 22 * s;
        // Coins chip
        std::string cs = std::to_string(coins);
        float cw = ui::textWidth(cs, 18) + 58 * s;
        Rectangle cr{rx - cw, 17 * s, cw, 38 * s};
        ui::rrect(cr, 19, Color{255, 255, 255, 16});
        ui::rrectLine(cr, 19, 1.0f, ui::withAlpha(ui::GOLDEN, 0.45f));
        ui::icon(ui::Icon::Coin, cr.x + 20 * s, cr.y + cr.height / 2, 24 * s, ui::GOLDEN);
        ui::text(cs, cr.x + 38 * s, cr.y + 8 * s, 18, ui::GOLDEN);
    } else {
        ui::icon(ui::Icon::Person, rx - 14 * s, 36 * s, 20 * s, ui::MUTED);
        ui::textRight(who, rx - 32 * s, 16 * s, 18, WHITE);
        ui::textRight("Offline", rx - 32 * s, 38 * s, 13, ui::MUTED);
    }
    Rectangle area{28 * s, bh + 22 * s, W - 56 * s, H - bh - 44 * s};
    switch (tab_) {
        case LobbyTab::Play: drawPlayTab(area); break;
        case LobbyTab::Locker: drawLockerTab(area); break;
        case LobbyTab::Shop: drawShopTab(area); break;
        case LobbyTab::Career: drawCareerTab(area); break;
        case LobbyTab::Settings: drawSettingsTab(area); break;
    }
}

void App::drawPlayTab(Rectangle a) {
    float s = ui::scale();
    float t = (float)GetTime();
    // Character on a pedestal, right side
    Rectangle prev{a.x + a.width * 0.52f, a.y - 10 * s, a.width * 0.48f, a.height - 120 * s};
    drawPreview(prev, currentLoadout());
    const CosmeticDef* outfit = findCosmetic(currentLoadout().outfit);
    if (outfit) {
        Color rc = ui::rarityColor((int)outfit->rarity);
        float nw = ui::textWidth(outfit->name, 20) + 40 * s;
        Rectangle np{prev.x + prev.width / 2 - nw / 2, prev.y + prev.height - 18 * s, nw, 34 * s};
        ui::rrect(np, 17, Color{10, 14, 24, 200});
        ui::rrectLine(np, 17, 1.0f, ui::withAlpha(rc, 0.8f));
        ui::textCentered(outfit->name, np.x + np.width / 2, np.y + 6 * s, 20, WHITE);
    }
    // Big PLAY button bottom-right
    const char* modeNames[] = {"SOLO", "DUOS", "SQUADS"};
    const char* diffNames[] = {"EASY", "MEDIUM", "HARD", "INSANE"};
    Rectangle play{a.x + a.width - 380 * s, a.y + a.height - 104 * s, 380 * s, 104 * s};
    if (!offline_) {
        std::string sub = playlist_ == "solo" ? "SOLO" : playlist_ == "duos" ? "DUOS" : "SQUADS";
        if (ui::bigButton(play, fMm_.valid() ? "JOINING..." : "PLAY", sub + "  -  ONLINE MATCHMAKING", !fMm_.valid())) {
            fMm_ = api_.post("/api/matchmaking/join", {{"playlist", playlist_}});
            audio_.play(Sfx::Click, 0.5f);
        }
    } else {
        std::string sub = std::string(modeNames[offlineMode_]) + "  -  " + std::to_string((int)offlineBots_) + " BOTS  -  " + diffNames[offlineDiff_];
        if (ui::bigButton(play, "PLAY", sub)) {
            saveSettings();
            startOffline();
        }
    }

    // Left column
    float x = a.x, y = a.y, w = std::min(a.width * 0.47f, 640 * s);
    // News hero card
    Rectangle news{x, y, w, 168 * s};
    ui::shadow(news, 16, 12, 0.35f);
    ui::rrectGrad(news, 16, Color{120, 70, 230, 255}, Color{50, 110, 230, 255});
    BeginScissorMode((int)news.x, (int)news.y, (int)news.width, (int)news.height);
    for (int i = 0; i < 5; i++) {
        float r = (60 + i * 36) * s;
        DrawRing({news.x + news.width - 70 * s, news.y + news.height * 0.5f}, r, r + 10 * s, t * 10 + i * 30, t * 10 + i * 30 + 200, 40, Color{255, 255, 255, 18});
    }
    EndScissorMode();
    ui::rrectLine(news, 16, 1.0f, Color{255, 255, 255, 50});
    ui::chip("NEW", news.x + 20 * s, news.y + 18 * s, 13, Color{40, 24, 0, 255}, ui::PLAY, ui::Icon::Sparkle, true);
    ui::text("Vehicles have landed", news.x + 20 * s, news.y + 50 * s, 30, WHITE);
    std::string blurb = !offline_ && !motd_.empty() ? motd_ : "Carts, quads, boards, trolleys and roller balls are parked all over the island.";
    auto lines = ui::wrap(blurb, news.width - 190 * s, 16);
    for (size_t li = 0; li < lines.size() && li < 3; li++) ui::text(lines[li], news.x + 20 * s, news.y + (92 + li * 22) * s, 16, ui::withAlpha(WHITE, 0.9f));
    ui::icon(ui::Icon::Car, news.x + news.width - 70 * s, news.y + news.height * 0.5f, 90 * s, Color{255, 255, 255, 220});
    y += news.height + 22 * s;

    if (!offline_) {
        sectionTitle("Choose a mode", x, y);
        y += 38 * s;
        const char* ids[] = {"solo", "duos", "squads"};
        const char* names[] = {"Solo", "Duos", "Squads"};
        const char* desc[] = {"Every player for themselves", "Teams of 2 - revive your partner", "Teams of 4"};
        ui::Icon icons[] = {ui::Icon::Person, ui::Icon::Users, ui::Icon::Users};
        float cw = (w - 24 * s) / 3;
        for (int i = 0; i < 3; i++) {
            Rectangle r{x + i * (cw + 12 * s), y, cw, 124 * s};
            bool sel = playlist_ == ids[i];
            bool h = ui::hovered(r);
            if (sel) ui::shadow(r, 14, 10, 0.4f);
            ui::rrectGrad(r, 14, sel ? Color{48, 88, 160, 255} : (h ? Color{34, 44, 70, 255} : Color{26, 34, 54, 240}), sel ? Color{30, 58, 118, 255} : Color{20, 26, 42, 240});
            ui::rrectLine(r, 14, sel ? 2.0f : 1.0f, sel ? ui::PLAY : Color{255, 255, 255, 22});
            ui::icon(icons[i], r.x + 30 * s, r.y + 32 * s, 34 * s, sel ? ui::PLAY : ui::ACCENT);
            ui::text(names[i], r.x + 58 * s, r.y + 18 * s, 24, WHITE);
            int online = 0;
            if (playlists_.is_array())
                for (auto& pl : playlists_)
                    if (pl.value("id", std::string()) == ids[i]) online = pl.value("online", 0);
            ui::chip(std::to_string(online) + " playing", r.x + 16 * s, r.y + 60 * s, 12, ui::GOOD, Color{62, 207, 142, 40});
            ui::text(desc[i], r.x + 16 * s, r.y + 94 * s, 13, ui::MUTED);
            if (h && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { playlist_ = ids[i]; audio_.play(Sfx::Click, 0.3f); }
        }
        y += 124 * s + 26 * s;
    }
    // Bot match settings
    Rectangle op{x, y, w, (offline_ ? 204 : 262) * s};
    ui::card(op, 16, Color{20, 26, 42, 235});
    float ox = op.x + 22 * s, oy = op.y + 18 * s, ow = op.width - 44 * s;
    ui::icon(ui::Icon::Crosshair, ox + 12 * s, oy + 12 * s, 22 * s, ui::PLAY);
    ui::text(offline_ ? "Match against bots" : "Practice offline against bots", ox + 32 * s, oy, 21, WHITE);
    oy += 44 * s;
    ui::text("Bots", ox, oy + 2 * s, 17, ui::MUTED);
    ui::slider({ox + 120 * s, oy, ow - 190 * s, 26 * s}, offlineBots_, 0, 99, 101);
    offlineBots_ = std::round(offlineBots_);
    ui::textRight(std::to_string((int)offlineBots_), ox + ow, oy + 1 * s, 19, WHITE);
    oy += 42 * s;
    ui::text("Difficulty", ox, oy + 8 * s, 17, ui::MUTED);
    const char* diffs[] = {"Easy", "Medium", "Hard", "Insane"};
    int d = offlineDiff_;
    if (segmented({ox + 120 * s, oy, ow - 120 * s, 38 * s}, diffs, 4, d)) offlineDiff_ = d;
    oy += 50 * s;
    ui::text("Mode", ox, oy + 8 * s, 17, ui::MUTED);
    const char* modes[] = {"Solo", "Duos", "Squads"};
    int m = offlineMode_;
    if (segmented({ox + 120 * s, oy, ow - 120 * s, 38 * s}, modes, 3, m)) offlineMode_ = m;
    if (!offline_) {
        oy += 52 * s;
        if (ui::button({ox, oy, ow, 40 * s}, "Start offline match")) {
            saveSettings();
            startOffline();
        }
    }
    y += op.height + 18 * s;
    // Direct connect (compact)
    if (y + 56 * s < a.y + a.height) {
        Rectangle dc{x, y, w, 56 * s};
        ui::rrect(dc, 14, Color{16, 21, 34, 200});
        ui::icon(ui::Icon::Globe, dc.x + 24 * s, dc.y + dc.height / 2, 22 * s, ui::MUTED);
        ui::text("Direct connect", dc.x + 44 * s, dc.y + 17 * s, 16, ui::MUTED);
        ui::textBox({dc.x + 180 * s, dc.y + 9 * s, dc.width - 312 * s, 38 * s}, directHost_, "host:port", 102, false, 64);
        if (ui::button({dc.x + dc.width - 120 * s, dc.y + 9 * s, 110 * s, 38 * s}, "Connect")) {
            std::string host = directHost_;
            uint16_t port = 7777;
            auto c = host.rfind(':');
            if (c != std::string::npos) { port = (uint16_t)std::atoi(host.substr(c + 1).c_str()); host = host.substr(0, c); }
            saveSettings();
            joinMatch(host, port, "");
        }
    }
}

void App::drawLockerTab(Rectangle a) {
    float s = ui::scale();
    Loadout l = currentLoadout();
    // Category column
    float y = a.y;
    float colW = 270 * s;
    float rowH = std::min(58.0f * s, (a.height - 10 * s * 10) / 11);
    for (int i = 0; i < 11; i++) {
        Rectangle r{a.x, y, colW, rowH};
        bool sel = lockerSlot_ == i;
        bool h = ui::hovered(r);
        const CosmeticDef* d = findCosmetic(equippedFor(l, i));
        Color rc = d ? ui::rarityColor((int)d->rarity) : ui::MUTED;
        ui::rrectGrad(r, 12, sel ? Color{48, 88, 160, 255} : (h ? Color{32, 42, 66, 240} : Color{22, 28, 44, 230}), sel ? Color{32, 62, 122, 255} : Color{18, 23, 37, 230});
        if (sel) ui::rrectLine(r, 12, 1.5f, ui::PLAY);
        DrawRectangleRounded({r.x + 6 * s, r.y + 8 * s, 4 * s, r.height - 16 * s}, 1.0f, 4, rc);
        ui::icon(slotIcon(i), r.x + 34 * s, r.y + r.height / 2, 24 * s, sel ? ui::PLAY : ui::withAlpha(WHITE, 0.8f));
        ui::text(kSlotNames[i], r.x + 58 * s, r.y + r.height * 0.14f, 13, ui::MUTED);
        ui::text(d ? d->name : "Empty", r.x + 58 * s, r.y + r.height * 0.46f, 17, d ? WHITE : ui::MUTED);
        if (h && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { lockerSlot_ = i; audio_.play(Sfx::Click, 0.3f); }
        y += rowH + 8 * s;
    }
    // Items grid
    float gx = a.x + colW + 24 * s;
    Rectangle prev{a.x + a.width * 0.64f, a.y, a.width * 0.36f, a.height};
    Rectangle grid{gx, a.y + 40 * s, prev.x - gx - 16 * s, a.height - 40 * s};
    CosmeticType t = slotType(lockerSlot_);
    sectionTitle(std::string(kSlotNames[lockerSlot_]), gx, a.y);
    std::string eq = equippedFor(l, lockerSlot_);
    float cell = 158 * s, gap = 14 * s;
    int cols = std::max(1, (int)((grid.width + gap) / (cell + gap)));
    int idx = 0;
    for (auto& c : cosmeticCatalog()) {
        if (c.type != t) continue;
        bool owned = ownsItem(c.id);
        int row = idx / cols, col = idx % cols;
        Rectangle r{grid.x + col * (cell + gap), grid.y + row * (cell * 1.08f + gap), cell, cell * 1.08f};
        if (r.y + r.height > a.y + a.height + 4 * s) break;
        Color rc = ui::rarityColor((int)c.rarity);
        bool h = ui::hovered(r);
        bool equipped = c.id == eq;
        if (h && owned) ui::shadow(r, 14, 10, 0.45f);
        ui::rrectGrad(r, 14, owned ? mix(Color{24, 30, 48, 255}, rc, 0.25f) : Color{24, 28, 38, 230}, owned ? mix(Color{24, 30, 48, 255}, rc, 0.75f) : Color{30, 34, 44, 230});
        Rectangle art{r.x + 12 * s, r.y + 12 * s, r.width - 24 * s, r.height * 0.5f};
        swatchArt(art, c.style, 10);
        if (!owned) {
            ui::rrect(art, 10, Color{0, 0, 0, 150});
            ui::icon(ui::Icon::Lock, art.x + art.width / 2, art.y + art.height / 2, 34 * s, ui::withAlpha(WHITE, 0.85f));
        }
        ui::text(c.name, r.x + 12 * s, r.y + r.height * 0.62f + 4 * s, 16, owned ? WHITE : ui::MUTED);
        std::string sub = owned ? RARITY_NAMES[(int)c.rarity] : c.unlockLevel > 0 ? "Reach level " + std::to_string(c.unlockLevel) : "Item Shop";
        ui::text(sub, r.x + 12 * s, r.y + r.height * 0.62f + 28 * s, 13, owned ? ui::withAlpha(WHITE, 0.75f) : ui::MUTED);
        ui::rrectLine(r, 14, equipped ? 2.5f : 1.0f, equipped ? ui::PLAY : (h ? ui::withAlpha(WHITE, 0.6f) : ui::withAlpha(rc, 0.5f)));
        if (equipped) {
            DrawCircleV({r.x + r.width - 18 * s, r.y + 18 * s}, 13 * s, ui::PLAY);
            ui::icon(ui::Icon::Check, r.x + r.width - 18 * s, r.y + 18 * s, 18 * s, Color{40, 24, 0, 255});
        }
        if (owned && h && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            equip(c.id, lockerSlot_ >= 5 ? lockerSlot_ - 5 : 0);
            audio_.play(Sfx::Click, 0.4f);
        }
        idx++;
    }
    drawPreview(prev, currentLoadout());
}

void App::drawShopTab(Rectangle a) {
    float s = ui::scale();
    sectionTitle("Item Shop", a.x, a.y);
    if (shop_.is_object()) {
        int64_t reset = shop_.value("reset_in", 0);
        std::string rs = "New items in " + std::to_string(reset / 3600) + "h " + std::to_string((reset % 3600) / 60) + "m";
        ui::chip(rs, a.x + 170 * s, a.y + 1 * s, 14, ui::TEXT, Color{255, 255, 255, 16}, ui::Icon::Clock, true);
    }
    if (!shop_.is_object() || !shop_.contains("items")) { ui::text("Loading...", a.x, a.y + 60 * s, 20, ui::MUTED); return; }
    float cell = 236 * s, gap = 18 * s;
    int cols = std::max(1, (int)((a.width + gap) / (cell + gap)));
    int i = 0;
    for (auto& it : shop_["items"]) {
        std::string id = it.value("id", std::string());
        const CosmeticDef* c = findCosmetic(id);
        if (!c) continue;
        int row = i / cols, col = i % cols;
        float ch = cell * 1.28f;
        Rectangle r{a.x + col * (cell + gap), a.y + 50 * s + row * (ch + gap), cell, ch};
        if (r.y + r.height > a.y + a.height + 10 * s) break;
        Color rc = rarityByName(it.value("rarity", std::string()));
        bool owned = it.value("owned", false);
        bool h = ui::hovered(r);
        ui::shadow(r, 16, h ? 14 : 8, h ? 0.5f : 0.3f);
        ui::rrectGrad(r, 16, mix(Color{20, 24, 40, 255}, rc, 0.35f), mix(Color{20, 24, 40, 255}, rc, 0.9f));
        Rectangle art{r.x + 14 * s, r.y + 14 * s, r.width - 28 * s, r.height * 0.52f};
        swatchArt(art, c->style, 12);
        if (it.value("section", std::string()) == "featured")
            ui::chip("FEATURED", art.x + 8 * s, art.y + 8 * s, 12, Color{40, 24, 0, 255}, ui::PLAY, ui::Icon::Star, true);
        float ty = art.y + art.height + 12 * s;
        ui::text(c->name, r.x + 16 * s, ty, 20, WHITE);
        ui::text(std::string(COSMETIC_TYPE_NAMES[(int)c->type]) + "  -  " + RARITY_NAMES[(int)c->rarity], r.x + 16 * s, ty + 28 * s, 14,
                 ui::withAlpha(WHITE, 0.8f));
        Rectangle b{r.x + 14 * s, r.y + r.height - 50 * s, r.width - 28 * s, 38 * s};
        if (owned) {
            ui::rrect(b, 10, Color{10, 40, 28, 220});
            ui::rrectLine(b, 10, 1.0f, ui::withAlpha(ui::GOOD, 0.7f));
            ui::icon(ui::Icon::Check, b.x + b.width / 2 - 40 * s, b.y + b.height / 2, 20 * s, ui::GOOD);
            ui::text("OWNED", b.x + b.width / 2 - 24 * s, b.y + 9 * s, 17, WHITE);
        } else {
            bool bh = ui::hovered(b) && !fBuy_.valid();
            ui::rrectGrad(b, 10, bh ? Color{40, 44, 60, 255} : Color{16, 18, 28, 235}, Color{10, 12, 20, 235});
            ui::rrectLine(b, 10, 1.0f, ui::withAlpha(ui::GOLDEN, bh ? 0.9f : 0.4f));
            std::string price = std::to_string(c->price);
            float pw = ui::textWidth(price, 18) + 30 * s;
            ui::icon(ui::Icon::Coin, b.x + b.width / 2 - pw / 2 + 10 * s, b.y + b.height / 2, 22 * s, ui::GOLDEN);
            ui::text(price, b.x + b.width / 2 - pw / 2 + 26 * s, b.y + 8 * s, 18, ui::GOLDEN);
            if (bh && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) fBuy_ = api_.post("/api/shop/buy", {{"item_id", id}});
        }
        i++;
    }
}

void App::drawCareerTab(Rectangle a) {
    float s = ui::scale();
    sectionTitle("Career", a.x, a.y);
    if (!account_.is_object()) return;
    json st = account_.value("stats", json::object());
    int wins = st.value("wins", 0), kills = st.value("kills", 0), matches = st.value("matches", 0), top10 = st.value("top10", 0);
    int deaths = std::max(1, matches - wins);
    char kd[32];
    std::snprintf(kd, sizeof(kd), "%.2f", (double)kills / deaths);
    struct Tile { ui::Icon ic; const char* label; std::string value; Color c; } tiles[] = {
        {ui::Icon::Star, "Level", std::to_string(account_.value("level", 1)), ui::PLAY},
        {ui::Icon::Trophy, "Wins", std::to_string(wins), ui::GOLDEN},
        {ui::Icon::Crosshair, "Eliminations", std::to_string(kills), ui::BAD},
        {ui::Icon::Flag, "Matches", std::to_string(matches), ui::ACCENT},
        {ui::Icon::Chart, "Top 10", std::to_string(top10), ui::GOOD},
        {ui::Icon::Skull, "K/D", kd, Color{200, 150, 255, 255}}};
    float tw = (a.width - 5 * 16 * s) / 6;
    for (int i = 0; i < 6; i++) {
        Rectangle r{a.x + i * (tw + 16 * s), a.y + 46 * s, tw, 104 * s};
        ui::card(r, 14, Color{20, 26, 42, 235});
        DrawCircleV({r.x + 34 * s, r.y + 34 * s}, 20 * s, ui::withAlpha(tiles[i].c, 0.18f));
        ui::icon(tiles[i].ic, r.x + 34 * s, r.y + 34 * s, 24 * s, tiles[i].c);
        ui::text(tiles[i].label, r.x + 64 * s, r.y + 24 * s, 14, ui::MUTED);
        ui::text(tiles[i].value, r.x + 18 * s, r.y + 58 * s, 32, WHITE);
    }
    // Recent matches table
    float y = a.y + 180 * s;
    float colW = a.width * 0.5f - 12 * s;
    Rectangle mt{a.x, y, colW, a.height - (y - a.y)};
    ui::card(mt, 16, Color{18, 23, 38, 235});
    sectionTitle("Recent matches", mt.x + 20 * s, mt.y + 16 * s, ui::ACCENT);
    float ry = mt.y + 58 * s;
    const char* heads[] = {"PLACE", "MODE", "ELIMS", "DAMAGE"};
    float cx[] = {mt.x + 24 * s, mt.x + mt.width * 0.3f, mt.x + mt.width * 0.58f, mt.x + mt.width * 0.8f};
    for (int k = 0; k < 4; k++) ui::text(heads[k], cx[k], ry, 12, ui::MUTED);
    ry += 24 * s;
    if (matches_.is_array()) {
        int n = 0;
        for (auto& m : matches_) {
            if (ry + 34 * s > mt.y + mt.height) break;
            int place = m.value("placement", 0);
            Rectangle row{mt.x + 12 * s, ry, mt.width - 24 * s, 32 * s};
            if (n % 2 == 0) ui::rrect(row, 8, Color{255, 255, 255, 8});
            Color pc = place == 1 ? ui::GOLDEN : place <= 10 ? ui::ACCENT : ui::withAlpha(WHITE, 0.25f);
            ui::rrect({cx[0], ry + 5 * s, 52 * s, 22 * s}, 11, ui::withAlpha(pc, place <= 10 ? 0.9f : 1.0f));
            ui::textCentered("#" + std::to_string(place), cx[0] + 26 * s, ry + 7 * s, 14, place <= 10 ? Color{20, 16, 4, 255} : WHITE);
            ui::text(m.value("playlist", std::string()), cx[1], ry + 7 * s, 16, ui::TEXT);
            ui::text(std::to_string(m.value("kills", 0)), cx[2], ry + 7 * s, 16, ui::TEXT);
            ui::text(std::to_string((int)m.value("damage", 0.0)), cx[3], ry + 7 * s, 16, ui::TEXT);
            ry += 34 * s;
            n++;
        }
        if (matches_.empty()) ui::text("No matches yet - go drop in!", mt.x + 24 * s, ry + 6 * s, 17, ui::MUTED);
    }
    // Leaderboard
    Rectangle lb{a.x + colW + 24 * s, y, a.width - colW - 24 * s, a.height - (y - a.y)};
    ui::card(lb, 16, Color{18, 23, 38, 235});
    sectionTitle("Leaderboard", lb.x + 20 * s, lb.y + 16 * s, ui::GOLDEN);
    float ly = lb.y + 58 * s;
    ui::text("RANK", lb.x + 24 * s, ly, 12, ui::MUTED);
    ui::text("PLAYER", lb.x + 90 * s, ly, 12, ui::MUTED);
    ui::textRight("WINS", lb.x + lb.width - 130 * s, ly, 12, ui::MUTED);
    ui::textRight("ELIMS", lb.x + lb.width - 24 * s, ly, 12, ui::MUTED);
    ly += 24 * s;
    if (leaderboard_.is_array()) {
        int rank = 1;
        std::string me = displayName();
        for (auto& p : leaderboard_) {
            if (ly + 34 * s > lb.y + lb.height) break;
            std::string name = p.value("display_name", std::string());
            Rectangle row{lb.x + 12 * s, ly, lb.width - 24 * s, 32 * s};
            if (name == me) ui::rrect(row, 8, ui::withAlpha(ui::ACCENT, 0.2f));
            else if (rank % 2 == 1) ui::rrect(row, 8, Color{255, 255, 255, 8});
            Color medal = rank == 1 ? ui::GOLDEN : rank == 2 ? Color{200, 210, 225, 255} : rank == 3 ? Color{214, 140, 80, 255} : Color{255, 255, 255, 30};
            DrawCircleV({lb.x + 40 * s, ly + 16 * s}, 12 * s, medal);
            ui::textCentered(std::to_string(rank), lb.x + 40 * s, ly + 6 * s, 14, rank <= 3 ? Color{20, 16, 4, 255} : WHITE);
            ui::text(name, lb.x + 90 * s, ly + 6 * s, 17, WHITE);
            ui::textRight(std::to_string(p.value("wins", 0)), lb.x + lb.width - 130 * s, ly + 6 * s, 17, ui::GOLDEN);
            ui::textRight(std::to_string(p.value("kills", 0)), lb.x + lb.width - 24 * s, ly + 6 * s, 17, ui::TEXT);
            ly += 34 * s;
            rank++;
        }
    }
}

void App::drawSettingsTab(Rectangle a) {
    float s = ui::scale();
    float colW = std::min(640 * s, a.width * 0.5f);
    float x = a.x;
    auto group = [&](const char* title, ui::Icon ic, float y, float h) {
        Rectangle r{x, y, colW, h};
        ui::card(r, 16, Color{20, 26, 42, 235});
        ui::icon(ic, r.x + 30 * s, r.y + 28 * s, 22 * s, ui::PLAY);
        ui::text(title, r.x + 52 * s, r.y + 16 * s, 20, WHITE);
        return r;
    };
    auto sliderRow = [&](Rectangle g, float yy, const std::string& label, const std::string& value, float& v, float lo, float hi, int id) {
        ui::text(label, g.x + 26 * s, yy + 3 * s, 17, ui::MUTED);
        bool ch = ui::slider({g.x + 220 * s, yy, g.width - 320 * s, 26 * s}, v, lo, hi, id);
        ui::textRight(value, g.x + g.width - 26 * s, yy + 2 * s, 17, WHITE);
        return ch;
    };
    float y = a.y;
    Rectangle g1 = group("Gameplay", ui::Icon::Crosshair, y, 176 * s);
    float sens = settings_.sensitivity * 1000;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f", sens);
    if (sliderRow(g1, g1.y + 62 * s, "Mouse sensitivity", buf, sens, 0.5f, 8.0f, 201)) settings_.sensitivity = sens / 1000;
    sliderRow(g1, g1.y + 100 * s, "Field of view", std::to_string((int)settings_.fov), settings_.fov, 60, 110, 202);
    ui::checkbox({g1.x + 26 * s, g1.y + 136 * s, 240 * s, 28 * s}, settings_.invertY, "Invert mouse Y");
    y += g1.height + 18 * s;
    Rectangle g2 = group("Graphics", ui::Icon::Sparkle, y, 176 * s);
    sliderRow(g2, g2.y + 62 * s, "View distance", std::to_string((int)settings_.viewDistance) + " m", settings_.viewDistance, 250, 1200, 204);
    ui::checkbox({g2.x + 26 * s, g2.y + 102 * s, 260 * s, 28 * s}, settings_.shadows, "Real-time shadows");
    ui::checkbox({g2.x + 26 * s, g2.y + 138 * s, 260 * s, 28 * s}, settings_.showFps, "Show FPS");
    y += g2.height + 18 * s;
    Rectangle g3 = group("Audio & network", ui::Icon::Globe, y, 170 * s);
    if (sliderRow(g3, g3.y + 62 * s, "Volume", std::to_string((int)std::round(settings_.volume * 100)) + "%", settings_.volume, 0, 1, 203))
        audio_.masterVolume = settings_.volume;
    ui::text("Backend URL", g3.x + 26 * s, g3.y + 112 * s, 17, ui::MUTED);
    ui::textBox({g3.x + 220 * s, g3.y + 102 * s, g3.width - 246 * s, 42 * s}, settings_.backendUrl, "http://host:8080", 205, false, 100);
    y += g3.height + 18 * s;
    if (ui::button({x, y, 220 * s, 50 * s}, "Save settings", true)) {
        api_.baseUrl = settings_.backendUrl;
        saveSettings();
        setStatus("Settings saved");
    }
    // Controls reference with keycaps
    Rectangle c{a.x + colW + 24 * s, a.y, a.width - colW - 24 * s, a.height};
    if (c.width > 300 * s) {
        ui::card(c, 16, Color{20, 26, 42, 235});
        ui::icon(ui::Icon::Gear, c.x + 30 * s, c.y + 28 * s, 22 * s, ui::PLAY);
        ui::text("Controls", c.x + 52 * s, c.y + 16 * s, 20, WHITE);
        struct Row { std::vector<const char*> keys; const char* what; } rows[] = {
            {{"W", "A", "S", "D"}, "Move"}, {{"Space"}, "Jump / glide / leave the drop ship"}, {{"Shift"}, "Sprint / vehicle boost"},
            {{"Ctrl"}, "Crouch"}, {{"LMB"}, "Fire / place / horn"}, {{"RMB"}, "Aim down sights / change material"},
            {{"1-5", "F"}, "Inventory slots, pickaxe"}, {{"E"}, "Interact, enter or exit vehicles"}, {{"R"}, "Reload / rotate piece"},
            {{"Q"}, "Build mode"}, {{"Z", "X", "C", "V"}, "Wall, floor, ramp, roof"}, {{"G"}, "Edit the targeted piece"},
            {{"C"}, "Switch seat (in a vehicle)"}, {{"Tab", "M"}, "Inventory, map"}, {{"B"}, "Emote wheel (1-6)"}, {{"Esc"}, "Menu"}};
        float yy = c.y + 62 * s;
        float kh = 28 * s;
        for (auto& r : rows) {
            if (yy + kh > c.y + c.height - 10 * s) break;
            float kx = c.x + 26 * s;
            for (auto k : r.keys) kx += ui::keycap(k, kx, yy, kh) + 6 * s;
            ui::text(r.what, std::max(kx + 10 * s, c.x + 200 * s), yy + 4 * s, 16, ui::TEXT);
            yy += kh + 10 * s;
        }
    }
}

void App::drawMatchmaking() {
    background(0.5f);
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    float s = ui::scale();
    float t = (float)GetTime();
    Vector2 c{W / 2, H * 0.38f};
    // Storm-eye spinner
    for (int i = 0; i < 4; i++) {
        float r = (70 + i * 26) * s;
        float a0 = t * (120 - i * 25) * (i % 2 ? -1 : 1) + i * 60;
        DrawRing(c, r, r + 6 * s, a0, a0 + 220 - i * 30, 64, ui::withAlpha(i % 2 ? ui::ACCENT : ui::STORM, 0.55f - i * 0.08f));
    }
    float pulse = 0.5f + 0.5f * std::sin(t * 3);
    DrawCircleV(c, (52 + 6 * pulse) * s, Color{24, 32, 58, 255});
    ui::icon(ui::Icon::Storm, c.x, c.y + 4 * s, 64 * s, ui::PLAY);
    ui::textCentered("FINDING A MATCH", W / 2, H * 0.6f, 36, WHITE);
    int secs = (int)mmElapsed_;
    char tb[16];
    std::snprintf(tb, sizeof(tb), "%d:%02d", secs / 60, secs % 60);
    std::string mode = playlist_ == "solo" ? "SOLO" : playlist_ == "duos" ? "DUOS" : "SQUADS";
    float cw = ui::textWidth(mode, 16) + 70 * s;
    ui::chip(mode, W / 2 - cw / 2 - 40 * s, H * 0.6f + 54 * s, 16, WHITE, Color{255, 255, 255, 20}, ui::Icon::Users, true);
    ui::chip(tb, W / 2 + cw / 2 - 30 * s, H * 0.6f + 54 * s, 16, ui::PLAY, Color{255, 214, 58, 30}, ui::Icon::Clock, true);
    const char* tip = kTips[(int)(t / 5.0f) % (int)(sizeof(kTips) / sizeof(kTips[0]))];
    ui::textCentered(tip, W / 2, H * 0.6f + 110 * s, 17, ui::MUTED);
    if (ui::button({W / 2 - 100 * s, H * 0.8f, 200 * s, 50 * s}, "Cancel")) {
        api_.post("/api/matchmaking/cancel", {{"ticket", mmTicket_}});
        screen_ = Screen::Lobby;
    }
}

} // namespace client
