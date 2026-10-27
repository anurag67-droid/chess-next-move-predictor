#pragma once

#include "chess.hpp"

#include <cstdint>
#include <string>

// Text and number formatting for the interface.
namespace gui {

inline const char* colorName(chess::Color c) { return c == chess::Color::WHITE ? "White" : "Black"; }

// "+0.35" / "M3"; without the sign for the narrow eval bar.
std::string formatScore(int score, bool withSign);
// "White is slightly better", "Black mates in 3", ...
std::string describeScore(int score);
// Fraction of the eval bar owned by White, using a logistic win-chance curve.
float evalBarShare(int score);
std::string formatNodes(std::uint64_t nodes);
std::string formatClock(float seconds);

} // namespace gui
