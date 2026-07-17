#pragma once
#include "chess.hpp"
#include <string>
namespace engine {
    extern long long nodes;
    // time limit in milliseconds
    std::string getBestMoveTime(chess::Board board, int timeLimitMs);
    // quick depth 4 search to return a stable evaluation for the UI
    float getFutureEvaluation(chess::Board board, int depth);
}