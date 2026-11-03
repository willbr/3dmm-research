/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_PREVIEW_H
#define THREE_MM_PREVIEW_H
#include "archive.h"

namespace ThreeMM
{
struct PreviewAction
{
    std::string name;
    std::uint32_t celCount = 0;
};
struct PreviewInfo
{
    Id asset;
    bool prop = false;
    bool scene = false, timeline = false;
    std::int32_t firstFrame = 0, lastFrame = 0, currentFrame = 0;
    std::uint32_t actors = 0;
    std::uint32_t cameraCount = 0, currentCamera = 0;
    std::uint32_t parts = 0, triangles = 0;
    std::vector<PreviewAction> actions;
};
struct PreviewView
{
    std::uint32_t width = 480, height = 360;
    float yaw = 0, pitch = 0, zoom = 1; // Degrees; zoom 1 frames the entire asset.
    bool authoredCamera = true;         // Scenes/backgrounds use their original view and depth.
};
struct PreviewImage
{
    std::uint32_t width = 0, height = 0;
    // Top-down, tightly packed RGBA8. Empty pixels/letterboxing have alpha zero;
    // authored backgrounds are opaque.
    std::vector<std::uint8_t> rgba;
    std::uint32_t coveredPixels = 0;
};
// Read-only engine preview adapter. Owns BRender's process-wide runtime;
// only one open Preview may exist at a time. Use on one thread, separately
// from Studio's renderer. The public boundary contains no native/UI types.
class Preview
{
    struct Impl;
    std::unique_ptr<Impl> _impl;

  public:
    Preview();
    ~Preview();
    Preview(const Preview &) = delete;
    Preview &operator=(const Preview &) = delete;
    // resourcesPath supplies TMAP/0 shading and GLCR/0 palette from 3dmovie.chk.
    // Failed Open retains the previous session.
    bool Open(const std::string &archivePath, const std::string &resourcesPath, Error &error);
    // Register an external source archive. Source IDs are preserved from TAGs;
    // 0 refers to the opened document and positive IDs identify content packs.
    bool AddContent(std::int32_t sourceId, const std::string &archivePath, Error &error);
    void Close();
    // BMDL models, TMPL actors/props/text, ACTR instances, SCEN scenes and
    // BKGD backgrounds. Failed selection clears the old asset.
    bool Select(Id asset, Error &error);
    const PreviewInfo &Info() const;
    bool SetPose(std::uint32_t action, std::uint32_t cel, Error &error);
    bool SetFrame(std::int32_t frame, Error &error);
    bool SetCamera(std::uint32_t camera, Error &error);
    // Dimensions 64..1024, finite camera values, pitch -85..85, zoom .1..10.
    // Output cleared on failure. Camera orbit preserves upright world Y.
    bool Render(const PreviewView &view, PreviewImage &image, Error &error);
};
} // namespace ThreeMM
#endif
