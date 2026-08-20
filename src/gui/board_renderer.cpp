#include "gui/board_renderer.hpp"
#include <iostream>
#include <algorithm>

namespace gui {

BoardRenderer::BoardRenderer()
    : m_squareSize(80.0f),
      m_boardOffset(50.0f, 50.0f),
      m_pieceSprite(m_pieceTexture)
{
    if (!m_pieceTexture.loadFromFile("../assets/pieces.png"))
        std::cerr << "Failed to load piece texture!" << std::endl;
    // Use Inter Regular for crisp coordinate labels
    if (!m_font.openFromFile("../assets/Inter-Regular.ttf"))
        if (!m_font.openFromFile("../assets/font.ttf"))   // fallback
            std::cerr << "Warning: Could not load coordinate font!" << std::endl;
    m_pieceTexture.setSmooth(true);
    sf::Vector2u texSize = m_pieceTexture.getSize();
    float pw = texSize.x / 6.f, ph = texSize.y / 2.f;
    m_pieceSprite.setScale({m_squareSize / pw, m_squareSize / ph});
}

sf::IntRect BoardRenderer::getTextureRectForPiece(char piece) const {
    sf::Vector2u texSize = m_pieceTexture.getSize();
    int pw = texSize.x / 6, ph = texSize.y / 2;
    int row = std::islower(piece) ? 1 : 0, col = 0;
    switch (std::tolower(piece)) {
        case 'k': col = 0; break;
        case 'q': col = 1; break;
        case 'b': col = 2; break;
        case 'n': col = 3; break;
        case 'r': col = 4; break;
        case 'p': col = 5; break;
        default: return {{0, 0}, {0, 0}};
    }
    return {{col * pw, row * ph}, {pw, ph}};
}

void BoardRenderer::render(sf::RenderWindow& window, const std::string& fen,
                           bool highlightKing, chess::Color kingColor)
{
    // ── geometry ────────────────────────────────────────────────────────────
    float scaleFactor   = static_cast<float>(window.getSize().y) / 768.0f;
    float uiPanelWidth  = 390.0f * scaleFactor;   // must match application.cpp
    float availableW    = window.getSize().x - uiPanelWidth;
    float availableH    = window.getSize().y;
    float maxBoardSize  = std::min(availableW, availableH) * 0.88f;

    m_squareSize   = maxBoardSize / 8.f;
    m_boardOffset.x = (availableW  - maxBoardSize) / 2.f;
    m_boardOffset.y = (availableH  - maxBoardSize) / 2.f;

    sf::Vector2u texSize = m_pieceTexture.getSize();
    float pw = texSize.x / 6.f, ph = texSize.y / 2.f;
    m_pieceSprite.setScale({m_squareSize / pw, m_squareSize / ph});

    // ── board frame / shadow ────────────────────────────────────────────────
    {
        float fp = m_squareSize * 0.09f;   // frame padding

        // Drop shadow (offset slightly)
        sf::RectangleShape shadow({maxBoardSize + fp * 2 + 6, maxBoardSize + fp * 2 + 6});
        shadow.setFillColor(sf::Color(6, 6, 6, 200));
        shadow.setPosition({m_boardOffset.x - fp - 3, m_boardOffset.y - fp - 3});
        window.draw(shadow);

        // Wooden frame
        sf::RectangleShape frame({maxBoardSize + fp * 2, maxBoardSize + fp * 2});
        frame.setFillColor(sf::Color(38, 26, 14));   // dark walnut
        frame.setPosition({m_boardOffset.x - fp, m_boardOffset.y - fp});
        window.draw(frame);
    }

    // ── squares ─────────────────────────────────────────────────────────────
    // Chess.com classic wood palette
    const sf::Color lightSq(240, 217, 181);   // cream
    const sf::Color darkSq (181, 136,  99);   // wood brown

    sf::RectangleShape square({m_squareSize, m_squareSize});
    for (int rank = 0; rank < 8; ++rank) {
        for (int file = 0; file < 8; ++file) {
            square.setFillColor((rank + file) % 2 == 0 ? lightSq : darkSq);
            square.setPosition({m_boardOffset.x + file * m_squareSize,
                                m_boardOffset.y + rank * m_squareSize});
            window.draw(square);
        }
    }

    // ── pieces ──────────────────────────────────────────────────────────────
    {
        int rank = 0, file = 0;
        for (char c : fen) {
            if (c == ' ') break;
            if (c == '/') { rank++; file = 0; }
            else if (std::isdigit(c)) { file += (c - '0'); }
            else {
                sf::IntRect tr = getTextureRectForPiece(c);
                if (tr.size.x > 0) {
                    m_pieceSprite.setTextureRect(tr);
                    m_pieceSprite.setPosition({m_boardOffset.x + file * m_squareSize,
                                               m_boardOffset.y + rank * m_squareSize});
                    window.draw(m_pieceSprite);
                }
                file++;
            }
        }
    }

    // ── checkmate / check king highlight (Lichess-style vivid red) ──────────
    if (highlightKing) {
        char kingChar = (kingColor == chess::Color::WHITE) ? 'K' : 'k';
        int kingFile = -1, kingRank = -1;
        int r = 0, f = 0;
        for (char c : fen) {
            if (c == ' ') break;
            if (c == '/')              { r++; f = 0; }
            else if (std::isdigit(c)) { f += (c - '0'); }
            else {
                if (c == kingChar) { kingFile = f; kingRank = r; }
                f++;
            }
        }
        if (kingFile >= 0 && kingRank >= 0) {
            float thickness = std::max(3.f, m_squareSize * 0.065f);
            sf::RectangleShape hl({m_squareSize, m_squareSize});
            hl.setFillColor(sf::Color(204, 0, 0, 140));    // vivid red ~55% opacity
            hl.setOutlineColor(sf::Color(204, 0, 0, 255)); // solid border
            hl.setOutlineThickness(-thickness);             // inset
            hl.setPosition({m_boardOffset.x + kingFile * m_squareSize,
                            m_boardOffset.y + kingRank * m_squareSize});
            window.draw(hl);
        }
    }

    // ── coordinates ─────────────────────────────────────────────────────────
    int   fontSize = static_cast<int>(m_squareSize * 0.18f);
    float padding  = m_squareSize * 0.05f;

    for (int i = 0; i < 8; ++i) {
        // On a chess.com board, rank labels on the left edge of each square
        // use the opposite square colour so they contrast with the background.
        bool rankOnLight = (i % 2 == 0); // rank 8 is on a light square (file a)
        bool fileOnLight = ((7 + i) % 2 == 0);

        // Rank numbers (left side)
        sf::Text rankText(m_font);
        rankText.setString(std::to_string(8 - i));
        rankText.setCharacterSize(fontSize);
        rankText.setFillColor(rankOnLight ? darkSq : lightSq);
        sf::FloatRect rb = rankText.getLocalBounds();
        rankText.setPosition({m_boardOffset.x + padding - rb.position.x,
                              m_boardOffset.y + i * m_squareSize + padding - rb.position.y});
        window.draw(rankText);

        // File letters (bottom side)
        sf::Text fileText(m_font);
        fileText.setString(std::string(1, 'a' + i));
        fileText.setCharacterSize(fontSize);
        fileText.setFillColor(fileOnLight ? darkSq : lightSq);
        sf::FloatRect fb = fileText.getLocalBounds();
        fileText.setPosition({
            m_boardOffset.x + i * m_squareSize + m_squareSize - fb.size.x - fb.position.x - padding,
            m_boardOffset.y + 7 * m_squareSize + m_squareSize - fb.size.y - fb.position.y - padding
        });
        window.draw(fileText);
    }
}

std::string BoardRenderer::getSquareFromPixel(int x, int y) const {
    int file = static_cast<int>((x - m_boardOffset.x) / m_squareSize);
    int rank = static_cast<int>((y - m_boardOffset.y) / m_squareSize);
    if (file >= 0 && file < 8 && rank >= 0 && rank < 8)
        return std::string(1, 'a' + file) + static_cast<char>('8' - rank);
    return "";
}}