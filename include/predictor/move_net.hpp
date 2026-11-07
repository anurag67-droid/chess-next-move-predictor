#pragma once

#include "chess.hpp"
#include "predictor/player_book.hpp"

#include <array>
#include <filesystem>
#include <optional>
#include <vector>

// MoveNet, the human move model trained in ml/ (see ml/chessml/model.py), run on the CPU.
// Reads the file written by `python -m chessml.export`; needs no machine-learning library.
namespace predictor {

// Who is to move: the network answers differently for a 1200 in bullet and a 2500 in classical.
struct NetContext {
    int elo = 1500;         // the player to move
    int opponentElo = 1500;
    Speed speed = Speed::Blitz; // correspondence and unknown count as classical, as in training
    std::optional<double> clockShare; // share of the starting time left, 0..1; empty = unknown
};

struct MoveProbability {
    chess::Move move{chess::Move::NO_MOVE};
    double probability = 0.;
};

class MoveNet {
public:
    // Throws std::runtime_error if the file is missing or not a MoveNet export.
    static MoveNet fromFile(const std::filesystem::path& path);

    // Every legal move with the probability that a player like `context` plays it, most likely first.
    // Empty when there are no legal moves.
    std::vector<MoveProbability> predict(const chess::Board& board, const NetContext& context) const;

    int channels() const { return m_channels; }
    int blocks() const { return static_cast<int>(m_blocks.size()); }
    std::size_t parameterCount() const;

private:
    struct Conv {
        int in = 0, out = 0, size = 0; // size: 3 for 3x3, 1 for 1x1
        std::vector<float> weight;     // [in * size * size][out], so the inner loop runs over outputs
        std::vector<float> bias;       // [out]
    };
    struct Linear {
        int in = 0, out = 0;
        std::vector<float> weight; // [out][in]
        std::vector<float> bias;   // [out]
    };
    struct Block {
        Conv conv1, conv2;
        Linear film; // context -> per-channel scale and shift
    };

    // Activations are stored square by square: x[square * channels + channel], with the squares
    // a1 = 0 ... h8 = 63 as seen by the side to move.
    static std::vector<float> apply(const Conv& conv, const std::vector<float>& in);
    std::vector<float> context(const NetContext& context) const;
    // The 73 x 64 move-plane scores for the position seen from the side to move.
    std::vector<float> policy(const std::vector<float>& planes, const std::vector<float>& context) const;

    int m_channels = 0;
    int m_contextDim = 0;
    std::array<std::vector<float>, 4> m_embeddings; // elo, opponent elo, speed, clock: [bucket][contextDim]
    std::array<int, 4> m_buckets{};
    Linear m_context;
    Conv m_stem;
    std::vector<Block> m_blocks;
    Conv m_policyConv, m_policyOut;
    // Move index -> (from, to, under-promotion) in the mover's orientation, and its move-plane cell.
    struct NetMove {
        int from, to, promotion, cell;
    };
    std::vector<NetMove> m_moves;
    std::vector<int> m_moveLookup; // (from * 64 + to) * 5 + promotion -> move index, -1 if none
};

// The book's counts and the network's probabilities as one prediction, as chessml/evaluate.py does:
// p(move) = (games with the move + alpha * p_network) / (games in the position + alpha).
// Where the player has a habit the book dominates; elsewhere the network fills in. Most likely first.
std::vector<MoveProbability> combine(const BookLookup& book, const std::vector<MoveProbability>& network,
                                     double alpha = 2.);

} // namespace predictor
