#include "imgui_bindings.h"
#include "dialog_widgets.h"

MarcusGizmoResult marcus_gizmo_manipulate(int id, const MarcusProjection *camera, const SDL_FRect *rect,
                                         int mode, float matrix[16], float size, const float *display_shift)
{
    MarcusGizmoResult result = {0, 0, 0};
    float view[16], projection[16], snap[3];
    ImDrawList *draw;
    if (rect->w <= 0 || rect->h <= 0 || !camera_projection_matrices(camera, view, projection)) return result;
    snap[0] = mode == MARCUS_GIZMO_ROTATE ? 15.f : mode == MARCUS_GIZMO_SCALE ? .1f : 1.f;
    snap[1] = snap[2] = snap[0];
    draw = igGetForegroundDrawList_Nil();
    ImDrawList_PushClipRect(draw, dialog_vec(rect->x, rect->y), dialog_vec(rect->x + rect->w, rect->y + rect->h), 1);
    marcus_gizmo_enable(id, 1);
    result = marcus_gizmo_apply(draw, view, projection, rect, camera->orthographic, mode, matrix,
                                size * MARCUS_GIZMO_SIZE_MULTIPLIER, 24.f * MARCUS_GIZMO_SIZE_MULTIPLIER,
                                12.f * MARCUS_GIZMO_SIZE_MULTIPLIER,
                                display_shift ? display_shift[0] * rect->w : 0.f,
                                display_shift ? display_shift[1] * rect->h : 0.f,
                                igGetIO_Nil()->KeyCtrl ? snap : NULL);
    ImDrawList_PopClipRect(draw);
    return result;
}
