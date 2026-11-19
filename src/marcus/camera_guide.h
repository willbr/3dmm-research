#ifndef MARCUS_CAMERA_GUIDE_H
#define MARCUS_CAMERA_GUIDE_H
#include "player.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MARCUS_GUIDE_MAX_LINES 24
#define MARCUS_GROUND_GRID_CELLS 16
#define MARCUS_GROUND_GRID_MAX_LINES (4 * MARCUS_GROUND_GRID_CELLS + 2)
typedef struct MarcusGuideLine
{
    float x1, y1, x2, y2;
} MarcusGuideLine;
typedef struct MarcusCameraGuide
{
    int line_count, has_label;
    float label_x, label_y;
    MarcusGuideLine lines[MARCUS_GUIDE_MAX_LINES];
} MarcusCameraGuide;

/* Build a compact camera marker in normalized image coordinates: one frame,
 * lines to the camera origin, and an upright marker. Display depth sets its
 * length independently of the camera's clipping planes. The original field of
 * view, aspect and orientation are preserved. Lines are clipped to the viewer;
 * invalid or absent cameras produce an empty guide. */
void camera_guide_build(const MarcusProjection *view, const MarcusProjection *camera, MarcusCameraGuide *guide);
/* Project work-view camera markers using the guide depth range and map them
 * to the full image, including authored-camera letterboxing. */
void camera_guide_build_image(const MarcusImage *image, const MarcusProjection *camera, MarcusCameraGuide *guide);
/* Project all twelve selection-box edges into the rendered image, preserving
 * authored letterboxing. The box uses the actor's current world-space corners. */
void object_bounds_build(const MarcusImage *image, const MarcusBounds *bounds, MarcusCameraGuide *guide);
/* Project an XZ grid at world Y=0 using the work guide depth range. */
int camera_ground_grid(const MarcusImage *image, MarcusGuideLine lines[MARCUS_GROUND_GRID_MAX_LINES]);
/* Composite ground lines against the renderer's camera-forward depth, one float per pixel. */
void camera_blend_ground_grid(const MarcusImage *image, unsigned char *rgba, const float *depth);
/* Column-major view/projection matrices matching the engine camera exactly. */
int camera_projection_matrices(const MarcusProjection *projection, float view[16], float clip[16]);
/* Project a visible world point to normalized X/Y and camera-forward depth. */
int camera_project_point(const MarcusProjection *projection, const float world[3], float image[3]);
/* Keep a display-only gizmo anchor inside normalized viewport insets. The
 * authored object position is never changed by this projection helper. */
int camera_gizmo_anchor(const MarcusProjection *, const float world[3], float inset_x, float inset_y, float anchor[3]);
/* Normalized screen offset for the gizmo drawing during a drag. Applying this
 * only to the overlay retains unrestricted world-space movement and snapping. */
int camera_gizmo_display_shift(const MarcusProjection *, const float world[3],
                               float inset_x, float inset_y, float shift[2]);
/* Append a world-space segment, clipped by the same planes as camera guides. */
void camera_guide_add_line(const MarcusProjection *projection, const float a[3], const float b[3], MarcusCameraGuide *guide);
#ifdef __cplusplus
}
#endif
#endif
