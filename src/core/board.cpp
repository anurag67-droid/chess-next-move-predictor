#include "core/board.hpp"
namespace core {
Board::Board(){}
std::string Board::getFen() const { return m_board.getFen(); }
bool Board::tryMove(const std::string& uciMove) {
    chess::Movelist moves;
    chess::movegen::legalmoves(moves, m_board);
    for (const auto& move : moves) {
        if (chess::uci::moveToUci(move) == uciMove) {
            m_board.makeMove(move);
            return true; 
        }
    }
    std::string promotionMove = uciMove + "q";
    for (const auto& move : moves) {
        if (chess::uci::moveToUci(move) == promotionMove) {
            m_board.makeMove(move);
            return true;
        }
    }
    return false;
}}