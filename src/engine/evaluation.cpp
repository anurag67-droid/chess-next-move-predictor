#include "engine/evaluation.hpp"
#include "chess.hpp"
namespace engine {
int evaluate(const chess::Board& board) {
    int score = 0;
    for (int i = 0; i < 64; ++i) {
        chess::Piece piece = board.at(chess::Square(i));
        if (piece == chess::Piece::NONE) continue;
        int materialValue = 0;
        int colorSign = 1;
        if (piece == chess::Piece::WHITEPAWN)        materialValue = 100;
        else if (piece == chess::Piece::WHITEKNIGHT) materialValue = 320;
        else if (piece == chess::Piece::WHITEBISHOP) materialValue = 330;
        else if (piece == chess::Piece::WHITEROOK)   materialValue = 500;
        else if (piece == chess::Piece::WHITEQUEEN)  materialValue = 900;
        else if (piece == chess::Piece::WHITEKING)   materialValue = 20000;
        else if (piece == chess::Piece::BLACKPAWN)   { materialValue = 100; colorSign = -1; }
        else if (piece == chess::Piece::BLACKKNIGHT) { materialValue = 320; colorSign = -1; }
        else if (piece == chess::Piece::BLACKBISHOP) { materialValue = 330; colorSign = -1; }
        else if (piece == chess::Piece::BLACKROOK)   { materialValue = 500; colorSign = -1; }
        else if (piece == chess::Piece::BLACKQUEEN)  { materialValue = 900; colorSign = -1; }
        else if (piece == chess::Piece::BLACKKING)   { materialValue = 20000; colorSign = -1; }

        int file = i % 8;
        int rank = i / 8;        
        float centerDistSq = (file - 3.5f) * (file - 3.5f) + (rank - 3.5f) * (rank - 3.5f);
        int positionalBonus = 25 - static_cast<int>(centerDistSq);
        score += (materialValue + positionalBonus) * colorSign;
    }
    if (board.at(chess::Square(6)) == chess::Piece::WHITEKING || 
        board.at(chess::Square(2)) == chess::Piece::WHITEKING) score += 20;
    
    if (board.at(chess::Square(62)) == chess::Piece::BLACKKING || 
        board.at(chess::Square(58)) == chess::Piece::BLACKKING) score -= 20;
    return score;
}}