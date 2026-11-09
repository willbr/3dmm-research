#ifndef THREE_MM_PREVIEW_RASTERIZER_H
#define THREE_MM_PREVIEW_RASTERIZER_H
#include "archive.h"
struct BrRasterTriangle;
namespace ThreeMM
{
// Optional rasterization service. BRender still evaluates, lights and clips
// geometry. Buffers contain indexed colour and unsigned 16-bit projected depth.
// Use on the Preview's owning thread. Triangle must not throw across C frames.
class PreviewRasterizer
{
  public:
    virtual ~PreviewRasterizer() = default;
    virtual bool Begin(unsigned width, unsigned height, Error &error) = 0;
    virtual void Triangle(const BrRasterTriangle &triangle) noexcept = 0;
    virtual bool End(unsigned char *colour, int colourStride, unsigned char *depth, int depthStride, Error &error) = 0;
};
} // namespace ThreeMM
#endif
