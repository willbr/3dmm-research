/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_PREVIEW_BACKGROUND_H
#define THREE_MM_PREVIEW_BACKGROUND_H
#include <memory>
#include <vector>
#include "preview_background_mesh.h"
#include "soc_core.h"
#include "three_mm/background_resource.h"
#include "three_mm/background_preview.h"
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
    ThreeMMBackgroundPreview preview;
    ThreeMMBackgroundPreviewServices Services(void *host);
    void Publish(const ThreeMMBackgroundPreview &);
    ThreeMMBackgroundResource resource = {};
    ThreeMMBackgroundCameraState cameraState = {};

  public:
    CameraPosition camera = {};
    PreviewBufferView<ChunkNumber> cameras;
    PreviewBufferView<ThreeMMBackgroundLight> lights;
    PreviewBufferView<byte> colours;
    PreviewBufferView<std::uint16_t> depths;
    std::uint32_t width = 0, height = 0;
    Color palette[256] = {};
    PreviewBackground();
    ~PreviewBackground();
    PreviewBackground(const PreviewBackground &) = delete;
    PreviewBackground &operator=(const PreviewBackground &) = delete;
    bool Load(PChunkyResourceFile file, ChunkNumber background, const Color *basePalette);
    bool SetCamera(std::uint32_t slot);
    PreviewBackgroundMesh &Mesh();
};
} // namespace Internal
} // namespace ThreeMM
#endif
