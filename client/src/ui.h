// Tiny immediate-mode UI helpers on top of raylib.
#pragma once
#include <string>

#include "raylib.h"

namespace ui {

// Palette
extern const Color BG, PANEL, PANEL2, LINE, TEXT, MUTED, ACCENT, GOOD, WARN, BAD, GOLDEN;

void beginFrame();
float scale();                       // UI scale factor based on window height
int px(float v);                     // scale a design-space pixel value

void text(const std::string& s, float x, float y, float size, Color c);
void textCentered(const std::string& s, float cx, float y, float size, Color c);
void textRight(const std::string& s, float rx, float y, float size, Color c);
float textWidth(const std::string& s, float size);
void textShadow(const std::string& s, float x, float y, float size, Color c);

void panel(Rectangle r, Color c = PANEL, float round = 0.12f);
bool button(Rectangle r, const std::string& label, bool primary = false, bool enabled = true);
bool tab(Rectangle r, const std::string& label, bool active);
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
