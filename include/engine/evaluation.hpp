#pragma once

#include "chess.hpp"

namespace engine {

// Static evaluation in centipawns from White's point of view
// (positive = White is better, negative = Black is better).
int evaluate(const chess::Board& board);

} // namespace engine
