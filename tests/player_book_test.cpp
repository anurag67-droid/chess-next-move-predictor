// Tests for predictor::PlayerBook, run with `ctest` (see CMakeLists.txt).
#include "predictor/player_book.hpp"

#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <sstream>
#include <string>

namespace {

int g_failures = 0;

#define CHECK(cond)                                                                \
    do {                                                                           \
        if (!(cond)) {                                                             \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                          \
        }                                                                          \
    } while (0)

using predictor::PlayerBook;
using predictor::Speed;
using predictor::speedBit;

chess::Board after(std::initializer_list<const char*> sans)
{
    chess::Board board;
    for (const char* san : sans) board.makeMove(chess::uci::parseSan(board, san));
    return board;
}

std::string san(const chess::Board& board, const predictor::MoveStat& stat)
{
    return chess::uci::moveToSan(board, stat.move);
}

void testSpeeds()
{
    CHECK(predictor::speedFromTimeControl("15+0") == Speed::Bullet);
    CHECK(predictor::speedFromTimeControl("60+0") == Speed::Bullet);
    CHECK(predictor::speedFromTimeControl("120+1") == Speed::Bullet); // 120 + 40 * 1 = 160 s
    CHECK(predictor::speedFromTimeControl("180+0") == Speed::Blitz);
    CHECK(predictor::speedFromTimeControl("300+3") == Speed::Blitz);
    CHECK(predictor::speedFromTimeControl("600+0") == Speed::Rapid);
    CHECK(predictor::speedFromTimeControl("900+10") == Speed::Rapid);
    CHECK(predictor::speedFromTimeControl("1800+0") == Speed::Classical);
    CHECK(predictor::speedFromTimeControl("-") == Speed::Correspondence);
    CHECK(predictor::speedFromTimeControl("1/86400") == Speed::Correspondence);
    CHECK(predictor::speedFromTimeControl("?") == Speed::Unknown);
    CHECK(predictor::speedFromTimeControl("") == Speed::Unknown);
}

void testLoading(const PlayerBook& book)
{
    CHECK(book.player() == "Alice");
    CHECK(book.stats().gamesInFile == 8);
    CHECK(book.stats().gamesUsed == 6);
    CHECK(book.stats().skippedVariant == 1);
    CHECK(book.stats().skippedNotPlayer == 1);
    CHECK(book.stats().skippedBadMoves == 0);

    // Sorted oldest first, whatever the order in the file.
    const auto& games = book.games();
    for (std::size_t i = 1; i < games.size(); ++i) CHECK(games[i - 1].date <= games[i].date);
    CHECK(games.front().date == "2024.01.01");
    CHECK(games.front().site == "https://example.org/game1");
    CHECK(games.front().outcome == predictor::Outcome::Win);
    CHECK(games[1].playerColor == chess::Color::BLACK);

    CHECK(book.gameCount(speedBit(Speed::Blitz)) == 3);
    CHECK(book.gameCount(speedBit(Speed::Rapid)) == 2);
    CHECK(book.gameCount(speedBit(Speed::Bullet)) == 1);
    CHECK(book.moveCount() == 16);
}

void testLookup(const PlayerBook& book)
{
    const chess::Board start;
    auto found = book.lookup(start);
    CHECK(found.total == 4);
    CHECK(found.moves.size() == 3);
    if (found.moves.size() == 3) {
        CHECK(san(start, found.moves[0]) == "e4");
        CHECK(found.moves[0].games == 2);
        CHECK(found.moves[0].share == 0.5);
        CHECK(found.moves[0].wins == 1 && found.moves[0].losses == 1);
        CHECK(found.moves[0].score() == 0.5);
        // d4 and Nf3 were each played once: the more recent one comes first.
        CHECK(san(start, found.moves[1]) == "Nf3");
        CHECK(san(start, found.moves[2]) == "d4");
    }

    // Speed filter.
    found = book.lookup(start, speedBit(Speed::Bullet));
    CHECK(found.moves.size() == 1 && san(start, found.moves[0]) == "d4");

    // Only games before a given index.
    found = book.lookup(start, predictor::kAllSpeeds, 1);
    CHECK(found.total == 1);

    // As Black: the side to move is part of the position, so White's games don't leak in.
    const auto afterE4 = after({"e4"});
    found = book.lookup(afterE4);
    CHECK(found.total == 2); // the normal game and the one that started from a FEN
    CHECK(found.moves.size() == 1 && san(afterE4, found.moves[0]) == "c5");

    // Transpositions: 1.d4 Nf6 2.Nf3 d5 and 1.Nf3 d5 2.d4 Nf6 reach the same position.
    const auto transposed = after({"d4", "Nf6", "Nf3", "d5"});
    found = book.lookup(transposed);
    CHECK(found.moves.size() == 1 && found.moves[0].games == 2 && san(transposed, found.moves[0]) == "c4");

    // A position where the opponent was to move is not in the player's book.
    CHECK(book.lookup(after({"e4", "e5"})).empty() == false); // Alice (White) to move: she played Nf3
    CHECK(book.lookup(after({"e4", "e5", "Nf3"})).empty());   // Bob to move
    CHECK(book.lookup(after({"a4"})).empty());
}

void testEvaluation(const PlayerBook& book)
{
    // 6 games, newest 2 held out: the Nf3 game and the game from a FEN.
    const auto report = predictor::evaluateHoldout(book, 0.34);
    CHECK(report.trainGames == 4);
    CHECK(report.testGames == 2);
    CHECK(report.overall.positions == 5);
    CHECK(report.overall.inBook == 4); // only 1.Nf3 d5 2.d4 was new
    CHECK(report.overall.top1 == 3);   // 3.c4 (transposition), ...c5 and ...d6
    CHECK(report.overall.top3 == 3);   // 1.Nf3 had never been played before
    CHECK(report.byMove[0].positions == 5);

    // Nothing to test on.
    CHECK(predictor::evaluateHoldout(book, 0.).overall.positions == 0);
}

void testExplicitPlayerAndErrors()
{
    std::istringstream pgn("[White \"Bob\"]\n[Black \"Carol\"]\n[Result \"1-0\"]\n\n1. e4 e5 1-0\n\n"
                           "[White \"Carol\"]\n[Black \"Bob\"]\n[Result \"0-1\"]\n\n1. d4 d5 0-1\n");
    const auto book = PlayerBook::fromPgn(pgn, "carol");
    CHECK(book.player() == "Carol");
    CHECK(book.games().size() == 2);
    CHECK(book.lookup(chess::Board()).moves.size() == 1);

    bool threw = false;
    try {
        std::istringstream empty("");
        (void)PlayerBook::fromPgn(empty);
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(threw);

    threw = false;
    try {
        std::istringstream other("[White \"Bob\"]\n[Black \"Carol\"]\n\n1. e4 e5 1-0\n");
        (void)PlayerBook::fromPgn(other, "nobody");
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(threw);

    // A bad move ends the game but keeps what came before it.
    std::istringstream bad("[White \"Ann\"]\n[Black \"Ben\"]\n\n1. e4 e5 2. Qxf7 Nc6 1-0\n");
    const auto partial = PlayerBook::fromPgn(bad, "Ann");
    CHECK(partial.stats().skippedBadMoves == 1);
    CHECK(partial.moveCount() == 1);
}

void testRepetitionCountsOnce()
{
    // The start position comes back after 2...Ng8: White played Nf3 there twice in one game.
    std::istringstream pgn("[White \"Ann\"]\n[Black \"Ben\"]\n[Result \"1/2-1/2\"]\n\n"
                           "1. Nf3 Nf6 2. Ng1 Ng8 3. Nf3 Nf6 1/2-1/2\n");
    const auto book = PlayerBook::fromPgn(pgn, "Ann");
    const auto found = book.lookup(chess::Board());
    CHECK(found.total == 1);
    CHECK(found.moves.size() == 1 && found.moves[0].games == 1);
}

void testRatings()
{
    // Ann's ratings: 1500 (blitz), 1600 (blitz), 1700 (rapid), and one unrated blitz game.
    std::istringstream pgn(
        "[White \"Ann\"]\n[Black \"Ben\"]\n[WhiteElo \"1500\"]\n[BlackElo \"1400\"]\n[TimeControl \"180+0\"]\n\n1. e4 1-0\n\n"
        "[White \"Cat\"]\n[Black \"Ann\"]\n[WhiteElo \"2000\"]\n[BlackElo \"1600\"]\n[TimeControl \"300+0\"]\n\n1. d4 0-1\n\n"
        "[White \"Ann\"]\n[Black \"Dan\"]\n[WhiteElo \"1700\"]\n[BlackElo \"1800\"]\n[TimeControl \"600+0\"]\n\n1. c4 1-0\n\n"
        "[White \"Ann\"]\n[Black \"Eve\"]\n[WhiteElo \"?\"]\n[TimeControl \"180+0\"]\n\n1. Nf3 1-0\n");
    const auto book = PlayerBook::fromPgn(pgn, "Ann");
    CHECK(book.games().size() == 4);
    CHECK(book.medianElo() == 1600);
    CHECK(book.medianElo(speedBit(Speed::Blitz)) == 1600); // 1500 and 1600: the upper middle
    CHECK(book.medianElo(speedBit(Speed::Rapid)) == 1700);
    CHECK(book.medianElo(speedBit(Speed::Bullet)) == 0);
    CHECK(book.medianOpponentElo(speedBit(Speed::Rapid)) == 1800);
    CHECK(book.mostPlayedSpeed() == Speed::Blitz);
    CHECK(book.mostPlayedSpeed(speedBit(Speed::Rapid)) == Speed::Rapid);
}

} // namespace

int main()
{
    testSpeeds();
    const auto book = PlayerBook::fromPgnFile(TEST_DATA_DIR "/alice.pgn");
    testLoading(book);
    testLookup(book);
    testEvaluation(book);
    testExplicitPlayerAndErrors();
    testRepetitionCountsOnce();
    testRatings();

    if (g_failures) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return EXIT_FAILURE;
    }
    std::puts("All player book tests passed");
    return EXIT_SUCCESS;
}
