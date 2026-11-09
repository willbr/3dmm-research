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
    bool freeCamera = false;            // Explicit world position, independent of actor bounds.
    float cameraX = 0, cameraY = 0, cameraZ = 0;
    bool frameSceneCamera = false; // Include the authored camera guide when fitting an orbit view.
    bool stableOrbit = false;      // Keep orbit fit / free-view clip reference until selection or ResetOrbitFraming().
};
struct PreviewCamera
{
    float x = 0, y = 0, z = 0, yaw = 0, pitch = 0;
    float moveSpeed = 1; // Suggested world units/second based on the visible geometry.
};
struct PreviewProjection
{
    bool valid = false;
    float transform[12] = {}; // Camera-to-world: right, up, backward, position rows.
    float verticalFov = 0, aspect = 0, nearClip = 0, farClip = 0;
    float displayDepth = 0; // Visible guide extent; actual clip distances are retained above.
};
struct PreviewImage
{
    std::uint32_t width = 0, height = 0;
    // Top-down, tightly packed RGBA8. Empty pixels/letterboxing have alpha zero;
    // authored backgrounds are opaque.
    std::vector<std::uint8_t> rgba;
    std::uint32_t coveredPixels = 0;
    PreviewCamera camera; // Actual rendered pose; useful when entering first-person navigation.
    PreviewProjection projection, sceneCamera;
};
// Evaluated values for an actor instance in the selected scene. Position is in
// the original movie's world coordinates; inactive actors have no pose.
struct PreviewObject
{
    Id asset, templateAsset;
    std::int32_t actorId = 0, sourceId = 0;
    std::int32_t firstFrame = 0, lastFrame = 0, action = 0, cel = 0;
    bool prop = false, text = false, visible = false, hasLifetime = false;
    float x = 0, y = 0, z = 0;
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
    std::vector<PreviewObject> Objects() const;
    bool SetPose(std::uint32_t action, std::uint32_t cel, Error &error);
    bool SetFrame(std::int32_t frame, Error &error);
    bool SetCamera(std::uint32_t camera, Error &error);
    void ResetOrbitFraming();
    // Dimensions 64..1024, finite camera values, pitch -85..85, zoom .1..10.
    // Free camera coordinates are limited to +/-32700 world units and take
    // precedence over authoredCamera. Returned images include the rendered pose.
    // Output cleared on failure. Camera orbit preserves upright world Y.
    bool Render(const PreviewView &view, PreviewImage &image, Error &error);
};
} // namespace ThreeMM
#endif
