#pragma once

#include <SFML/Graphics/Color.hpp>
#include <imgui.h>

#include <array>

// Colours shared by the SFML board and the ImGui panel.
namespace gui {

inline ImVec4 toImVec4(sf::Color c, float alpha = 1.f)
{
    return {c.r / 255.f, c.g / 255.f, c.b / 255.f, alpha * c.a / 255.f};
}

inline ImU32 toImU32(sf::Color c, float alpha = 1.f)
{
    return ImGui::ColorConvertFloat4ToU32(toImVec4(c, alpha));
}

namespace color {
inline const sf::Color Background{22, 24, 28};
inline const sf::Color Panel{29, 32, 37};
inline const sf::Color Card{36, 40, 46};
inline const sf::Color CardHover{44, 49, 56};
inline const sf::Color Border{50, 55, 63};
inline const sf::Color Text{232, 234, 237};
inline const sf::Color TextMuted{140, 147, 157};
inline const sf::Color Accent{118, 172, 82};
inline const sf::Color AccentHover{135, 192, 97};
inline const sf::Color AccentActive{96, 146, 64};
inline const sf::Color Danger{224, 76, 76};
inline const sf::Color Warning{232, 170, 60};
inline const sf::Color Arrow{82, 148, 232, 210};
inline const sf::Color Highlight{255, 235, 59, 110};
inline const sf::Color EvalWhite{240, 240, 236};
inline const sf::Color EvalBlack{48, 50, 54};
} // namespace color

struct BoardTheme {
    const char* name;
    sf::Color light;
    sf::Color dark;
};

inline const std::array<BoardTheme, 4> kBoardThemes = {{
    {"Green", {235, 236, 208}, {119, 149, 86}},
    {"Wood", {240, 217, 181}, {181, 136, 99}},
    {"Blue", {222, 227, 230}, {140, 162, 173}},
    {"Slate", {200, 205, 212}, {101, 112, 128}},
}};

} // namespace gui
