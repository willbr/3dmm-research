#ifndef MARCUS_TRANSFORM_GIZMO_H
#define MARCUS_TRANSFORM_GIZMO_H
#include "camera_guide.h"
#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { MARCUS_GIZMO_OBJECT = 1, MARCUS_GIZMO_LIGHT = 2 };
enum { MARCUS_GIZMO_MOVE, MARCUS_GIZMO_ROTATE, MARCUS_GIZMO_SCALE };
typedef struct MarcusGizmoResult { int changed, active, over; } MarcusGizmoResult;

void marcus_gizmo_enable(int id, int enabled);
void marcus_gizmo_compose(const float position[3], const float angles[3], const float scale[3], float matrix[16]);
void marcus_gizmo_decompose(const float matrix[16], float position[3], float angles[3], float scale[3]);
MarcusGizmoResult marcus_gizmo_manipulate(int id, const MarcusProjection *camera, const SDL_FRect *rect,
                                         int mode, float matrix[16], float size, const float *display_shift);

#ifdef __cplusplus
}
#endif
#endif
