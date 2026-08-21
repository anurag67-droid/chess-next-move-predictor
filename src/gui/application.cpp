#include "gui/application.hpp"
#include "engine/search.hpp"
#include <imgui.h>
#include <imgui-SFML.h>
#include <stdexcept>
#include <iostream>
#include <algorithm>
#include <cmath>

namespace gui {

static std::string formatTime(float seconds) {
    if (seconds < 0.f) seconds = 0.f;
    int total = static_cast<int>(seconds);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", total / 60, total % 60);
    return buf;
}

// Pick a colour for the clock digit based on remaining seconds
static ImVec4 clockColor(float t) {
    if (t > 60.f) return {0.90f, 0.90f, 0.90f, 1.f};   // near-white
    if (t > 20.f) return {1.00f, 0.72f, 0.00f, 1.f};   // amber
    return               {0.90f, 0.15f, 0.15f, 1.f};   // danger red
}


void Application::setupImGuiStyle() {
    ImGuiStyle& s = ImGui::GetStyle();

    // Shape
    s.WindowRounding    = 0.f;
    s.ChildRounding     = 6.f;
    s.FrameRounding     = 5.f;
    s.GrabRounding      = 4.f;
    s.ScrollbarRounding = 5.f;
    s.PopupRounding     = 6.f;

    // Spacing / padding
    s.WindowPadding     = ImVec2(14.f, 12.f);
    s.FramePadding      = ImVec2(10.f,  6.f);
    s.ItemSpacing       = ImVec2( 8.f,  8.f);
    s.ItemInnerSpacing  = ImVec2( 6.f,  5.f);
    s.IndentSpacing     = 18.f;
    s.ScrollbarSize     = 8.f;

    // Borders
    s.WindowBorderSize  = 0.f;
    s.FrameBorderSize   = 0.f;
    s.ChildBorderSize   = 1.f;

    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg]             = {0.098f, 0.098f, 0.098f, 1.f};  // #191919
    c[ImGuiCol_ChildBg]              = {0.118f, 0.118f, 0.118f, 1.f};  // #1e1e1e
    c[ImGuiCol_PopupBg]              = {0.118f, 0.118f, 0.118f, 1.f};
    c[ImGuiCol_Border]               = {0.220f, 0.220f, 0.220f, 1.f};
    c[ImGuiCol_Text]                 = {0.920f, 0.920f, 0.920f, 1.f};
    c[ImGuiCol_TextDisabled]         = {0.420f, 0.420f, 0.420f, 1.f};
    c[ImGuiCol_TitleBg]              = {0.060f, 0.060f, 0.060f, 1.f};
    c[ImGuiCol_TitleBgActive]        = {0.060f, 0.060f, 0.060f, 1.f};
    c[ImGuiCol_TitleBgCollapsed]     = {0.060f, 0.060f, 0.060f, 1.f};
    c[ImGuiCol_ScrollbarBg]          = {0.098f, 0.098f, 0.098f, 1.f};
    c[ImGuiCol_ScrollbarGrab]        = {0.280f, 0.280f, 0.280f, 1.f};
    c[ImGuiCol_ScrollbarGrabHovered] = {0.380f, 0.380f, 0.380f, 1.f};
    c[ImGuiCol_ScrollbarGrabActive]  = {0.480f, 0.480f, 0.480f, 1.f};
    c[ImGuiCol_FrameBg]              = {0.160f, 0.160f, 0.160f, 1.f};
    c[ImGuiCol_FrameBgHovered]       = {0.200f, 0.200f, 0.200f, 1.f};
    c[ImGuiCol_FrameBgActive]        = {0.240f, 0.240f, 0.240f, 1.f};
    c[ImGuiCol_Header]               = {0.200f, 0.200f, 0.200f, 1.f};
    c[ImGuiCol_HeaderHovered]        = {0.280f, 0.280f, 0.280f, 1.f};
    c[ImGuiCol_HeaderActive]         = {0.340f, 0.340f, 0.340f, 1.f};
    c[ImGuiCol_Separator]            = {0.200f, 0.200f, 0.200f, 1.f};
    c[ImGuiCol_SeparatorHovered]     = {0.320f, 0.320f, 0.320f, 1.f};
    c[ImGuiCol_SeparatorActive]      = {0.420f, 0.420f, 0.420f, 1.f};
    // chess.com green buttons
    c[ImGuiCol_Button]               = {0.310f, 0.540f, 0.180f, 1.f};
    c[ImGuiCol_ButtonHovered]        = {0.380f, 0.640f, 0.230f, 1.f};
    c[ImGuiCol_ButtonActive]         = {0.230f, 0.430f, 0.120f, 1.f};
    c[ImGuiCol_CheckMark]            = {0.490f, 0.790f, 0.320f, 1.f};
    c[ImGuiCol_SliderGrab]           = {0.490f, 0.790f, 0.320f, 1.f};
    c[ImGuiCol_SliderGrabActive]     = {0.600f, 0.890f, 0.420f, 1.f};
}

// Stored as file-level statics so update() can push/pop them at will.
static ImFont* g_fontBody   = nullptr;  // Inter Regular  16 px
static ImFont* g_fontBold   = nullptr;  // Inter Bold     16 px

Application::Application()
    : m_window(sf::VideoMode({1024, 768}), "C++ Chess Engine", sf::Style::Default),
      m_isDragging(false),
      m_selectedSquare(""),
      m_currentEval(0.0f),
      m_whiteTimeLeft(600.f),
      m_blackTimeLeft(600.f),
      m_isTimeless(false),
      m_gameOver(false),
      m_matedColor(chess::Color::WHITE)
{
    m_window.setFramerateLimit(60);
    if (!ImGui::SFML::Init(m_window))
        throw std::runtime_error("Failed to initialize ImGui-SFML bridge");

    rebakeFonts(18.f);
    setupImGuiStyle();
    m_moveClock.restart();
    updateEvaluation();
}
void Application::rebakeFonts(float px) {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    io.FontGlobalScale = 1.0f;
    ImFontConfig cfg;
    cfg.OversampleH  = 3;
    cfg.OversampleV  = 1;
    cfg.PixelSnapH   = false;

    g_fontBody = io.Fonts->AddFontFromFileTTF("../assets/Inter-Regular.ttf", px, &cfg);
    g_fontBold = io.Fonts->AddFontFromFileTTF("../assets/Inter-Bold.ttf",    px, &cfg);

    if (!g_fontBody) g_fontBody = io.Fonts->AddFontDefault();
    if (!g_fontBold) g_fontBold = io.Fonts->AddFontDefault();

    [[maybe_unused]] bool ok = ImGui::SFML::UpdateFontTexture();
}


Application::~Application() { ImGui::SFML::Shutdown(); }

void Application::run() {
    while (m_window.isOpen()) {
        processEvents();
        sf::Time dt = m_deltaClock.restart();
        update(dt);
        render();
    }
}

void Application::updateEvaluation() {
    m_currentEval = engine::getFutureEvaluation(m_board.getInternalBoard(), 4);
}

void Application::processEvents() {
    while (const auto event = m_window.pollEvent()) {
        ImGui::SFML::ProcessEvent(m_window, *event);

        if (event->is<sf::Event::Closed>()) m_window.close();

        if (const auto* r = event->getIf<sf::Event::Resized>()) {
            m_window.setView(sf::View(sf::FloatRect(
                {0.f, 0.f},
                {static_cast<float>(r->size.x), static_cast<float>(r->size.y)})));
            // Rebake fonts at the native pixel size for this window height.
            float sf_ = static_cast<float>(r->size.y) / 768.f;
            rebakeFonts(18.f * sf_);
            setupImGuiStyle();
        }

        if (const auto* mc = event->getIf<sf::Event::MouseButtonPressed>()) {
            if (mc->button == sf::Mouse::Button::Left) {
                std::string sq = m_boardRenderer.getSquareFromPixel(mc->position.x, mc->position.y);
                if (!sq.empty()) { m_isDragging = true; m_selectedSquare = sq; }
            }
        }

        if (const auto* mm = event->getIf<sf::Event::MouseMoved>())
            m_mouseCoords = mm->position;

        if (const auto* mr = event->getIf<sf::Event::MouseButtonReleased>()) {
            if (mr->button == sf::Mouse::Button::Left && m_isDragging) {
                m_isDragging = false;
                std::string target = m_boardRenderer.getSquareFromPixel(mr->position.x, mr->position.y);
                if (!target.empty() && target != m_selectedSquare && !m_gameOver) {
                    if (m_board.tryMove(m_selectedSquare + target)) {
                        m_moveHistory.push_back(m_selectedSquare + target);
                        m_moveClock.restart();
                        updateEvaluation();
                        if (m_board.isCheckmate()) { m_gameOver = true; m_matedColor = m_board.sideToMove(); }
                    }
                }
                m_selectedSquare = "";
            }
        }
    }
}

void Application::update(sf::Time dt) {
    ImGui::SFML::Update(m_window, dt);

    // Tick the active side's clock
    if (!m_gameOver && !m_isTimeless) {
        bool isWhiteTurn = (m_board.sideToMove() == chess::Color::WHITE);
        float& active = isWhiteTurn ? m_whiteTimeLeft : m_blackTimeLeft;
        active -= dt.asSeconds();
        if (active <= 0.f) { active = 0.f; m_gameOver = true; m_matedColor = m_board.sideToMove(); }
    }

    float winH       = static_cast<float>(m_window.getSize().y);
    float winW       = static_cast<float>(m_window.getSize().x);
    float sf_        = winH / 768.f;           // scaleFactor
    float uiWidth    = 390.f * sf_;
    bool  whiteTurn  = (m_board.sideToMove() == chess::Color::WHITE);

    ImGui::SetNextWindowPos ({winW - uiWidth, 0});
    ImGui::SetNextWindowSize({uiWidth, winH});

    constexpr ImGuiWindowFlags kPanelFlags =
        ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoResize  |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar|
        ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin("##ChessPanel", nullptr, kPanelFlags);
    // Use Inter Regular for the whole panel
    if (g_fontBody) ImGui::PushFont(g_fontBody);
    float cw = ImGui::GetContentRegionAvail().x;   // usable content width
    ImGui::Spacing();
    // Gold title — use Bold for the main heading
    if (g_fontBold) { ImGui::PopFont(); ImGui::PushFont(g_fontBold); }
    ImGui::TextColored({0.784f, 0.651f, 0.267f, 1.f}, "C++ CHESS ENGINE");
    if (g_fontBody) { ImGui::PopFont(); ImGui::PushFont(g_fontBody); }
    // Dim subtitle
    ImGui::TextColored({0.35f, 0.35f, 0.35f, 1.f},
        "%.0f FPS  |  depth-4 eval", ImGui::GetIO().Framerate);

    ImGui::Spacing();
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        dl->AddLine(p, {p.x + cw, p.y}, IM_COL32(120, 100, 40, 180), 1.f);
        ImGui::Dummy({cw, 1.f});
    }
    ImGui::Spacing();
    ImGui::TextColored({0.42f, 0.42f, 0.42f, 1.f}, "CLOCKS");
    ImGui::Spacing();

    float clockH = 50.f * sf_;

    // Macro-like lambda to avoid repeating the clock-box draw code
    auto drawClock = [&](const char* id, const char* label, float timeLeft, bool active) {
        // Child background: subtle green tint when active
        ImGui::PushStyleColor(ImGuiCol_ChildBg,
            active ? ImVec4(0.12f, 0.20f, 0.10f, 1.f)
                   : ImVec4(0.08f, 0.08f, 0.08f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_Border,
            active ? ImVec4(0.31f, 0.54f, 0.18f, 0.90f)
                   : ImVec4(0.16f, 0.16f, 0.16f, 1.f));

        ImGui::BeginChild(id, ImVec2(cw, clockH), true);
        {
            // Label (dimmed)
            ImGui::TextColored(
                active ? ImVec4(0.56f, 0.80f, 0.42f, 1.f)
                       : ImVec4(0.40f, 0.40f, 0.40f, 1.f),
                "%s", label);

            // Time right aligned
            std::string ts = m_isTimeless ? "--:--" : formatTime(timeLeft);
            if (active && !m_isTimeless) ts += " <<";
            float tw = ImGui::CalcTextSize(ts.c_str()).x;
            ImGui::SameLine(ImGui::GetWindowWidth() - tw - 12.f * sf_);
            ImGui::TextColored(clockColor(timeLeft), "%s", ts.c_str());
        }
        ImGui::EndChild();
        ImGui::PopStyleColor(2);
    };

    drawClock("##BlackClock", "BLACK", m_blackTimeLeft, !m_gameOver && !whiteTurn);
    ImGui::Spacing();
    drawClock("##WhiteClock", "WHITE", m_whiteTimeLeft, !m_gameOver &&  whiteTurn);

    ImGui::Spacing();
    ImGui::Checkbox("Timeless Mode", &m_isTimeless);
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextColored({0.42f, 0.42f, 0.42f, 1.f}, "EVALUATION");
    ImGui::Spacing();

    // Visual bar: white (left) vs black (right)
    {
        ImDrawList* dl  = ImGui::GetWindowDrawList();
        ImVec2 p        = ImGui::GetCursorScreenPos();
        float  barH     = 18.f * sf_;
        float  clampEv  = std::max(-10.f, std::min(10.f, m_currentEval));
        float  wRatio   = std::max(0.04f, std::min(0.96f, 0.5f + clampEv / 20.f));
        float  r        = 4.f;
        // Black background (full bar)
        dl->AddRectFilled(p, {p.x + cw, p.y + barH}, IM_COL32(28, 28, 28, 255), r);
        // White portion
        dl->AddRectFilled(p, {p.x + cw * wRatio, p.y + barH}, IM_COL32(228, 222, 205, 255), r);
        // Center tick
        float cx = p.x + cw * 0.5f;
        dl->AddLine({cx, p.y + 2}, {cx, p.y + barH - 2}, IM_COL32(90, 90, 90, 180), 1.5f);
        // Evaluation needle
        float nx = p.x + cw * wRatio;
        dl->AddRectFilled({nx - 2.f, p.y}, {nx + 2.f, p.y + barH}, IM_COL32(200, 170, 80, 220));

        ImGui::Dummy({cw, barH});
    }
    ImGui::Spacing();
    // Evaluation text
    {
        char evBuf[32];
        std::snprintf(evBuf, sizeof(evBuf), "%+.2f", m_currentEval);
        if (m_currentEval >  0.15f)
            ImGui::TextColored({0.86f, 0.86f, 0.82f, 1.f}, "%s  White is better", evBuf);
        else if (m_currentEval < -0.15f)
            ImGui::TextColored({0.52f, 0.52f, 0.52f, 1.f}, "%s  Black is better", evBuf);
        else
            ImGui::TextColored({0.45f, 0.45f, 0.45f, 1.f}, "%s  Equal position",  evBuf);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();


    // Lambda for a coloured status box
    auto drawBadge = [&](const char* id, ImVec4 bg, ImVec4 border,
                         ImVec4 labelCol, const char* labelTxt,
                         ImVec4 bodyCol,  const char* bodyFmt, const char* bodyArg) {
        float badgeH = 40.f * sf_;
        ImGui::PushStyleColor(ImGuiCol_ChildBg, bg);
        ImGui::PushStyleColor(ImGuiCol_Border,  border);
        ImGui::BeginChild(id, ImVec2(cw, badgeH), true);
        ImGui::TextColored(labelCol, "%s", labelTxt);
        ImGui::SameLine();
        char tmp[128];
        std::snprintf(tmp, sizeof(tmp), bodyFmt, bodyArg);
        ImGui::TextColored(bodyCol, "%s", tmp);
        ImGui::EndChild();
        ImGui::PopStyleColor(2);
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
    };

    if (m_gameOver) {
        if (m_board.isCheckmate()) {
            const char* winner = (m_matedColor == chess::Color::WHITE) ? "Black" : "White";
            drawBadge("##CM",
                {0.22f,0.04f,0.04f,1.f}, {0.65f,0.08f,0.08f,1.f},
                {1.f,0.22f,0.22f,1.f},   "CHECKMATE",
                {0.88f,0.88f,0.88f,1.f}, "- %s wins!", winner);
        } else {
            const char* loser = (m_matedColor == chess::Color::WHITE) ? "White" : "Black";
            drawBadge("##TO",
                {0.18f,0.11f,0.02f,1.f}, {0.65f,0.45f,0.08f,1.f},
                {1.f,0.70f,0.00f,1.f},   "TIME'S UP!",
                {0.88f,0.88f,0.88f,1.f}, "%s loses!", loser);
        }
    } else if (m_board.isInCheck()) {
        const char* side = whiteTurn ? "White" : "Black";
        drawBadge("##CHK",
            {0.20f,0.04f,0.04f,1.f}, {0.60f,0.06f,0.06f,1.f},
            {1.f,0.22f,0.22f,1.f},   "CHECK",
            {0.72f,0.72f,0.72f,1.f}, "- %s king is in danger!", side);
    }

    if (!m_gameOver) {
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 7.f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,  ImVec2(12.f, 9.f));
        if (ImGui::Button("Predict Best Next Move  (6 sec)", ImVec2(-1.f, 42.f * sf_))) {
            std::string best = engine::getBestMoveTime(m_board.getInternalBoard(), 6000);
            m_board.tryMove(best);
            m_moveHistory.push_back(best);
            m_moveClock.restart();
            updateEvaluation();
            if (m_board.isCheckmate()) { m_gameOver = true; m_matedColor = m_board.sideToMove(); }
        }
        ImGui::PopStyleVar(2);
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
    }

    ImGui::TextColored({0.42f, 0.42f, 0.42f, 1.f}, "MOVE HISTORY");
    ImGui::Spacing();

    float histH = ImGui::GetContentRegionAvail().y - 6.f;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.08f, 0.08f, 1.f));
    ImGui::BeginChild("##MoveLog", ImVec2(0, histH), true);

    for (size_t i = 0; i < m_moveHistory.size(); i += 2) {
        int moveNum = static_cast<int>(i / 2 + 1);

        // Subtle zebra stripe (every other pair)
        if ((i / 2) % 2 == 0) {
            ImVec2 rMin = ImGui::GetCursorScreenPos();
            ImVec2 rMax = {rMin.x + ImGui::GetContentRegionAvail().x,
                           rMin.y + ImGui::GetTextLineHeightWithSpacing()};
            ImGui::GetWindowDrawList()->AddRectFilled(rMin, rMax, IM_COL32(255, 255, 255, 8));
        }

        // Move number
        ImGui::TextColored({0.36f, 0.36f, 0.36f, 1.f}, "%2d.", moveNum);
        ImGui::SameLine(32.f * sf_);
        // White's move
        ImGui::TextColored({0.86f, 0.86f, 0.84f, 1.f}, "%-7s", m_moveHistory[i].c_str());
        // Black's move (if exists)
        if (i + 1 < m_moveHistory.size()) {
            ImGui::SameLine(88.f * sf_);
            ImGui::TextColored({0.55f, 0.55f, 0.55f, 1.f}, "%s", m_moveHistory[i + 1].c_str());
        }
    }

    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.f);
    ImGui::EndChild();
    ImGui::PopStyleColor();

    // Balance the PushFont at the top of this window
    if (g_fontBody) ImGui::PopFont();
    ImGui::End();
}

void Application::render() {
    m_window.clear(sf::Color(20, 20, 20));
    bool showHighlight = m_board.isInCheck();
    chess::Color checkedSide = m_board.sideToMove();
    m_boardRenderer.render(m_window, m_board.getFen(), showHighlight, checkedSide);

    ImGui::SFML::Render(m_window);
    m_window.display();
}}