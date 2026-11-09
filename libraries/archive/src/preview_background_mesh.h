#ifndef THREE_MM_PREVIEW_BACKGROUND_MESH_H
#define THREE_MM_PREVIEW_BACKGROUND_MESH_H
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

struct br_actor;

namespace ThreeMM
{
namespace Internal
{
// Row-vector matrices, with the same layout and projection as BRender.
using BackgroundMatrix = std::array<double, 16>;
class PreviewBackgroundMesh
{
    struct Native;
    std::unique_ptr<Native> native;
    struct Vertex
    {
        double x, y, z, u, v;
    };
    std::vector<Vertex> vertices;
    std::vector<std::array<std::uint32_t, 3>> triangles;
    std::vector<std::uint8_t> texture;
    unsigned textureWidth = 0, textureHeight = 0;

  public:
    PreviewBackgroundMesh();
    ~PreviewBackgroundMesh();
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
    // Cached BRender models/material; the frame owns only the attached actors.
    // All scenery goes through the same scene renderer as movie actors.
    bool AddToScene(br_actor *world);
};
} // namespace Internal
} // namespace ThreeMM
#endif
