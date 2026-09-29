// Lobby / login / matchmaking screens.
#include <algorithm>
#include <cmath>
#include <cstdio>

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

void background() {
    int W = GetScreenWidth(), H = GetScreenHeight();
    DrawRectangleGradientV(0, 0, W, H, Color{26, 44, 86, 255}, Color{12, 16, 28, 255});
    // Soft animated storm swirl
    float t = (float)GetTime();
    for (int i = 0; i < 6; i++) {
        float a = t * 0.05f + i;
        DrawCircleV({W * (0.5f + 0.4f * std::cos(a)), H * (0.8f + 0.1f * std::sin(a * 1.3f))}, H * 0.35f, Color{120, 60, 200, 14});
    }
}

} // namespace

void App::drawStatus() {
    if (statusTimer_ <= 0 || status_.empty()) return;
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    float w = ui::textWidth(status_, 20) + ui::px(40);
    Rectangle r{W / 2 - w / 2, H - ui::px(80), w, (float)ui::px(44)};
    ui::panel(r, ui::withAlpha(ui::PANEL2, std::min(1.0f, statusTimer_)));
    ui::textCentered(status_, W / 2, r.y + ui::px(12), 20, ui::withAlpha(ui::TEXT, std::min(1.0f, statusTimer_)));
}

void App::drawPreview(Rectangle area, const Loadout& l) {
    spin_ += GetFrameTime() * 0.6f;
    int w = std::max(16, (int)area.width), h = std::max(16, (int)area.height);
    if (previewRT_.id == 0 || previewRT_.texture.width != w || previewRT_.texture.height != h) {
        if (previewRT_.id) UnloadRenderTexture(previewRT_);
        previewRT_ = LoadRenderTexture(w, h);
    }
    Camera3D cam{};
    cam.position = {0, 1.3f, 4.6f};
    cam.target = {0, 1.0f, 0};
    cam.up = {0, 1, 0};
    cam.fovy = 36;
    cam.projection = CAMERA_PERSPECTIVE;
    BeginTextureMode(previewRT_);
    ClearBackground(BLANK);
    BeginMode3D(cam);
    previewLight_.shadows = false;
    previewLight_.begin(cam, (float)GetTime());
    BeginShaderMode(previewLight_.shader);
    drawBox({0, -0.1f, 0}, {1.1f, 0.1f, 1.1f}, yawBasis(spin_ * 0.3f), Color{60, 80, 120, 255});
    CharPose p;
    p.pos = {0, 0, 0};
    p.yaw = spin_;
    p.animTime = (float)GetTime();
    p.heldType = 0;
    if (lockerSlot_ >= 5 && tab_ == LobbyTab::Locker) {
        const CosmeticDef* e = findCosmetic(equippedFor(l, lockerSlot_));
        if (e) p.emote = e->style.shape;
    }
    if (lockerSlot_ == 3 && tab_ == LobbyTab::Locker) {
        p.mode = MoveMode::Glide;
        p.pos = {0, -1.4f, 0};
    }
    drawCharacter(p, l, (float)GetTime());
    EndShaderMode();
    previewLight_.endObjects();
    EndMode3D();
    EndTextureMode();
    // Render textures are stored upside down
    DrawTextureRec(previewRT_.texture, {0, 0, (float)w, (float)-h}, {area.x, area.y}, WHITE);
}

void App::drawLogin() {
    background();
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    float s = ui::scale();
    ui::textShadow("STORM ISLAND", W / 2 - ui::textWidth("STORM ISLAND", 72) / 2, H * 0.1f, 72, WHITE);
    ui::textCentered("drop in - loot up - build - be the last one standing", W / 2, H * 0.1f + 86 * s, 20, ui::MUTED);
    Rectangle p{W / 2 - 230 * s, H * 0.3f, 460 * s, (registerMode_ ? 470 : 420) * s};
    ui::panel(p);
    float x = p.x + 30 * s, w = p.width - 60 * s, y = p.y + 24 * s;
    ui::text(registerMode_ ? "Create account" : "Sign in", x, y, 26, WHITE);
    y += 46 * s;
    bool enter = false;
    enter |= ui::textBox({x, y, w, 44 * s}, user_, "Username", 1, false, 16);
    y += 54 * s;
    enter |= ui::textBox({x, y, w, 44 * s}, pass_, "Password", 2, true, 64);
    y += 54 * s;
    if (registerMode_) {
        enter |= ui::textBox({x, y, w, 44 * s}, display_, "Display name (optional)", 3, false, 20);
        y += 54 * s;
    }
    bool busy = fLogin_.valid();
    if ((ui::button({x, y, w, 48 * s}, busy ? "Please wait..." : (registerMode_ ? "Create account" : "Sign in"), true, !busy) || enter) && !busy) {
        api_.baseUrl = settings_.backendUrl;
        if (registerMode_) fLogin_ = api_.post("/api/register", {{"username", user_}, {"password", pass_}, {"display_name", display_}});
        else fLogin_ = api_.post("/api/login", {{"username", user_}, {"password", pass_}});
    }
    y += 58 * s;
    if (ui::button({x, y, w, 40 * s}, registerMode_ ? "I already have an account" : "Create a new account")) registerMode_ = !registerMode_;
    y += 52 * s;
    ui::text("Backend", x, y + 10 * s, 16, ui::MUTED);
    ui::textBox({x + 80 * s, y, w - 80 * s, 38 * s}, settings_.backendUrl, "http://host:8080", 4, false, 100);
    // Offline
    Rectangle o{W / 2 - 230 * s, p.y + p.height + 20 * s, 460 * s, 56 * s};
    if (ui::button(o, "Play offline against bots (no account)")) {
        offline_ = true;
        account_ = json();
        screen_ = Screen::Lobby;
        tab_ = LobbyTab::Play;
    }
    if (ui::button({W - 130 * s, 20 * s, 110 * s, 40 * s}, "Quit")) quit_ = true;
}

void App::drawLobby() {
    background();
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    float s = ui::scale();
    // Top bar
    DrawRectangle(0, 0, (int)W, (int)(64 * s), Color{10, 14, 24, 230});
    ui::text("STORM ISLAND", 24 * s, 20 * s, 26, WHITE);
    const char* tabs[] = {"PLAY", "LOCKER", "ITEM SHOP", "CAREER", "SETTINGS"};
    float tx = 260 * s;
    for (int i = 0; i < 5; i++) {
        if (offline_ && (i == 2 || i == 3)) continue;
        Rectangle r{tx, 0, 150 * s, 64 * s};
        if (ui::tab(r, tabs[i], (int)tab_ == i)) { tab_ = (LobbyTab)i; audio_.play(Sfx::Click, 0.4f); }
        tx += 150 * s;
    }
    // Account info
    std::string who = displayName();
    if (!offline_ && account_.is_object()) {
        int level = account_.value("level", 1);
        int64_t coins = account_.value("coins", 0);
        ui::textRight(std::to_string(coins) + " coins", W - 150 * s, 12 * s, 18, ui::GOLDEN);
        ui::textRight(who + "  -  Level " + std::to_string(level), W - 150 * s, 36 * s, 16, ui::TEXT);
        float prog = account_.value("level_progress", 0.0);
        DrawRectangle((int)(W - 360 * s), (int)(58 * s), (int)(210 * s), (int)(3 * s), ui::PANEL2);
        DrawRectangle((int)(W - 360 * s), (int)(58 * s), (int)(210 * s * prog), (int)(3 * s), ui::ACCENT);
    } else {
        ui::textRight(who + " (offline)", W - 150 * s, 24 * s, 18, ui::TEXT);
    }
    if (ui::button({W - 130 * s, 12 * s, 110 * s, 40 * s}, offline_ ? "Sign in" : "Sign out")) {
        if (!offline_) api_.post("/api/logout", json::object());
        api_.token.clear();
        offline_ = false;
        screen_ = Screen::Login;
        saveSettings();
        return;
    }
    Rectangle area{24 * s, 84 * s, W - 48 * s, H - 108 * s};
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
    // Character preview on the right
    Rectangle prev{a.x + a.width * 0.58f, a.y, a.width * 0.42f, a.height};
    drawPreview(prev, currentLoadout());
    const CosmeticDef* outfit = findCosmetic(currentLoadout().outfit);
    if (outfit) ui::textCentered(outfit->name, prev.x + prev.width / 2, prev.y + prev.height - 40 * s, 22, WHITE);

    float x = a.x, y = a.y, w = a.width * 0.55f;
    if (!offline_) {
        ui::text("Online", x, y, 28, WHITE);
        y += 44 * s;
        const char* ids[] = {"solo", "duos", "squads"};
        const char* names[] = {"Solo", "Duos", "Squads"};
        float cw = (w - 20 * s) / 3;
        for (int i = 0; i < 3; i++) {
            Rectangle r{x + i * (cw + 10 * s), y, cw, 110 * s};
            bool sel = playlist_ == ids[i];
            DrawRectangleRounded(r, 0.1f, 6, sel ? Color{40, 70, 120, 240} : ui::PANEL);
            DrawRectangleRoundedLinesEx(r, 0.1f, 6, sel ? 3 : 1, sel ? ui::ACCENT : ui::LINE);
            ui::text(names[i], r.x + 16 * s, r.y + 16 * s, 26, WHITE);
            int online = 0;
            if (playlists_.is_array())
                for (auto& p : playlists_)
                    if (p.value("id", std::string()) == ids[i]) online = p.value("online", 0);
            ui::text(std::to_string(online) + " playing", r.x + 16 * s, r.y + 56 * s, 16, ui::MUTED);
            ui::text(i == 0 ? "Every player for themselves" : i == 1 ? "Teams of 2, revive your partner" : "Teams of 4", r.x + 16 * s, r.y + 80 * s, 14, ui::MUTED);
            if (ui::hovered(r) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) playlist_ = ids[i];
        }
        y += 126 * s;
        if (ui::button({x, y, w, 70 * s}, fMm_.valid() ? "Joining queue..." : "PLAY", true, !fMm_.valid())) {
            fMm_ = api_.post("/api/matchmaking/join", {{"playlist", playlist_}});
            audio_.play(Sfx::Click, 0.5f);
        }
        y += 90 * s;
        if (!motd_.empty()) {
            Rectangle n{x, y, w, 80 * s};
            ui::panel(n);
            ui::text("NEWS", n.x + 16 * s, n.y + 12 * s, 14, ui::ACCENT);
            ui::text(motd_.substr(0, 90), n.x + 16 * s, n.y + 36 * s, 16, ui::TEXT);
            if (motd_.size() > 90) ui::text(motd_.substr(90, 90), n.x + 16 * s, n.y + 56 * s, 16, ui::TEXT);
            y += 100 * s;
        }
    }
    // Offline practice
    Rectangle op{x, y, w, 250 * s};
    ui::panel(op);
    ui::text(offline_ ? "Play against bots" : "Practice offline against bots", op.x + 20 * s, op.y + 16 * s, 22, WHITE);
    ui::text("Bots: " + std::to_string((int)offlineBots_), op.x + 20 * s, op.y + 58 * s, 18, ui::MUTED);
    ui::slider({op.x + 150 * s, op.y + 56 * s, op.width - 180 * s, 24 * s}, offlineBots_, 0, 99, 101);
    offlineBots_ = std::round(offlineBots_);
    ui::text("Difficulty", op.x + 20 * s, op.y + 98 * s, 18, ui::MUTED);
    const char* diffs[] = {"Easy", "Medium", "Hard", "Insane"};
    for (int i = 0; i < 4; i++)
        if (ui::button({op.x + 150 * s + i * 100 * s, op.y + 90 * s, 92 * s, 36 * s}, diffs[i], offlineDiff_ == i)) offlineDiff_ = i;
    ui::text("Mode", op.x + 20 * s, op.y + 144 * s, 18, ui::MUTED);
    const char* modes[] = {"Solo", "Duos", "Squads"};
    for (int i = 0; i < 3; i++)
        if (ui::button({op.x + 150 * s + i * 100 * s, op.y + 136 * s, 92 * s, 36 * s}, modes[i], offlineMode_ == i)) offlineMode_ = i;
    if (ui::button({op.x + 20 * s, op.y + 186 * s, op.width - 40 * s, 48 * s}, "Start offline match", offline_)) {
        saveSettings();
        startOffline();
    }
    y += 266 * s;
    // Direct connect
    Rectangle dc{x, y, w, 64 * s};
    ui::panel(dc);
    ui::text("Direct connect", dc.x + 20 * s, dc.y + 22 * s, 18, ui::MUTED);
    ui::textBox({dc.x + 170 * s, dc.y + 12 * s, dc.width - 320 * s, 40 * s}, directHost_, "host:port", 102, false, 64);
    if (ui::button({dc.x + dc.width - 136 * s, dc.y + 12 * s, 120 * s, 40 * s}, "Connect")) {
        std::string host = directHost_;
        uint16_t port = 7777;
        auto c = host.rfind(':');
        if (c != std::string::npos) { port = (uint16_t)std::atoi(host.substr(c + 1).c_str()); host = host.substr(0, c); }
        saveSettings();
        joinMatch(host, port, "");
    }
}

void App::drawLockerTab(Rectangle a) {
    float s = ui::scale();
    Loadout l = currentLoadout();
    // Slots column
    float y = a.y;
    for (int i = 0; i < 11; i++) {
        Rectangle r{a.x, y, 250 * s, 52 * s};
        bool sel = lockerSlot_ == i;
        DrawRectangleRounded(r, 0.12f, 6, sel ? Color{40, 70, 120, 240} : ui::PANEL);
        ui::text(kSlotNames[i], r.x + 14 * s, r.y + 6 * s, 14, ui::MUTED);
        const CosmeticDef* d = findCosmetic(equippedFor(l, i));
        ui::text(d ? d->name : "-", r.x + 14 * s, r.y + 26 * s, 18, d ? ui::rarityColor((int)d->rarity) : ui::TEXT);
        if (ui::hovered(r) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { lockerSlot_ = i; audio_.play(Sfx::Click, 0.3f); }
        y += 58 * s;
    }
    // Items grid
    Rectangle grid{a.x + 270 * s, a.y, a.width * 0.5f - 270 * s + a.width * 0.1f, a.height};
    CosmeticType t = slotType(lockerSlot_);
    std::string eq = equippedFor(l, lockerSlot_);
    float cell = 150 * s, gap = 12 * s;
    int cols = std::max(1, (int)((grid.width + gap) / (cell + gap)));
    int idx = 0;
    for (auto& c : cosmeticCatalog()) {
        if (c.type != t) continue;
        bool owned = ownsItem(c.id);
        int row = idx / cols, col = idx % cols;
        Rectangle r{grid.x + col * (cell + gap), grid.y + row * (cell * 0.72f + gap), cell, cell * 0.72f};
        Color rc = ui::rarityColor((int)c.rarity);
        DrawRectangleRounded(r, 0.1f, 6, owned ? ui::withAlpha(rc, 0.35f) : Color{30, 34, 44, 220});
        DrawRectangleRoundedLinesEx(r, 0.1f, 6, c.id == eq ? 3 : 1, c.id == eq ? WHITE : ui::withAlpha(rc, 0.8f));
        // Color swatch
        DrawRectangle((int)(r.x + 10 * s), (int)(r.y + 10 * s), (int)(26 * s), (int)(26 * s), C(c.style.primary));
        DrawRectangle((int)(r.x + 38 * s), (int)(r.y + 10 * s), (int)(26 * s), (int)(26 * s), C(c.style.secondary));
        ui::text(c.name, r.x + 10 * s, r.y + r.height - 44 * s, 15, owned ? WHITE : ui::MUTED);
        std::string sub = owned ? RARITY_NAMES[(int)c.rarity] : c.unlockLevel > 0 ? "Reach level " + std::to_string(c.unlockLevel) : "Item Shop";
        ui::text(sub, r.x + 10 * s, r.y + r.height - 22 * s, 13, owned ? rc : ui::MUTED);
        if (owned && ui::hovered(r) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            equip(c.id, lockerSlot_ >= 5 ? lockerSlot_ - 5 : 0);
            audio_.play(Sfx::Click, 0.4f);
        }
        idx++;
    }
    Rectangle prev{a.x + a.width * 0.62f, a.y, a.width * 0.38f, a.height};
    drawPreview(prev, currentLoadout());
}

void App::drawShopTab(Rectangle a) {
    float s = ui::scale();
    ui::text("ITEM SHOP", a.x, a.y, 30, WHITE);
    if (shop_.is_object()) {
        int64_t reset = shop_.value("reset_in", 0);
        ui::text("New items in " + std::to_string(reset / 3600) + "h " + std::to_string((reset % 3600) / 60) + "m", a.x + 220 * s, a.y + 8 * s, 18, ui::MUTED);
    }
    if (!shop_.is_object() || !shop_.contains("items")) { ui::text("Loading...", a.x, a.y + 60 * s, 20, ui::MUTED); return; }
    float cell = 260 * s, gap = 16 * s;
    int cols = std::max(1, (int)((a.width + gap) / (cell + gap)));
    int i = 0;
    for (auto& it : shop_["items"]) {
        std::string id = it.value("id", std::string());
        const CosmeticDef* c = findCosmetic(id);
        if (!c) continue;
        int row = i / cols, col = i % cols;
        Rectangle r{a.x + col * (cell + gap), a.y + 56 * s + row * (cell * 0.8f + gap), cell, cell * 0.8f};
        Color rc = rarityByName(it.value("rarity", std::string()));
        DrawRectangleRounded(r, 0.08f, 6, ui::withAlpha(rc, 0.3f));
        DrawRectangleRoundedLinesEx(r, 0.08f, 6, 2, rc);
        if (it.value("section", std::string()) == "featured") ui::text("FEATURED", r.x + 14 * s, r.y + 12 * s, 13, ui::GOLDEN);
        DrawRectangle((int)(r.x + 14 * s), (int)(r.y + 36 * s), (int)(46 * s), (int)(46 * s), C(c->style.primary));
        DrawRectangle((int)(r.x + 64 * s), (int)(r.y + 36 * s), (int)(46 * s), (int)(46 * s), C(c->style.secondary));
        DrawRectangle((int)(r.x + 114 * s), (int)(r.y + 36 * s), (int)(46 * s), (int)(46 * s), C(c->style.accent));
        ui::text(c->name, r.x + 14 * s, r.y + r.height - 88 * s, 20, WHITE);
        ui::text(std::string(COSMETIC_TYPE_NAMES[(int)c->type]) + " - " + RARITY_NAMES[(int)c->rarity], r.x + 14 * s, r.y + r.height - 62 * s, 14, rc);
        bool owned = it.value("owned", false);
        Rectangle b{r.x + 14 * s, r.y + r.height - 40 * s, r.width - 28 * s, 32 * s};
        if (owned) ui::button(b, "Owned", false, false);
        else if (ui::button(b, std::to_string(c->price) + " coins", true, !fBuy_.valid())) fBuy_ = api_.post("/api/shop/buy", {{"item_id", id}});
        i++;
    }
}

void App::drawCareerTab(Rectangle a) {
    float s = ui::scale();
    ui::text("CAREER", a.x, a.y, 30, WHITE);
    if (!account_.is_object()) return;
    json st = account_.value("stats", json::object());
    int wins = st.value("wins", 0), kills = st.value("kills", 0), matches = st.value("matches", 0), top10 = st.value("top10", 0);
    int deaths = std::max(1, matches - wins);
    char kd[32];
    std::snprintf(kd, sizeof(kd), "%.2f", (double)kills / deaths);
    struct Tile { const char* label; std::string value; } tiles[] = {
        {"Level", std::to_string(account_.value("level", 1))}, {"Wins", std::to_string(wins)}, {"Eliminations", std::to_string(kills)},
        {"Matches", std::to_string(matches)}, {"Top 10", std::to_string(top10)}, {"K/D", kd}};
    for (int i = 0; i < 6; i++) {
        Rectangle r{a.x + i * 190 * s, a.y + 50 * s, 180 * s, 90 * s};
        ui::panel(r);
        ui::text(tiles[i].label, r.x + 14 * s, r.y + 12 * s, 14, ui::MUTED);
        ui::text(tiles[i].value, r.x + 14 * s, r.y + 38 * s, 32, WHITE);
    }
    // Recent matches
    float y = a.y + 170 * s;
    ui::text("Recent matches", a.x, y, 22, WHITE);
    y += 34 * s;
    if (matches_.is_array()) {
        int n = 0;
        for (auto& m : matches_) {
            if (n++ >= 10) break;
            int place = m.value("placement", 0);
            std::string line = "#" + std::to_string(place) + "   " + m.value("playlist", std::string()) + "   " + std::to_string(m.value("kills", 0)) +
                               " elims   " + std::to_string((int)m.value("damage", 0.0)) + " dmg";
            ui::text(line, a.x, y, 18, place == 1 ? ui::GOLDEN : ui::TEXT);
            y += 26 * s;
        }
        if (matches_.empty()) ui::text("No matches yet - go drop in!", a.x, y, 18, ui::MUTED);
    }
    // Leaderboard
    float lx = a.x + a.width * 0.55f;
    float ly = a.y + 170 * s;
    ui::text("Leaderboard (wins)", lx, ly, 22, WHITE);
    ly += 34 * s;
    if (leaderboard_.is_array()) {
        int rank = 1;
        for (auto& p : leaderboard_) {
            if (rank > 15) break;
            ui::text(std::to_string(rank) + ". " + p.value("display_name", std::string()), lx, ly, 18, ui::TEXT);
            ui::textRight(std::to_string(p.value("wins", 0)) + " wins  " + std::to_string(p.value("kills", 0)) + " elims", lx + 420 * s, ly, 16, ui::MUTED);
            ly += 26 * s;
            rank++;
        }
    }
}

void App::drawSettingsTab(Rectangle a) {
    float s = ui::scale();
    Rectangle p{a.x, a.y, std::min(a.width, 700 * s), 470 * s};
    ui::panel(p);
    float x = p.x + 30 * s, w = p.width - 60 * s, y = p.y + 24 * s;
    ui::text("SETTINGS", x, y, 26, WHITE);
    y += 50 * s;
    float sens = settings_.sensitivity * 1000;
    ui::text("Mouse sensitivity", x, y, 18, ui::MUTED);
    if (ui::slider({x + 220 * s, y - 2 * s, w - 220 * s, 24 * s}, sens, 0.5f, 8.0f, 201)) settings_.sensitivity = sens / 1000;
    y += 44 * s;
    ui::text("Field of view: " + std::to_string((int)settings_.fov), x, y, 18, ui::MUTED);
    ui::slider({x + 220 * s, y - 2 * s, w - 220 * s, 24 * s}, settings_.fov, 60, 110, 202);
    y += 44 * s;
    ui::text("Volume", x, y, 18, ui::MUTED);
    if (ui::slider({x + 220 * s, y - 2 * s, w - 220 * s, 24 * s}, settings_.volume, 0, 1, 203)) audio_.masterVolume = settings_.volume;
    y += 44 * s;
    ui::text("View distance: " + std::to_string((int)settings_.viewDistance) + "m", x, y, 18, ui::MUTED);
    ui::slider({x + 220 * s, y - 2 * s, w - 220 * s, 24 * s}, settings_.viewDistance, 250, 1200, 204);
    y += 44 * s;
    ui::checkbox({x, y, w, 30 * s}, settings_.showFps, "Show FPS");
    y += 40 * s;
    ui::checkbox({x, y, w * 0.45f, 30 * s}, settings_.invertY, "Invert mouse Y");
    ui::checkbox({x + w * 0.5f, y, w * 0.5f, 30 * s}, settings_.shadows, "Real-time shadows");
    y += 50 * s;
    ui::text("Backend URL", x, y + 10 * s, 18, ui::MUTED);
    ui::textBox({x + 220 * s, y, w - 220 * s, 40 * s}, settings_.backendUrl, "http://host:8080", 205, false, 100);
    y += 60 * s;
    if (ui::button({x, y, 200 * s, 46 * s}, "Save", true)) {
        api_.baseUrl = settings_.backendUrl;
        saveSettings();
        setStatus("Settings saved");
    }
    // Controls reference
    Rectangle c{p.x + p.width + 20 * s, a.y, a.width - p.width - 20 * s, 470 * s};
    if (c.width > 250 * s) {
        ui::panel(c);
        const char* lines[] = {"WASD - move", "Space - jump / glide / leave drop ship", "Shift - sprint", "Ctrl - crouch",
                               "Mouse - aim, LMB fire, RMB aim down sights", "1-5 - inventory slots, F - pickaxe", "E - interact (hold for chests/revive)",
                               "R - reload / rotate piece", "Q - build mode, Z X C V - wall floor ramp roof", "RMB in build mode - change material",
                               "G - edit the piece you look at", "Tab - inventory, M - map", "B + 1-6 - emotes", "Esc - menu"};
        float yy = c.y + 24 * s;
        ui::text("CONTROLS", c.x + 24 * s, yy, 22, WHITE);
        yy += 40 * s;
        for (auto l : lines) { ui::text(l, c.x + 24 * s, yy, 16, ui::TEXT); yy += 26 * s; }
    }
}

void App::drawMatchmaking() {
    background();
    float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
    float s = ui::scale();
    float t = (float)GetTime();
    for (int i = 0; i < 12; i++) {
        float a = t * 3 + i * kPi / 6;
        DrawCircleV({W / 2 + std::cos(a) * 40 * s, H * 0.4f + std::sin(a) * 40 * s}, (3 + (i % 4)) * s, ui::withAlpha(ui::ACCENT, 0.3f + 0.05f * i));
    }
    ui::textCentered("Finding a match...", W / 2, H * 0.52f, 34, WHITE);
    int secs = (int)mmElapsed_;
    ui::textCentered(playlist_ + "  -  " + std::to_string(secs / 60) + ":" + (secs % 60 < 10 ? "0" : "") + std::to_string(secs % 60), W / 2, H * 0.52f + 48 * s, 20, ui::MUTED);
    if (ui::button({W / 2 - 90 * s, H * 0.66f, 180 * s, 48 * s}, "Cancel")) {
        api_.post("/api/matchmaking/cancel", {{"ticket", mmTicket_}});
        screen_ = Screen::Lobby;
    }
}

} // namespace client
