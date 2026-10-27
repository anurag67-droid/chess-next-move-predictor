#pragma once

#include "chess.hpp"
#include "gui/theme.hpp"

#include <SFML/Graphics.hpp>

#include <optional>
#include <string>
#include <vector>

namespace gui {

struct MoveSquares {
    chess::Square from;
    chess::Square to;
};

// Everything that changes from frame to frame about how the board looks.
struct BoardScene {
    const chess::Board* board = nullptr;
    std::optional<MoveSquares> lastMove;
    std::optional<chess::Square> selected;
    std::vector<chess::Square> legalTargets;
    std::optional<chess::Square> checkedKing;
    std::optional<chess::Square> dragFrom; // piece being dragged, drawn at dragPosition
    sf::Vector2f dragPosition;
    std::optional<MoveSquares> arrow; // engine's suggested move
};

class BoardRenderer {
public:
    BoardRenderer();

    void setArea(sf::FloatRect area) { m_area = area; }
    void setFlipped(bool flipped) { m_flipped = flipped; }
    void setTheme(const BoardTheme& theme) { m_theme = theme; }
    sf::FloatRect area() const { return m_area; }

    void draw(sf::RenderTarget& target, const BoardScene& scene) const;
    // Vertical evaluation bar; whiteShare is the fraction of the bar owned by White.
    void drawEvalBar(sf::RenderTarget& target, sf::FloatRect rect, float whiteShare, const sf::String& label) const;

    std::optional<chess::Square> squareAt(sf::Vector2f pixel) const;
    sf::Vector2f squareTopLeft(chess::Square sq) const;
    float squareSize() const { return m_area.size.x / 8.f; }

    const sf::Texture& pieceTexture() const { return m_pieceTexture; }
    sf::IntRect pieceRect(chess::Piece piece) const;

private:
    void drawSquares(sf::RenderTarget& target, const BoardScene& scene) const;
    void drawCoordinates(sf::RenderTarget& target) const;
    void drawCheck(sf::RenderTarget& target, chess::Square king) const;
    void drawMoveHints(sf::RenderTarget& target, const BoardScene& scene) const;
    void drawPieces(sf::RenderTarget& target, const BoardScene& scene) const;
    void drawArrow(sf::RenderTarget& target, MoveSquares move) const;

    sf::Texture m_pieceTexture;
    sf::Font m_font;
    sf::FloatRect m_area{{0.f, 0.f}, {640.f, 640.f}};
    bool m_flipped = false;
    BoardTheme m_theme = kBoardThemes[0];
};

} // namespace gui
