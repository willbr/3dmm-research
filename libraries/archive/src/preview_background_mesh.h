#ifndef THREE_MM_PREVIEW_BACKGROUND_MESH_H
#define THREE_MM_PREVIEW_BACKGROUND_MESH_H
#include <array>
#include <cstdint>
#include <memory>
#include <vector>
#include "three_mm/background_mesh.h"
#include "preview_buffer_view.h"

struct br_actor;

namespace ThreeMM
{
namespace Internal
{
// Row-vector matrices, with the same layout and projection as BRender.
using BackgroundMatrix = std::array<double, 16>;
class PreviewBackgroundMesh
{
    friend struct BackgroundMeshProbe;
    ThreeMMBackgroundMesh core;
    PreviewBufferView<std::uint8_t> infiniteColours;
    static ThreeMMBackgroundMeshServices Services();

  public:
    PreviewBackgroundMesh();
    ~PreviewBackgroundMesh();
    PreviewBackgroundMesh(const PreviewBackgroundMesh &) = delete;
    PreviewBackgroundMesh &operator=(const PreviewBackgroundMesh &) = delete;
    std::array<float, 3> low = {}, high = {};
    // Missing depth stays missing. Coarse cells subdivide at depth edges;
    // triangles crossing a discontinuity are discarded even at pixel scale.
    void Build(unsigned width, unsigned height, const std::vector<std::uint8_t> &colours,
               const std::vector<std::uint16_t> &depths, const BackgroundMatrix &cameraToWorld,
               const BackgroundMatrix &projection);
    void Build(unsigned width, unsigned height, const std::uint8_t *colors, std::uint32_t colorCount,
               const std::uint16_t *depths, std::uint32_t depthCount, const BackgroundMatrix &,
               const BackgroundMatrix &);
    bool Empty() const
    {
        return core.triangle_count == 0;
    }
    std::size_t TriangleCount() const
    {
        return core.triangle_count;
    }
    // Screen-fixed backdrop, extended only from pixels with infinite depth.
    // Empty when this camera has no infinite background pixels.
    const PreviewBufferView<std::uint8_t> &InfiniteColours() const
    {
        return infiniteColours;
    }
    // Cached BRender models/material; the frame owns only the attached actors.
    // All scenery goes through the same scene renderer as movie actors.
    bool AddToScene(br_actor *world);
};
} // namespace Internal
} // namespace ThreeMM
#endif
