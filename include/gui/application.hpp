#pragma once
#include <SFML/Graphics.hpp>
#include "gui/board_renderer.hpp"
#include "core/board.hpp"
#include "gui/board_renderer.hpp"
#include "chess.hpp"

namespace gui {
    class Application {
        void processEvents();
        void update(sf::Time dt);
        void render();
        void setupImGuiStyle();         // one-time style setup
        sf::RenderWindow m_window;
        sf::Clock m_deltaClock;
        sf::Clock m_moveClock;
        BoardRenderer m_boardRenderer;
        core::Board m_board;
        bool m_isDragging;
        std::string m_selectedSquare;
        sf::Vector2i m_mouseCoords;
        std::vector<std::string> m_moveHistory;
        float m_currentEval;
        void updateEvaluation();
        // Timer state
        float m_whiteTimeLeft;
        float m_blackTimeLeft;
        // Game-over state
        bool m_gameOver;
        chess::Color m_matedColor;
    public:
        Application();
        ~Application();
        void run();
    };
}