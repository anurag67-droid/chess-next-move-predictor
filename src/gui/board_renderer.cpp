#include "gui/board_renderer.hpp"

#include "gui/assets.hpp"

#include <cmath>
#include <stdexcept>

namespace gui {
namespace {

// Column of each piece type in pieces.png (white on the top row, black below),
// indexed by chess::PieceType: pawn, knight, bishop, rook, queen, king.
int spriteColumn(chess::PieceType type)
{
    static constexpr int kColumns[] = {5, 3, 2, 4, 1, 0};
    const int index = static_cast<int>(type);
    return index >= 0 && index < 6 ? kColumns[index] : -1;
}

sf::Color withAlpha(sf::Color c, std::uint8_t alpha)
{
    c.a = alpha;
    return c;
}

sf::ConvexShape polygon(std::initializer_list<sf::Vector2f> points, sf::Color fill)
{
    sf::ConvexShape shape(points.size());
    std::size_t i = 0;
    for (const auto& p : points) shape.setPoint(i++, p);
    shape.setFillColor(fill);
    return shape;
}

} // namespace

BoardRenderer::BoardRenderer()
{
    if (!m_pieceTexture.loadFromFile(assetPath("pieces.png")))
        throw std::runtime_error("Could not load assets/pieces.png");
    m_pieceTexture.setSmooth(true);
    (void)m_pieceTexture.generateMipmap(); // sharper pieces when scaled down; optional

    if (!m_font.openFromFile(assetPath("Inter-Bold.ttf")))
        throw std::runtime_error("Could not load assets/Inter-Bold.ttf");
}

sf::IntRect BoardRenderer::pieceRect(chess::Piece piece) const
{
    const int column = spriteColumn(piece.type());
    if (column < 0) return {};
    const auto size = m_pieceTexture.getSize();
    const int w = static_cast<int>(size.x) / 6;
    const int h = static_cast<int>(size.y) / 2;
    const int row = piece.color() == chess::Color::WHITE ? 0 : 1;
    return {{column * w, row * h}, {w, h}};
}

sf::Vector2f BoardRenderer::squareTopLeft(chess::Square sq) const
{
    const int file = sq.index() % 8;
    const int rank = sq.index() / 8;
    const int col = m_flipped ? 7 - file : file;
    const int row = m_flipped ? rank : 7 - rank;
    return {m_area.position.x + col * squareSize(), m_area.position.y + row * squareSize()};
}

std::optional<chess::Square> BoardRenderer::squareAt(sf::Vector2f pixel) const
{
    if (!m_area.contains(pixel)) return std::nullopt;
    const int col = std::min(7, static_cast<int>((pixel.x - m_area.position.x) / squareSize()));
    const int row = std::min(7, static_cast<int>((pixel.y - m_area.position.y) / squareSize()));
    const int file = m_flipped ? 7 - col : col;
    const int rank = m_flipped ? row : 7 - row;
    return chess::Square(rank * 8 + file);
}

void BoardRenderer::draw(sf::RenderTarget& target, const BoardScene& scene) const
{
    drawSquares(target, scene);
    drawCoordinates(target);
    if (scene.checkedKing) drawCheck(target, *scene.checkedKing);
    drawMoveHints(target, scene);
    drawPieces(target, scene);
    if (scene.arrow) drawArrow(target, *scene.arrow);

    if (scene.dragFrom) {
        const chess::Piece piece = scene.board->at(*scene.dragFrom);
        sf::Sprite sprite(m_pieceTexture, pieceRect(piece));
        const float size = squareSize() * 1.08f; // lifted pieces look slightly bigger
        sprite.setScale({size / sprite.getTextureRect().size.x, size / sprite.getTextureRect().size.y});
        sprite.setPosition(scene.dragPosition - sf::Vector2f(size, size) / 2.f);
        target.draw(sprite);
    }
}

void BoardRenderer::drawSquares(sf::RenderTarget& target, const BoardScene& scene) const
{
    const float size = squareSize();
    sf::RectangleShape square({size, size});

    for (int sq = 0; sq < 64; ++sq) {
        const bool light = (sq / 8 + sq % 8) % 2 == 1;
        square.setPosition(squareTopLeft(chess::Square(sq)));
        square.setFillColor(light ? m_theme.light : m_theme.dark);
        target.draw(square);
    }

    square.setFillColor(color::Highlight);
    if (scene.lastMove) {
        for (const auto sq : {scene.lastMove->from, scene.lastMove->to}) {
            square.setPosition(squareTopLeft(sq));
            target.draw(square);
        }
    }
    if (scene.selected) {
        square.setFillColor(withAlpha(color::Highlight, 150));
        square.setPosition(squareTopLeft(*scene.selected));
        target.draw(square);
    }
}

void BoardRenderer::drawCoordinates(sf::RenderTarget& target) const
{
    const float size = squareSize();
    const float pad = size * 0.06f;
    sf::Text text(m_font, "", static_cast<unsigned>(size * 0.16f));

    for (int i = 0; i < 8; ++i) {
        // Rank numbers along the left edge, file letters along the bottom edge,
        // each drawn in the colour of the opposite square for contrast.
        const int rank = m_flipped ? i : 7 - i;
        const int leftFile = m_flipped ? 7 : 0;
        const bool rankSquareLight = (rank + leftFile) % 2 == 1;
        text.setString(std::string(1, static_cast<char>('1' + rank)));
        text.setFillColor(rankSquareLight ? m_theme.dark : m_theme.light);
        const auto rankBounds = text.getLocalBounds();
        text.setPosition({m_area.position.x + pad - rankBounds.position.x,
                          m_area.position.y + i * size + pad - rankBounds.position.y});
        target.draw(text);

        const int file = m_flipped ? 7 - i : i;
        const int bottomRank = m_flipped ? 7 : 0;
        const bool fileSquareLight = (bottomRank + file) % 2 == 1;
        text.setString(std::string(1, static_cast<char>('a' + file)));
        text.setFillColor(fileSquareLight ? m_theme.dark : m_theme.light);
        const auto fileBounds = text.getLocalBounds();
        text.setPosition({m_area.position.x + (i + 1) * size - pad - fileBounds.size.x - fileBounds.position.x,
                          m_area.position.y + 8 * size - pad - fileBounds.size.y - fileBounds.position.y});
        target.draw(text);
    }
}

void BoardRenderer::drawCheck(sf::RenderTarget& target, chess::Square king) const
{
    // Radial red glow under the king in check.
    const float size = squareSize();
    const sf::Vector2f center = squareTopLeft(king) + sf::Vector2f(size, size) / 2.f;
    constexpr int kSegments = 40;
    sf::VertexArray glow(sf::PrimitiveType::TriangleFan, kSegments + 2);
    glow[0] = sf::Vertex{center, sf::Color(255, 0, 0, 230)};
    for (int i = 0; i <= kSegments; ++i) {
        const float angle = 2.f * 3.14159265f * static_cast<float>(i) / kSegments;
        glow[i + 1] = sf::Vertex{center + sf::Vector2f(std::cos(angle), std::sin(angle)) * size * 0.55f,
                                 sf::Color(200, 0, 0, 0)};
    }
    target.draw(glow);
}

void BoardRenderer::drawMoveHints(sf::RenderTarget& target, const BoardScene& scene) const
{
    const float size = squareSize();
    const sf::Color hint(20, 30, 20, 60);

    for (const auto sq : scene.legalTargets) {
        const sf::Vector2f topLeft = squareTopLeft(sq);
        if (scene.board->at(sq) == chess::Piece::NONE) {
            const float radius = size * 0.16f;
            sf::CircleShape dot(radius);
            dot.setFillColor(hint);
            dot.setPosition(topLeft + sf::Vector2f(size / 2.f - radius, size / 2.f - radius));
            target.draw(dot);
        } else {
            const float thickness = size * 0.08f;
            sf::CircleShape ring(size / 2.f - thickness);
            ring.setFillColor(sf::Color::Transparent);
            ring.setOutlineColor(hint);
            ring.setOutlineThickness(thickness);
            ring.setPosition(topLeft + sf::Vector2f(thickness, thickness));
            target.draw(ring);
        }
    }
}

void BoardRenderer::drawPieces(sf::RenderTarget& target, const BoardScene& scene) const
{
    const float size = squareSize();
    sf::Sprite sprite(m_pieceTexture);

    for (int sq = 0; sq < 64; ++sq) {
        const chess::Piece piece = scene.board->at(chess::Square(sq));
        if (piece == chess::Piece::NONE) continue;

        sprite.setTextureRect(pieceRect(piece));
        sprite.setScale({size / sprite.getTextureRect().size.x, size / sprite.getTextureRect().size.y});
        sprite.setPosition(squareTopLeft(chess::Square(sq)));
        // The dragged piece leaves a faint ghost on its starting square.
        sprite.setColor(scene.dragFrom && scene.dragFrom->index() == sq ? sf::Color(255, 255, 255, 70)
                                                                        : sf::Color::White);
        target.draw(sprite);
    }
}

void BoardRenderer::drawArrow(sf::RenderTarget& target, MoveSquares move) const
{
    const float size = squareSize();
    const sf::Vector2f half(size / 2.f, size / 2.f);
    const sf::Vector2f start = squareTopLeft(move.from) + half;
    const sf::Vector2f end = squareTopLeft(move.to) + half;
    const sf::Vector2f delta = end - start;
    const float length = std::hypot(delta.x, delta.y);
    if (length < 1.f) return;

    const sf::Vector2f dir = delta / length;
    const sf::Vector2f normal(-dir.y, dir.x);
    const float shaftHalf = size * 0.09f;
    const float headHalf = size * 0.24f;
    const float headLength = size * 0.42f;
    const sf::Vector2f tip = end - dir * (size * 0.12f);
    const sf::Vector2f base = tip - dir * headLength;

    target.draw(polygon({start + normal * shaftHalf, base + normal * shaftHalf,
                         base - normal * shaftHalf, start - normal * shaftHalf},
                        color::Arrow));
    target.draw(polygon({base + normal * headHalf, tip, base - normal * headHalf}, color::Arrow));
}

void BoardRenderer::drawEvalBar(sf::RenderTarget& target, sf::FloatRect rect, float whiteShare,
                                const sf::String& label) const
{
    sf::RectangleShape background(rect.size);
    background.setPosition(rect.position);
    background.setFillColor(color::EvalBlack);
    target.draw(background);

    // White's share grows from White's side of the board.
    const float whiteHeight = rect.size.y * whiteShare;
    sf::RectangleShape white({rect.size.x, whiteHeight});
    white.setPosition({rect.position.x, m_flipped ? rect.position.y : rect.position.y + rect.size.y - whiteHeight});
    white.setFillColor(color::EvalWhite);
    target.draw(white);

    // Score label sits at the end of the side that is ahead.
    const bool whiteAhead = whiteShare >= 0.5f;
    const bool labelAtBottom = whiteAhead != m_flipped;
    sf::Text text(m_font, label, static_cast<unsigned>(rect.size.x * 0.36f));
    text.setFillColor(whiteAhead ? color::EvalBlack : color::EvalWhite);
    const auto bounds = text.getLocalBounds();
    const float pad = rect.size.x * 0.25f;
    const float x = rect.position.x + (rect.size.x - bounds.size.x) / 2.f - bounds.position.x;
    const float y = labelAtBottom ? rect.position.y + rect.size.y - pad - bounds.size.y - bounds.position.y
                                  : rect.position.y + pad - bounds.position.y;
    text.setPosition({std::round(x), std::round(y)});
    target.draw(text);
}

} // namespace gui
