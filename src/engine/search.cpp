#include "engine/search.hpp"
#include "engine/evaluation.hpp"
#include <algorithm>
#include <iostream>
#include <vector>
#include <chrono>

namespace engine {

long long nodes = 0;
bool timeIsUp = false;
std::chrono::time_point<std::chrono::steady_clock> startTime;
int limitMs = 0;
enum HashFlag{ HASH_EXACT, HASH_ALPHA, HASH_BETA };
struct TTEntry {
    uint64_t zobristKey = 0;
    int depth = -1;
    int score = 0;
    HashFlag flag = HASH_EXACT;
};
const int TT_SIZE = 1000000;
std::vector<TTEntry> transpositionTable(TT_SIZE);
int scoreMove(const chess::Board& board, const chess::Move& move) {
    int score = 0;
    if (move.typeOf() == chess::Move::PROMOTION) score += 900; 
    if (board.isCapture(move)){
        auto getPieceValue = [](chess::PieceType pt) {
            if (pt == chess::PieceType::PAWN) return 100;
            else if (pt == chess::PieceType::KNIGHT) return 320;
            else if (pt == chess::PieceType::BISHOP) return 330;
            else if (pt == chess::PieceType::ROOK) return 500;
            else if (pt == chess::PieceType::QUEEN) return 900;
            else if (pt == chess::PieceType::KING) return 20000;
            return 0;
        };

        int victimValue = getPieceValue(board.at(move.to()).type());
        int attackerValue = getPieceValue(board.at(move.from()).type());
        score += (10 * victimValue) - attackerValue;
    }
    return score;
}
int quiescence(chess::Board& board, int alpha, int beta, bool isMaximizing) {
    nodes++;
    // Draw Detection
    if (board.isRepetition() || board.isHalfMoveDraw() || board.isInsufficientMaterial()) {
        return 0;
    }

    int standPat = evaluate(board);
    if (isMaximizing) {
        if (standPat >= beta) return beta;
        if (alpha < standPat) alpha = standPat;
    } else {
        if (standPat <= alpha) return alpha;
        if (beta > standPat) beta = standPat;
    }
    chess::Movelist moves;
    chess::movegen::legalmoves(moves, board);
    // Checkmate/Stalemate detection inside Q-Search
    if (moves.empty()) {
        if (board.inCheck()) return isMaximizing ? -20000 : 20000;
        return 0; // Stalemate
    }
    std::sort(moves.begin(), moves.end(), [&board](const chess::Move& a, const chess::Move& b){
        return scoreMove(board, a) > scoreMove(board, b);
    });
    if (isMaximizing) {
        int maxScore = standPat;
        for (const auto& move : moves) {
            if (!board.isCapture(move)) continue;
            board.makeMove(move);
            int score = quiescence(board, alpha, beta, false);
            board.unmakeMove(move);

            if (timeIsUp) return 0;
            if (score > maxScore) maxScore = score;
            if (score > alpha) alpha = score;
            if (beta <= alpha) break;
        }
        return maxScore;
    } 
    else {
        int minScore = standPat;
        for (const auto& move : moves) {
            if (!board.isCapture(move)) continue;
            
            board.makeMove(move);
            int score = quiescence(board, alpha, beta, true);
            board.unmakeMove(move);

            if (timeIsUp) return 0;
            if (score < minScore) minScore = score;
            if (score < beta) beta = score;
            if (beta <= alpha) break;
        }
        return minScore;
    }
}
int minimax(chess::Board& board, int depth, int alpha, int beta, bool isMaximizing) {
    nodes++;
    if ((nodes & 2047) == 0){
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime).count();
        if (elapsed >= limitMs) timeIsUp = true;
    }
    if (timeIsUp) return 0;
    // draw detection
    if (board.isRepetition() || board.isHalfMoveDraw() || board.isInsufficientMaterial()) return 0;
    uint64_t hash = board.zobrist();
    int ttIndex = hash % TT_SIZE;
    TTEntry& ttEntry = transpositionTable[ttIndex];

    if (ttEntry.zobristKey == hash && ttEntry.depth >= depth) {
        if (ttEntry.flag == HASH_EXACT) return ttEntry.score;
        if (ttEntry.flag == HASH_ALPHA && ttEntry.score <= alpha) return alpha;
        if (ttEntry.flag == HASH_BETA && ttEntry.score >= beta) return beta;
    }
    if (depth == 0) return quiescence(board, alpha, beta, isMaximizing);
    chess::Movelist moves;
    chess::movegen::legalmoves(moves, board);
    if (moves.empty()){
        if (board.inCheck()) return isMaximizing ? -20000 : 20000; 
        return 0;//stalemate
    }
    int originalAlpha = alpha;
    std::sort(moves.begin(), moves.end(), [&board](const chess::Move& a, const chess::Move& b){
        return scoreMove(board, a) > scoreMove(board, b);
    });
    int bestScore = isMaximizing ? -99999 : 99999; 
    if (isMaximizing) { 
        for (const auto& move : moves) {
            board.makeMove(move);
            int score = minimax(board, depth - 1, alpha, beta, false);
            board.unmakeMove(move); 
            if (timeIsUp) return 0;
            if (score > bestScore) bestScore = score;
            if (score > alpha) alpha = score;
            if (beta <= alpha) break; 
        }
    } 
    else { 
        for (const auto& move : moves) {
            board.makeMove(move);
            int score = minimax(board, depth - 1, alpha, beta, true);
            board.unmakeMove(move);
            if (timeIsUp) return 0;
            if (score < bestScore) bestScore = score;
            if (score < beta) beta = score;
            if (beta <= alpha) break; 
        }
    }  
    ttEntry.zobristKey = hash;
    ttEntry.depth = depth;
    ttEntry.score = bestScore;
    if (bestScore <= originalAlpha) ttEntry.flag = HASH_ALPHA; 
    else if (bestScore >= beta) ttEntry.flag = HASH_BETA;  
    else ttEntry.flag = HASH_EXACT;
    return bestScore;
}

std::string getBestMoveTime(chess::Board board, int timeLimitMs) {
    nodes = 0; 
    timeIsUp = false;
    limitMs = timeLimitMs;
    startTime = std::chrono::steady_clock::now();
    std::fill(transpositionTable.begin(), transpositionTable.end(), TTEntry());
    chess::Movelist moves;
    chess::movegen::legalmoves(moves, board);
    std::sort(moves.begin(), moves.end(), [&board](const chess::Move& a, const chess::Move& b) {
        return scoreMove(board, a) > scoreMove(board, b);
    });
    std::string overallBestMove = "";
    bool isWhite = (board.sideToMove() == chess::Color::WHITE);  
    for (int currentDepth = 1; currentDepth <= 64; currentDepth++) {
        int alpha = -99999;
        int beta = 99999;
        int bestScore = isWhite ? -99999 : 99999;
        std::string currentDepthBestMove = "";       
        for (const auto& move : moves) {
            board.makeMove(move);
            int score = minimax(board, currentDepth - 1, alpha, beta, !isWhite);
            board.unmakeMove(move);

            if (timeIsUp) break;
            if (isWhite) {
                if (score > bestScore) { bestScore = score; currentDepthBestMove = chess::uci::moveToUci(move); }
                if (score > alpha) alpha = score; 
            } 
            else {
                if (score < bestScore) { bestScore = score; currentDepthBestMove = chess::uci::moveToUci(move); }
                if (score < beta) beta = score; 
            }
        }
        if (timeIsUp){
            std::cout << "Time's up! Aborting Depth " << currentDepth << std::endl;
            break; 
        }
        overallBestMove = currentDepthBestMove;
        std::cout << "Depth " << currentDepth << " completed. Nodes: " << nodes << " | Best Move so far: " << overallBestMove << std::endl;
    }
    return overallBestMove;
}
float getFutureEvaluation(chess::Board board, int depth) {
    timeIsUp = false;
    nodes = 0; 
    bool isWhite = (board.sideToMove() == chess::Color::WHITE);
    int scoreInCentipawns = minimax(board, depth, -99999, 99999, isWhite);
    return static_cast<float>(scoreInCentipawns) / 100.0f;
}}