// Tiny immediate-mode UI helpers on top of raylib.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "raylib.h"

namespace ui {

// Palette
extern const Color BG, PANEL, PANEL2, LINE, TEXT, MUTED, ACCENT, GOOD, WARN, BAD, GOLDEN;
extern const Color PLAY, SHIELD, HEALTH, STORM, INK;

// Vector icons drawn from primitives (no image assets needed).
enum class Icon {
    Shield, Health, Person, Users, Crosshair, Storm, Clock, Coin, Gear, Lock, Star, Check, Trophy, Skull,
    Wood, Brick, Metal, Bullet, Car, Map, Backpack, Bolt, Play, Globe, Flag, Parachute, Chart, Bag, Sparkle, Pickaxe, Hammer, Close
};
void icon(Icon i, float cx, float cy, float size, Color c);
// Side-on pictogram of an inventory item (si::ItemType), pointing right.
void itemIcon(unsigned char itemType, float cx, float cy, float size, Color c);

void loadFonts();   // call after InitWindow
void unloadFonts(); // call before CloseWindow
void beginFrame();
float scale();                       // UI scale factor based on window height
int px(float v);                     // scale a design-space pixel value

void text(const std::string& s, float x, float y, float size, Color c);
void textCentered(const std::string& s, float cx, float y, float size, Color c);
void textRight(const std::string& s, float rx, float y, float size, Color c);
float textWidth(const std::string& s, float size);
// Greedy word wrap to a pixel width.
std::vector<std::string> wrap(const std::string& s, float maxWidth, float size);
void textShadow(const std::string& s, float x, float y, float size, Color c);

void panel(Rectangle r, Color c = PANEL, float round = 0.12f);
// Shapes with a corner radius in (unscaled) pixels, independent of the rectangle size.
void rrect(Rectangle r, float radius, Color c);
void rrectGrad(Rectangle r, float radius, Color top, Color bottom);
void rrectLine(Rectangle r, float radius, float thick, Color c);
void shadow(Rectangle r, float radius, float spread, float alpha = 0.35f);
// Card: shadow + fill + hairline border + subtle top highlight.
void card(Rectangle r, float radius = 12, Color fill = PANEL);
// Key hint like [E]; returns its width.
float keycap(const std::string& key, float x, float y, float h);
// Rounded label chip; returns its width.
float chip(const std::string& label, float x, float y, float size, Color fg, Color bg, Icon ic = Icon::Close, bool withIcon = false);
// Horizontal meter with optional segment ticks.
void meter(Rectangle r, float t, Color fill, Color back, int segments = 0);
// Circular progress ring.
void ring(Vector2 c, float radius, float thick, float t, Color fg, Color bg);
// Hero call-to-action (big yellow button with a pulse); returns true when clicked.
bool bigButton(Rectangle r, const std::string& label, const std::string& sub, bool enabled = true);
float ease(float t); // smoothstep
bool button(Rectangle r, const std::string& label, bool primary = false, bool enabled = true);
bool tab(Rectangle r, const std::string& label, bool active);
// Animated underline for a row of tabs: call after drawing tabs with the active rect.
void tabUnderline(Rectangle active);
// Returns true when Enter is pressed while focused.
bool textBox(Rectangle r, std::string& value, const std::string& placeholder, int id, bool password = false, size_t maxLen = 32);
bool slider(Rectangle r, float& value, float lo, float hi, int id);
bool checkbox(Rectangle r, bool& value, const std::string& label);
bool hovered(Rectangle r);
// Capture/release the mouse only when the state changes. raylib's Enable/DisableCursor
// warp the pointer to the window center, so calling them every frame freezes the mouse.
// Returns true on the frame the state changed.
bool setMouseCaptured(bool captured);
void clearFocus();
int focused();

Color withAlpha(Color c, float a);
Color rarityColor(int rarity);

} // namespace ui
