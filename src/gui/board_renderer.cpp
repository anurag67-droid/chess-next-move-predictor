#include "gui/board_renderer.hpp"
#include <iostream>
#include <algorithm>

namespace gui {
BoardRenderer::BoardRenderer() 
    : m_squareSize(80.0f), 
      m_boardOffset(50.0f, 50.0f),
      m_pieceSprite(m_pieceTexture) 
{
    if (!m_pieceTexture.loadFromFile("../assets/pieces/pieces.png")) std::cerr << "Failed to load piece texture!" << std::endl;
    if (!m_font.openFromFile("../assets/font.ttf")) std::cerr << "Warning: Could not load coordinate font!" << std::endl;
    m_pieceTexture.setSmooth(true);
    sf::Vector2u texSize = m_pieceTexture.getSize();
    float pieceWidth = texSize.x / 6.0f;
    float pieceHeight = texSize.y / 2.0f;
    m_pieceSprite.setScale({m_squareSize / pieceWidth, m_squareSize / pieceHeight});
}

sf::IntRect BoardRenderer::getTextureRectForPiece(char piece) const {
    sf::Vector2u texSize = m_pieceTexture.getSize();
    int pieceWidth = texSize.x / 6, pieceHeight = texSize.y / 2;
    int row = (std::islower(piece)) ? 1 : 0, col = 0;
    switch (std::tolower(piece)) {
        case 'k': col = 0; break;
        case 'q': col = 1; break;
        case 'b': col = 2; break;
        case 'n': col = 3; break;
        case 'r': col = 4; break;
        case 'p': col = 5; break;
        default: return {{0, 0}, {0, 0}}; 
    }
    return {{col * pieceWidth, row * pieceHeight}, {pieceWidth, pieceHeight}};
}

void BoardRenderer::render(sf::RenderWindow& window, const std::string& fen) {
    float scaleFactor = static_cast<float>(window.getSize().y) / 768.0f,
    uiPanelWidth = 420.0f * scaleFactor,
    availableWidth = window.getSize().x - uiPanelWidth,
    availableHeight = window.getSize().y,
    maxBoardSize = std::min(availableWidth, availableHeight)*0.9f; 
    m_squareSize = maxBoardSize / 8.0f;
    m_boardOffset.x = (availableWidth - maxBoardSize) / 2.0f;
    m_boardOffset.y = (availableHeight - maxBoardSize) / 2.0f;
    sf::Vector2u texSize = m_pieceTexture.getSize();
    float pieceWidth = texSize.x / 6.0f,
    pieceHeight = texSize.y / 2.0f;
    m_pieceSprite.setScale({m_squareSize / pieceWidth, m_squareSize / pieceHeight});

    //Board
    sf::RectangleShape square({m_squareSize, m_squareSize}); 
    for (int rank = 0; rank < 8; ++rank) {
        for (int file = 0; file < 8; ++file) {
            bool isLight = (rank + file) % 2 == 0;
            sf::Color lightCol(238, 238, 210);
            sf::Color darkCol(118, 150, 86);
            square.setFillColor(isLight ? lightCol : darkCol); 
            square.setPosition({m_boardOffset.x + file * m_squareSize, m_boardOffset.y + rank * m_squareSize});
            window.draw(square);
        }
    }

    //Pieces
    int rank = 0,file = 0;
    for (char c : fen) {
        if (c == ' ') break;
        if (c == '/') {
            rank++;
            file = 0;
        } 
        else if (std::isdigit(c)) file += (c-'0'); 
        else{
            sf::IntRect texRect = getTextureRectForPiece(c);
            if (texRect.size.x > 0){
                m_pieceSprite.setTextureRect(texRect);
                m_pieceSprite.setPosition({
                    m_boardOffset.x + (file * m_squareSize),
                    m_boardOffset.y + (rank * m_squareSize)
                });
                window.draw(m_pieceSprite);
            }
            file++;
        }
    }

    //Cordinates
    int fontSize = static_cast<int>(m_squareSize * 0.18f); 
    float padding = m_squareSize * 0.05f; 

    for (int i = 0; i < 8; ++i) {
        bool isLightRank = (i % 2) == 0;
        bool isLightFile = (7 + i) % 2 == 0;
        sf::Color lightCol(238, 238, 210);
        sf::Color darkCol(118, 150, 86);
        // Rank Numbers
        sf::Text rankText(m_font);
        rankText.setString(std::to_string(8 - i));
        rankText.setCharacterSize(fontSize);
        rankText.setFillColor(isLightRank ? darkCol : lightCol);
        sf::FloatRect rBounds = rankText.getLocalBounds();
        rankText.setPosition({
            m_boardOffset.x + padding - rBounds.position.x, 
            m_boardOffset.y + i * m_squareSize + padding - rBounds.position.y
        });
        window.draw(rankText);

        // File Letters
        sf::Text fileText(m_font);
        fileText.setString(std::string(1, 'a' + i));
        fileText.setCharacterSize(fontSize);
        fileText.setFillColor(isLightFile ? darkCol : lightCol);
        
        sf::FloatRect fBounds = fileText.getLocalBounds();
        fileText.setPosition({
            m_boardOffset.x + i * m_squareSize + m_squareSize - fBounds.size.x - fBounds.position.x - padding,
            m_boardOffset.y + 7 * m_squareSize + m_squareSize - fBounds.size.y - fBounds.position.y - padding
        });
        window.draw(fileText);
    }
} 

std::string BoardRenderer::getSquareFromPixel(int x, int y) const {
    int file = static_cast<int>((x - m_boardOffset.x) / m_squareSize),
    rank = static_cast<int>((y - m_boardOffset.y) / m_squareSize);
    if (file >= 0 && file < 8 && rank >= 0 && rank < 8) {
        char fileChar = 'a' + file,
        rankChar = '8' - rank;
        return std::string(1, fileChar) + rankChar;
    }
    return "";
}}