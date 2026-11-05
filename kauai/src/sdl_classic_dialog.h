/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
// Private presentation helpers. ImGui continues to own control input/focus.
#ifndef KAUAI_SDL_CLASSIC_DIALOG_H
#define KAUAI_SDL_CLASSIC_DIALOG_H
#include "imgui.h"
namespace KauaiSDL
{
namespace ClassicDialog
{
constexpr ImU32 Face = IM_COL32(192, 192, 192, 255);
constexpr ImU32 Light = IM_COL32(223, 223, 223, 255);
constexpr ImU32 White = IM_COL32(255, 255, 255, 255);
constexpr ImU32 Shadow = IM_COL32(128, 128, 128, 255);
constexpr ImU32 Black = IM_COL32(0, 0, 0, 255);
constexpr ImU32 Navy = IM_COL32(0, 0, 128, 255);
constexpr float Border = 4, CaptionHeight = 18, ContentTop = 26;

inline void Style(float scale)
{
    ImGui::StyleColorsClassic();
    auto &style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(0, 0);
    style.FramePadding = ImVec2(2, 0);
    style.ItemInnerSpacing = ImVec2(4, 4);
    style.WindowRounding = style.ChildRounding = style.FrameRounding = style.PopupRounding = 0;
    style.ScrollbarRounding = style.GrabRounding = style.TabRounding = 0;
    style.WindowBorderSize = style.ChildBorderSize = style.FrameBorderSize = 0;
    style.PopupBorderSize = 1;
    style.ScrollbarSize = 16;
    style.GrabMinSize = 10;
    style.DisabledAlpha = 1;
    auto color = [&](ImGuiCol target, ImU32 value) { style.Colors[target] = ImGui::ColorConvertU32ToFloat4(value); };
    color(ImGuiCol_Text, Black);
    color(ImGuiCol_TextDisabled, Shadow);
    for (auto target :
         {ImGuiCol_WindowBg, ImGuiCol_ChildBg, ImGuiCol_PopupBg, ImGuiCol_Button, ImGuiCol_ButtonHovered,
          ImGuiCol_ButtonActive, ImGuiCol_ScrollbarGrab, ImGuiCol_ScrollbarGrabHovered, ImGuiCol_ScrollbarGrabActive})
        color(target, Face);
    color(ImGuiCol_Border, Shadow);
    color(ImGuiCol_FrameBg, White);
    color(ImGuiCol_FrameBgHovered, White);
    color(ImGuiCol_FrameBgActive, White);
    color(ImGuiCol_CheckMark, Black);
    color(ImGuiCol_ScrollbarBg, Light);
    color(ImGuiCol_Header, IM_COL32(0, 0, 128, 80));
    color(ImGuiCol_HeaderHovered, IM_COL32(0, 0, 128, 50));
    color(ImGuiCol_HeaderActive, IM_COL32(0, 0, 128, 100));
    color(ImGuiCol_TextSelectedBg, IM_COL32(0, 0, 128, 80));
    color(ImGuiCol_NavCursor, IM_COL32(0, 0, 0, 0));
    style.ScaleAllSizes(scale);
    style.FontScaleDpi = scale;
}
inline void Edge(ImDrawList *draw, ImVec2 low, ImVec2 high, ImU32 top, ImU32 bottom, float pixel)
{
    draw->AddRectFilled(low, ImVec2(high.x, low.y + pixel), top);
    draw->AddRectFilled(low, ImVec2(low.x + pixel, high.y), top);
    draw->AddRectFilled(ImVec2(low.x, high.y - pixel), high, bottom);
    draw->AddRectFilled(ImVec2(high.x - pixel, low.y), high, bottom);
}
inline void Bevel(ImDrawList *draw, ImVec2 low, ImVec2 high, bool sunken, float scale)
{
    Edge(draw, low, high, sunken ? Shadow : White, sunken ? White : Black, scale);
    low.x += scale;
    low.y += scale;
    high.x -= scale;
    high.y -= scale;
    Edge(draw, low, high, sunken ? Black : Light, sunken ? Light : Shadow, scale);
}
inline void Focus(ImDrawList *draw, ImVec2 low, ImVec2 high, float scale)
{
    for (float x = low.x; x < high.x; x += 2 * scale)
    {
        draw->AddRectFilled(ImVec2(x, low.y), ImVec2(x + scale, low.y + scale), Black);
        draw->AddRectFilled(ImVec2(x, high.y - scale), ImVec2(x + scale, high.y), Black);
    }
    for (float y = low.y; y < high.y; y += 2 * scale)
    {
        draw->AddRectFilled(ImVec2(low.x, y), ImVec2(low.x + scale, y + scale), Black);
        draw->AddRectFilled(ImVec2(high.x - scale, y), ImVec2(high.x, y + scale), Black);
    }
}
inline void Button(ImDrawList *draw, ImVec2 low, ImVec2 high, bool pressed, bool focused, bool primary, float scale)
{
    draw->AddRectFilled(low, high, Face);
    if (primary)
    {
        Edge(draw, low, high, Black, Black, scale);
        low.x += scale;
        low.y += scale;
        high.x -= scale;
        high.y -= scale;
    }
    Bevel(draw, low, high, pressed, scale);
    if (focused)
        Focus(draw, ImVec2(low.x + 4 * scale, low.y + 4 * scale), ImVec2(high.x - 4 * scale, high.y - 4 * scale),
              scale);
}
inline void Check(ImDrawList *draw, ImVec2 low, bool checked, bool enabled, bool radio, float scale)
{
    const float side = ImGui::GetFrameHeight();
    const ImVec2 high(low.x + side, low.y + side);
    const auto ink = enabled ? Black : Shadow;
    if (radio)
    {
        const ImVec2 center(low.x + side / 2, low.y + side / 2);
        const float radius = side / 2 - scale;
        draw->AddCircleFilled(center, radius, enabled ? White : Face, 16);
        draw->PathArcTo(center, radius, 2.35619449f, 5.49778714f, 8);
        draw->PathStroke(Shadow, 0, scale);
        draw->PathArcTo(center, radius, -0.78539816f, 2.35619449f, 8);
        draw->PathStroke(White, 0, scale);
        if (checked)
            draw->AddCircleFilled(center, 2.5f * scale, ink, 12);
    }
    else
    {
        draw->AddRectFilled(low, high, enabled ? White : Face);
        Bevel(draw, low, high, true, scale);
        if (checked)
        {
            draw->AddLine(ImVec2(low.x + 3 * scale, low.y + side / 2), ImVec2(low.x + 6 * scale, high.y - 4 * scale),
                          ink, 2 * scale);
            draw->AddLine(ImVec2(low.x + 6 * scale, high.y - 4 * scale), ImVec2(high.x - 3 * scale, low.y + 4 * scale),
                          ink, 2 * scale);
        }
    }
}
} // namespace ClassicDialog
} // namespace KauaiSDL
#endif
