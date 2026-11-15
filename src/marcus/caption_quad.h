#ifndef MARCUS_CAPTION_QUAD_H
#define MARCUS_CAPTION_QUAD_H
#include "mm/preview.h"

// Project a transparent caption texture onto the authored camera's display
// frame. Both faces are visible; projection, clipping and scene depth use the
// inspection camera. All images and transforms are copied/owned host records.
void marcus_caption_quad(const MM::PreviewImage &texture, const MM::PreviewProjection &camera,
                         MM::PreviewImage &scene);
#endif
