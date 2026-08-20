#pragma once
#include <SFML/Graphics.hpp>
#include <string>
#include <SFML/Graphics/Font.hpp>
#include <SFML/Graphics/Text.hpp>
#include "chess.hpp"
namespace gui {
    class BoardRenderer {
        float m_squareSize;
        sf::Vector2f m_boardOffset;
        // texture and sprite for the pieces
        sf::Texture m_pieceTexture;
        sf::Sprite m_pieceSprite;
        // function to map a FEN character (e.g., 'K' or 'p') to a rectangle on the sprite sheet
        sf::IntRect getTextureRectForPiece(char piece) const;
        sf::Font m_font;
    public:
        BoardRenderer();
        // FEN string, renderer knows pieces go
        // highlightKing=true draws a red border around the mated king's square
        void render(sf::RenderWindow& window, const std::string& fen,
                    bool highlightKing = false,
                    chess::Color kingColor = chess::Color::WHITE);
        // translates screen pixels into chess squares using the renderer's internal scale
        std::string getSquareFromPixel(int x, int y) const;
    };
}