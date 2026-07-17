#pragma once
#include "chess.hpp"
namespace engine {
    // Looks at a board and returns a score. 
    // +ve = white winning, -ve = black winning, 0 = draw.
    int evaluate(const chess::Board& board);
}