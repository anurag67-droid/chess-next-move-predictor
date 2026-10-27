// The ImGui parts of the window: side panel, player bars and promotion picker.
#include "gui/application.hpp"

#include "gui/format.hpp"
#include "gui/widgets.hpp"

#include <imgui-SFML.h>

#include <algorithm>
#include <cstdio>
#include <iterator>

namespace gui {

void Application::drawPanel()
{
    constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
                                      | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::SetNextWindowPos(toImVec2(m_layout.panel.position));
    ImGui::SetNextWindowSize(toImVec2(m_layout.panel.size));
    ImGui::Begin("##Panel", nullptr, kFlags);

    ImGui::PushFont(m_fonts.title);
    ImGui::TextUnformatted("Next Move Predictor");
    ImGui::PopFont();
    ImGui::TextDisabled("Alpha-beta search  \xC2\xB7  PeSTO evaluation");
    ImGui::Dummy({0.f, 6.f * m_layout.scale});

    drawStatusBanner();
    drawEngineCard();
    drawGameCard();
    drawMoveList();

    ImGui::End();
}

void Application::drawStatusBanner()
{
    std::string title, detail;
    sf::Color tint;
    if (isGameOver()) {
        title = resultText();
        detail = "Start a new game or undo the last move.";
        tint = color::Accent;
    } else if (m_game.inCheck()) {
        title = std::string(colorName(m_game.sideToMove())) + " is in check";
        tint = color::Danger;
    } else {
        return;
    }

    const float s = m_layout.scale;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const float lineHeight = ImGui::GetTextLineHeight();
    const float height = 14.f * s * 2.f + lineHeight + (detail.empty() ? 0.f : ImGui::GetTextLineHeightWithSpacing());

    draw->AddRectFilled(start, {start.x + width, start.y + height}, toImU32(tint, 0.16f), 10.f * s);
    draw->AddRectFilled(start, {start.x + 4.f * s, start.y + height}, toImU32(tint), 10.f * s,
                        ImDrawFlags_RoundCornersLeft);

    ImGui::SetCursorScreenPos({start.x + 18.f * s, start.y + 14.f * s});
    ImGui::PushFont(m_fonts.bold);
    ImGui::TextUnformatted(title.c_str());
    ImGui::PopFont();
    if (!detail.empty()) {
        ImGui::SetCursorScreenPos({start.x + 18.f * s, ImGui::GetCursorScreenPos().y});
        ImGui::TextDisabled("%s", detail.c_str());
    }
    ImGui::SetCursorScreenPos({start.x, start.y + height});
    ImGui::Dummy({width, 4.f * s});
}

void Application::drawEngineCard()
{
    const float s = m_layout.scale;
    beginCard("##Engine");
    cardTitle(m_fonts.bold, "ENGINE");

    // Big score on the left, verdict and suggested move stacked on the right.
    const bool hasResult = m_analysis.depth > 0 || isGameOver();
    const std::string score = isGameOver() ? scoreSheetResult()
                            : hasResult    ? formatScore(m_analysis.score, true)
                                           : "...";
    ImGui::PushFont(m_fonts.score);
    const float scoreWidth = std::max(ImGui::CalcTextSize(score.c_str()).x, ImGui::CalcTextSize("+0.00").x);
    const float rowTop = ImGui::GetCursorPosY();
    ImGui::TextColored(toImVec4(hasResult ? color::Text : color::TextMuted), "%s", score.c_str());
    const float rowBottom = ImGui::GetCursorPosY();
    ImGui::PopFont();

    const float textX = ImGui::GetCursorPosX() + scoreWidth + 16.f * s;
    const float textTop = rowTop + (rowBottom - rowTop - 2.f * ImGui::GetTextLineHeightWithSpacing()) / 2.f;
    ImGui::SetCursorPos({textX, textTop});
    if (isGameOver()) ImGui::TextDisabled("Game over");
    else ImGui::TextUnformatted(hasResult ? describeScore(m_analysis.score).c_str() : "Analysing...");
    ImGui::SetCursorPosX(textX);
    if (hasResult && !isGameOver()) {
        ImGui::TextDisabled("Best move");
        ImGui::SameLine();
        ImGui::PushFont(m_fonts.bold);
        ImGui::TextColored(toImVec4(color::Accent), "%s",
                           chess::uci::moveToSan(m_game.board(), m_analysis.bestMove).c_str());
        ImGui::PopFont();
    } else {
        ImGui::NewLine();
    }
    ImGui::SetCursorPosY(rowBottom);

    if (m_analysis.depth > 0) {
        ImGui::TextDisabled("Depth %d  \xC2\xB7  %s nodes  \xC2\xB7  %.1f s%s", m_analysis.depth,
                            formatNodes(m_analysis.nodes).c_str(), m_analysis.elapsedMs / 1000.0,
                            m_engine.busy() ? "  \xC2\xB7  searching" : "");
    } else {
        ImGui::TextDisabled(" ");
    }

    ImGui::Dummy({0.f, 2.f * s});
    ImGui::Separator();
    ImGui::Dummy({0.f, 2.f * s});
    rowLabel("Think time");
    ImGui::SliderInt("##ThinkTime", &m_thinkSeconds, 1, 15, "%d s");
    toggle("Show best-move arrow", &m_showArrow);
    toggle("Engine replies to my moves", &m_autoReply);

    const ImVec2 buttonSize(-1.f, 42.f * s);
    if (engineIsThinking()) {
        // "Move now" doubles as a progress bar for the think time.
        char label[48];
        // "###" keeps the button's ID stable while its text changes.
        std::snprintf(label, sizeof(label), "Thinking (depth %d)  \xC2\xB7  move now###MoveNow", m_analysis.depth);
        if (ImGui::Button(label, buttonSize)) m_engine.finishNow();
        const float progress = std::min(1.f, m_thinkClock.getElapsedTime().asSeconds() / m_thinkSeconds);
        const ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddRectFilled(min, {min.x + (max.x - min.x) * progress, max.y},
                                                  toImU32(color::Accent, 0.35f), ImGui::GetStyle().FrameRounding);
    } else {
        ImGui::BeginDisabled(isGameOver() || m_pendingPromotion.has_value());
        if (accentButton("Play best move", buttonSize)) startEngineMove();
        ImGui::EndDisabled();
    }
    endCard();
}

void Application::drawGameCard()
{
    beginCard("##Game");
    cardTitle(m_fonts.bold, "GAME");

    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const ImVec2 size((ImGui::GetContentRegionAvail().x - 2.f * spacing) / 3.f, 0.f);
    if (ImGui::Button("New game", size)) newGame();
    ImGui::SameLine();
    ImGui::BeginDisabled(m_game.history().empty() || m_flagged.has_value() || engineIsThinking());
    if (ImGui::Button("Undo", size)) undoMove();
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Flip board", size)) m_flipped = !m_flipped;

    rowLabel("Time control");
    if (ImGui::BeginCombo("##TimeControl", kTimeControls[m_timeControl].name)) {
        for (int i = 0; i < static_cast<int>(std::size(kTimeControls)); ++i) {
            if (ImGui::Selectable(kTimeControls[i].name, i == m_timeControl)) {
                m_timeControl = i;
                newGame();
            }
        }
        ImGui::EndCombo();
    }
    rowLabel("Board");
    if (ImGui::BeginCombo("##Board", kBoardThemes[m_boardTheme].name)) {
        for (int i = 0; i < static_cast<int>(kBoardThemes.size()); ++i)
            if (ImGui::Selectable(kBoardThemes[i].name, i == m_boardTheme)) m_boardTheme = i;
        ImGui::EndCombo();
    }

    // Paste a position (e.g. from a real game) to analyse it, or copy the current one.
    const float copyWidth = ImGui::CalcTextSize("Copy FEN").x + 2.f * ImGui::GetStyle().FramePadding.x;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - copyWidth - spacing);
    if (ImGui::InputTextWithHint("##Fen", "Paste a FEN and press Enter", m_fenInput.data(), m_fenInput.size(),
                                 ImGuiInputTextFlags_EnterReturnsTrue))
        loadPosition(m_fenInput.data());
    ImGui::SameLine();
    if (ImGui::Button("Copy FEN")) ImGui::SetClipboardText(m_game.fen().c_str());
    if (!m_fenError.empty()) ImGui::TextColored(toImVec4(color::Danger), "%s", m_fenError.c_str());
    endCard();
}

void Application::drawMoveList()
{
    beginCard("##Moves", std::max(ImGui::GetContentRegionAvail().y, 80.f));
    cardTitle(m_fonts.bold, "MOVES");

    const auto& history = m_game.history();
    if (history.empty()) {
        ImGui::TextDisabled("Drag or click a piece to start.");
        endCard();
        return;
    }

    constexpr ImGuiTableFlags kFlags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg;
    if (ImGui::BeginTable("##MoveTable", 3, kFlags)) {
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("000.").x);
        ImGui::TableSetupColumn("White", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Black", ImGuiTableColumnFlags_WidthStretch);

        // When Black moved first, the first row starts with an empty White cell.
        const std::size_t offset = m_game.blackMovedFirst() ? 1 : 0;
        for (std::size_t cell = 0; cell < history.size() + offset; ++cell) {
            if (cell % 2 == 0) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%zu.", m_game.firstMoveNumber() + cell / 2);
            }
            ImGui::TableNextColumn();
            if (cell < offset) {
                ImGui::TextDisabled("...");
                continue;
            }
            const std::size_t ply = cell - offset;
            const bool isLast = ply + 1 == history.size();
            if (isLast) ImGui::PushFont(m_fonts.bold);
            ImGui::TextUnformatted(history[ply].san.c_str());
            if (isLast) ImGui::PopFont();
        }
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.f) ImGui::SetScrollHereY(1.f);
        ImGui::EndTable();
    }
    endCard();
}

void Application::drawPlayerBar(const char* id, sf::FloatRect rect, chess::Color side)
{
    constexpr ImGuiWindowFlags kFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
                                      | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoBackground
                                      | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::SetNextWindowPos(toImVec2(rect.position));
    ImGui::SetNextWindowSize(toImVec2(rect.size));
    ImGui::Begin(id, nullptr, kFlags);

    const float s = m_layout.scale;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 min = toImVec2(rect.position);
    const float midY = min.y + rect.size.y / 2.f;
    const bool toMove = m_game.sideToMove() == side && !isGameOver();

    // Colour swatch, name, material and "thinking" note.
    const float radius = 9.f * s;
    const ImVec2 swatch(min.x + radius + 2.f * s, midY);
    draw->AddCircleFilled(swatch, radius, toImU32(side == chess::Color::WHITE ? color::EvalWhite : color::EvalBlack));
    draw->AddCircle(swatch, radius, toImU32(toMove ? color::Accent : color::Border), 0, 2.f * s);

    const char* name = colorName(side);
    const int material = m_game.materialBalance(side);
    std::string note;
    if (material > 0) note = "+" + std::to_string(material);
    if (toMove && engineIsThinking()) note += note.empty() ? "thinking..." : "  \xC2\xB7  thinking...";

    ImGui::PushFont(m_fonts.bold);
    const ImVec2 namePos(swatch.x + radius + 10.f * s, midY - ImGui::GetTextLineHeight() / 2.f);
    draw->AddText(namePos, toImU32(color::Text), name);
    const float nameWidth = ImGui::CalcTextSize(name).x;
    ImGui::PopFont();
    draw->AddText(m_fonts.body, m_fonts.body->FontSize, {namePos.x + nameWidth + 10.f * s, namePos.y + 1.f * s},
                  toImU32(color::TextMuted), note.c_str());

    // Clock.
    if (isTimed()) {
        const float seconds = m_clock[side == chess::Color::WHITE ? 0 : 1];
        const bool low = seconds < 20.f;
        const ImVec2 size(110.f * s, rect.size.y - 12.f * s);
        const ImVec2 boxMin(min.x + rect.size.x - size.x, midY - size.y / 2.f);
        const ImVec2 boxMax(boxMin.x + size.x, boxMin.y + size.y);

        sf::Color background = color::Card, text = color::TextMuted;
        if (toMove) {
            background = low ? color::Danger : color::EvalWhite;
            text = low ? sf::Color::White : color::EvalBlack;
        } else if (m_flagged == side) {
            background = color::Danger;
            text = sf::Color::White;
        }
        draw->AddRectFilled(boxMin, boxMax, toImU32(background), 6.f * s);

        const std::string time = formatClock(seconds);
        ImGui::PushFont(m_fonts.clock);
        const ImVec2 textSize = ImGui::CalcTextSize(time.c_str());
        draw->AddText({boxMax.x - textSize.x - 12.f * s, midY - textSize.y / 2.f}, toImU32(text), time.c_str());
        ImGui::PopFont();
    }
    ImGui::End();
}

std::optional<sf::FloatRect> Application::promotionPickerRect() const
{
    if (!m_pendingPromotion) return std::nullopt;
    // A column of four choices growing from the target square towards the board's centre.
    const float square = m_renderer.squareSize();
    sf::Vector2f position = m_renderer.squareTopLeft(m_pendingPromotion->to);
    if (position.y + 4.f * square > m_layout.board.position.y + m_layout.board.size.y + 1.f)
        position.y -= 3.f * square;
    return sf::FloatRect(position, {square, 4.f * square});
}

void Application::drawPromotionPicker()
{
    if (!m_pendingPromotion) return;

    const float s = m_layout.scale;
    const float square = m_renderer.squareSize();
    const sf::FloatRect board = m_layout.board;
    ImGui::GetBackgroundDrawList()->AddRectFilled(toImVec2(board.position), toImVec2(board.position + board.size),
                                                  IM_COL32(0, 0, 0, 120));

    const sf::FloatRect rect = *promotionPickerRect();
    const bool growsUp = rect.position.y < m_renderer.squareTopLeft(m_pendingPromotion->to).y;
    ImGui::SetNextWindowPos(toImVec2(rect.position));
    ImGui::SetNextWindowSize(toImVec2(rect.size));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.f, 0.f});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0.f, 0.f});
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {4.f * s, 4.f * s});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.f * s);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, toImVec4(color::EvalWhite));
    ImGui::PushStyleColor(ImGuiCol_Button, toImVec4(color::EvalWhite));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, toImVec4(color::Accent, 0.45f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, toImVec4(color::Accent, 0.7f));
    ImGui::SetNextWindowFocus();
    ImGui::Begin("##Promotion", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove);

    const chess::Color side = m_game.sideToMove();
    const chess::PieceType choices[] = {chess::PieceType::QUEEN, chess::PieceType::KNIGHT, chess::PieceType::ROOK,
                                        chess::PieceType::BISHOP};
    std::optional<chess::PieceType> chosen;
    for (int i = 0; i < 4; ++i) {
        const auto type = choices[growsUp ? 3 - i : i]; // queen always nearest the target square
        const sf::Sprite sprite(m_renderer.pieceTexture(), m_renderer.pieceRect(chess::Piece(type, side)));
        const float inner = square - 8.f * s;
        ImGui::PushID(i);
        if (ImGui::ImageButton("##Piece", sprite, {inner, inner})) chosen = type;
        ImGui::PopID();
    }
    ImGui::End();
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(4);

    if (chosen) {
        const auto pending = *m_pendingPromotion;
        m_pendingPromotion.reset();
        if (const auto move = m_game.findMove(pending.from, pending.to, *chosen)) playMove(*move, false);
    }
}

} // namespace gui
