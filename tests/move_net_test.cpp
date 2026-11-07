// Tests for predictor::MoveNet, run with `ctest` (see CMakeLists.txt).
//
// The C++ network must give the same probabilities as PyTorch. tests/data/tiny.movenet is a small
// random network and tiny-reference.txt holds PyTorch's predictions with it, both written by
// ml/tests/make_cpp_fixture.py. To check a real model the same way:
//
//     python -m chessml.export models/medium.pt --reference medium-reference.txt   (in ml/)
//     move_net_test ml/models/medium.movenet ml/medium-reference.txt
#include "predictor/move_net.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
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

using predictor::MoveNet;
using predictor::NetContext;
using predictor::Speed;

Speed parseSpeed(const std::string& name)
{
    if (name == "bullet") return Speed::Bullet;
    if (name == "blitz") return Speed::Blitz;
    if (name == "rapid") return Speed::Rapid;
    return Speed::Classical;
}

// Each line: FEN | elo opponent-elo speed clock-share (-1 = unknown) | uci probability ...
void testAgainstReference(const MoveNet& net, const std::string& referencePath)
{
    std::ifstream file(referencePath);
    CHECK(file.good());
    int positions = 0;
    double worst = 0.;
    for (std::string line; std::getline(file, line);) {
        const auto bar1 = line.find(" | "), bar2 = line.find(" | ", bar1 + 3);
        if (bar1 == std::string::npos || bar2 == std::string::npos) continue;
        const chess::Board board(line.substr(0, bar1));

        NetContext context;
        std::istringstream ctx(line.substr(bar1 + 3, bar2 - bar1 - 3));
        std::string speed;
        double clock = -1.;
        ctx >> context.elo >> context.opponentElo >> speed >> clock;
        context.speed = parseSpeed(speed);
        if (clock >= 0.) context.clockShare = clock;

        std::map<std::string, double> expected;
        std::istringstream moves(line.substr(bar2 + 3));
        std::string uci;
        double p = 0.;
        std::string expectedFirst;
        while (moves >> uci >> p) {
            if (expected.empty()) expectedFirst = uci;
            expected[uci] = p;
        }

        const auto predicted = net.predict(board, context);
        CHECK(predicted.size() == expected.size());
        double total = 0.;
        for (const auto& m : predicted) {
            const auto it = expected.find(chess::uci::moveToUci(m.move));
            CHECK(it != expected.end());
            if (it == expected.end()) continue;
            worst = std::max(worst, std::abs(it->second - m.probability));
            total += m.probability;
        }
        CHECK(std::abs(total - 1.) < 1e-6);
        // The most likely move agrees unless PyTorch's top two are too close to call.
        if (!predicted.empty() && chess::uci::moveToUci(predicted.front().move) != expectedFirst)
            CHECK(std::abs(predicted.front().probability - expected[expectedFirst]) < 1e-4);
        ++positions;
    }
    CHECK(positions >= 10);
    CHECK(worst < 1e-4);
    std::printf("  %d positions, largest difference from PyTorch %.2g\n", positions, worst);
}

void testCombine()
{
    const chess::Board board;
    const auto e4 = chess::uci::uciToMove(board, "e2e4");
    const auto d4 = chess::uci::uciToMove(board, "d2d4");
    const auto c4 = chess::uci::uciToMove(board, "c2c4");
    const std::vector<predictor::MoveProbability> network = {{e4, 0.5}, {d4, 0.3}, {c4, 0.2}};

    // No book games: the network's prediction is unchanged.
    auto combined = predictor::combine({}, network);
    CHECK(combined.size() == 3);
    CHECK(combined[0].move == e4 && std::abs(combined[0].probability - 0.5) < 1e-12);

    // 8 games, always 1.c4: (8 + 2 * 0.2) / (8 + 2) = 0.84 for c4, 2 * 0.5 / 10 = 0.1 for e4.
    predictor::BookLookup book;
    predictor::MoveStat stat;
    stat.move = c4;
    stat.games = 8;
    book.moves.push_back(stat);
    book.total = 8;
    combined = predictor::combine(book, network);
    CHECK(combined[0].move == c4 && std::abs(combined[0].probability - 0.84) < 1e-12);
    CHECK(combined[1].move == e4 && std::abs(combined[1].probability - 0.1) < 1e-12);
}

void testBadFiles()
{
    bool threw = false;
    try {
        MoveNet::fromFile(TEST_DATA_DIR "/alice.pgn");
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(threw);
}

} // namespace

int main(int argc, char** argv)
{
    const std::string model = argc > 2 ? argv[1] : TEST_DATA_DIR "/tiny.movenet";
    const std::string reference = argc > 2 ? argv[2] : TEST_DATA_DIR "/tiny-reference.txt";
    try {
        const MoveNet net = MoveNet::fromFile(model);
        std::printf("%s: %d blocks x %d channels, %zu parameters\n", model.c_str(), net.blocks(), net.channels(),
                    net.parameterCount());
        testAgainstReference(net, reference);

        const auto start = std::chrono::steady_clock::now();
        constexpr int kRuns = 20;
        for (int i = 0; i < kRuns; ++i) net.predict(chess::Board(), {});
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        std::printf("  %.1f ms per position\n", ms / kRuns);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what());
        ++g_failures;
    }
    testCombine();
    testBadFiles();

    if (g_failures) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("All MoveNet tests passed\n");
    return 0;
}
