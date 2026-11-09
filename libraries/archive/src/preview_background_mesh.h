#ifndef THREE_MM_PREVIEW_BACKGROUND_MESH_H
#define THREE_MM_PREVIEW_BACKGROUND_MESH_H
#include <array>
#include <cstdint>
#include <vector>

namespace ThreeMM
{
namespace Internal
{
// Row-vector matrices, with the same layout and projection as BRender.
using BackgroundMatrix = std::array<double, 16>;
class PreviewBackgroundMesh
{
    struct Vertex
    {
        double x, y, z, u, v;
    };
    std::vector<Vertex> vertices;
    std::vector<std::array<std::uint32_t, 3>> triangles;
    std::vector<std::uint8_t> texture;
    unsigned textureWidth = 0, textureHeight = 0;

  public:
    std::array<float, 3> low = {}, high = {};
    // Missing depth stays missing. Coarse cells subdivide at depth edges;
    // triangles crossing a discontinuity are discarded even at pixel scale.
    void Build(unsigned width, unsigned height, const std::vector<std::uint8_t> &colours,
               const std::vector<std::uint16_t> &depths, const BackgroundMatrix &cameraToWorld,
               const BackgroundMatrix &projection);
    bool Empty() const
    {
        return triangles.empty();
    }
    std::size_t TriangleCount() const
    {
        return triangles.size();
    }
    // Seed native INDEX8/DEPTH16 buffers before actors render. All palette
    // indices are opaque, including zero. Geometry is visible from both sides.
    void Render(const BackgroundMatrix &worldToClip, unsigned width, unsigned height, std::uint8_t *colour,
                unsigned colourStride, std::uint8_t *depth, unsigned depthStride) const;
};
} // namespace Internal
} // namespace ThreeMM
#endif
