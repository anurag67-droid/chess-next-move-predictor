#include "core/game.hpp"

namespace core {
namespace {

// chess.hpp assumes a well-formed board, so check the piece placement field ourselves:
// eight ranks of eight squares, exactly one king per side and no pawns on the back ranks.
bool isValidPlacement(const std::string& fen)
{
    const std::string placement = fen.substr(0, fen.find(' '));
    int rank = 0, file = 0, whiteKings = 0, blackKings = 0;
    for (const char c : placement) {
        if (c == '/') {
            if (file != 8) return false;
            ++rank;
            file = 0;
        } else if (c >= '1' && c <= '8') {
            file += c - '0';
        } else if (std::string("pnbrqkPNBRQK").find(c) != std::string::npos) {
            if ((c == 'p' || c == 'P') && (rank == 0 || rank == 7)) return false;
            whiteKings += c == 'K';
            blackKings += c == 'k';
            ++file;
        } else {
            return false;
        }
        if (file > 8) return false;
    }
    return rank == 7 && file == 8 && whiteKings == 1 && blackKings == 1;
}

} // namespace

void Game::reset()
{
    m_board = chess::Board();
    m_history.clear();
    m_firstMoveNumber = 1;
    m_blackMovedFirst = false;
}

bool Game::loadFen(const std::string& fen)
{
    if (!isValidPlacement(fen)) return false;
    chess::Board board;
    if (!board.setFen(fen)) return false;
    // The side that just moved cannot still be in check.
    if (board.isAttacked(board.kingSq(~board.sideToMove()), board.sideToMove())) return false;

    m_board = board;
    m_history.clear();
    m_firstMoveNumber = static_cast<int>(board.fullMoveNumber());
    m_blackMovedFirst = board.sideToMove() == chess::Color::BLACK;
    return true;
}

std::optional<chess::Move> Game::lastMove() const
{
    if (m_history.empty()) return std::nullopt;
    return m_history.back().move;
}

GameStatus Game::status() const
{
    switch (m_board.isGameOver().first) {
    case chess::GameResultReason::CHECKMATE: return GameStatus::Checkmate;
    case chess::GameResultReason::STALEMATE: return GameStatus::Stalemate;
    case chess::GameResultReason::THREEFOLD_REPETITION: return GameStatus::Repetition;
    case chess::GameResultReason::FIFTY_MOVE_RULE: return GameStatus::FiftyMoveRule;
    case chess::GameResultReason::INSUFFICIENT_MATERIAL: return GameStatus::InsufficientMaterial;
    case chess::GameResultReason::NONE: break;
    }
    return GameStatus::Ongoing;
}

int Game::materialBalance(chess::Color side) const
{
    static constexpr int kValues[] = {1, 3, 3, 5, 9};
    int balance = 0;
    for (int pt = 0; pt < 5; ++pt) {
        const chess::PieceType type(static_cast<chess::PieceType::underlying>(pt));
        balance += kValues[pt] * (m_board.pieces(type, side).count() - m_board.pieces(type, ~side).count());
    }
    return balance;
}

std::vector<chess::Move> Game::legalMovesFrom(chess::Square from) const
{
    chess::Movelist moves;
    chess::movegen::legalmoves(moves, m_board);
    std::vector<chess::Move> result;
    for (const auto& move : moves)
        if (move.from() == from) result.push_back(move);
    return result;
}

std::optional<chess::Move> Game::findMove(chess::Square from, chess::Square to, chess::PieceType promotion) const
{
    for (const auto& move : legalMovesFrom(from)) {
        if (targetSquare(move) != to) continue;
        if (move.typeOf() == chess::Move::PROMOTION && move.promotionType() != promotion) continue;
        return move;
    }
    return std::nullopt;
}

bool Game::isPromotion(chess::Square from, chess::Square to) const
{
    const auto move = findMove(from, to);
    return move && move->typeOf() == chess::Move::PROMOTION;
}

void Game::play(chess::Move move)
{
    m_history.push_back({move, chess::uci::moveToSan(m_board, move)});
    m_board.makeMove(move);
}

bool Game::undo()
{
    if (m_history.empty()) return false;
    m_board.unmakeMove(m_history.back().move);
    m_history.pop_back();
    return true;
}

chess::Square targetSquare(chess::Move move)
{
    if (move.typeOf() != chess::Move::CASTLING) return move.to();
    const bool kingSide = move.to() > move.from();
    return chess::Square(kingSide ? chess::File::FILE_G : chess::File::FILE_C, move.from().rank());
}

std::string statusText(GameStatus status)
{
    switch (status) {
    case GameStatus::Ongoing: return "";
    case GameStatus::Checkmate: return "Checkmate";
    case GameStatus::Stalemate: return "Stalemate";
    case GameStatus::Repetition: return "Draw by threefold repetition";
    case GameStatus::FiftyMoveRule: return "Draw by fifty-move rule";
    case GameStatus::InsufficientMaterial: return "Draw by insufficient material";
    }
    return "";
}

} // namespace core
