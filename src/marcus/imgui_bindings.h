#ifndef MARCUS_IMGUI_BINDINGS_H
#define MARCUS_IMGUI_BINDINGS_H
#include "transform_gizmo.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Borrow the current cimgui context and the supplied SDL handles. */
int marcus_imgui_sdl_init(SDL_Window *, SDL_Renderer *);
void marcus_imgui_sdl_shutdown(void);
void marcus_imgui_sdl_event(const SDL_Event *);
void marcus_imgui_sdl_new_frame(void);
void marcus_imgui_sdl_draw(void *draw_data, SDL_Renderer *);
void marcus_gizmo_new_frame(void);
void marcus_gizmo_set_id(int id);
MarcusGizmoResult marcus_gizmo_apply(void *draw_list, const float view[16], const float projection[16],
                                    const SDL_FRect *rect, int orthographic, int mode, float matrix[16],
                                    float size, float min_axis, float min_plane, float shift_x, float shift_y,
                                    const float *snap);

#ifdef __cplusplus
}
#endif
#endif
