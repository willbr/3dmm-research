/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_PREVIEW_BACKGROUND_H
#define MM_PREVIEW_BACKGROUND_H
#include <memory>
#include "preview_background_mesh.h"
#include "soc_core.h"
#include "mm/background_resource.h"
#include "mm/background_preview.h"
#include "mm/preview_camera.h"
namespace MM
{
struct PreviewProjection;
namespace Internal
{
// Resolve indexed movie colours using Studio's truecolour display policy.
void ResolveDisplayPalette(Color *palette);
class PreviewBackground
{
    PChunkyResourceFile source = nullptr; // Borrowed from the preview's source set.
    ChunkNumber number = 0;
    MMBackgroundPreview preview;
    MMBackgroundPreviewServices Services(void *host);
    void Publish(const MMBackgroundPreview &);
    MMBackgroundResource resource = {};
    MMPreviewProjectionCache cameraProjections = {};

  public:
    const MMBackgroundCameraData &Camera() const { return preview.camera.data; }
    PreviewBufferView<ChunkNumber> cameras;
    PreviewBufferView<MMBackgroundLight> lights;
    PreviewBufferView<byte> colours;
    PreviewBufferView<std::uint16_t> depths;
    std::uint32_t width = 0, height = 0;
    Color palette[256] = {};
    PreviewBackground();
    ~PreviewBackground();
    PreviewBackground(const PreviewBackground &) = delete;
    PreviewBackground &operator=(const PreviewBackground &) = delete;
    bool Load(PChunkyResourceFile file, ChunkNumber background, const Color *basePalette);
    void OverlayAuthoredPalette(unsigned char bgrx[1024]) const;
    bool SetCamera(std::uint32_t slot, bool updatePlacement = true);
    bool NextPlacementPoint(float point[3]);
    void ReusePlacementPoint();
    bool CameraProjection(std::uint32_t slot, PreviewProjection &projection);
    PreviewBackgroundMesh &Mesh();
};
} // namespace Internal
} // namespace MM
#endif
