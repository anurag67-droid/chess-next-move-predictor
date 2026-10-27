#include "engine/evaluation.hpp"

#include <algorithm>
#include <array>
#include <cstdint>

namespace engine {
namespace {

using chess::Color;
using chess::PieceType;

// PeSTO piece values and piece-square tables (by Ronald Friederich).
// Tables are laid out from White's point of view with a8 as index 0.
constexpr int kMgValue[6] = {82, 337, 365, 477, 1025, 0};
constexpr int kEgValue[6] = {94, 281, 297, 512, 936, 0};
constexpr int kPhaseWeight[6] = {0, 1, 1, 2, 4, 0};
constexpr int kMaxPhase = 24;

using Table = std::array<int, 64>;

constexpr Table kMgPawn = {
      0,   0,   0,   0,   0,   0,   0,   0,
     98, 134,  61,  95,  68, 126,  34, -11,
     -6,   7,  26,  31,  65,  56,  25, -20,
    -14,  13,   6,  21,  23,  12,  17, -23,
    -27,  -2,  -5,  12,  17,   6,  10, -25,
    -26,  -4,  -4, -10,   3,   3,  33, -12,
    -35,  -1, -20, -23, -15,  24,  38, -22,
      0,   0,   0,   0,   0,   0,   0,   0,
};
constexpr Table kEgPawn = {
      0,   0,   0,   0,   0,   0,   0,   0,
    178, 173, 158, 134, 147, 132, 165, 187,
     94, 100,  85,  67,  56,  53,  82,  84,
     32,  24,  13,   5,  -2,   4,  17,  17,
     13,   9,  -3,  -7,  -7,  -8,   3,  -1,
      4,   7,  -6,   1,   0,  -5,  -1,  -8,
     13,   8,   8,  10,  13,   0,   2,  -7,
      0,   0,   0,   0,   0,   0,   0,   0,
};
constexpr Table kMgKnight = {
    -167, -89, -34, -49,  61, -97, -15, -107,
     -73, -41,  72,  36,  23,  62,   7,  -17,
     -47,  60,  37,  65,  84, 129,  73,   44,
      -9,  17,  19,  53,  37,  69,  18,   22,
     -13,   4,  16,  13,  28,  19,  21,   -8,
     -23,  -9,  12,  10,  19,  17,  28,  -16,
     -29, -53, -12,  -3,  -1,  18, -14,  -19,
    -105, -21, -58, -33, -17, -28, -19,  -23,
};
constexpr Table kEgKnight = {
    -58, -38, -13, -28, -31, -27, -63, -99,
    -25,  -8, -25,  -2,  -9, -25, -24, -52,
    -24, -20,  10,   9,  -1,  -9, -19, -41,
    -17,   3,  22,  22,  22,  11,   8, -18,
    -18,  -6,  16,  25,  16,  17,   4, -18,
    -23,  -3,  -1,  15,  10,  -3, -20, -22,
    -42, -20, -10,  -5,  -2, -20, -23, -44,
    -29, -51, -23, -38, -29, -27, -43, -74,
};
constexpr Table kMgBishop = {
    -29,   4, -82, -37, -25, -42,   7,  -8,
    -26,  16, -18, -13,  30,  59,  18, -47,
    -16,  37,  43,  40,  35,  50,  37,  -2,
     -4,   5,  19,  50,  37,  37,   7,  -2,
     -6,  13,  13,  26,  34,  12,  10,   4,
      0,  15,  15,  15,  14,  27,  18,  10,
      4,  15,  16,   0,   7,  21,  33,   1,
    -33,  -3, -14, -21, -13, -12, -39, -21,
};
constexpr Table kEgBishop = {
    -14, -21, -11,  -8,  -7,  -9, -17, -24,
     -8,  -4,   7, -12,  -3, -13,  -4, -14,
      2,  -8,   0,  -1,  -2,   6,   0,   4,
     -3,   9,  12,   9,  14,  10,   3,   2,
     -6,   3,  13,  19,   7,  10,  -3,  -9,
    -12,  -3,   8,  10,  13,   3,  -7, -15,
    -14, -18,  -7,  -1,   4,  -9, -15, -27,
    -23,  -9, -23,  -5,  -9, -16,  -5, -17,
};
constexpr Table kMgRook = {
     32,  42,  32,  51,  63,   9,  31,  43,
     27,  32,  58,  62,  80,  67,  26,  44,
     -5,  19,  26,  36,  17,  45,  61,  16,
    -24, -11,   7,  26,  24,  35,  -8, -20,
    -36, -26, -12,  -1,   9,  -7,   6, -23,
    -45, -25, -16, -17,   3,   0,  -5, -33,
    -47, -59, -43, -36, -32, -32, -10, -18,
    -19, -13,   1,  17,  16,   7, -37, -26,
};
constexpr Table kEgRook = {
     13,  10,  18,  15,  12,  12,   8,   5,
     11,  13,  13,  11,  -3,   3,   8,   3,
      7,   7,   7,   5,   4,  -3,  -5,  -3,
      4,   3,  13,   1,   2,   1,  -1,   2,
      3,   5,   8,   4,  -5,  -6,  -8, -11,
     -4,   0,  -5,  -1,  -7, -12,  -8, -16,
     -6,  -6,   0,   2,  -9,  -9, -11,  -3,
     -9,   2,   3,  -1,  -5, -13,   4, -20,
};
constexpr Table kMgQueen = {
    -28,   0,  29,  12,  59,  44,  43,  45,
    -24, -39,  -5,   1, -16,  57,  28,  54,
    -13, -17,   7,   8,  29,  56,  47,  57,
    -27, -27, -16, -16,  -1,  17,  -2,   1,
     -9, -26,  -9, -10,  -2,  -4,   3,  -3,
    -14,   2, -11,  -2,  -5,   2,  14,   5,
    -35,  -8,  11,   2,   8,  15,  -3,   1,
     -1, -18,  -9,  10, -15, -25, -31, -50,
};
constexpr Table kEgQueen = {
     -9,  22,  22,  27,  27,  19,  10,  20,
    -17,  20,  32,  41,  58,  25,  30,   0,
    -20,   6,   9,  49,  47,  35,  19,   9,
      3,  22,  24,  45,  57,  40,  57,  36,
    -18,  28,  19,  47,  31,  34,  12,  11,
     16,  20,  27,  29,   9,  33,  14,   5,
     -2, -15,  -1,  -3,   2,   3,   0,   3,
    -27, -15, -15, -14, -29, -15, -17, -34,
};
constexpr Table kMgKing = {
    -65,  23,  16, -15, -56, -34,   2,  13,
     29,  -1, -20,  -7,  -8,  -4, -38, -29,
     -9,  24,   2, -16, -20,   6,  22, -22,
    -17, -20, -12, -27, -30, -25, -14, -36,
    -49,  -1, -27, -39, -46, -44, -33, -51,
    -14, -14, -22, -46, -44, -30, -15, -27,
      1,   7,  -8, -64, -43, -16,   9,   8,
    -15,  36,  12, -54,   8, -28,  24,  14,
};
constexpr Table kEgKing = {
    -74, -35, -18, -18, -11,  15,   4, -17,
    -12,  17,  14,  17,  17,  38,  23,  11,
     10,  17,  23,  15,  20,  45,  44,  13,
     -8,  22,  24,  27,  26,  33,  26,   3,
    -18,  -4,  21,  24,  27,  23,   9, -11,
    -19,  -3,  11,  21,  23,  16,   7,  -9,
    -27, -11,   4,  13,  14,   4,  -5, -17,
    -53, -34, -21, -11, -28, -14, -24, -43,
};

constexpr const Table* kMgTables[6] = {&kMgPawn, &kMgKnight, &kMgBishop, &kMgRook, &kMgQueen, &kMgKing};
constexpr const Table* kEgTables[6] = {&kEgPawn, &kEgKnight, &kEgBishop, &kEgRook, &kEgQueen, &kEgKing};

// Positional bonuses (applied equally to middlegame and endgame unless noted).
constexpr int kBishopPair = 30;
constexpr int kDoubledPawn = -15;
constexpr int kIsolatedPawn = -20;
constexpr int kPassedPawnBase = 20;
constexpr int kPassedPawnPerRank = 10;
constexpr int kKnightClosed = 15, kKnightOpen = -10;
constexpr int kBishopOpen = 15, kBishopClosed = -10;
constexpr int kRookNoOwnPawns = 15;
constexpr int kKingMissingShield = -30; // middlegame only
constexpr int kKingOpenFile = -40;      // middlegame only

struct Score {
    int mg = 0;
    int eg = 0;
    void add(int both) { mg += both; eg += both; }
};

// Squares in front of a pawn (own file and both neighbours) that an enemy pawn must
// occupy to stop it from being passed.
constexpr std::uint64_t frontSpan(int sq, Color color)
{
    const int file = sq % 8, rank = sq / 8;
    std::uint64_t mask = 0;
    for (int r = 0; r < 8; ++r) {
        const bool ahead = color == Color::WHITE ? r > rank : r < rank;
        if (!ahead) continue;
        for (int f = std::max(0, file - 1); f <= std::min(7, file + 1); ++f)
            mask |= 1ULL << (r * 8 + f);
    }
    return mask;
}

constexpr auto kFrontSpans = [] {
    std::array<std::array<std::uint64_t, 64>, 2> spans{};
    for (int sq = 0; sq < 64; ++sq) {
        spans[0][sq] = frontSpan(sq, Color::WHITE);
        spans[1][sq] = frontSpan(sq, Color::BLACK);
    }
    return spans;
}();

struct PawnInfo {
    int onFile[2][8] = {};
    int total = 0;
};

PawnInfo countPawns(const chess::Board& board)
{
    PawnInfo info;
    for (Color color : {Color::WHITE, Color::BLACK}) {
        auto pawns = board.pieces(PieceType::PAWN, color);
        while (pawns) {
            ++info.onFile[static_cast<int>(color)][pawns.pop() % 8];
            ++info.total;
        }
    }
    return info;
}

Score evaluatePawn(const chess::Board& board, const PawnInfo& pawns, int sq, Color color)
{
    const int us = static_cast<int>(color);
    const int file = sq % 8;
    const int* ownFiles = pawns.onFile[us];
    Score s;

    if (ownFiles[file] > 1) s.add(kDoubledPawn);

    const bool leftSupport = file > 0 && ownFiles[file - 1] > 0;
    const bool rightSupport = file < 7 && ownFiles[file + 1] > 0;
    if (!leftSupport && !rightSupport) s.add(kIsolatedPawn);

    const auto enemyPawns = board.pieces(PieceType::PAWN, ~color).getBits();
    if ((enemyPawns & kFrontSpans[us][sq]) == 0) {
        const int relativeRank = color == Color::WHITE ? sq / 8 : 7 - sq / 8;
        s.add(kPassedPawnBase + kPassedPawnPerRank * relativeRank);
    }
    return s;
}

Score evaluateKingShelter(const PawnInfo& pawns, int sq, Color color)
{
    const int us = static_cast<int>(color);
    const int file = sq % 8;
    int missingShield = 0, openFiles = 0;
    for (int f = std::max(0, file - 1); f <= std::min(7, file + 1); ++f) {
        if (pawns.onFile[us][f] > 0) continue;
        ++missingShield;
        if (pawns.onFile[us ^ 1][f] == 0) ++openFiles;
    }
    Score s;
    s.mg = missingShield * kKingMissingShield + openFiles * kKingOpenFile;
    return s;
}

} // namespace

int evaluate(const chess::Board& board)
{
    const PawnInfo pawns = countPawns(board);
    const bool closed = pawns.total >= 12;
    const bool open = pawns.total <= 8;

    Score side[2];
    int phase = 0;

    for (Color color : {Color::WHITE, Color::BLACK}) {
        const int us = static_cast<int>(color);
        Score& s = side[us];

        if (board.pieces(PieceType::BISHOP, color).count() >= 2) s.add(kBishopPair);

        for (int pt = 0; pt < 6; ++pt) {
            const PieceType type(static_cast<PieceType::underlying>(pt));
            auto bb = board.pieces(type, color);
            while (bb) {
                const int sq = bb.pop();
                // Square index into the a8-first tables, mirrored for Black.
                const int tableSq = color == Color::WHITE ? sq ^ 56 : sq;

                s.mg += kMgValue[pt] + (*kMgTables[pt])[tableSq];
                s.eg += kEgValue[pt] + (*kEgTables[pt])[tableSq];
                phase += kPhaseWeight[pt];

                Score extra;
                if (type == PieceType::PAWN) {
                    extra = evaluatePawn(board, pawns, sq, color);
                } else if (type == PieceType::KNIGHT) {
                    if (closed) extra.add(kKnightClosed);
                    if (open) extra.add(kKnightOpen);
                } else if (type == PieceType::BISHOP) {
                    if (open) extra.add(kBishopOpen);
                    if (closed) extra.add(kBishopClosed);
                } else if (type == PieceType::ROOK) {
                    if (pawns.onFile[us][sq % 8] == 0) extra.add(kRookNoOwnPawns);
                } else if (type == PieceType::KING) {
                    extra = evaluateKingShelter(pawns, sq, color);
                }
                s.mg += extra.mg;
                s.eg += extra.eg;
            }
        }
    }

    const int mgPhase = std::min(phase, kMaxPhase);
    const int egPhase = kMaxPhase - mgPhase;
    const int mg = side[0].mg - side[1].mg;
    const int eg = side[0].eg - side[1].eg;
    return (mg * mgPhase + eg * egPhase) / kMaxPhase;
}

} // namespace engine
