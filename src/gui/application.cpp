#include "gui/application.hpp"

#include "gui/assets.hpp"
#include "gui/format.hpp"
#include "gui/widgets.hpp"

#include <imgui-SFML.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <stdexcept>

namespace gui {
namespace {

constexpr int kAnalysisMs = 3000; // background analysis after every move

} // namespace

Application::Application()
{
    sf::ContextSettings settings;
    settings.antiAliasingLevel = 4;
    m_window.create(sf::VideoMode({1280u, 800u}), "Chess Next Move Predictor", sf::Style::Default,
                    sf::State::Windowed, settings);
    m_window.setFramerateLimit(60);
    m_window.setMinimumSize(sf::Vector2u{960u, 640u});

    if (!ImGui::SFML::Init(m_window, false))
        throw std::runtime_error("Failed to initialise ImGui-SFML");
    ImGui::GetIO().IniFilename = nullptr; // don't litter the working directory with imgui.ini

    resetClocks();
    updateLayout();
    startAnalysis();
}

Application::~Application()
{
    m_engine.cancel();
    ImGui::SFML::Shutdown();
}

void Application::run()
{
    while (m_window.isOpen()) {
        while (const auto event = m_window.pollEvent()) {
            ImGui::SFML::ProcessEvent(m_window, *event);
            handleEvent(*event);
        }
        update(m_frameClock.restart().asSeconds());
        render();
    }
}

// ---------------------------------------------------------------------------
// Input

void Application::handleEvent(const sf::Event& event)
{
    if (event.is<sf::Event::Closed>()) {
        m_window.close();
    } else if (const auto* resized = event.getIf<sf::Event::Resized>()) {
        const sf::Vector2f size(resized->size);
        m_window.setView(sf::View(sf::FloatRect({0.f, 0.f}, size)));
        updateLayout();
    } else if (const auto* moved = event.getIf<sf::Event::MouseMoved>()) {
        m_mouse = sf::Vector2f(moved->position);
    } else if (const auto* pressed = event.getIf<sf::Event::MouseButtonPressed>()) {
        // Clicks on ImGui windows are handled by ImGui. (Hit-testing here instead of using
        // io.WantCaptureMouse, which lags a frame behind fast mouse movements.)
        const sf::Vector2f position(pressed->position);
        const auto picker = promotionPickerRect();
        const bool onInterface = m_layout.panel.contains(position) || (picker && picker->contains(position));
        if (pressed->button == sf::Mouse::Button::Left && !onInterface) onMousePressed(position);
    } else if (const auto* released = event.getIf<sf::Event::MouseButtonReleased>()) {
        if (released->button == sf::Mouse::Button::Left) onMouseReleased(sf::Vector2f(released->position));
    } else if (const auto* key = event.getIf<sf::Event::KeyPressed>()) {
        if (key->code == sf::Keyboard::Key::Escape) {
            m_selected.reset();
            m_dragging = false;
            m_pendingPromotion.reset();
        }
    }
}

void Application::onMousePressed(sf::Vector2f position)
{
    if (m_pendingPromotion) { // clicking anywhere but the picker cancels the promotion
        m_pendingPromotion.reset();
        return;
    }
    if (!canMovePieces()) return;

    const auto square = m_renderer.squareAt(position);
    if (!square) {
        m_selected.reset();
        return;
    }

    // Click-to-move: second click on a legal destination.
    if (m_selected && *square != *m_selected && m_game.findMove(*m_selected, *square)) {
        tryUserMove(*m_selected, *square);
        return;
    }

    const chess::Piece piece = m_game.board().at(*square);
    if (piece != chess::Piece::NONE && piece.color() == m_game.sideToMove()) {
        m_deselectOnRelease = m_selected == square;
        m_selected = square;
        m_dragging = true;
        m_mouse = position;
    } else {
        m_selected.reset();
    }
}

void Application::onMouseReleased(sf::Vector2f position)
{
    if (!m_dragging) return;
    m_dragging = false;

    const auto square = m_renderer.squareAt(position);
    if (!square || !m_selected) {
        m_selected.reset();
        return;
    }
    if (*square == *m_selected) { // a click: keep the piece selected, unless it already was
        if (m_deselectOnRelease) m_selected.reset();
        return;
    }
    if (m_game.findMove(*m_selected, *square)) tryUserMove(*m_selected, *square);
    else m_selected.reset();
}

void Application::tryUserMove(chess::Square from, chess::Square to)
{
    m_selected.reset();
    if (m_game.isPromotion(from, to)) {
        m_pendingPromotion = MoveSquares{from, to};
        return;
    }
    if (const auto move = m_game.findMove(from, to)) playMove(*move, false);
}

bool Application::canMovePieces() const
{
    return !isGameOver() && !engineIsThinking() && !m_pendingPromotion;
}

// ---------------------------------------------------------------------------
// Game flow

void Application::playMove(chess::Move move, bool byEngine)
{
    m_engine.cancel();
    m_game.play(move);
    m_selected.reset();
    m_dragging = false;
    m_analysis = {};

    if (isGameOver()) return;
    if (!byEngine && m_autoReply) startEngineMove();
    else startAnalysis();
}

void Application::newGame()
{
    m_fenError.clear();
    m_engine.cancel();
    m_engine.clearMemory();
    m_game.reset();
    m_selected.reset();
    m_pendingPromotion.reset();
    m_analysis = {};
    resetClocks();
    startAnalysis();
}

void Application::loadPosition(const std::string& fen)
{
    m_engine.cancel();
    if (!m_game.loadFen(fen)) {
        m_fenError = "Not a valid FEN position.";
        startAnalysis();
        return;
    }
    m_fenError.clear();
    m_fenInput.fill('\0');
    m_engine.clearMemory();
    m_selected.reset();
    m_pendingPromotion.reset();
    m_analysis = {};
    resetClocks();
    startAnalysis();
}

void Application::undoMove()
{
    m_engine.cancel();
    if (!m_game.undo()) return;
    m_selected.reset();
    m_pendingPromotion.reset();
    m_analysis = {};
    startAnalysis();
}

void Application::resetClocks()
{
    const auto seconds = static_cast<float>(kTimeControls[m_timeControl].seconds);
    m_clock = {seconds, seconds};
    m_flagged.reset();
}

bool Application::isTimed() const { return kTimeControls[m_timeControl].seconds > 0; }

void Application::tickClock(float dt)
{
    // Clocks start with the first move, like on most chess sites.
    if (!isTimed() || isGameOver() || m_game.history().empty()) return;

    float& clock = m_clock[m_game.sideToMove() == chess::Color::WHITE ? 0 : 1];
    clock -= dt;
    if (clock <= 0.f) {
        clock = 0.f;
        m_flagged = m_game.sideToMove();
        m_engine.cancel();
    }
}

bool Application::isGameOver() const { return m_flagged.has_value() || m_game.isOver(); }

std::string Application::scoreSheetResult() const
{
    const bool whiteWon = m_flagged ? *m_flagged == chess::Color::BLACK
                                    : m_game.sideToMove() == chess::Color::BLACK;
    if (m_flagged || m_game.status() == core::GameStatus::Checkmate) return whiteWon ? "1-0" : "0-1";
    return "\xC2\xBD-\xC2\xBD";
}

std::string Application::resultText() const
{
    if (m_flagged) return std::string(colorName(~*m_flagged)) + " wins on time";
    const auto status = m_game.status();
    if (status == core::GameStatus::Checkmate)
        return std::string(colorName(~m_game.sideToMove())) + " wins by checkmate";
    return core::statusText(status);
}

// ---------------------------------------------------------------------------
// Engine

void Application::startAnalysis()
{
    if (isGameOver()) return;
    engine::SearchLimits limits;
    limits.timeMs = kAnalysisMs;
    m_engine.start(EngineWorker::Task::Analyse, m_game.board(), limits);
}

void Application::startEngineMove()
{
    if (isGameOver()) return;
    m_selected.reset();
    m_pendingPromotion.reset();
    engine::SearchLimits limits;
    limits.timeMs = m_thinkSeconds * 1000;
    m_engine.start(EngineWorker::Task::Play, m_game.board(), limits);
    m_thinkClock.restart();
}

bool Application::engineIsThinking() const
{
    return m_engine.busy() && m_engine.task() == EngineWorker::Task::Play;
}

void Application::pollEngine()
{
    const auto latest = m_engine.latest();
    if (latest.depth > 0) m_analysis = latest;

    const auto finished = m_engine.takeFinished();
    if (!finished || finished->task != EngineWorker::Task::Play) return;
    if (finished->positionKey != m_game.board().hash()) return;
    if (finished->result.bestMove.move() == chess::Move::NO_MOVE) return;
    playMove(finished->result.bestMove, true);
}

// ---------------------------------------------------------------------------
// Frame

void Application::updateLayout()
{
    const sf::Vector2f size(m_window.getSize());
    const float s = std::clamp(size.y / 800.f, 0.8f, 2.5f);
    Layout& l = m_layout;
    l.scale = s;

    const float panelWidth = std::clamp(size.x * 0.3f, 340.f * s, 420.f * s);
    l.panel = {{size.x - panelWidth, 0.f}, {panelWidth, size.y}};

    const float margin = 28.f * s;
    const float barHeight = 46.f * s;
    const float evalWidth = 30.f * s;
    const float gap = 10.f * s;

    const float maxBoardW = size.x - panelWidth - 2.f * margin - evalWidth - gap;
    const float maxBoardH = size.y - 2.f * margin - 2.f * barHeight;
    // A whole number of pixels per square keeps the grid crisp.
    const float boardSize = std::floor(std::max(160.f, std::min(maxBoardW, maxBoardH)) / 8.f) * 8.f;

    const float blockWidth = evalWidth + gap + boardSize;
    const float left = std::round((size.x - panelWidth - blockWidth) / 2.f);
    const float top = std::round((size.y - boardSize - 2.f * barHeight) / 2.f);

    l.evalBar = {{left, top + barHeight}, {evalWidth, boardSize}};
    l.board = {{left + evalWidth + gap, top + barHeight}, {boardSize, boardSize}};
    l.topBar = {{l.board.position.x, top}, {boardSize, barHeight}};
    l.bottomBar = {{l.board.position.x, top + barHeight + boardSize}, {boardSize, barHeight}};

    m_renderer.setArea(l.board);
}

void Application::loadFonts(float scale)
{
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();

    ImFontConfig config;
    config.OversampleH = 2;
    const auto load = [&](const char* file, float size) {
        const auto path = assetPath(file);
        if (!std::filesystem::exists(path)) return io.Fonts->AddFontDefault();
        return io.Fonts->AddFontFromFileTTF(path.c_str(), std::round(size * scale), &config);
    };
    m_fonts.body = load("Inter-Regular.ttf", 15.f); // first font = ImGui default
    m_fonts.bold = load("Inter-Bold.ttf", 15.f);
    m_fonts.title = load("Inter-Bold.ttf", 21.f);
    m_fonts.clock = load("Inter-Bold.ttf", 20.f);
    m_fonts.score = load("Inter-Bold.ttf", 34.f);

    if (!ImGui::SFML::UpdateFontTexture()) throw std::runtime_error("Failed to build the font atlas");
    applyStyle(scale);
    m_fontScale = scale;
}

void Application::update(float dt)
{
    // Fonts can only be rebuilt between frames.
    if (std::abs(m_layout.scale - m_fontScale) > 0.01f) loadFonts(m_layout.scale);

    ImGui::SFML::Update(m_window, sf::seconds(dt));

    pollEngine();
    tickClock(dt);

    float target = m_evalShare;
    if (isGameOver()) {
        const std::string result = scoreSheetResult();
        target = result == "1-0" ? 1.f : result == "0-1" ? 0.f : 0.5f;
    } else if (m_analysis.depth > 0) {
        target = evalBarShare(m_analysis.score);
    }
    m_evalShare += (target - m_evalShare) * std::min(1.f, dt * 8.f);

    const chess::Color top = m_flipped ? chess::Color::WHITE : chess::Color::BLACK;
    drawPlayerBar("##TopPlayer", m_layout.topBar, top);
    drawPlayerBar("##BottomPlayer", m_layout.bottomBar, ~top);
    drawPanel();
    drawPromotionPicker();
}

BoardScene Application::buildScene() const
{
    BoardScene scene;
    scene.board = &m_game.board();

    if (const auto last = m_game.lastMove()) scene.lastMove = MoveSquares{last->from(), core::targetSquare(*last)};
    if (m_game.inCheck()) scene.checkedKing = m_game.board().kingSq(m_game.sideToMove());

    if (m_selected) {
        scene.selected = m_selected;
        for (const auto& move : m_game.legalMovesFrom(*m_selected)) {
            const auto to = core::targetSquare(move);
            if (std::find(scene.legalTargets.begin(), scene.legalTargets.end(), to) == scene.legalTargets.end())
                scene.legalTargets.push_back(to);
        }
        if (m_dragging) {
            scene.dragFrom = m_selected;
            scene.dragPosition = m_mouse;
        }
    }

    const bool hasSuggestion = m_analysis.depth > 0 && m_analysis.bestMove.move() != chess::Move::NO_MOVE;
    if (m_showArrow && hasSuggestion && !isGameOver() && !m_pendingPromotion)
        scene.arrow = MoveSquares{m_analysis.bestMove.from(), core::targetSquare(m_analysis.bestMove)};
    return scene;
}

void Application::render()
{
    m_window.clear(color::Background);

    m_renderer.setFlipped(m_flipped);
    m_renderer.setTheme(kBoardThemes[m_boardTheme]);
    m_renderer.draw(m_window, buildScene());

    sf::String label;
    if (isGameOver()) {
        std::string result = scoreSheetResult();
        if (result.size() > 3) result = "\xC2\xBD"; // a draw: the full "1/2-1/2" is too wide
        label = sf::String::fromUtf8(result.begin(), result.end());
    } else if (m_analysis.depth > 0) {
        label = formatScore(m_analysis.score, false);
    }
    m_renderer.drawEvalBar(m_window, m_layout.evalBar, m_evalShare, label);

    ImGui::SFML::Render(m_window);
    m_window.display();
}

} // namespace gui
