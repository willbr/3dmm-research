#ifndef MARCUS_CAMERA_GUIDE_H
#define MARCUS_CAMERA_GUIDE_H
#include "player.h"

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

/* Build viewport-clipped lines in normalized image coordinates. Invalid or
 * absent cameras produce an empty guide. The display depth caps the outline,
 * preserving the camera's actual field of view, aspect, orientation and near plane. */
void camera_guide_build(const MarcusProjection *view, const MarcusProjection *camera, MarcusCameraGuide *guide);
#endif
