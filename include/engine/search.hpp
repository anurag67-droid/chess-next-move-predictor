#pragma once

#include "chess.hpp"

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <memory>

namespace engine {

constexpr int kMateScore = 30000;
constexpr int kMaxPly = 128;

inline bool isMateScore(int score) { return std::abs(score) >= kMateScore - kMaxPly; }

// Full moves until mate for a mate score (positive: White mates, negative: Black mates).
inline int mateInMoves(int score)
{
    const int moves = (kMateScore - std::abs(score) + 1) / 2;
    return score > 0 ? moves : -moves;
}

struct SearchLimits {
    int maxDepth = 64;
    int timeMs = 0;  // 0 = no time limit
    int threads = 0; // 0 = one per hardware thread
    // Optional flag another thread can set to abort the search early.
    const std::atomic<bool>* stopSignal = nullptr;
};

struct SearchResult {
    chess::Move bestMove{chess::Move::NO_MOVE};
    int score = 0; // centipawns from White's point of view
    int depth = 0; // deepest fully completed iteration
    std::uint64_t nodes = 0;
    std::int64_t elapsedMs = 0;
};

class TranspositionTable;

// Iterative-deepening alpha-beta (negamax) search with a transposition table,
// null-move pruning, check extensions, quiescence search and Lazy SMP threading.
class Searcher {
public:
    using ProgressCallback = std::function<void(const SearchResult&)>;

    Searcher();
    ~Searcher();

    // Blocks until a limit is reached. onIteration is called after every completed depth.
    SearchResult search(const chess::Board& board, const SearchLimits& limits,
                        const ProgressCallback& onIteration = {});

    // Forget everything learned from previous searches (e.g. when a new game starts).
    void clear();

private:
    std::unique_ptr<TranspositionTable> m_tt;
};

} // namespace engine
