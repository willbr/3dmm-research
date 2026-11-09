#ifndef MARCUS_CAPTION_QUAD_H
#define MARCUS_CAPTION_QUAD_H
#include "three_mm/preview.h"

// Project a transparent caption texture onto the authored camera's display
// frame. Both faces are visible; projection, clipping and scene depth use the
// inspection camera. All images and transforms are copied/owned host records.
void marcus_caption_quad(const ThreeMM::PreviewImage &texture, const ThreeMM::PreviewProjection &camera,
                         ThreeMM::PreviewImage &scene);
#endif
