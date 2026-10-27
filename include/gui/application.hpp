#pragma once

#include "core/game.hpp"
#include "gui/board_renderer.hpp"
#include "gui/engine_worker.hpp"

#include <SFML/Graphics.hpp>
#include <imgui.h>

#include <array>
#include <optional>
#include <string>

namespace gui {

struct TimeControl {
    const char* name;
    int seconds; // 0 = unlimited
};

inline constexpr TimeControl kTimeControls[] = {
    {"Unlimited", 0}, {"1 min", 60}, {"3 min", 180}, {"5 min", 300}, {"10 min", 600}, {"30 min", 1800},
};

// The main window: chessboard on the left, controls on the right.
class Application {
public:
    Application();
    ~Application();
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    void run();

private:
    struct Layout {
        float scale = 1.f;
        sf::FloatRect panel;
        sf::FloatRect board;
        sf::FloatRect evalBar;
        sf::FloatRect topBar;
        sf::FloatRect bottomBar;
    };

    struct Fonts {
        ImFont* body = nullptr;
        ImFont* bold = nullptr;
        ImFont* title = nullptr;
        ImFont* clock = nullptr;
        ImFont* score = nullptr;
    };

    // Main loop
    void handleEvent(const sf::Event& event);
    void update(float dt);
    void render();

    // Board interaction
    void onMousePressed(sf::Vector2f position);
    void onMouseReleased(sf::Vector2f position);
    void tryUserMove(chess::Square from, chess::Square to);
    bool canMovePieces() const;
    BoardScene buildScene() const;

    // Game flow
    void playMove(chess::Move move, bool byEngine);
    void newGame();
    void loadPosition(const std::string& fen);
    void undoMove();
    void resetClocks();
    void tickClock(float dt);
    bool isGameOver() const;
    bool isTimed() const;
    std::string resultText() const;       // e.g. "White wins by checkmate"
    std::string scoreSheetResult() const; // "1-0", "0-1" or "1/2-1/2"

    // Engine
    void startAnalysis();
    void startEngineMove();
    void pollEngine();
    bool engineIsThinking() const;

    // Interface (layout and fonts in application.cpp, drawing in panel.cpp)
    void updateLayout();
    void loadFonts(float scale);
    void drawPanel();
    void drawStatusBanner();
    void drawEngineCard();
    void drawGameCard();
    void drawMoveList();
    void drawPlayerBar(const char* id, sf::FloatRect rect, chess::Color side);
    void drawPromotionPicker();
    std::optional<sf::FloatRect> promotionPickerRect() const;

    sf::RenderWindow m_window;
    sf::Clock m_frameClock;
    BoardRenderer m_renderer;
    core::Game m_game;
    EngineWorker m_engine;

    Layout m_layout;
    Fonts m_fonts;
    float m_fontScale = 0.f;

    // Board interaction
    std::optional<chess::Square> m_selected;
    bool m_dragging = false;
    bool m_deselectOnRelease = false;
    sf::Vector2f m_mouse;
    std::optional<MoveSquares> m_pendingPromotion;

    // Engine
    engine::SearchResult m_analysis; // latest result for the current position
    float m_evalShare = 0.5f;        // animated eval bar fill
    sf::Clock m_thinkClock;
    int m_thinkSeconds = 5;
    bool m_showArrow = true;
    bool m_autoReply = false;

    // Clocks
    int m_timeControl = 0;
    std::array<float, 2> m_clock{}; // seconds left for White and Black
    std::optional<chess::Color> m_flagged;

    // Position input
    std::array<char, 128> m_fenInput{};
    std::string m_fenError;

    // View
    bool m_flipped = false;
    int m_boardTheme = 0;
};

} // namespace gui
