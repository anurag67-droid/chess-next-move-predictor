#pragma once
#include "chess.hpp"
#include <string>
namespace core {
    class Board {
        chess::Board m_board;
    public:
        Board();
        // current board state as a string for renderer
        std::string getFen() const;
        // true if the legal move, false if illegal
        bool tryMove(const std::string& uciMove);
        const chess::Board& getInternalBoard() const { return m_board; }
    };
}