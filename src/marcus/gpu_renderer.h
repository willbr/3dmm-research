#ifndef MARCUS_GPU_RENDERER_H
#define MARCUS_GPU_RENDERER_H
#include "mm/preview_rasterizer.h"
#include "player.h"
#include <memory>
// Creates and validates a complete backend before the live renderer is replaced.
std::unique_ptr<MM::PreviewRasterizer> MarcusCreateGPURenderer(bool compute, MM::Error &error);
void MarcusConfigureGPURenderer(MM::PreviewRasterizer *renderer, const MarcusRenderOptions &options);
#endif
