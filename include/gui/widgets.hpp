#pragma once

#include <SFML/System/Vector2.hpp>
#include <imgui.h>

// Small Dear ImGui helpers that give the side panel its look.
namespace gui {

inline ImVec2 toImVec2(sf::Vector2f v) { return {v.x, v.y}; }

void applyStyle(float scale);

// A rounded panel section. height 0 = fit the contents.
void beginCard(const char* id, float height = 0.f);
void endCard();
void cardTitle(ImFont* font, const char* text);

bool accentButton(const char* label, ImVec2 size);
// Switch-style checkbox.
bool toggle(const char* label, bool* value);
// Muted label on the left, with the next widget filling the rest of the row.
void rowLabel(const char* label);

} // namespace gui
