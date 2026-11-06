// player-book: inspect a player's games and measure how well their past moves predict their newest ones.
//
//   player-book players/magnus.pgn
//   player-book players/magnus.pgn --speed blitz,rapid --test-fraction 0.2
//   player-book players/magnus.pgn --fen "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1"
#include "predictor/player_book.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void usage()
{
    std::puts("usage: player-book <games.pgn> [options]\n"
              "\n"
              "  --player NAME         whose moves to learn (default: the name in the most games)\n"
              "  --speed LIST          only these speeds: bullet,blitz,rapid,classical,correspondence\n"
              "  --fen FEN             show the player's moves in this position\n"
              "  --test-fraction F     hold out the newest F of the games for the evaluation (default 0.1)\n"
              "  --top N               moves to list per position (default 5)");
}

predictor::SpeedMask parseSpeeds(const std::string& list)
{
    predictor::SpeedMask mask = 0;
    std::size_t start = 0;
    while (start <= list.size()) {
        const std::size_t end = std::min(list.find(',', start), list.size());
        const std::string name = list.substr(start, end - start);
        bool found = false;
        for (int s = 0; s < predictor::kSpeedCount; ++s) {
            std::string speed = predictor::speedName(static_cast<predictor::Speed>(s));
            for (char& c : speed) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (speed == name) {
                mask |= predictor::speedBit(static_cast<predictor::Speed>(s));
                found = true;
            }
        }
        if (!found) throw std::runtime_error("Unknown speed: " + name);
        start = end + 1;
    }
    return mask;
}

std::string percent(double fraction)
{
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%5.1f%%", 100. * fraction);
    return buffer;
}

void printMoves(const predictor::PlayerBook& book, const chess::Board& board, predictor::SpeedMask speeds, int top,
                const char* indent)
{
    const auto found = book.lookup(board, speeds);
    if (found.empty()) {
        std::printf("%s(never had this position)\n", indent);
        return;
    }
    for (int i = 0; i < static_cast<int>(found.moves.size()) && i < top; ++i) {
        const auto& m = found.moves[i];
        const auto& last = book.games()[m.lastGame];
        std::printf("%s%-7s %s  %4d games  score %s  last %s\n", indent,
                    chess::uci::moveToSan(board, m.move).c_str(), percent(m.share).c_str(), m.games,
                    percent(m.score()).c_str(), last.date.c_str());
    }
}

void printRepertoire(const predictor::PlayerBook& book, predictor::SpeedMask speeds, int top)
{
    chess::Board board;
    std::puts("\nAs White, first move:");
    printMoves(book, board, speeds, top, "  ");

    for (const char* first : {"e4", "d4"}) {
        chess::Board reply;
        reply.makeMove(chess::uci::parseSan(reply, first));
        std::printf("\nAs Black, after 1.%s:\n", first);
        printMoves(book, reply, speeds, top, "  ");
    }
}

void printEvaluation(const predictor::PlayerBook& book, double testFraction, predictor::SpeedMask speeds)
{
    const auto report = predictor::evaluateHoldout(book, testFraction, speeds);
    std::printf("\nHeld-out test: book from the oldest %d games, tested on the newest %d\n", report.trainGames,
                report.testGames);
    if (report.overall.positions == 0) {
        std::puts("  (not enough games to split)");
        return;
    }
    std::puts("  (top-1/top-3 count every test position; a position the book never saw is a miss)\n");
    std::printf("  %-12s %9s %9s %9s %9s %14s\n", "", "positions", "in book", "top-1", "top-3", "top-1 in book");
    const auto row = [](const predictor::EvalBucket& b) {
        std::printf("  %-12s %9d %9s %9s %9s %14s\n", b.label, b.positions, percent(b.coverage()).c_str(),
                    percent(b.top1Accuracy()).c_str(), percent(b.top3Accuracy()).c_str(),
                    percent(b.top1WhenInBook()).c_str());
    };
    for (const auto& bucket : report.byMove) row(bucket);
    row(report.overall);
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2 || !std::strcmp(argv[1], "-h") || !std::strcmp(argv[1], "--help")) {
        usage();
        return argc < 2 ? EXIT_FAILURE : EXIT_SUCCESS;
    }

    try {
        std::string path = argv[1], player, fen;
        predictor::SpeedMask speeds = predictor::kAllSpeeds;
        double testFraction = 0.1;
        int top = 5;
        for (int i = 2; i < argc; ++i) {
            const std::string arg = argv[i];
            const auto value = [&]() -> std::string {
                if (i + 1 >= argc) throw std::runtime_error(arg + " needs a value");
                return argv[++i];
            };
            if (arg == "--player") player = value();
            else if (arg == "--speed") speeds = parseSpeeds(value());
            else if (arg == "--fen") fen = value();
            else if (arg == "--test-fraction") testFraction = std::stod(value());
            else if (arg == "--top") top = std::max(1, std::stoi(value()));
            else throw std::runtime_error("Unknown option " + arg);
        }

        const auto book = predictor::PlayerBook::fromPgnFile(path, player);
        const auto& stats = book.stats();
        std::printf("Player     %s\n", book.player().c_str());
        std::printf("Games      %d used of %d in the file", stats.gamesUsed, stats.gamesInFile);
        if (stats.skippedVariant) std::printf(", %d variant games skipped", stats.skippedVariant);
        if (stats.skippedNotPlayer) std::printf(", %d without the player", stats.skippedNotPlayer);
        if (stats.skippedBadMoves) std::printf(", %d cut short by unreadable moves", stats.skippedBadMoves);
        std::printf("\nBy speed  ");
        for (int s = 0; s < predictor::kSpeedCount; ++s) {
            const auto speed = static_cast<predictor::Speed>(s);
            if (const int n = book.gameCount(predictor::speedBit(speed))) std::printf("  %s %d", predictor::speedName(speed), n);
        }
        std::printf("\nPositions  %zu distinct, %zu moves by the player\n", book.positionCount(), book.moveCount());
        if (!book.games().empty())
            std::printf("Dates      %s to %s\n", book.games().front().date.c_str(), book.games().back().date.c_str());
        if (speeds != predictor::kAllSpeeds) std::printf("Filter     %d games match --speed\n", book.gameCount(speeds));

        if (!fen.empty()) {
            chess::Board board;
            if (!board.setFen(fen)) throw std::runtime_error("Invalid FEN");
            std::printf("\nIn %s:\n", fen.c_str());
            printMoves(book, board, speeds, top, "  ");
            return EXIT_SUCCESS;
        }

        printRepertoire(book, speeds, top);
        printEvaluation(book, testFraction, speeds);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
