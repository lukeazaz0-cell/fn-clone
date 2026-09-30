#include "ui.h"

#include "font_data.h"
#include "rlgl.h"

#include <algorithm>
#include <cmath>

namespace ui {

const Color BG = {12, 16, 27, 255};
const Color PANEL = {22, 28, 44, 238};
const Color PANEL2 = {33, 43, 64, 255};
const Color LINE = {52, 64, 92, 255};
const Color TEXT = {233, 238, 247, 255};
const Color MUTED = {142, 154, 181, 255};
const Color ACCENT = {79, 163, 255, 255};
const Color GOOD = {62, 207, 142, 255};
const Color WARN = {242, 184, 75, 255};
const Color BAD = {239, 91, 91, 255};
const Color GOLDEN = {255, 205, 60, 255};
const Color PLAY = {255, 214, 58, 255};
const Color SHIELD = {72, 160, 255, 255};
const Color HEALTH = {86, 214, 104, 255};
const Color STORM = {178, 104, 255, 255};
const Color INK = {10, 13, 22, 255};

static int gFocus = -1;
static int gActiveSlider = -1;
static float gScale = 1.0f;

void beginFrame() {
    gScale = std::max(0.6f, GetScreenHeight() / 900.0f);
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) gActiveSlider = -1;
}

float scale() { return gScale; }
int px(float v) { return (int)std::round(v * gScale); }

static Font gBold{}, gSemi{};
static bool gFontsLoaded = false;

void loadFonts() {
    gBold = LoadFontFromMemory(".ttf", kFontBold, (int)kFontBoldSize, 72, nullptr, 0);
    gSemi = LoadFontFromMemory(".ttf", kFontSemiBold, (int)kFontSemiBoldSize, 48, nullptr, 0);
    GenTextureMipmaps(&gBold.texture);
    GenTextureMipmaps(&gSemi.texture);
    SetTextureFilter(gBold.texture, TEXTURE_FILTER_TRILINEAR);
    SetTextureFilter(gSemi.texture, TEXTURE_FILTER_TRILINEAR);
    gFontsLoaded = gBold.texture.id != 0 && gSemi.texture.id != 0;
}

void unloadFonts() {
    if (!gFontsLoaded) return;
    UnloadFont(gBold);
    UnloadFont(gSemi);
    gFontsLoaded = false;
}

// Font sizes below were tuned for raylib's small bitmap font; Titillium renders
// visually smaller at the same pixel size, so scale it up a bit.
static Font fontFor(float sz) {
    if (!gFontsLoaded) return GetFontDefault();
    return sz >= 22.0f ? gBold : gSemi;
}
static float sizeFor(float size) { return size * gScale * (gFontsLoaded ? 1.18f : 1.0f); }

float textWidth(const std::string& s, float size) {
    float sz = sizeFor(size);
    return MeasureTextEx(fontFor(size), s.c_str(), sz, gFontsLoaded ? 0.5f : sz / 10.0f).x;
}

std::vector<std::string> wrap(const std::string& s, float maxWidth, float size) {
    std::vector<std::string> lines;
    std::string line, word;
    auto flushWord = [&]() {
        if (word.empty()) return;
        std::string cand = line.empty() ? word : line + " " + word;
        if (!line.empty() && textWidth(cand, size) > maxWidth) { lines.push_back(line); line = word; }
        else line = cand;
        word.clear();
    };
    for (char ch : s) {
        if (ch == ' ' || ch == '\n') {
            flushWord();
            if (ch == '\n') { lines.push_back(line); line.clear(); }
        } else {
            word.push_back(ch);
        }
    }
    flushWord();
    if (!line.empty()) lines.push_back(line);
    return lines;
}

void text(const std::string& s, float x, float y, float size, Color c) {
    float sz = sizeFor(size);
    // Keep the visual center where the old font put it
    float yo = gFontsLoaded ? -sz * 0.08f : 0.0f;
    DrawTextEx(fontFor(size), s.c_str(), {std::round(x), std::round(y + yo)}, sz, gFontsLoaded ? 0.5f : sz / 10.0f, c);
}

void textShadow(const std::string& s, float x, float y, float size, Color c) {
    text(s, x + 2 * gScale, y + 2 * gScale, size, {0, 0, 0, (unsigned char)(c.a * 0.6f)});
    text(s, x, y, size, c);
}

void textCentered(const std::string& s, float cx, float y, float size, Color c) { text(s, cx - textWidth(s, size) / 2, y, size, c); }
void textRight(const std::string& s, float rx, float y, float size, Color c) { text(s, rx - textWidth(s, size), y, size, c); }

// ------------------------------------------------------------------ shapes

float ease(float t) { t = std::clamp(t, 0.0f, 1.0f); return t * t * (3 - 2 * t); }

static float roundnessFor(Rectangle r, float radiusPx) {
    float m = std::min(r.width, r.height);
    if (m <= 0) return 0;
    return std::clamp(2.0f * radiusPx * gScale / m, 0.0f, 1.0f);
}

void rrect(Rectangle r, float radius, Color c) { DrawRectangleRounded(r, roundnessFor(r, radius), 8, c); }

void rrectLine(Rectangle r, float radius, float thick, Color c) {
    DrawRectangleRoundedLinesEx(r, roundnessFor(r, radius), 8, thick * gScale, c);
}

// Rounded rectangle as a triangle fan with per-vertex vertical gradient.
void rrectGrad(Rectangle r, float radius, Color top, Color bottom) {
    float rad = std::min(radius * gScale, std::min(r.width, r.height) * 0.5f);
    auto colAt = [&](float y) {
        float t = std::clamp((y - r.y) / std::max(1.0f, r.height), 0.0f, 1.0f);
        return Color{(unsigned char)(top.r + (bottom.r - top.r) * t), (unsigned char)(top.g + (bottom.g - top.g) * t),
                     (unsigned char)(top.b + (bottom.b - top.b) * t), (unsigned char)(top.a + (bottom.a - top.a) * t)};
    };
    const int seg = 6;
    Vector2 pts[4 * (seg + 1)];
    int n = 0;
    Vector2 centers[4] = {{r.x + r.width - rad, r.y + rad}, {r.x + rad, r.y + rad}, {r.x + rad, r.y + r.height - rad}, {r.x + r.width - rad, r.y + r.height - rad}};
    for (int c = 0; c < 4; c++)
        for (int i = 0; i <= seg; i++) {
            float a = (c * 90.0f + 90.0f * i / seg) * DEG2RAD;
            pts[n++] = {centers[c].x + std::cos(a) * rad, centers[c].y - std::sin(a) * rad};
        }
    Vector2 ctr{r.x + r.width / 2, r.y + r.height / 2};
    Color cc = colAt(ctr.y);
    rlBegin(RL_TRIANGLES);
    for (int i = 0; i < n; i++) {
        Vector2 a = pts[i], b = pts[(i + 1) % n];
        Color ca = colAt(a.y), cb = colAt(b.y);
        // raylib's 2D winding (same as DrawTriangle)
        rlColor4ub(cc.r, cc.g, cc.b, cc.a); rlVertex2f(ctr.x, ctr.y);
        rlColor4ub(ca.r, ca.g, ca.b, ca.a); rlVertex2f(a.x, a.y);
        rlColor4ub(cb.r, cb.g, cb.b, cb.a); rlVertex2f(b.x, b.y);
    }
    rlEnd();
}

void shadow(Rectangle r, float radius, float spread, float alpha) {
    const int steps = 5;
    for (int i = steps; i >= 1; i--) {
        float e = spread * gScale * i / steps;
        Rectangle q{r.x - e, r.y - e + spread * gScale * 0.35f, r.width + 2 * e, r.height + 2 * e};
        rrect(q, radius + spread * i / steps, Color{0, 0, 0, (unsigned char)(255 * alpha / steps * 0.9f)});
    }
}

void card(Rectangle r, float radius, Color fill) {
    shadow(r, radius, 10, 0.28f);
    Color top{(unsigned char)std::min(255, fill.r + 10), (unsigned char)std::min(255, fill.g + 12), (unsigned char)std::min(255, fill.b + 16), fill.a};
    rrectGrad(r, radius, top, fill);
    rrectLine(r, radius, 1.0f, Color{255, 255, 255, 22});
}

void panel(Rectangle r, Color c, float round) {
    shadow(r, 12, 8, 0.22f);
    DrawRectangleRounded(r, round, 8, c);
    DrawRectangleRoundedLinesEx(r, round, 8, 1.0f, Color{255, 255, 255, 18});
}

bool hovered(Rectangle r) { return CheckCollisionPointRec(GetMousePosition(), r); }

// ------------------------------------------------------------------ widgets

bool button(Rectangle r, const std::string& label, bool primary, bool enabled) {
    bool h = enabled && hovered(r);
    bool down = h && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    Rectangle q = r;
    if (down) { q.y += 1 * gScale; }
    if (primary) {
        Color top = enabled ? (h ? Color{120, 192, 255, 255} : Color{95, 176, 255, 255}) : withAlpha(PANEL2, 0.6f);
        Color bot = enabled ? (h ? Color{70, 150, 245, 255} : Color{58, 136, 235, 255}) : withAlpha(PANEL2, 0.6f);
        if (enabled) shadow(q, 10, h ? 8 : 5, h ? 0.35f : 0.25f);
        rrectGrad(q, 10, top, bot);
        if (enabled) rrectLine(q, 10, 1.0f, Color{255, 255, 255, (unsigned char)(h ? 90 : 50)});
    } else {
        Color top = enabled ? (h ? Color{52, 66, 98, 255} : Color{40, 51, 76, 255}) : withAlpha(PANEL2, 0.5f);
        Color bot = enabled ? (h ? Color{42, 54, 82, 255} : Color{31, 40, 61, 255}) : withAlpha(PANEL2, 0.5f);
        rrectGrad(q, 10, top, bot);
        rrectLine(q, 10, 1.0f, h ? withAlpha(ACCENT, 0.9f) : Color{255, 255, 255, 26});
    }
    float size = std::min(20.0f, q.height / gScale * 0.42f);
    Color fg = primary ? (enabled ? Color{6, 22, 44, 255} : MUTED) : (enabled ? TEXT : MUTED);
    textCentered(label, q.x + q.width / 2, q.y + q.height / 2 - size * gScale * 0.55f, size, fg);
    return h && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

bool bigButton(Rectangle r, const std::string& label, const std::string& sub, bool enabled) {
    bool h = enabled && hovered(r);
    bool down = h && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    float t = (float)GetTime();
    Rectangle q = r;
    if (down) q.y += 2 * gScale;
    if (enabled) {
        // Soft pulsing glow
        float pulse = 0.5f + 0.5f * std::sin(t * 3.0f);
        for (int i = 4; i >= 1; i--) {
            float e = (4 + i * 3 + pulse * 3) * gScale;
            rrect({q.x - e, q.y - e, q.width + 2 * e, q.height + 2 * e}, 16 + i * 3, Color{255, 214, 60, (unsigned char)(10 + (h ? 8 : 0))});
        }
        shadow(q, 14, 10, 0.4f);
    }
    Color top = enabled ? (h ? Color{255, 236, 120, 255} : Color{255, 226, 84, 255}) : withAlpha(PANEL2, 0.8f);
    Color bot = enabled ? (h ? Color{255, 190, 40, 255} : Color{245, 176, 20, 255}) : withAlpha(PANEL2, 0.8f);
    rrectGrad(q, 14, top, bot);
    // Diagonal sheen sweeping across
    if (enabled) {
        BeginScissorMode((int)q.x, (int)q.y, (int)q.width, (int)q.height);
        float sweep = std::fmod(t * 0.35f, 1.6f) - 0.3f;
        float sx = q.x + q.width * sweep;
        DrawTriangle({sx, q.y}, {sx - 40 * gScale, q.y + q.height}, {sx - 10 * gScale, q.y + q.height}, Color{255, 255, 255, 60});
        DrawTriangle({sx, q.y}, {sx - 10 * gScale, q.y + q.height}, {sx + 30 * gScale, q.y}, Color{255, 255, 255, 60});
        EndScissorMode();
        rrectLine(q, 14, 1.5f, Color{255, 255, 255, 120});
    }
    Color ink = enabled ? Color{40, 24, 0, 255} : MUTED;
    float big = std::min(46.0f, q.height / gScale * 0.44f);
    float yTitle = q.y + q.height / 2 - big * gScale * (sub.empty() ? 0.6f : 0.85f);
    textCentered(label, q.x + q.width / 2, yTitle, big, ink);
    if (!sub.empty()) textCentered(sub, q.x + q.width / 2, yTitle + big * gScale * 1.1f, 15, withAlpha(ink, 0.75f));
    return h && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

static Rectangle gTabLine{};
static bool gTabLineInit = false;

bool tab(Rectangle r, const std::string& label, bool active) {
    bool h = hovered(r);
    if (h && !active) rrect({r.x + 8 * gScale, r.y + 10 * gScale, r.width - 16 * gScale, r.height - 20 * gScale}, 8, Color{255, 255, 255, 10});
    textCentered(label, r.x + r.width / 2, r.y + r.height / 2 - 11 * gScale, 21, active ? TEXT : (h ? TEXT : MUTED));
    return h && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

void tabUnderline(Rectangle active) {
    Rectangle target{active.x + active.width * 0.2f, active.y + active.height - 4 * gScale, active.width * 0.6f, 4 * gScale};
    if (!gTabLineInit) { gTabLine = target; gTabLineInit = true; }
    float k = std::min(1.0f, GetFrameTime() * 14.0f);
    gTabLine.x += (target.x - gTabLine.x) * k;
    gTabLine.width += (target.width - gTabLine.width) * k;
    gTabLine.y = target.y;
    gTabLine.height = target.height;
    DrawRectangleRounded(gTabLine, 1.0f, 6, PLAY);
}

bool textBox(Rectangle r, std::string& value, const std::string& placeholder, int id, bool password, size_t maxLen) {
    bool h = hovered(r);
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        if (h) gFocus = id;
        else if (gFocus == id) gFocus = -1;
    }
    bool focusedNow = gFocus == id;
    if (focusedNow) rrect({r.x - 3 * gScale, r.y - 3 * gScale, r.width + 6 * gScale, r.height + 6 * gScale}, 12, withAlpha(ACCENT, 0.18f));
    rrectGrad(r, 9, Color{20, 26, 40, 255}, Color{26, 33, 50, 255});
    rrectLine(r, 9, focusedNow ? 1.5f : 1.0f, focusedNow ? ACCENT : (h ? Color{80, 96, 130, 255} : LINE));
    bool enter = false;
    if (focusedNow) {
        int ch;
        while ((ch = GetCharPressed()) > 0) {
            if (ch >= 32 && ch < 127 && value.size() < maxLen) value.push_back((char)ch);
        }
        if ((IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) && !value.empty()) value.pop_back();
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)) enter = true;
        if ((IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_LEFT_SUPER)) && IsKeyPressed(KEY_V)) {
            const char* clip = GetClipboardText();
            if (clip) {
                for (const char* p = clip; *p && value.size() < maxLen; p++)
                    if (*p >= 32 && *p < 127) value.push_back(*p);
            }
        }
    }
    std::string shown = password ? std::string(value.size(), '*') : value;
    float size = 19;
    float ty = r.y + r.height / 2 - size * gScale * 0.55f;
    BeginScissorMode((int)r.x, (int)r.y, (int)r.width, (int)r.height);
    if (shown.empty() && !focusedNow) text(placeholder, r.x + 14 * gScale, ty, size, withAlpha(MUTED, 0.8f));
    else {
        float w = textWidth(shown, size);
        float offset = std::max(0.0f, w - (r.width - 28 * gScale));
        text(shown, r.x + 14 * gScale - offset, ty, size, TEXT);
        if (focusedNow && std::fmod(GetTime(), 1.0) < 0.55)
            DrawRectangle((int)(r.x + 16 * gScale + w - offset), (int)(r.y + r.height * 0.25f), (int)std::max(2.0f, 2 * gScale), (int)(r.height * 0.5f), ACCENT);
    }
    EndScissorMode();
    return enter;
}

bool slider(Rectangle r, float& value, float lo, float hi, int id) {
    bool h = hovered(r);
    if (h && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) gActiveSlider = id;
    bool changed = false;
    bool active = gActiveSlider == id && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    if (active) {
        float t = std::clamp((GetMousePosition().x - r.x) / r.width, 0.0f, 1.0f);
        float nv = lo + (hi - lo) * t;
        if (nv != value) { value = nv; changed = true; }
    }
    float t = std::clamp((value - lo) / (hi - lo), 0.0f, 1.0f);
    Rectangle track{r.x, r.y + r.height / 2 - 3 * gScale, r.width, 6 * gScale};
    DrawRectangleRounded(track, 1.0f, 6, Color{16, 21, 33, 255});
    if (t > 0.01f) {
        Rectangle fill{track.x, track.y, track.width * t, track.height};
        DrawRectangleRounded(fill, 1.0f, 6, ACCENT);
    }
    Vector2 knob{r.x + r.width * t, r.y + r.height / 2};
    float kr = (active ? 10.5f : (h ? 10.0f : 9.0f)) * gScale;
    DrawCircleV({knob.x, knob.y + 1.5f * gScale}, kr + 1, Color{0, 0, 0, 90});
    DrawCircleV(knob, kr, WHITE);
    DrawCircleV(knob, kr * 0.45f, ACCENT);
    return changed;
}

bool checkbox(Rectangle r, bool& value, const std::string& label) {
    bool h = hovered(r);
    // Toggle switch
    Rectangle sw{r.x, r.y + r.height / 2 - 12 * gScale, 44 * gScale, 24 * gScale};
    DrawRectangleRounded(sw, 1.0f, 8, value ? ACCENT : Color{16, 21, 33, 255});
    DrawRectangleRoundedLinesEx(sw, 1.0f, 8, 1.0f * gScale, h ? withAlpha(ACCENT, 0.9f) : Color{255, 255, 255, 30});
    float kx = value ? sw.x + sw.width - 12 * gScale : sw.x + 12 * gScale;
    DrawCircleV({kx, sw.y + sw.height / 2}, 9 * gScale, WHITE);
    text(label, sw.x + sw.width + 12 * gScale, r.y + r.height / 2 - 11 * gScale, 19, TEXT);
    if (h && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { value = !value; return true; }
    return false;
}

float keycap(const std::string& key, float x, float y, float h) {
    float sz = h / gScale * 0.52f;
    float w = std::max(h, textWidth(key, sz) + 14 * gScale);
    Rectangle r{x, y, w, h};
    rrect({r.x, r.y + 2 * gScale, r.width, r.height}, 6, Color{0, 0, 0, 120});
    rrectGrad(r, 6, Color{250, 250, 252, 255}, Color{212, 216, 226, 255});
    textCentered(key, r.x + r.width / 2, r.y + r.height / 2 - sz * gScale * 0.58f, sz, Color{24, 28, 40, 255});
    return w;
}

float chip(const std::string& label, float x, float y, float size, Color fg, Color bg, Icon ic, bool withIcon) {
    float h = size * gScale * 1.7f;
    float iw = withIcon ? h * 0.8f : 0;
    float w = textWidth(label, size) + h * 0.9f + iw;
    Rectangle r{x, y, w, h};
    rrect(r, h / gScale / 2, bg);
    if (withIcon) icon(ic, x + h * 0.55f + iw * 0.1f, y + h / 2, h * 0.62f, fg);
    text(label, x + h * 0.45f + iw, y + h / 2 - size * gScale * 0.58f, size, fg);
    return w;
}

void meter(Rectangle r, float t, Color fill, Color back, int segments) {
    t = std::clamp(t, 0.0f, 1.0f);
    float rad = r.height / gScale * 0.35f;
    rrect(r, rad, back);
    if (t > 0.005f) {
        Rectangle f{r.x, r.y, std::max(r.height * 0.6f, r.width * t), r.height};
        Color top{(unsigned char)std::min(255, fill.r + 40), (unsigned char)std::min(255, fill.g + 40), (unsigned char)std::min(255, fill.b + 40), fill.a};
        rrectGrad(f, rad, top, fill);
    }
    for (int i = 1; i < segments; i++) {
        float x = r.x + r.width * i / segments;
        DrawRectangle((int)x - 1, (int)r.y, std::max(2, (int)(2 * gScale)), (int)r.height, Color{0, 0, 0, 110});
    }
}

void ring(Vector2 c, float radius, float thick, float t, Color fg, Color bg) {
    DrawRing(c, radius - thick, radius, 0, 360, 48, bg);
    if (t > 0) DrawRing(c, radius - thick, radius, -90, -90 + 360 * std::clamp(t, 0.0f, 1.0f), 48, fg);
}

bool setMouseCaptured(bool captured) {
    if (captured == IsCursorHidden()) return false;
    if (captured) DisableCursor();
    else EnableCursor();
    return true;
}

void clearFocus() { gFocus = -1; }
int focused() { return gFocus; }

Color withAlpha(Color c, float a) { return Color{c.r, c.g, c.b, (unsigned char)std::clamp(c.a * a, 0.0f, 255.0f)}; }

Color rarityColor(int rarity) {
    switch (rarity) {
        case 0: return {170, 170, 170, 255};
        case 1: return {96, 170, 58, 255};
        case 2: return {73, 172, 242, 255};
        case 3: return {177, 91, 226, 255};
        case 4: return {226, 140, 60, 255};
        default: return WHITE;
    }
}

} // namespace ui
