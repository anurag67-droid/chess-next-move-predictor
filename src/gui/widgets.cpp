#include "gui/widgets.hpp"

#include "gui/theme.hpp"

namespace gui {

void applyStyle(float scale)
{
    ImGuiStyle style;
    style.WindowPadding = {20.f, 20.f};
    style.FramePadding = {10.f, 7.f};
    style.ItemSpacing = {8.f, 8.f};
    style.ItemInnerSpacing = {8.f, 6.f};
    style.CellPadding = {6.f, 4.f};
    style.ScrollbarSize = 10.f;
    style.GrabMinSize = 12.f;
    style.WindowRounding = 0.f;
    style.ChildRounding = 10.f;
    style.FrameRounding = 7.f;
    style.PopupRounding = 8.f;
    style.GrabRounding = 6.f;
    style.ScrollbarRounding = 6.f;
    style.WindowBorderSize = 0.f;
    style.ChildBorderSize = 1.f;
    style.FrameBorderSize = 0.f;
    style.ScaleAllSizes(scale);

    ImVec4* c = style.Colors;
    c[ImGuiCol_Text] = toImVec4(color::Text);
    c[ImGuiCol_TextDisabled] = toImVec4(color::TextMuted);
    c[ImGuiCol_WindowBg] = toImVec4(color::Panel);
    c[ImGuiCol_ChildBg] = toImVec4(color::Card);
    c[ImGuiCol_PopupBg] = toImVec4(color::Card);
    c[ImGuiCol_Border] = toImVec4(color::Border);
    c[ImGuiCol_FrameBg] = toImVec4(color::CardHover);
    c[ImGuiCol_FrameBgHovered] = toImVec4(color::Border);
    c[ImGuiCol_FrameBgActive] = toImVec4(color::Border);
    c[ImGuiCol_Button] = toImVec4(color::CardHover);
    c[ImGuiCol_ButtonHovered] = toImVec4(color::Border);
    c[ImGuiCol_ButtonActive] = toImVec4(color::Card);
    c[ImGuiCol_Header] = toImVec4(color::CardHover);
    c[ImGuiCol_HeaderHovered] = toImVec4(color::Border);
    c[ImGuiCol_HeaderActive] = toImVec4(color::Border);
    c[ImGuiCol_CheckMark] = toImVec4(color::Accent);
    c[ImGuiCol_SliderGrab] = toImVec4(color::Accent);
    c[ImGuiCol_SliderGrabActive] = toImVec4(color::AccentHover);
    c[ImGuiCol_PlotHistogram] = toImVec4(color::Accent);
    c[ImGuiCol_Separator] = toImVec4(color::Border);
    c[ImGuiCol_ScrollbarBg] = toImVec4(color::Card, 0.f);
    c[ImGuiCol_ScrollbarGrab] = toImVec4(color::Border);
    c[ImGuiCol_ScrollbarGrabHovered] = toImVec4(color::TextMuted);
    c[ImGuiCol_ScrollbarGrabActive] = toImVec4(color::TextMuted);
    c[ImGuiCol_TableRowBg] = toImVec4(color::Card, 0.f);
    c[ImGuiCol_TableRowBgAlt] = toImVec4(color::CardHover, 0.6f);
    ImGui::GetStyle() = style;
}

void cardTitle(ImFont* font, const char* text)
{
    ImGui::PushFont(font);
    ImGui::PushStyleColor(ImGuiCol_Text, toImVec4(color::TextMuted));
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

void beginCard(const char* id, float height)
{
    const ImGuiChildFlags flags = ImGuiChildFlags_Borders | (height > 0.f ? 0 : ImGuiChildFlags_AutoResizeY);
    ImGui::BeginChild(id, {0.f, height}, flags, ImGuiWindowFlags_NoScrollbar);
}

void endCard()
{
    ImGui::EndChild();
    ImGui::Spacing();
}

bool accentButton(const char* label, ImVec2 size)
{
    ImGui::PushStyleColor(ImGuiCol_Button, toImVec4(color::Accent));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, toImVec4(color::AccentHover));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, toImVec4(color::AccentActive));
    ImGui::PushStyleColor(ImGuiCol_Text, toImVec4(sf::Color::White));
    const bool clicked = ImGui::Button(label, size);
    ImGui::PopStyleColor(4);
    return clicked;
}

bool toggle(const char* label, bool* value)
{
    const float frame = ImGui::GetFrameHeight();
    const float trackHeight = frame * 0.6f;
    const float trackWidth = trackHeight * 1.75f;
    const float spacing = ImGui::GetStyle().ItemInnerSpacing.x * 1.5f;
    const ImVec2 labelSize = ImGui::CalcTextSize(label);

    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(label, {trackWidth + spacing + labelSize.x, frame});
    if (clicked) *value = !*value;

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 trackMin(pos.x, pos.y + (frame - trackHeight) / 2.f);
    const ImVec2 trackMax(trackMin.x + trackWidth, trackMin.y + trackHeight);
    const sf::Color track = *value ? (ImGui::IsItemHovered() ? color::AccentHover : color::Accent)
                                   : (ImGui::IsItemHovered() ? color::TextMuted : color::Border);
    draw->AddRectFilled(trackMin, trackMax, toImU32(track), trackHeight / 2.f);
    const float radius = trackHeight / 2.f - 2.f;
    const float knobX = *value ? trackMax.x - trackHeight / 2.f : trackMin.x + trackHeight / 2.f;
    draw->AddCircleFilled({knobX, trackMin.y + trackHeight / 2.f}, radius, toImU32(color::Text));
    draw->AddText({trackMax.x + spacing, pos.y + (frame - labelSize.y) / 2.f}, toImU32(color::Text), label);
    return clicked;
}

void rowLabel(const char* label)
{
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", label);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.42f);
    ImGui::SetNextItemWidth(-1.f);
}

} // namespace gui
