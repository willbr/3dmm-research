#ifndef MARCUS_CAMERA_GUIDE_H
#define MARCUS_CAMERA_GUIDE_H
#include "player.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MARCUS_GUIDE_MAX_LINES 24
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
/* Map guide geometry to the full image, including authored-camera letterboxing. */
void camera_guide_build_image(const MarcusImage *image, const MarcusProjection *camera, MarcusCameraGuide *guide);
/* Project all twelve selection-box edges into the rendered image, preserving
 * authored letterboxing. The box uses the actor's current world-space corners. */
void object_bounds_build(const MarcusImage *image, const MarcusBounds *bounds, MarcusCameraGuide *guide);
/* Column-major view/projection matrices matching the engine camera exactly. */
int camera_projection_matrices(const MarcusProjection *projection, float view[16], float clip[16]);
#ifdef __cplusplus
}
#endif
#endif
