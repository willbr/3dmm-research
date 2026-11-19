#include "imgui_bindings.h"
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include "ImGuizmo.h"

extern "C"
{
int marcus_imgui_sdl_init(SDL_Window *window, SDL_Renderer *renderer)
{
    if (!IMGUI_CHECKVERSION()) return 0;
    if (!ImGui_ImplSDL3_InitForSDLRenderer(window, renderer)) return 0;
    if (ImGui_ImplSDLRenderer3_Init(renderer))
    {
        auto &style = ImGuizmo::GetStyle();
        style = ImGuizmo::Style{};
        style.ScaleAllSizes(MARCUS_GIZMO_SIZE_MULTIPLIER);
        return 1;
    }
    ImGui_ImplSDL3_Shutdown();
    return 0;
}

void marcus_imgui_sdl_shutdown(void)
{
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
}

void marcus_imgui_sdl_event(const SDL_Event *event) { ImGui_ImplSDL3_ProcessEvent(event); }
void marcus_imgui_sdl_new_frame(void)
{
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
}
void marcus_imgui_sdl_draw(void *data, SDL_Renderer *renderer)
{
    ImGui_ImplSDLRenderer3_RenderDrawData(static_cast<ImDrawData *>(data), renderer);
}

void marcus_gizmo_new_frame(void) { ImGuizmo::BeginFrame(); }
void marcus_gizmo_set_id(int id) { ImGuizmo::SetID(id); }
void marcus_gizmo_enable(int id, int enabled)
{
    ImGuizmo::SetID(id);
    ImGuizmo::Enable(enabled != 0);
}

void marcus_gizmo_compose(const float position[3], const float angles[3], const float scale[3], float matrix[16])
{
    ImGuizmo::RecomposeMatrixFromComponents(position, angles, scale, matrix);
}
void marcus_gizmo_decompose(const float matrix[16], float position[3], float angles[3], float scale[3])
{
    ImGuizmo::DecomposeMatrixToComponents(matrix, position, angles, scale);
}

MarcusGizmoResult marcus_gizmo_apply(void *draw_list, const float view[16], const float projection[16],
                                    const SDL_FRect *rect, int orthographic, int mode, float matrix[16],
                                    float size, float min_axis, float min_plane, float shift_x, float shift_y,
                                    const float *snap)
{
    ImGuizmo::SetDrawlist(static_cast<ImDrawList *>(draw_list));
    ImGuizmo::SetRect(rect->x, rect->y, rect->w, rect->h);
    ImGuizmo::SetOrthographic(orthographic != 0);
    ImGuizmo::SetGizmoSizeClipSpace(size);
    ImGuizmo::AllowAxisFlip(false);
    ImGuizmo::SetTranslationAxisMinimumSize(min_axis);
    ImGuizmo::SetTranslationPlaneMinimumSize(min_plane);
    const auto operation = mode == MARCUS_GIZMO_ROTATE ? ImGuizmo::ROTATE :
                           mode == MARCUS_GIZMO_SCALE ? ImGuizmo::SCALE : ImGuizmo::TRANSLATE;
    ImGuizmo::SetTranslationDisplayOffset(shift_x, shift_y);
    const MarcusGizmoResult result = {
        ImGuizmo::Manipulate(view, projection, operation, mode == MARCUS_GIZMO_MOVE ? ImGuizmo::WORLD : ImGuizmo::LOCAL,
                            matrix, nullptr, snap),
        ImGuizmo::IsUsing(), ImGuizmo::IsOver()
    };
    ImGuizmo::SetTranslationDisplayOffset(0.f, 0.f);
    return result;
}
}
