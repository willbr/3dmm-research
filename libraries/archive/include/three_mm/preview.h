/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_PREVIEW_H
#define THREE_MM_PREVIEW_H
#include "archive.h"

namespace ThreeMM
{
class PreviewRasterizer;
// Matches the bundled BRender output pixelmap limit.
constexpr std::uint32_t PreviewMaxDimension = 4096;
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
    float roll = 0, verticalFov = 45, nearClip = 0; // Zero nearClip uses the normal inspection default.
    bool authoredCamera = true;         // Original image/depth; other views reconstruct scenery with a static backdrop.
    bool freeCamera = false;            // Explicit world position, independent of actor bounds.
    float cameraX = 0, cameraY = 0, cameraZ = 0;
    bool frameSceneCamera = false; // Include the authored camera guide when fitting an orbit view.
    bool stableOrbit = false;      // Keep orbit fit / free-view clip reference until refitting is requested.
    bool includeDepth = false;     // Return camera-forward distances for host composition.
    PreviewRasterizer *rasterizer = nullptr; // Borrowed for this Render call; null uses BRender CPU.
};
struct PreviewCamera
{
    float x = 0, y = 0, z = 0, yaw = 0, pitch = 0;
    float moveSpeed = 1; // Suggested world units/second based on the visible geometry.
    float roll = 0, verticalFov = 45;
};
struct PreviewProjection
{
    bool valid = false;
    float transform[12] = {}; // Camera-to-world: right, up, backward, position rows.
    float verticalFov = 0, aspect = 0, nearClip = 0, farClip = 0;
    float displayDepth = 0; // Camera marker length, independent of the clip distances above.
};
struct PreviewImage
{
    std::uint32_t width = 0, height = 0;
    // Top-down, tightly packed RGBA8. Empty pixels/letterboxing have alpha zero;
    // Authored backgrounds and static infinite backdrops are opaque.
    std::vector<std::uint8_t> rgba;
    // Optional top-down camera-forward distances, one per pixel. Empty pixels
    // and letterboxing have positive infinity, as do static infinite backdrops.
    // Requested with includeDepth.
    std::vector<float> depth;
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
struct PreviewBounds
{
    bool valid = false;
    float corners[24] = {};   // Eight world-space corners; bits 0/1/2 choose max X/Y/Z.
    std::uint32_t colour = 0; // RGB selection colour from the movie palette.
};
enum class PreviewAnimationKind
{
    Entrance,
    Exit,
    Action,
    Rotation,
    Scale,
    Stretch,
    Move,
    Path,
    Costume,
    Step,
    Freeze
};
struct PreviewActionSpan
{
    std::int64_t firstFrame = 0, endFrame = 0; // Half-open, clipped to the scene.
    std::int32_t action = 0;
    bool frozen = false;
    std::string name;
};
struct PreviewAnimationEvent
{
    std::int32_t frame = 0;
    PreviewAnimationKind kind = PreviewAnimationKind::Entrance;
    std::string name, detail;
};
struct PreviewAnimationTimeline
{
    std::vector<PreviewActionSpan> spans; // On-stage intervals, split at action/restart/freeze changes.
    std::vector<PreviewAnimationEvent> events;
};
// Read-only presentation records. Colours retain Kauai's stored abstract RGB,
// palette-index and transparent encodings; character positions are UTF-16 units.
struct PreviewTextProperty
{
    std::uint32_t position = 0, kind = 0, value = 0;
    std::string font;
};
struct PreviewCaption
{
    std::int32_t firstFrame = 0, endFrame = 0;
    std::int32_t left = 0, top = 0, right = 0, bottom = 0, fontSize = 12;
    bool story = true;
    std::uint32_t background = 0xffffffff;
    std::string font;
    std::vector<std::uint16_t> text;
    std::vector<PreviewTextProperty> properties;
};
struct PreviewSoundCue
{
    std::int32_t frame = 0, source = 0, actor = 0, style = 0, volume = 0x10000;
    Id sound;
    Id scene; // Optional origin for imported sound-child remapping across cuts.
    std::uint32_t child = 0;
    bool actorSound = false, loop = false, queue = false, silent = false, motion = false;
    std::int32_t motionCel = -1; // Motion binding cel; -1 for an ordinary cue.
};
struct PreviewPause
{
    std::int32_t frame = 0, kind = 0, milliseconds = 0;
};
struct PreviewPresentation
{
    std::int32_t transition = 0;
    std::vector<PreviewCaption> captions;
    std::vector<PreviewPause> pauses;
    std::vector<PreviewSoundCue> sounds;
};
struct PreviewMidiEvent
{
    std::uint32_t milliseconds = 0, message = 0;
};
struct PreviewAudio
{
    Id asset; // Resolved WAVE/MIDI child identity, after imported-scene remapping.
    std::int32_t source = 0;
    bool midi = false, silent = false;
    std::vector<std::uint8_t> wave;
    std::vector<PreviewMidiEvent> events;
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
    // Playback can preserve the inspection view across scene changes.
    bool Select(Id asset, Error &error, bool preserveFraming = false);
    const PreviewInfo &Info() const;
    std::vector<PreviewObject> Objects() const;
    // Current posed bounds in actor-local axes, transformed with the actor.
    // An off-stage actor succeeds with invalid bounds.
    bool ObjectBounds(int object, PreviewBounds &bounds, Error &error);
    // Evaluate a private track copy; leaves live frame, pose and sounds unchanged.
    bool ReadAnimationTimeline(int object, PreviewAnimationTimeline &timeline, Error &error);
    bool ReadPresentation(PreviewPresentation &presentation, Error &error);
    bool ReadPresentation(Id scene, PreviewPresentation &presentation, Error &error);
    // Authored scene cues and actor sound events, without seeking or playing.
    // Motion sounds mark the binding event, not every subsequent cel match.
    bool ReadSoundCues(std::vector<PreviewSoundCue> &cues, Error &error);
    // Read another scene's authored sound events without selecting or seeking it.
    bool ReadSoundCues(Id scene, std::vector<PreviewSoundCue> &cues, Error &error);
    std::vector<PreviewSoundCue> FrameSounds() const;
    bool ReadSound(const PreviewSoundCue &cue, PreviewAudio &audio, Error &error);
    // Resolve stored indexed caption colours against the active movie palette.
    std::uint32_t PresentationColour(std::uint32_t colour) const;
    bool SetPose(std::uint32_t action, std::uint32_t cel, Error &error);
    bool SetFrame(std::int32_t frame, Error &error);
    bool SetCamera(std::uint32_t camera, Error &error);
    void ResetOrbitFraming();
    // Seed inspection position, orientation, lens and orbit pivot from the shot.
    bool MatchSceneCamera(float outputAspect, PreviewCamera &camera, Error &error);
    // Translate the cached orbit center in its view plane. Offsets are fractions
    // of the viewport width/right and height/up; zoom and angles set the scale.
    bool PanOrbit(const PreviewView &view, float right, float up, Error &error);
    // Pick a visible scene actor at an output-image pixel using the rendered
    // projection and current evaluated frame. Background scenery only occludes.
    // A successful miss returns object = -1; object indices match Objects().
    // Distance is camera-forward depth at the hit, or infinity on a miss.
    bool Pick(const PreviewProjection &projection, bool authoredCamera, std::uint32_t width, std::uint32_t height,
              int x, int y, int &object, float &distance, Error &error);
    // Dimensions 64..PreviewMaxDimension, finite camera values, pitch -90..90, zoom .1..10.
    // Free camera coordinates are limited to +/-32700 world units and take
    // precedence over authoredCamera. Returned images include the rendered pose.
    // Output cleared on failure. Camera orbit uses world Y and the supplied roll.
    bool Render(const PreviewView &view, PreviewImage &image, Error &error);
};
} // namespace ThreeMM
#endif
