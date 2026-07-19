#include "engine/evaluation.hpp"
#include "chess.hpp"
#include <cmath>
#include <algorithm>

namespace engine {
int evaluate(const chess::Board& board) {
    int score = 0;
    int whitePawnsOnFile[8] = {0};
    int blackPawnsOnFile[8] = {0};
    int totalPawns = 0;
    int whiteBishops = 0;
    int blackBishops = 0;    
    int phase = 0; // max 24 (knights=1, bishops=1, rooks=2, queens=4)
    
    // Mop-up trackers
    int whiteKingSq = 0;
    int blackKingSq = 0;

    //map the pawns and pieces
    for (int i = 0; i < 64; ++i) {
        chess::Piece piece = board.at(chess::Square(i));
        int file = i % 8;
        if (piece == chess::Piece::WHITEPAWN) {
            whitePawnsOnFile[file]++;
            totalPawns++;
        } 
        else if (piece == chess::Piece::BLACKPAWN) {
            blackPawnsOnFile[file]++;
            totalPawns++;
        }
        else if (piece == chess::Piece::WHITEBISHOP) { whiteBishops++; phase += 1; }
        else if (piece == chess::Piece::BLACKBISHOP) { blackBishops++; phase += 1; }
        else if (piece == chess::Piece::WHITEKNIGHT || piece == chess::Piece::BLACKKNIGHT) phase += 1;
        else if (piece == chess::Piece::WHITEROOK || piece == chess::Piece::BLACKROOK) phase += 2;
        else if (piece == chess::Piece::WHITEQUEEN || piece == chess::Piece::BLACKQUEEN) phase += 4;
        
        // Track Kings for endgame mop-up
        else if (piece == chess::Piece::WHITEKING) { whiteKingSq = i; }
        else if (piece == chess::Piece::BLACKKING) { blackKingSq = i; }
    }

    //close or open posn helpful for knight and bishop strengths
    bool isClosed = (totalPawns >= 12);
    bool isOpen = (totalPawns <= 8);
    // middlegame/endgame blend
    if (phase > 24) phase = 24; //stay at 24 in case of pawn promotions
    int mgWeight = phase;           // 24 at start, 0 in pure endgame
    int egWeight = 24 - phase;      // 0 at start, 24 in pure endgame
    //bishop pair bonus
    if (whiteBishops >= 2) score += 30;
    if (blackBishops >= 2) score -= 30;
    
    // piece evaluation
    for (int i = 0; i < 64; ++i) {
        chess::Piece piece = board.at(chess::Square(i));
        if (piece == chess::Piece::NONE) continue;
        int materialValue = 0;
        int positionalBonus = 0;
        int colorSign = 1; // 1 for White, -1 for Black
        int file = i%8;
        int rank = i/8;
        
        //center proximity bonus
        float centerDistSq = (file-3.5f)*(file-3.5f) + (rank-3.5f) * (rank-3.5f);
        positionalBonus += 25 - static_cast<int>(centerDistSq);

        switch (piece.internal()) {
            case chess::Piece::WHITEPAWN: {
                materialValue = 100;
                // double pawn loss
                if (whitePawnsOnFile[file] > 1) positionalBonus -= 15;
                // isolated pawn loss
                bool isolated = true;
                if (file > 0 && whitePawnsOnFile[file -1] > 0) isolated = false;
                if (file < 7 && whitePawnsOnFile[file +1] > 0) isolated = false;
                if (isolated) positionalBonus -= 20;
                // passed pawn adv
                bool passed = true;
                for (int r = rank + 1; r < 8; ++r) {
                    if (board.at(chess::Square(r * 8 + file)) == chess::Piece::BLACKPAWN) passed = false;
                    if (file > 0 && board.at(chess::Square(r * 8 + file - 1)) == chess::Piece::BLACKPAWN) passed = false;
                    if (file < 7 && board.at(chess::Square(r * 8 + file + 1)) == chess::Piece::BLACKPAWN) passed = false;
                }
                if (passed) positionalBonus += 20 + (rank * 10); //passed pawns adv if closer to promotion 
                break;
            }
            case chess::Piece::WHITEKNIGHT:
                materialValue = 320;
                if (isClosed) positionalBonus += 15; //knight good for close posn
                if (isOpen) positionalBonus -= 10;
                if (file == 0 || file == 7) positionalBonus -= 15; // knight on edge penalty
                break;
            case chess::Piece::WHITEBISHOP:
                materialValue = 330;
                if (isOpen) positionalBonus += 15;   //bishops good for open posn
                if (isClosed) positionalBonus -= 10;
                break;
            case chess::Piece::WHITEROOK:
                materialValue = 500;
                if (whitePawnsOnFile[file] == 0) positionalBonus += 15; //rooks good for open file
                if (rank == 6) positionalBonus += 30; // rook 7th rank adv
                break;
            case chess::Piece::WHITEQUEEN: materialValue = 900; break;

            // king safety & activity
            case chess::Piece::WHITEKING:{
                materialValue = 20000;
                int mgBonus = 0;
                int egBonus = 0;
                
                //king to center = penalty
                mgBonus -= (25 - static_cast<int>(centerDistSq));
                // middlegame score (pawn shield check)
                if (rank == 0 && (file == 2 || file == 6)) {
                    int shieldPawns = whitePawnsOnFile[file] + 
                                     (file > 0 ? whitePawnsOnFile[file-1] : 0) + 
                                     (file < 7 ? whitePawnsOnFile[file+1] : 0);
                    if (shieldPawns < 2) mgBonus -= 40;
                } 
                //king danger penalty
                else {
                    mgBonus -= 40; // Flat penalty for not being tucked away in castling
                    mgBonus -= rank*15; //penalty for walking up the board (e.g. rank 3 = -45)
                }
                // endgame score calculation
                egBonus += 30 - static_cast<int>(centerDistSq * 3);
                //bonus based on the phase
                positionalBonus += (mgBonus * mgWeight + egBonus * egWeight) / 24;
                break;
            }
            case chess::Piece::BLACKPAWN: {
                materialValue = 100;
                colorSign = -1;
                if (blackPawnsOnFile[file] > 1) positionalBonus -= 15;
                bool isolated = true;
                if (file > 0 && blackPawnsOnFile[file - 1] > 0) isolated = false;
                if (file < 7 && blackPawnsOnFile[file + 1] > 0) isolated = false;
                if (isolated) positionalBonus -= 20;
                bool passed = true;
                for (int r = rank - 1; r >= 0; --r) {
                    if (board.at(chess::Square(r * 8 + file)) == chess::Piece::WHITEPAWN) passed = false;
                    if (file > 0 && board.at(chess::Square(r * 8 + file - 1)) == chess::Piece::WHITEPAWN) passed = false;
                    if (file < 7 && board.at(chess::Square(r * 8 + file + 1)) == chess::Piece::WHITEPAWN) passed = false;
                }
                if (passed) positionalBonus += 20 + ((7 - rank) * 10);
                break;
            }
            case chess::Piece::BLACKKNIGHT:
                materialValue = 320;
                colorSign = -1;
                if (isClosed) positionalBonus += 15;
                if (isOpen) positionalBonus -= 10;
                if (file == 0 || file == 7) positionalBonus -= 15;
                break;
            case chess::Piece::BLACKBISHOP:
                materialValue = 330;
                colorSign = -1;
                if (isOpen) positionalBonus += 15;
                if (isClosed) positionalBonus -= 10;
                break;
            case chess::Piece::BLACKROOK:
                materialValue = 500;
                colorSign = -1;
                if (blackPawnsOnFile[file] == 0) positionalBonus += 15;
                if (rank == 1) positionalBonus += 30;
                break;
            case chess::Piece::BLACKQUEEN:
                materialValue = 900;
                colorSign = -1;
                break; 
            case chess::Piece::BLACKKING: {
                materialValue = 20000;
                colorSign = -1;
                int mgBonus = 0;
                int egBonus = 0;
                mgBonus -= (25-static_cast<int>(centerDistSq));
                if (rank == 7 && (file == 2 || file == 6)) {
                    int shieldPawns = blackPawnsOnFile[file] + 
                                     (file > 0 ? blackPawnsOnFile[file-1] : 0) + 
                                     (file < 7 ? blackPawnsOnFile[file+1] : 0);
                    if (shieldPawns < 2) mgBonus -= 40;
                }
                else{
                    mgBonus -= 40; 
                    mgBonus -= (7-rank)*15;
                }
                
                egBonus += 30 - static_cast<int>(centerDistSq * 3);               
                positionalBonus += (mgBonus * mgWeight + egBonus * egWeight) / 24;
                break;
            }
            default: break;
        }
        score += (materialValue + positionalBonus) * colorSign;
    }
    //castling bonus (tends to 0 as endgame arrives)
    int castlingBonus = 0;
    if (board.at(chess::Square(6)) == chess::Piece::WHITEKING || 
        board.at(chess::Square(2)) == chess::Piece::WHITEKING) castlingBonus += 20;
    if (board.at(chess::Square(62)) == chess::Piece::BLACKKING || 
        board.at(chess::Square(58)) == chess::Piece::BLACKKING) castlingBonus -= 20;
    //formula
    score += (castlingBonus * mgWeight) / 24;
    
    if (egWeight > 0) {
        int mopUpScore = 0;
        // x and y coordinates for both kings (0 to 7)
        int wKFile = whiteKingSq % 8, wKRank = whiteKingSq / 8;
        int bKFile = blackKingSq % 8, bKRank = blackKingSq / 8;
        // Manhattan Distance between the two kings
        int kingDist = std::abs(wKFile - bKFile) + std::abs(wKRank - bKRank);
        if (score > 400) {
            //pushing opp king to the edges/corners
            int bCenterDist = std::max(3 - bKFile, bKFile - 4) + std::max(3 - bKRank, bKRank - 4);
            mopUpScore += bCenterDist * 10;
            //move king closer to opp king bonus
            mopUpScore += (14 - kingDist) * 4;
            // blend based on how deep in the endgame we are
            score += (mopUpScore * egWeight) / 24;
        }
        else if (score < -400) {
            int wCenterDist = std::max(3 - wKFile, wKFile - 4) + std::max(3 - wKRank, wKRank - 4);
            mopUpScore += wCenterDist * 10;
            mopUpScore += (14 - kingDist) * 4;
            score -= (mopUpScore * egWeight) / 24;
        }
    }
    return score;
}}