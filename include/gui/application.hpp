#pragma once
#include <SFML/Graphics.hpp>
#include "gui/board_renderer.hpp"
#include "core/board.hpp"
#include "gui/board_renderer.hpp"

namespace gui {
    class Application {
        // internal lifecycle methods
        void processEvents();
        void update(sf::Time dt);
        void render();
        sf::RenderWindow m_window;
        sf::Clock m_deltaClock;
        BoardRenderer m_boardRenderer;
        core::Board m_board;
        bool m_isDragging;
        std::string m_selectedSquare;
        sf::Vector2i m_mouseCoords;
        std::vector<std::string> m_moveHistory; 
        float m_currentEval;
        // update the evaluation only when the board changes
        void updateEvaluation();
    public:
        Application();
        ~Application();
        // execution loop      
        void run();
    };
}