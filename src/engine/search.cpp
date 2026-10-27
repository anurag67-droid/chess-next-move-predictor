#include "engine/search.hpp"

#include "engine/evaluation.hpp"

#include <algorithm>
#include <chrono>
#include <thread>
#include <vector>

namespace engine {

using Clock = std::chrono::steady_clock;

namespace {

constexpr int kInfinity = kMateScore + 1;
constexpr int kMateBound = kMateScore - kMaxPly;
constexpr int kNullMoveReduction = 2;
constexpr std::uint64_t kNodeBatch = 1024; // how often a thread checks the clock

enum class Bound : std::uint8_t { Exact, Upper, Lower };

// Mate scores are stored relative to the node instead of the root so they stay
// valid when the same position is reached at a different ply.
int scoreToTT(int score, int ply)
{
    if (score >= kMateBound) return score + ply;
    if (score <= -kMateBound) return score - ply;
    return score;
}

int scoreFromTT(int score, int ply)
{
    if (score >= kMateBound) return score - ply;
    if (score <= -kMateBound) return score + ply;
    return score;
}

int pieceValue(chess::PieceType type)
{
    static constexpr int kValues[] = {100, 320, 330, 500, 900, 20000, 0};
    return kValues[static_cast<int>(type)];
}

// Higher is searched first: hash move, then promotions and captures (MVV-LVA).
void orderMoves(const chess::Board& board, chess::Movelist& moves, chess::Move hashMove)
{
    for (auto& move : moves) {
        int score = 0;
        if (move == hashMove) {
            score = 30000;
        } else {
            if (move.typeOf() == chess::Move::PROMOTION) score += 900;
            if (board.isCapture(move))
                score += 10 * pieceValue(board.at(move.to()).type()) - pieceValue(board.at(move.from()).type());
        }
        move.setScore(static_cast<std::int16_t>(score));
    }
    std::sort(moves.begin(), moves.end(),
              [](const chess::Move& a, const chess::Move& b) { return a.score() > b.score(); });
}

} // namespace

// Lock-free, always-replace hash table shared by all search threads. Each slot
// stores key ^ data so a torn write from another thread is detected as a miss.
class TranspositionTable {
public:
    struct Entry {
        int depth = 0;
        int score = 0;
        Bound bound = Bound::Exact;
        chess::Move move{chess::Move::NO_MOVE};
    };

    explicit TranspositionTable(std::size_t slotCount = 1 << 20) : m_slots(slotCount) {}

    bool probe(std::uint64_t key, Entry& out) const
    {
        const Slot& slot = m_slots[key & (m_slots.size() - 1)];
        const std::uint64_t data = slot.data.load(std::memory_order_relaxed);
        if ((slot.key.load(std::memory_order_relaxed) ^ data) != key) return false;

        out.depth = static_cast<int>(data & 0xFF);
        out.score = static_cast<std::int16_t>((data >> 8) & 0xFFFF);
        out.bound = static_cast<Bound>((data >> 24) & 0x3);
        out.move = chess::Move(static_cast<std::uint16_t>((data >> 26) & 0xFFFF));
        return true;
    }

    void store(std::uint64_t key, const Entry& e)
    {
        const std::uint64_t data = (static_cast<std::uint64_t>(e.depth) & 0xFF)
                                 | (static_cast<std::uint64_t>(static_cast<std::uint16_t>(e.score)) << 8)
                                 | (static_cast<std::uint64_t>(e.bound) << 24)
                                 | (static_cast<std::uint64_t>(e.move.move()) << 26);
        Slot& slot = m_slots[key & (m_slots.size() - 1)];
        slot.data.store(data, std::memory_order_relaxed);
        slot.key.store(key ^ data, std::memory_order_relaxed);
    }

    void clear()
    {
        for (auto& slot : m_slots) {
            slot.key.store(0, std::memory_order_relaxed);
            slot.data.store(0, std::memory_order_relaxed);
        }
    }

private:
    struct Slot {
        std::atomic<std::uint64_t> key{0};
        std::atomic<std::uint64_t> data{0};
    };
    std::vector<Slot> m_slots; // size must be a power of two
};

namespace {

// State shared by every thread taking part in one search.
struct SharedState {
    TranspositionTable& tt;
    Clock::time_point start;
    Clock::time_point deadline;
    const std::atomic<bool>* stopSignal;
    std::atomic<bool> stop{false};
    std::atomic<std::uint64_t> nodes{0};
};

class SearchThread {
public:
    SearchThread(const chess::Board& board, SharedState& shared) : m_board(board), m_shared(shared) {}

    // Iterative deepening. Helper threads (id > 0) search some iterations one ply
    // deeper so they fill the shared table with different work than the main thread.
    SearchResult run(int maxDepth, int id, const Searcher::ProgressCallback& onIteration)
    {
        SearchResult result;
        chess::Movelist rootMoves;
        chess::movegen::legalmoves(rootMoves, m_board);
        if (rootMoves.empty()) {
            result.score = m_board.inCheck() ? -kMateScore : 0;
            if (m_board.sideToMove() == chess::Color::BLACK) result.score = -result.score;
            return result;
        }
        orderMoves(m_board, rootMoves, chess::Move(chess::Move::NO_MOVE));
        result.bestMove = rootMoves[0]; // fallback if not even depth 1 finishes

        for (int depth = 1; depth <= maxDepth; ++depth) {
            const int searchDepth = depth + (id % 2);
            chess::Move bestMove = rootMoves[0];
            const int score = searchRoot(rootMoves, searchDepth, bestMove);
            if (m_shared.stop.load(std::memory_order_relaxed)) break;

            // Search the best move first in the next iteration.
            const auto best = std::find(rootMoves.begin(), rootMoves.end(), bestMove);
            std::rotate(rootMoves.begin(), best, best + 1);

            result.bestMove = bestMove;
            result.score = m_board.sideToMove() == chess::Color::WHITE ? score : -score;
            result.depth = searchDepth;
            result.nodes = m_shared.nodes.load(std::memory_order_relaxed) + m_pendingNodes;
            result.elapsedMs = elapsedMs();
            if (onIteration) onIteration(result);

            if (score >= kMateBound) break; // found a forced mate: no need to look deeper
        }
        flushNodes();
        return result;
    }

private:
    int searchRoot(chess::Movelist& moves, int depth, chess::Move& bestMove)
    {
        int alpha = -kInfinity;
        int bestScore = -kInfinity;
        for (const auto& move : moves) {
            m_board.makeMove(move);
            const int score = -negamax(depth - 1, 1, -kInfinity, -alpha);
            m_board.unmakeMove(move);
            if (m_shared.stop.load(std::memory_order_relaxed)) return 0;

            if (score > bestScore) {
                bestScore = score;
                bestMove = move;
                alpha = std::max(alpha, score);
            }
        }
        return bestScore;
    }

    int negamax(int depth, int ply, int alpha, int beta)
    {
        if (depth <= 0) return quiescence(ply, alpha, beta);
        if (visitNode()) return 0;
        if (isDraw()) return 0;
        if (ply >= kMaxPly) return staticEval();

        const std::uint64_t key = m_board.hash();
        TranspositionTable::Entry entry;
        chess::Move hashMove{chess::Move::NO_MOVE};
        if (m_shared.tt.probe(key, entry)) {
            hashMove = entry.move;
            const int score = scoreFromTT(entry.score, ply);
            if (entry.depth >= depth) {
                if (entry.bound == Bound::Exact) return score;
                if (entry.bound == Bound::Lower && score >= beta) return score;
                if (entry.bound == Bound::Upper && score <= alpha) return score;
            }
        }

        const bool inCheck = m_board.inCheck();

        // Null move pruning: if passing still fails high, this node is very likely a cut-node.
        // Skipped without pieces, where zugzwang makes "passing" unreliable.
        if (depth > kNullMoveReduction && !inCheck && m_board.hasNonPawnMaterial(m_board.sideToMove())) {
            m_board.makeNullMove();
            const int score = -negamax(depth - 1 - kNullMoveReduction, ply + 1, -beta, -beta + 1);
            m_board.unmakeNullMove();
            if (m_shared.stop.load(std::memory_order_relaxed)) return 0;
            if (score >= beta) return beta;
        }

        chess::Movelist moves;
        chess::movegen::legalmoves(moves, m_board);
        if (moves.empty()) return inCheck ? -kMateScore + ply : 0;
        orderMoves(m_board, moves, hashMove);

        const int originalAlpha = alpha;
        const int childDepth = depth - 1 + (inCheck ? 1 : 0); // check extension
        int bestScore = -kInfinity;
        chess::Move bestMove{chess::Move::NO_MOVE};

        for (const auto& move : moves) {
            m_board.makeMove(move);
            const int score = -negamax(childDepth, ply + 1, -beta, -alpha);
            m_board.unmakeMove(move);
            if (m_shared.stop.load(std::memory_order_relaxed)) return 0;

            if (score > bestScore) {
                bestScore = score;
                bestMove = move;
            }
            alpha = std::max(alpha, score);
            if (alpha >= beta) break;
        }

        const Bound bound = bestScore <= originalAlpha ? Bound::Upper
                          : bestScore >= beta          ? Bound::Lower
                                                       : Bound::Exact;
        m_shared.tt.store(key, {depth, scoreToTT(bestScore, ply), bound, bestMove});
        return bestScore;
    }

    // Resolve captures (or all evasions when in check) so the static evaluation
    // is never taken in the middle of an exchange.
    int quiescence(int ply, int alpha, int beta)
    {
        if (visitNode()) return 0;
        if (isDraw()) return 0;
        if (ply >= kMaxPly) return staticEval();

        const bool inCheck = m_board.inCheck();
        int bestScore = -kInfinity;
        if (!inCheck) {
            bestScore = staticEval();
            if (bestScore >= beta) return bestScore;
            alpha = std::max(alpha, bestScore);
        }

        chess::Movelist moves;
        if (inCheck) {
            chess::movegen::legalmoves(moves, m_board);
            if (moves.empty()) return -kMateScore + ply;
        } else {
            chess::movegen::legalmoves<chess::movegen::MoveGenType::CAPTURE>(moves, m_board);
        }
        orderMoves(m_board, moves, chess::Move(chess::Move::NO_MOVE));

        for (const auto& move : moves) {
            m_board.makeMove(move);
            const int score = -quiescence(ply + 1, -beta, -alpha);
            m_board.unmakeMove(move);
            if (m_shared.stop.load(std::memory_order_relaxed)) return 0;

            bestScore = std::max(bestScore, score);
            alpha = std::max(alpha, score);
            if (alpha >= beta) break;
        }
        return bestScore;
    }

    // Evaluation from the side to move's point of view.
    int staticEval() const
    {
        const int score = evaluate(m_board);
        return m_board.sideToMove() == chess::Color::WHITE ? score : -score;
    }

    bool isDraw() const
    {
        return m_board.isRepetition(1) || m_board.isHalfMoveDraw() || m_board.isInsufficientMaterial();
    }

    // Counts a node and returns true once the search must stop.
    bool visitNode()
    {
        if (++m_pendingNodes == kNodeBatch) {
            flushNodes();
            const bool externalStop = m_shared.stopSignal && m_shared.stopSignal->load(std::memory_order_relaxed);
            if (externalStop || Clock::now() >= m_shared.deadline)
                m_shared.stop.store(true, std::memory_order_relaxed);
        }
        return m_shared.stop.load(std::memory_order_relaxed);
    }

    void flushNodes()
    {
        m_shared.nodes.fetch_add(m_pendingNodes, std::memory_order_relaxed);
        m_pendingNodes = 0;
    }

    std::int64_t elapsedMs() const
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - m_shared.start).count();
    }

    chess::Board m_board;
    SharedState& m_shared;
    std::uint64_t m_pendingNodes = 0;
};

} // namespace

Searcher::Searcher() : m_tt(std::make_unique<TranspositionTable>()) {}

Searcher::~Searcher() = default;

void Searcher::clear() { m_tt->clear(); }

SearchResult Searcher::search(const chess::Board& board, const SearchLimits& limits,
                              const ProgressCallback& onIteration)
{
    const auto start = Clock::now();
    const auto deadline = limits.timeMs > 0 ? start + std::chrono::milliseconds(limits.timeMs)
                                            : Clock::time_point::max();
    SharedState shared{*m_tt, start, deadline, limits.stopSignal};

    int threadCount = limits.threads > 0 ? limits.threads : static_cast<int>(std::thread::hardware_concurrency());
    threadCount = std::max(threadCount, 1);

    std::vector<std::thread> helpers;
    for (int id = 1; id < threadCount; ++id) {
        helpers.emplace_back([&board, &shared, &limits, id] {
            SearchThread(board, shared).run(limits.maxDepth, id, {});
        });
    }

    SearchResult result = SearchThread(board, shared).run(limits.maxDepth, 0, onIteration);

    shared.stop.store(true, std::memory_order_relaxed); // release the helpers
    for (auto& helper : helpers) helper.join();

    result.nodes = shared.nodes.load(std::memory_order_relaxed);
    result.elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
    return result;
}

} // namespace engine
