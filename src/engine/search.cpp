#include "engine/search.hpp"
#include "engine/evaluation.hpp"
#include <algorithm>
#include <iostream>
#include <vector>
#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>

namespace engine {

std::atomic<long long> nodes{0};
std::atomic<bool> timeIsUp{false};
std::chrono::time_point<std::chrono::steady_clock> startTime;
int limitMs = 0;
enum HashFlag{ HASH_EXACT, HASH_ALPHA, HASH_BETA };
struct TTEntry {
    std::atomic<uint64_t> key{0};
    std::atomic<uint64_t> data{0};
};
const int TT_SIZE = 1000000;
std::vector<TTEntry> transpositionTable(TT_SIZE);

bool readTT(uint64_t hash, int& depth, int& score, HashFlag& flag, chess::Move& bestMove) {
    int ttIndex = hash % TT_SIZE;
    uint64_t k = transpositionTable[ttIndex].key.load(std::memory_order_relaxed);
    uint64_t d = transpositionTable[ttIndex].data.load(std::memory_order_relaxed);
    if ((k ^ d) == hash) {
        depth = static_cast<int>(d & 0xFF);
        int16_t signedScore = static_cast<int16_t>((d >> 8) & 0xFFFF);
        score = static_cast<int>(signedScore);
        flag = static_cast<HashFlag>((d >> 24) & 0x3);
        bestMove = chess::Move(static_cast<uint16_t>((d >> 26) & 0xFFFF));
        return true;
    }
    return false;
}

void writeTT(uint64_t hash, int depth, int score, HashFlag flag, chess::Move bestMove) {
    int ttIndex = hash % TT_SIZE;
    uint64_t d = 0;
    d |= (static_cast<uint64_t>(depth) & 0xFF);
    d |= ((static_cast<uint64_t>(static_cast<uint16_t>(score))) & 0xFFFF) << 8;
    d |= (static_cast<uint64_t>(flag) & 0x3) << 24;
    d |= (static_cast<uint64_t>(bestMove.move()) & 0xFFFF) << 26;

    transpositionTable[ttIndex].data.store(d, std::memory_order_relaxed);
    transpositionTable[ttIndex].key.store(hash ^ d, std::memory_order_relaxed);
}

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
    nodes.fetch_add(1, std::memory_order_relaxed);
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
    if (moves.empty()) {
        if (board.inCheck()) return isMaximizing ? -20000 : 20000;
        return 0; 
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

            if (timeIsUp.load(std::memory_order_relaxed)) return 0;
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

            if (timeIsUp.load(std::memory_order_relaxed)) return 0;
            if (score < minScore) minScore = score;
            if (score < beta) beta = score;
            if (beta <= alpha) break;
        }
        return minScore;
    }
}

int minimax(chess::Board& board, int depth, int alpha, int beta, bool isMaximizing) {
    long long n = nodes.fetch_add(1, std::memory_order_relaxed);
    if ((n & 2047) == 0){
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime).count();
        if (elapsed >= limitMs) timeIsUp.store(true, std::memory_order_relaxed);
    }
    if (timeIsUp.load(std::memory_order_relaxed)) return 0;
    
    if (board.isRepetition() || board.isHalfMoveDraw() || board.isInsufficientMaterial()) return 0;
    
    uint64_t hash = board.zobrist();
    int ttDepth, ttScore;
    HashFlag ttFlag;
    chess::Move ttMove = chess::Move(0);

    if (readTT(hash, ttDepth, ttScore, ttFlag, ttMove)) {
        if (ttDepth >= depth) {
            if (ttFlag == HASH_EXACT) return ttScore;
            if (ttFlag == HASH_ALPHA && ttScore <= alpha) return alpha;
            if (ttFlag == HASH_BETA && ttScore >= beta) return beta;
        }
    }

    if (depth <= 0) return quiescence(board, alpha, beta, isMaximizing);

    bool inCheck = board.inCheck();
    
    if (depth >= 3 && !inCheck) {
        board.makeNullMove();
        int nullScore = 0;
        if (isMaximizing) {
            nullScore = minimax(board, depth - 3, beta - 1, beta, false);
            board.unmakeNullMove();
            if (timeIsUp.load(std::memory_order_relaxed)) return 0;
            if (nullScore >= beta) return beta;
        } else {
            nullScore = minimax(board, depth - 3, alpha, alpha + 1, true);
            board.unmakeNullMove();
            if (timeIsUp.load(std::memory_order_relaxed)) return 0;
            if (nullScore <= alpha) return alpha;
        }
    }

    chess::Movelist moves;
    chess::movegen::legalmoves(moves, board);
    if (moves.empty()){
        if (inCheck) return isMaximizing ? -20000 : 20000; 
        return 0;
    }

    int originalAlpha = alpha;
    
    auto scoreMoveForSort = [&board, ttMove](const chess::Move& move) {
        if (move == ttMove) return 1000000;
        return scoreMove(board, move);
    };

    std::sort(moves.begin(), moves.end(), [&scoreMoveForSort](const chess::Move& a, const chess::Move& b){
        return scoreMoveForSort(a) > scoreMoveForSort(b);
    });

    int bestScore = isMaximizing ? -99999 : 99999; 
    chess::Move bestMoveInPos = chess::Move(0);

    if (isMaximizing) { 
        for (const auto& move : moves) {
            board.makeMove(move);
            int score = minimax(board, depth - 1, alpha, beta, false);
            board.unmakeMove(move); 
            if (timeIsUp.load(std::memory_order_relaxed)) return 0;
            if (score > bestScore) {
                bestScore = score;
                bestMoveInPos = move;
            }
            if (score > alpha) alpha = score;
            if (beta <= alpha) break; 
        }
    } 
    else { 
        for (const auto& move : moves) {
            board.makeMove(move);
            int score = minimax(board, depth - 1, alpha, beta, true);
            board.unmakeMove(move);
            if (timeIsUp.load(std::memory_order_relaxed)) return 0;
            if (score < bestScore) {
                bestScore = score;
                bestMoveInPos = move;
            }
            if (score < beta) beta = score;
            if (beta <= alpha) break; 
        }
    }  
    
    HashFlag flag;
    if (bestScore <= originalAlpha) flag = HASH_ALPHA; 
    else if (bestScore >= beta) flag = HASH_BETA;  
    else flag = HASH_EXACT;
    
    writeTT(hash, depth, bestScore, flag, bestMoveInPos);
    
    return bestScore;
}

void iterativeDeepening(chess::Board board, int threadID, std::string& outBestMove) {
    chess::Movelist moves;
    chess::movegen::legalmoves(moves, board);
    std::sort(moves.begin(), moves.end(), [&board](const chess::Move& a, const chess::Move& b) {
        return scoreMove(board, a) > scoreMove(board, b);
    });
    
    chess::Move prevBestMove = chess::Move(0);
    bool isWhite = (board.sideToMove() == chess::Color::WHITE);  
    
    for (int currentDepth = 1; currentDepth <= 64; currentDepth++) {
        int depthToSearch = currentDepth;
        if (threadID != 0) {
            depthToSearch = currentDepth + (threadID % 2); 
        }
        
        int alpha = -99999;
        int beta = 99999;
        int bestScore = isWhite ? -99999 : 99999;
        std::string currentDepthBestMove = "";
        chess::Move currentBestMoveObj = chess::Move(0);

        if (prevBestMove != chess::Move(0)) {
            auto it = std::find(moves.begin(), moves.end(), prevBestMove);
            if (it != moves.end()) {
                std::rotate(moves.begin(), it, it + 1);
            }
        }
       
        for (const auto& move : moves) {
            board.makeMove(move);
            int score = minimax(board, depthToSearch - 1, alpha, beta, !isWhite);
            board.unmakeMove(move);

            if (timeIsUp.load(std::memory_order_relaxed)) break;
            if (isWhite) {
                if (score > bestScore) { bestScore = score; currentDepthBestMove = chess::uci::moveToUci(move); currentBestMoveObj = move; }
                if (score > alpha) alpha = score; 
            } 
            else {
                if (score < bestScore) { bestScore = score; currentDepthBestMove = chess::uci::moveToUci(move); currentBestMoveObj = move; }
                if (score < beta) beta = score; 
            }
        }
        
        if (timeIsUp.load(std::memory_order_relaxed)){
            if (threadID == 0) {
                std::cout << "Time's up! Aborting Depth " << depthToSearch << std::endl;
            }
            break; 
        }
        
        prevBestMove = currentBestMoveObj;
        
        if (threadID == 0) {
            outBestMove = currentDepthBestMove;
            std::cout << "Depth " << depthToSearch << " completed. Nodes: " << nodes.load(std::memory_order_relaxed) 
                      << " | Best Move so far: " << outBestMove << std::endl;
        }
    }
}

std::string getBestMoveTime(chess::Board board, int timeLimitMs) {
    nodes.store(0, std::memory_order_relaxed); 
    timeIsUp.store(false, std::memory_order_relaxed);
    limitMs = timeLimitMs;
    startTime = std::chrono::steady_clock::now();
    
    for (auto& entry : transpositionTable) {
        entry.key.store(0, std::memory_order_relaxed);
    }

    int numThreads = std::thread::hardware_concurrency();
    if (numThreads == 0) numThreads = 4;
    
    std::string overallBestMove = "";
    std::vector<std::thread> threads;
    
    for (int i = 1; i < numThreads; i++) {
        threads.emplace_back(iterativeDeepening, board, i, std::ref(overallBestMove));
    }
    
    iterativeDeepening(board, 0, overallBestMove);
    
    for (auto& t : threads) {
        if (t.joinable()) t.join();
    }
    
    return overallBestMove;
}

float getFutureEvaluation(chess::Board board, int depth) {
    timeIsUp.store(false, std::memory_order_relaxed);
    nodes.store(0, std::memory_order_relaxed); 
    startTime = std::chrono::steady_clock::now();
    limitMs = 9999999; // Give it infinite time for this quick check
    bool isWhite = (board.sideToMove() == chess::Color::WHITE);
    int scoreInCentipawns = minimax(board, depth, -99999, 99999, isWhite);
    return static_cast<float>(scoreInCentipawns) / 100.0f;
}}
