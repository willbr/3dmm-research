/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_PREVIEW_BACKGROUND_H
#define THREE_MM_PREVIEW_BACKGROUND_H
#include <memory>
#include <vector>
#include "preview_background_mesh.h"
#include "soc_core.h"
namespace ThreeMM
{
namespace Internal
{
// Resolve indexed movie colours using Studio's truecolour display policy.
void ResolveDisplayPalette(Color *palette);
class PreviewBackground
{
    PChunkyResourceFile source = nullptr; // Borrowed from the preview's source set.
    ChunkNumber number = 0;
    std::uint32_t activeCamera = 0;
    struct CachedMesh
    {
        std::uint32_t camera;
        std::unique_ptr<PreviewBackgroundMesh> mesh;
    };
    std::vector<CachedMesh> meshes; // Two most recently inspected cameras.

  public:
    CameraPosition camera = {};
    std::vector<ChunkNumber> cameras;
    std::vector<LightPosition> lights;
    std::vector<byte> colours;
    std::vector<std::uint16_t> depths;
    std::uint32_t width = 0, height = 0;
    Color palette[256] = {};
    bool Load(PChunkyResourceFile file, ChunkNumber background, const Color *basePalette);
    bool SetCamera(std::uint32_t slot);
    PreviewBackgroundMesh &Mesh();
};
} // namespace Internal
} // namespace ThreeMM
#endif
