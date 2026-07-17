#include "gui/application.hpp"
#include "engine/search.hpp"
#include <imgui.h>
#include <imgui-SFML.h>
#include <stdexcept>
#include <iostream>

namespace gui {
Application::Application()
    : m_window(sf::VideoMode({1024, 768}), "C++ Chess Next-Move Predictor", sf::Style::Default),
      m_isDragging(false),
      m_selectedSquare(""),
      m_currentEval(0.0f)
{
    m_window.setFramerateLimit(60);
    if (!ImGui::SFML::Init(m_window)) throw std::runtime_error("Failed to initialize ImGui-SFML bridge");
    ImGui::GetIO().FontGlobalScale = 1.25f;
    updateEvaluation(); 
}

Application::~Application() {
    ImGui::SFML::Shutdown();
}

void Application::run() {
    while (m_window.isOpen()) {
        processEvents();
        sf::Time dt = m_deltaClock.restart();
        update(dt);
        render();
    }
}

void Application::updateEvaluation() {
    // a quick Depth 4 search so the UI sees past immediate trades
    m_currentEval = engine::getFutureEvaluation(m_board.getInternalBoard(), 4);
}

void Application::processEvents() {
    while (const auto event = m_window.pollEvent()) {
        ImGui::SFML::ProcessEvent(m_window, *event);
        if (event->is<sf::Event::Closed>()) m_window.close();
        if (const auto* resized = event->getIf<sf::Event::Resized>()) {
            sf::FloatRect visibleArea({0.0f, 0.0f}, {static_cast<float>(resized->size.x), static_cast<float>(resized->size.y)});
            m_window.setView(sf::View(visibleArea));
            float scaleFactor = static_cast<float>(resized->size.y) / 768.0f;
            ImGui::GetIO().FontGlobalScale = 1.25f * scaleFactor;
        }
        if (const auto* mouseClick = event->getIf<sf::Event::MouseButtonPressed>()) {
            if (mouseClick->button == sf::Mouse::Button::Left){
                std::string clickedSquare = m_boardRenderer.getSquareFromPixel(mouseClick->position.x, mouseClick->position.y);
                if (!clickedSquare.empty()) {
                    m_isDragging = true;
                    m_selectedSquare = clickedSquare;
                }
            }
        }

        if (const auto* mouseMove = event->getIf<sf::Event::MouseMoved>()) {
            m_mouseCoords = mouseMove->position;
        }

        if (const auto* mouseRelease = event->getIf<sf::Event::MouseButtonReleased>()) {
            if (mouseRelease->button == sf::Mouse::Button::Left && m_isDragging) {
                m_isDragging = false;
                std::string targetSquare = m_boardRenderer.getSquareFromPixel(mouseRelease->position.x, mouseRelease->position.y);
                
                if (!targetSquare.empty() && targetSquare != m_selectedSquare) {
                    std::string uciMove = m_selectedSquare + targetSquare;
                    
                    if (m_board.tryMove(uciMove)) {
                        m_moveHistory.push_back(uciMove); // log player's move
                        updateEvaluation(); // update eval bar
                    }
                }
                m_selectedSquare = "";
            }
        }
    }
}

void Application::update(sf::Time dt) {
    ImGui::SFML::Update(m_window, dt);

    float scaleFactor = static_cast<float>(m_window.getSize().y) / 768.0f;
    float uiWidth = 420.0f * scaleFactor;
    ImGui::SetNextWindowPos(ImVec2(m_window.getSize().x - uiWidth, 0));
    ImGui::SetNextWindowSize(ImVec2(uiWidth, m_window.getSize().y));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;
    ImGui::Begin("Engine Telemetry", nullptr, flags);
    ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
    
    ImGui::Separator();
    
    // using cached stable evaluation!
    ImGui::Text("Live Evaluation: %+.2f", m_currentEval);
    if (m_currentEval > 0.1f) ImGui::TextColored({0.0f, 1.0f, 0.0f, 1.0f}, "White has the advantage");
    else if (m_currentEval < -0.1f) ImGui::TextColored({1.0f, 0.3f, 0.3f, 1.0f}, "Black has the advantage");
    else ImGui::TextColored({0.7f, 0.7f, 0.7f, 1.0f}, "Position is roughly equal");
    
    ImGui::Separator();
    // -1.0f forces the button to fill the width. We also scale the height so it stays chunky!
    if (ImGui::Button("Predict Best Next Move (2s Limit)", ImVec2(-1.0f, 35.0f * scaleFactor))){
        std::string bestMove = engine::getBestMoveTime(m_board.getInternalBoard(), 2000);
        m_board.tryMove(bestMove);
        m_moveHistory.push_back(bestMove);
        updateEvaluation(); // update eval bar
    }

    // ---MOVE LOGGER UI---
    ImGui::Separator();
    ImGui::Text("Match History:");
    // scrollable box for the moves
    ImGui::BeginChild("MoveLogger", ImVec2(0, 150), true);
    for (size_t i = 0; i < m_moveHistory.size(); ++i) {
        // white move
        if (i % 2 == 0) ImGui::Text("%d. %s", static_cast<int>(i / 2 + 1), m_moveHistory[i].c_str());
        // black move
        else {
            ImGui::SameLine(120.0f * scaleFactor);
            ImGui::Text("%s", m_moveHistory[i].c_str());
        }
    }
    // auto-scroll to the bottom when a new move is added
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
    ImGui::End();
}

void Application::render() {
    m_window.clear(sf::Color(40, 40, 40));
    m_boardRenderer.render(m_window, m_board.getFen());
    ImGui::SFML::Render(m_window);
    m_window.display();
}}