#ifndef MARCUS_GPU_RENDERER_H
#define MARCUS_GPU_RENDERER_H
#include "three_mm/preview_rasterizer.h"
#include <memory>
// Creates and validates a complete backend before the live renderer is replaced.
std::unique_ptr<ThreeMM::PreviewRasterizer> MarcusCreateGPURenderer(bool compute, ThreeMM::Error &error);
#endif
