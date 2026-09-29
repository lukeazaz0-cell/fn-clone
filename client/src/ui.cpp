#include "ui.h"

#include <algorithm>
#include <cmath>

namespace ui {

const Color BG = {14, 19, 31, 255};
const Color PANEL = {24, 31, 48, 235};
const Color PANEL2 = {33, 43, 64, 255};
const Color LINE = {48, 60, 88, 255};
const Color TEXT = {233, 238, 247, 255};
const Color MUTED = {142, 154, 181, 255};
const Color ACCENT = {79, 163, 255, 255};
const Color GOOD = {62, 207, 142, 255};
const Color WARN = {242, 184, 75, 255};
const Color BAD = {239, 91, 91, 255};
const Color GOLDEN = {255, 205, 60, 255};

static int gFocus = -1;
static int gActiveSlider = -1;
static float gScale = 1.0f;

void beginFrame() {
    gScale = std::max(0.6f, GetScreenHeight() / 900.0f);
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) gActiveSlider = -1;
}

float scale() { return gScale; }
int px(float v) { return (int)std::round(v * gScale); }

static Font font() { return GetFontDefault(); }

float textWidth(const std::string& s, float size) {
    float sz = size * gScale;
    return MeasureTextEx(font(), s.c_str(), sz, sz / 10.0f).x;
}

void text(const std::string& s, float x, float y, float size, Color c) {
    float sz = size * gScale;
    DrawTextEx(font(), s.c_str(), {x, y}, sz, sz / 10.0f, c);
}

void textShadow(const std::string& s, float x, float y, float size, Color c) {
    text(s, x + 2 * gScale, y + 2 * gScale, size, {0, 0, 0, (unsigned char)(c.a * 0.6f)});
    text(s, x, y, size, c);
}

void textCentered(const std::string& s, float cx, float y, float size, Color c) { text(s, cx - textWidth(s, size) / 2, y, size, c); }
void textRight(const std::string& s, float rx, float y, float size, Color c) { text(s, rx - textWidth(s, size), y, size, c); }

void panel(Rectangle r, Color c, float round) { DrawRectangleRounded(r, round, 6, c); }

bool hovered(Rectangle r) { return CheckCollisionPointRec(GetMousePosition(), r); }

bool button(Rectangle r, const std::string& label, bool primary, bool enabled) {
    bool h = enabled && hovered(r);
    Color bg = primary ? ACCENT : PANEL2;
    if (!enabled) bg = withAlpha(PANEL2, 0.5f);
    else if (h) bg = primary ? Color{110, 185, 255, 255} : Color{45, 58, 86, 255};
    DrawRectangleRounded(r, 0.2f, 6, bg);
    if (!primary) DrawRectangleRoundedLinesEx(r, 0.2f, 6, 1.0f, h ? ACCENT : LINE);
    float size = std::min(22.0f, r.height / gScale * 0.45f);
    textCentered(label, r.x + r.width / 2, r.y + r.height / 2 - size * gScale / 2, size, primary ? Color{6, 20, 38, 255} : (enabled ? TEXT : MUTED));
    return h && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

bool tab(Rectangle r, const std::string& label, bool active) {
    bool h = hovered(r);
    if (active) DrawRectangleRec({r.x, r.y + r.height - 3 * gScale, r.width, 3 * gScale}, ACCENT);
    textCentered(label, r.x + r.width / 2, r.y + r.height / 2 - 11 * gScale, 22, active ? TEXT : (h ? TEXT : MUTED));
    return h && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

bool textBox(Rectangle r, std::string& value, const std::string& placeholder, int id, bool password, size_t maxLen) {
    bool h = hovered(r);
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        if (h) gFocus = id;
        else if (gFocus == id) gFocus = -1;
    }
    bool focusedNow = gFocus == id;
    DrawRectangleRounded(r, 0.2f, 6, PANEL2);
    DrawRectangleRoundedLinesEx(r, 0.2f, 6, 1.0f, focusedNow ? ACCENT : LINE);
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
    float size = 20;
    float ty = r.y + r.height / 2 - size * gScale / 2;
    BeginScissorMode((int)r.x, (int)r.y, (int)r.width, (int)r.height);
    if (shown.empty() && !focusedNow) text(placeholder, r.x + 10 * gScale, ty, size, MUTED);
    else {
        float w = textWidth(shown, size);
        float offset = std::max(0.0f, w - (r.width - 24 * gScale));
        text(shown, r.x + 10 * gScale - offset, ty, size, TEXT);
        if (focusedNow && std::fmod(GetTime(), 1.0) < 0.55) DrawRectangle((int)(r.x + 12 * gScale + w - offset), (int)ty, 2, (int)(size * gScale), TEXT);
    }
    EndScissorMode();
    return enter;
}

bool slider(Rectangle r, float& value, float lo, float hi, int id) {
    bool h = hovered(r);
    if (h && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) gActiveSlider = id;
    bool changed = false;
    if (gActiveSlider == id && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        float t = std::clamp((GetMousePosition().x - r.x) / r.width, 0.0f, 1.0f);
        float nv = lo + (hi - lo) * t;
        if (nv != value) { value = nv; changed = true; }
    }
    float t = (value - lo) / (hi - lo);
    DrawRectangleRounded({r.x, r.y + r.height / 2 - 3 * gScale, r.width, 6 * gScale}, 1.0f, 6, PANEL2);
    DrawRectangleRounded({r.x, r.y + r.height / 2 - 3 * gScale, r.width * t, 6 * gScale}, 1.0f, 6, ACCENT);
    DrawCircle((int)(r.x + r.width * t), (int)(r.y + r.height / 2), 9 * gScale, h || gActiveSlider == id ? WHITE : TEXT);
    return changed;
}

bool checkbox(Rectangle r, bool& value, const std::string& label) {
    bool h = hovered(r);
    Rectangle box{r.x, r.y + r.height / 2 - 11 * gScale, 22 * gScale, 22 * gScale};
    DrawRectangleRounded(box, 0.25f, 4, value ? ACCENT : PANEL2);
    DrawRectangleRoundedLinesEx(box, 0.25f, 4, 1.0f, h ? ACCENT : LINE);
    if (value) textCentered("x", box.x + box.width / 2, box.y + 2 * gScale, 18, Color{6, 20, 38, 255});
    text(label, box.x + box.width + 10 * gScale, r.y + r.height / 2 - 10 * gScale, 20, TEXT);
    if (h && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { value = !value; return true; }
    return false;
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
