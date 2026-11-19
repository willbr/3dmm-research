#ifndef MARCUS_PROP_GIZMO_H
#define MARCUS_PROP_GIZMO_H
#include "player.h"
#include <SDL3/SDL.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MarcusPropGizmo MarcusPropGizmo;
MarcusPropGizmo *prop_gizmo_create(SDL_Window *window, SDL_Renderer *renderer);
void prop_gizmo_destroy(MarcusPropGizmo *gizmo);
/* Feed every event before ordinary Marcus input. Returns nonzero when consumed. */
int prop_gizmo_event(MarcusPropGizmo *gizmo, const SDL_Event *event);
void prop_gizmo_begin(MarcusPropGizmo *gizmo, MarcusPlayer *player, int object, int enabled, int width, int height);
void prop_gizmo_view(MarcusPropGizmo *gizmo, const MarcusImage *image, const SDL_FRect *viewport);
/* C89 widgets receive a snapshot of the transform and its numeric limits. */
typedef struct MarcusPropFields { int enabled, mode, dragging_gizmo; float value[3], low, high; } MarcusPropFields;
void prop_gizmo_field_state(MarcusPropGizmo *gizmo, MarcusPropFields *state);
void prop_gizmo_field_edit(MarcusPropGizmo *gizmo, int axis, float value, int begin, int finish);
int prop_gizmo_properties(MarcusPropGizmo *gizmo);
/* Apply input before rendering the scene: 1 changed, 2 committed, -1 error. */
int prop_gizmo_update(MarcusPropGizmo *gizmo);
int prop_gizmo_active(const MarcusPropGizmo *gizmo);
int prop_gizmo_mode(const MarcusPropGizmo *gizmo);
void prop_gizmo_set_mode(MarcusPropGizmo *gizmo, int mode);
/* Finish ImGui and render its queued overlay after the native scene/UI. */
void prop_gizmo_end(MarcusPropGizmo *gizmo);
const char *prop_gizmo_error(const MarcusPropGizmo *gizmo);
#ifdef __cplusplus
}
#endif
#endif
