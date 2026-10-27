#pragma once

#include "chess.hpp"

#include <optional>
#include <string>
#include <vector>

namespace core {

enum class GameStatus {
    Ongoing,
    Checkmate,
    Stalemate,
    Repetition,
    FiftyMoveRule,
    InsufficientMaterial,
};

struct PlayedMove {
    chess::Move move;
    std::string san;
};

// A game of chess: the current position plus the moves that led to it.
class Game {
public:
    void reset();
    // Starts from the given position. Returns false (and changes nothing) if it is invalid.
    bool loadFen(const std::string& fen);
    std::string fen() const { return m_board.getFen(); }

    const chess::Board& board() const { return m_board; }
    const std::vector<PlayedMove>& history() const { return m_history; }
    // Move number and side to move of the starting position, for numbering the history.
    int firstMoveNumber() const { return m_firstMoveNumber; }
    bool blackMovedFirst() const { return m_blackMovedFirst; }
    std::optional<chess::Move> lastMove() const;

    chess::Color sideToMove() const { return m_board.sideToMove(); }
    bool inCheck() const { return m_board.inCheck(); }
    GameStatus status() const;
    bool isOver() const { return status() != GameStatus::Ongoing; }
    // Material balance in pawns (1/3/3/5/9) from `side`'s point of view.
    int materialBalance(chess::Color side) const;

    // Legal moves of the piece standing on `from`.
    std::vector<chess::Move> legalMovesFrom(chess::Square from) const;
    // Finds the legal move from -> to (castling uses the king's destination square).
    std::optional<chess::Move> findMove(chess::Square from, chess::Square to,
                                        chess::PieceType promotion = chess::PieceType::QUEEN) const;
    bool isPromotion(chess::Square from, chess::Square to) const;

    // `move` must be legal in the current position.
    void play(chess::Move move);
    bool undo();

private:
    chess::Board m_board;
    std::vector<PlayedMove> m_history;
    int m_firstMoveNumber = 1;
    bool m_blackMovedFirst = false;
};

// Where the king lands for a castling move (chess.hpp encodes castling as king-takes-rook).
chess::Square targetSquare(chess::Move move);

std::string statusText(GameStatus status);

} // namespace core
