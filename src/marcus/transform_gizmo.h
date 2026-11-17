#ifndef MARCUS_TRANSFORM_GIZMO_H
#define MARCUS_TRANSFORM_GIZMO_H
#include "camera_guide.h"
#include "imgui.h"
#include "ImGuizmo.h"
#include <SDL3/SDL.h>

namespace MarcusGizmo
{
constexpr int Object = 1, Light = 2;
struct Result { bool changed = false, active = false, over = false; };
inline void Enable(int id, bool enabled)
{
    ImGuizmo::SetID(id);
    ImGuizmo::Enable(enabled);
}
// Both tools share one ImGui frame and the same controls, sizing and snapping.
inline bool Controls(const char *title, const SDL_FRect &rect, int &mode, int count, bool editing)
{
    ImGui::SetNextWindowPos(ImVec2(rect.x, rect.y));
    ImGui::SetNextWindowSize(ImVec2(rect.w, rect.h));
    ImGui::Begin(title, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNav);
    ImGui::BeginDisabled(editing);
    const char *labels[] = {"Move", "Rotate", "Size"};
    for (int i = 0; i < count; ++i)
    {
        if (i) ImGui::SameLine();
        if (ImGui::Selectable(labels[i], mode == i, 0, ImVec2(56, 24))) mode = i;
    }
    const bool over = ImGui::IsWindowHovered();
    ImGui::EndDisabled(); ImGui::End();
    return over;
}
inline Result Manipulate(int id, const MarcusProjection &camera, const SDL_FRect &rect, int mode, float matrix[16],
                         float size = .1f, const float *displayShift = nullptr)
{
    float view[16], projection[16];
    if (rect.w <= 0 || rect.h <= 0 || !camera_projection_matrices(&camera, view, projection)) return {};
    auto draw = ImGui::GetForegroundDrawList();
    draw->PushClipRect(ImVec2(rect.x, rect.y), ImVec2(rect.x + rect.w, rect.y + rect.h), true);
    Enable(id, true);
    ImGuizmo::SetDrawlist(draw);
    ImGuizmo::SetRect(rect.x, rect.y, rect.w, rect.h);
    ImGuizmo::SetOrthographic(camera.orthographic != 0);
    ImGuizmo::SetGizmoSizeClipSpace(size);
    ImGuizmo::AllowAxisFlip(false);
    ImGuizmo::SetTranslationAxisMinimumSize(24.f);
    ImGuizmo::SetTranslationPlaneMinimumSize(12.f);
    const float step = mode == 1 ? 15.f : mode == 2 ? .1f : 1.f;
    const float snap[3] = {step, step, step};
    const auto operation = mode == 1 ? ImGuizmo::ROTATE : mode == 2 ? ImGuizmo::SCALE : ImGuizmo::TRANSLATE;
    ImGuizmo::SetTranslationDisplayOffset(displayShift ? displayShift[0] * rect.w : 0.f,
        displayShift ? displayShift[1] * rect.h : 0.f);
    Result result;
    result.changed = ImGuizmo::Manipulate(view, projection, operation, mode ? ImGuizmo::LOCAL : ImGuizmo::WORLD,
        matrix, nullptr, ImGui::GetIO().KeyCtrl ? snap : nullptr);
    result.active = ImGuizmo::IsUsing(); result.over = ImGuizmo::IsOver();
    ImGuizmo::SetTranslationDisplayOffset(0.f, 0.f);
    draw->PopClipRect();
    return result;
}
}
#endif
