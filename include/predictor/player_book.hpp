#pragma once

#include "chess.hpp"

#include <climits>
#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

// A per-player "opening book": every position a player had to move in, and what they played there.
// Built from a PGN file of the player's games (see tools/fetch_games.py).
namespace predictor {

enum class Speed : std::uint8_t { Bullet, Blitz, Rapid, Classical, Correspondence, Unknown };
inline constexpr int kSpeedCount = 6;

// Bit mask of speeds, e.g. speedBit(Speed::Blitz) | speedBit(Speed::Rapid).
using SpeedMask = unsigned;
inline constexpr SpeedMask speedBit(Speed s) { return 1u << static_cast<unsigned>(s); }
inline constexpr SpeedMask kAllSpeeds = (1u << kSpeedCount) - 1;

const char* speedName(Speed speed); // "Bullet", "Blitz", ...
// Lichess' categories, from a PGN TimeControl tag like "180+2" (estimated duration = base + 40 * increment).
Speed speedFromTimeControl(std::string_view timeControl);

// Result from the player's point of view.
enum class Outcome : std::uint8_t { Win, Draw, Loss, Unknown };

struct GameInfo {
    std::string white;
    std::string black;
    std::string date; // "YYYY.MM.DD", as in the PGN
    std::string site; // link to the game, when the PGN has one
    Speed speed = Speed::Unknown;
    chess::Color playerColor = chess::Color::WHITE;
    Outcome outcome = Outcome::Unknown;
};

// How often the player chose one move in one position.
struct MoveStat {
    chess::Move move{chess::Move::NO_MOVE};
    int games = 0;
    int wins = 0;
    int draws = 0;
    int losses = 0;
    int lastGame = -1; // index into PlayerBook::games() of the most recent game with this move
    double share = 0.; // games / total games in the position

    // Player's score in these games (win = 1, draw = 0.5), 0..1.
    double score() const;
};

struct BookLookup {
    std::vector<MoveStat> moves; // most frequent first, ties broken by the most recent
    int total = 0;               // sum of MoveStat::games: the games in which the player had this position
    bool empty() const { return moves.empty(); }
};

struct LoadStats {
    int gamesInFile = 0;
    int gamesUsed = 0;
    int skippedVariant = 0;   // chess960 and other variants
    int skippedNotPlayer = 0; // games the player did not take part in
    int skippedBadMoves = 0;  // games cut short by a move that could not be read
};

class PlayerBook {
public:
    // Reads every game in a PGN stream. `player` is the player's name as in the White/Black tags
    // (case-insensitive); leave it empty to pick the name that appears in the most games.
    // Throws std::runtime_error if the stream has no usable games.
    static PlayerBook fromPgn(std::istream& pgn, const std::string& player = {});
    static PlayerBook fromPgnFile(const std::filesystem::path& path, const std::string& player = {});

    const std::string& player() const { return m_player; }
    // Oldest first, so a game's index also orders it in time.
    const std::vector<GameInfo>& games() const { return m_games; }
    const LoadStats& stats() const { return m_stats; }
    int gameCount(SpeedMask speeds = kAllSpeeds) const;
    std::size_t positionCount() const { return m_positions; }
    std::size_t moveCount() const { return m_occurrences.size(); }

    // What the player played in this position (only legal moves are returned).
    // Only games whose speed is in `speeds` and whose index is below `beforeGame` count.
    BookLookup lookup(const chess::Board& board, SpeedMask speeds = kAllSpeeds, int beforeGame = INT_MAX) const;
    // Same, by the position's Zobrist key and without the legality check.
    BookLookup lookupKey(std::uint64_t key, SpeedMask speeds = kAllSpeeds, int beforeGame = INT_MAX) const;

    // One move the player made: the position before it, the move and where it happened.
    struct Occurrence {
        std::uint64_t key;  // Zobrist key of the position
        std::uint32_t game; // index into games()
        std::uint16_t move; // chess::Move::move()
        std::uint16_t ply;  // half-moves since the start of the game
    };
    // Sorted by position key, then by game.
    const std::vector<Occurrence>& occurrences() const { return m_occurrences; }

private:
    std::string m_player;
    std::vector<GameInfo> m_games;
    std::vector<Occurrence> m_occurrences;
    std::size_t m_positions = 0;
    LoadStats m_stats;
};

// ---------------------------------------------------------------------------
// Evaluation: how well does "play what you played before" predict a player's newest games?

struct EvalBucket {
    const char* label = "";
    int positions = 0; // test positions where the player was to move
    int inBook = 0;    // ...that the player also had in an older game
    int top1 = 0;      // the book's most frequent move was the move played
    int top3 = 0;      // the move played was among the book's three most frequent

    double coverage() const { return positions ? double(inBook) / positions : 0.; }
    double top1Accuracy() const { return positions ? double(top1) / positions : 0.; }
    double top3Accuracy() const { return positions ? double(top3) / positions : 0.; }
    // Accuracy counting only the positions the book knew.
    double top1WhenInBook() const { return inBook ? double(top1) / inBook : 0.; }
};

struct EvalReport {
    int trainGames = 0;
    int testGames = 0;
    EvalBucket overall{"All moves"};
    // By move number: 1-10, 11-25, 26 and later.
    EvalBucket byMove[3] = {{"Moves 1-10"}, {"Moves 11-25"}, {"Moves 26+"}};
};

// Builds the book from the oldest games and tests it on the newest `testFraction` of them,
// so no game is used to predict itself or anything played before it.
EvalReport evaluateHoldout(const PlayerBook& book, double testFraction = 0.1, SpeedMask speeds = kAllSpeeds);

} // namespace predictor
