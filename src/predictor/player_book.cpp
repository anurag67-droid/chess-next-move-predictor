#include "predictor/player_book.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <fstream>
#include <map>
#include <numeric>
#include <stdexcept>
#include <unordered_map>

namespace predictor {
namespace {

std::string lowercase(std::string_view s)
{
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

int parseInt(std::string_view s)
{
    int value = -1;
    const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
    return ec == std::errc() ? value : -1;
}

// A game as read from the file, before we know whose games these are.
struct RawGame {
    std::string white, black, result, date, time, timeControl, variant, fen, site;
    std::vector<std::string> moves; // SAN
};

class Collector : public chess::pgn::Visitor {
public:
    std::vector<RawGame> games;

    void startPgn() override { games.emplace_back(); }
    void header(std::string_view key, std::string_view value) override
    {
        RawGame& g = games.back();
        if (key == "White") g.white = value;
        else if (key == "Black") g.black = value;
        else if (key == "Result") g.result = value;
        else if (key == "UTCDate") g.date = value;
        else if (key == "Date" && g.date.empty()) g.date = value;
        else if (key == "UTCTime") g.time = value;
        else if (key == "TimeControl") g.timeControl = value;
        else if (key == "Variant") g.variant = value;
        else if (key == "FEN") g.fen = value;
        else if (key == "Site" || key == "Link") {
            // Chess.com puts "Chess.com" in Site and the game's address in Link.
            if (value.rfind("http", 0) == 0) g.site = value;
        }
    }
    void startMoves() override {}
    void move(std::string_view san, std::string_view) override { games.back().moves.emplace_back(san); }
    void endPgn() override {}
};

bool isStandardChess(const RawGame& g)
{
    if (g.variant.empty()) return true;
    const std::string v = lowercase(g.variant);
    return v == "standard" || v == "from position";
}

Outcome outcomeFor(const std::string& result, chess::Color player)
{
    if (result == "1/2-1/2") return Outcome::Draw;
    if (result == "1-0") return player == chess::Color::WHITE ? Outcome::Win : Outcome::Loss;
    if (result == "0-1") return player == chess::Color::BLACK ? Outcome::Win : Outcome::Loss;
    return Outcome::Unknown;
}

// The name in the most White/Black tags: in a file of one player's games, that player.
std::string mostCommonName(const std::vector<RawGame>& games)
{
    std::map<std::string, int> counts;
    for (const auto& g : games) {
        for (const auto* name : {&g.white, &g.black})
            if (!name->empty() && *name != "?") ++counts[lowercase(*name)];
    }
    std::string best;
    int bestCount = 0;
    for (const auto& [name, count] : counts) {
        if (count > bestCount) {
            best = name;
            bestCount = count;
        }
    }
    return best;
}

} // namespace

// ---------------------------------------------------------------------------

const char* speedName(Speed speed)
{
    switch (speed) {
    case Speed::Bullet: return "Bullet";
    case Speed::Blitz: return "Blitz";
    case Speed::Rapid: return "Rapid";
    case Speed::Classical: return "Classical";
    case Speed::Correspondence: return "Correspondence";
    case Speed::Unknown: break;
    }
    return "Unknown";
}

Speed speedFromTimeControl(std::string_view tc)
{
    if (tc == "-") return Speed::Correspondence;
    // Chess.com daily games: "1/86400" (one move per day).
    if (const auto slash = tc.find('/'); slash != std::string_view::npos) return Speed::Correspondence;

    const auto plus = tc.find('+');
    const int base = parseInt(tc.substr(0, plus));
    const int increment = plus == std::string_view::npos ? 0 : parseInt(tc.substr(plus + 1));
    if (base < 0 || increment < 0) return Speed::Unknown;

    const int estimate = base + 40 * increment;
    if (estimate < 180) return Speed::Bullet;
    if (estimate < 480) return Speed::Blitz;
    if (estimate < 1500) return Speed::Rapid;
    return Speed::Classical;
}

double MoveStat::score() const
{
    const int decided = wins + draws + losses;
    return decided ? (wins + 0.5 * draws) / decided : 0.;
}

// ---------------------------------------------------------------------------

PlayerBook PlayerBook::fromPgnFile(const std::filesystem::path& path, const std::string& player)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Cannot open " + path.string());
    return fromPgn(file, player);
}

PlayerBook PlayerBook::fromPgn(std::istream& pgn, const std::string& player)
{
    Collector collector;
    chess::pgn::StreamParser parser(pgn);
    parser.readGames(collector);
    if (collector.games.empty()) throw std::runtime_error("No games found in the PGN");

    PlayerBook book;
    book.m_stats.gamesInFile = static_cast<int>(collector.games.size());
    const std::string wanted = player.empty() ? mostCommonName(collector.games) : lowercase(player);
    if (wanted.empty()) throw std::runtime_error("The PGN has no player names");

    // Oldest game first. Lichess exports newest first, Chess.com oldest first; a stable sort keeps
    // the file order for games without a date.
    std::vector<std::size_t> order(collector.games.size());
    std::iota(order.begin(), order.end(), 0);
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        const RawGame& ga = collector.games[a];
        const RawGame& gb = collector.games[b];
        return std::tie(ga.date, ga.time) < std::tie(gb.date, gb.time);
    });

    chess::Movelist scratch;
    for (const std::size_t index : order) {
        const RawGame& raw = collector.games[index];
        if (!isStandardChess(raw)) {
            ++book.m_stats.skippedVariant;
            continue;
        }
        const bool isWhite = lowercase(raw.white) == wanted;
        const bool isBlack = lowercase(raw.black) == wanted;
        if (isWhite == isBlack) { // not their game (or a game against themselves)
            ++book.m_stats.skippedNotPlayer;
            continue;
        }

        chess::Board board;
        if (!raw.fen.empty() && !board.setFen(raw.fen)) {
            ++book.m_stats.skippedBadMoves;
            continue;
        }

        GameInfo info;
        info.white = raw.white;
        info.black = raw.black;
        info.date = raw.date;
        info.site = raw.site;
        info.speed = speedFromTimeControl(raw.timeControl);
        info.playerColor = isWhite ? chess::Color::WHITE : chess::Color::BLACK;
        info.outcome = outcomeFor(raw.result, info.playerColor);
        book.m_player = isWhite ? raw.white : raw.black; // ends as the newest game's spelling

        const auto gameIndex = static_cast<std::uint32_t>(book.m_games.size());
        book.m_games.push_back(std::move(info));

        std::uint16_t ply = 0;
        for (const auto& san : raw.moves) {
            chess::Move move;
            try {
                scratch.clear();
                move = chess::uci::parseSan(board, san, scratch);
            } catch (const std::exception&) {
                move = chess::Move::NO_MOVE;
            }
            if (move == chess::Move::NO_MOVE) {
                ++book.m_stats.skippedBadMoves;
                break;
            }
            if (board.sideToMove() == book.m_games.back().playerColor)
                book.m_occurrences.push_back({board.hash(), gameIndex, move.move(), ply});
            board.makeMove(move);
            ++ply;
        }
    }

    book.m_stats.gamesUsed = static_cast<int>(book.m_games.size());
    if (book.m_games.empty()) throw std::runtime_error("None of the games in the PGN were played by " + wanted);

    std::sort(book.m_occurrences.begin(), book.m_occurrences.end(), [](const Occurrence& a, const Occurrence& b) {
        return std::tie(a.key, a.game, a.ply) < std::tie(b.key, b.game, b.ply);
    });
    for (std::size_t i = 0; i < book.m_occurrences.size(); ++i)
        if (i == 0 || book.m_occurrences[i].key != book.m_occurrences[i - 1].key) ++book.m_positions;
    return book;
}

int PlayerBook::gameCount(SpeedMask speeds) const
{
    return static_cast<int>(std::count_if(m_games.begin(), m_games.end(),
                                          [&](const GameInfo& g) { return speeds & speedBit(g.speed); }));
}

BookLookup PlayerBook::lookupKey(std::uint64_t key, SpeedMask speeds, int beforeGame) const
{
    BookLookup result;
    const auto first = std::lower_bound(m_occurrences.begin(), m_occurrences.end(), key,
                                        [](const Occurrence& o, std::uint64_t k) { return o.key < k; });
    const auto last = std::upper_bound(first, m_occurrences.end(), key,
                                       [](std::uint64_t k, const Occurrence& o) { return k < o.key; });

    // A position can come up more than once in a game (a repetition): count each move once per game.
    // Entries are sorted by game within a key, so one game's entries are next to each other.
    int currentGame = -1;
    std::vector<std::uint16_t> countedInGame;
    for (auto it = first; it != last; ++it) {
        if (static_cast<long long>(it->game) >= beforeGame) break;
        const GameInfo& game = m_games[it->game];
        if (!(speeds & speedBit(game.speed))) continue;

        if (static_cast<int>(it->game) != currentGame) {
            currentGame = static_cast<int>(it->game);
            countedInGame.clear();
        }
        if (std::find(countedInGame.begin(), countedInGame.end(), it->move) != countedInGame.end()) continue;
        countedInGame.push_back(it->move);

        auto stat = std::find_if(result.moves.begin(), result.moves.end(),
                                 [&](const MoveStat& s) { return s.move.move() == it->move; });
        if (stat == result.moves.end()) {
            result.moves.push_back({});
            stat = result.moves.end() - 1;
            stat->move = chess::Move(it->move);
        }
        ++stat->games;
        stat->wins += game.outcome == Outcome::Win;
        stat->draws += game.outcome == Outcome::Draw;
        stat->losses += game.outcome == Outcome::Loss;
        stat->lastGame = static_cast<int>(it->game);
        ++result.total;
    }

    for (auto& stat : result.moves) stat.share = result.total ? double(stat.games) / result.total : 0.;
    std::sort(result.moves.begin(), result.moves.end(), [](const MoveStat& a, const MoveStat& b) {
        return a.games != b.games ? a.games > b.games : a.lastGame > b.lastGame;
    });
    return result;
}

BookLookup PlayerBook::lookup(const chess::Board& board, SpeedMask speeds, int beforeGame) const
{
    BookLookup result = lookupKey(board.hash(), speeds, beforeGame);
    if (result.empty()) return result;

    // Guard against the (astronomically unlikely) hash collision.
    chess::Movelist legal;
    chess::movegen::legalmoves(legal, board);
    const auto isLegal = [&](const MoveStat& s) { return std::find(legal.begin(), legal.end(), s.move) != legal.end(); };
    if (std::all_of(result.moves.begin(), result.moves.end(), isLegal)) return result;

    result.moves.erase(std::remove_if(result.moves.begin(), result.moves.end(),
                                      [&](const MoveStat& s) { return !isLegal(s); }),
                       result.moves.end());
    result.total = 0;
    for (const auto& s : result.moves) result.total += s.games;
    for (auto& s : result.moves) s.share = result.total ? double(s.games) / result.total : 0.;
    return result;
}

// ---------------------------------------------------------------------------

EvalReport evaluateHoldout(const PlayerBook& book, double testFraction, SpeedMask speeds)
{
    // Split the games that match the speed filter by time.
    std::vector<int> eligible;
    for (int i = 0; i < static_cast<int>(book.games().size()); ++i)
        if (speeds & speedBit(book.games()[i].speed)) eligible.push_back(i);

    EvalReport report;
    const int testCount = static_cast<int>(eligible.size() * std::clamp(testFraction, 0., 1.));
    report.testGames = testCount;
    report.trainGames = static_cast<int>(eligible.size()) - testCount;
    if (testCount == 0 || report.trainGames == 0) return report;
    const int cutoff = eligible[report.trainGames]; // first test game

    std::unordered_map<std::uint64_t, BookLookup> cache;
    for (const auto& occ : book.occurrences()) {
        if (static_cast<int>(occ.game) < cutoff || !(speeds & speedBit(book.games()[occ.game].speed))) continue;

        auto it = cache.find(occ.key);
        if (it == cache.end()) it = cache.emplace(occ.key, book.lookupKey(occ.key, speeds, cutoff)).first;
        const BookLookup& known = it->second;

        int rank = -1;
        for (int i = 0; i < static_cast<int>(known.moves.size()); ++i)
            if (known.moves[i].move.move() == occ.move) rank = i;

        const int moveNumber = occ.ply / 2 + 1;
        EvalBucket& bucket = report.byMove[moveNumber <= 10 ? 0 : moveNumber <= 25 ? 1 : 2];
        for (EvalBucket* b : {&report.overall, &bucket}) {
            ++b->positions;
            b->inBook += !known.empty();
            b->top1 += rank == 0;
            b->top3 += rank >= 0 && rank < 3;
        }
    }
    return report;
}

} // namespace predictor
