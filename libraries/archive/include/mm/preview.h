/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_PREVIEW_H
#define MM_PREVIEW_H
#include "archive.h"
#include <cstddef>

struct MMPreviewLiveDefinitions;
struct MMPreviewLiveCameraEdits;
struct MMPreviewLightValues;
struct MMMovieActorEdits;
struct MMMovieActorPlacement;
struct MMMovieTextJournal;

namespace MM
{
class PreviewRasterizer;
struct PreviewBounds;
using PreviewActorNamePublish = int (*)(void *, std::int32_t actor, const char *utf8);
// Matches the bundled BRender output pixelmap limit.
constexpr std::uint32_t PreviewMaxDimension = 4096;
// Retail Studio's movie workspace; saved 2D text bounds use these coordinates.
constexpr int PreviewCaptionWidth = 544, PreviewCaptionHeight = 306;
struct PreviewAction
{
    std::string name;
    std::uint32_t celCount = 0;
};
struct PreviewCostume
{
    int bodySet = 0, costume = 0;
    bool selected = false, accessory = false;
};
struct PreviewPathPoint { float x, y, z; };
struct PreviewAnimationEdit
{
    enum class Kind { action, record, tweak } kind = Kind::action;
    int action = 0, cel = 0;
    bool freeze = false, replace = true;
    // Path positions are world-axis offsets from the object at the playhead.
    std::vector<PreviewPathPoint> points;
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
    bool renderAtOutputResolution = false; // Rasterize authored geometry at the fitted output size.
    bool freeCamera = false;            // Explicit world position, independent of actor bounds.
    float cameraX = 0, cameraY = 0, cameraZ = 0;
    bool frameSceneCamera = false; // Include the authored camera guide when fitting an orbit view.
    bool stableOrbit = false;      // Keep orbit fit / free-view clip reference until refitting is requested.
    int orbitObject = -1;          // Scene object index to follow; -1 fits the scene.
    int onlyObject = -1;           // Isolated authoring thumbnail; omit scenery and other objects.
    const PreviewBounds *orbitBounds = nullptr; // Optional framing region for this render only.
    bool includeDepth = false;     // Return camera-forward distances for host composition.
    bool includePaletteIndices = false;
    bool fitSceneDepth = false;    // Fit inspection clip distances to visible scene geometry.
    int displayMode = 0;           // Host-selected built-in material/edge mode; zero preserves authored materials.
    bool orthographic = false;
    int axisView = 0;              // 0 free, 1 front, 2 side, 3 top.
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
    bool orthographic = false;
    float orthoHeight = 0;
};
struct PreviewImage
{
    std::uint32_t width = 0, height = 0;
    // Top-down, tightly packed RGBA8. Empty pixels/letterboxing have alpha zero;
    // Authored backgrounds and static infinite backdrops are opaque.
    std::vector<std::uint8_t> rgba;
    // Native raster indices in the same geometry, when requested and available.
    std::vector<std::uint8_t> paletteIndices;
    // Optional top-down camera-forward distances, one per pixel. Empty pixels
    // and letterboxing have positive infinity, as do static infinite backdrops.
    // Requested with includeDepth.
    std::vector<float> depth;
    std::uint32_t coveredPixels = 0;
    int contentRect[4] = {}; // Optional authoring crop, x/y/width/height; zero size means full image.
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
struct PreviewObjectText
{
    std::string text;
    std::int32_t fontSource = 0, shape = 0;
    std::uint32_t font = 0;
};
struct PreviewTextMaterial
{
    bool textured = false;
    std::uint32_t colour = 0;
    unsigned indexBase = 15, indexRange = 15;
    float ambient = .1f, diffuse = .6f, specular = .6f, shininess = 50;
    float mapping[6] = {1, 0, 0, 1, 0, 0};
};
// Flat live-edit definitions; native snapshots can be generated on demand.
struct PreviewSceneDefinition
{
    std::uint32_t number = 0, background = 0;
    std::int32_t source = 2;
};
struct PreviewObjectShape
{
    float rotation[9] = {1,0,0, 0,1,0, 0,0,1};
    float scale[3] = {1,1,1};
};
struct PreviewTrackPose
{
    std::int32_t node = 0, offset = 0, routeFrame = 0;
    std::int32_t position[3] = {}, translation[3] = {}; // Native 16.16 route values.
    float rotation[9] = {};
};
// The legacy name covers inserted actor, prop and generated-text instances.
struct PreviewPropDefinition
{
    std::uint32_t scene = 0, number = 0, templateNumber = 0;
    std::int32_t source = 2, actorId = 0, first = 1, last = 1;
    float point[3] = {};
};
struct PreviewCameraEdit
{
    std::int32_t frame = 0;
    std::uint32_t camera = 0;
};
struct PreviewLight
{
    // World pose uses the work-camera angle convention; intensity is brightness [0,1].
    float x = 0, y = 0, z = 0, yaw = 0, pitch = 0, intensity = 1;
    bool point = false;
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
    std::uint32_t chunk = 0;
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
    bool OpenCore(Archive &&, const std::string &, Error &, bool, bool);
    struct LightInput
    {
        const MMPreviewLightValues *values;
        std::size_t count;
    };
    bool RenderView(const PreviewView &, PreviewImage &, Error &, int object, const float bounds[24], bool crop,
                    const LightInput *lights = nullptr);
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
    // Consume an already indexed movie archive instead of scanning it again.
    bool Open(Archive &&archive, const std::string &resourcesPath, Error &error, bool preserveFraming = false);
    // Register an external source archive. Source IDs are preserved from TAGs;
    // 0 refers to the opened document and positive IDs identify content packs.
    bool AddContent(std::int32_t sourceId, const std::string &archivePath, Error &error);
    bool OpenEmpty(const std::string &resourcesPath, Error &error);
    void ClearScene();
    // Reconcile added objects while retaining bodies and templates already loaded.
    bool SetDefinitions(const std::vector<PreviewSceneDefinition> &, const std::vector<PreviewPropDefinition> &, Error &);
    bool SetCameraEdits(const std::vector<PreviewCameraEdit> &, Error &);
    bool SetLiveDefinitions(const MMPreviewLiveDefinitions &, Error &);
    bool ReplayActorEdits(const MMMovieActorEdits &, void *, PreviewActorNamePublish, Error &);
    bool ReplayActorNames(const MMMovieTextJournal &, void *, PreviewActorNamePublish, Error &);
    bool ReplayActorPlacements(const MMMovieActorPlacement *, std::size_t count, Error &);
    bool SetLiveCameraEdits(const MMPreviewLiveCameraEdits &, Error &);
    // Replace scene scenery without changing actor tracks or frame events.
    bool SetBackground(std::int32_t source, std::uint32_t background, Error &);
    // Temporary lighting for an authoring preview; restores the scene after rendering.
    bool RenderWithLights(const PreviewView &, const std::vector<PreviewLight> &, PreviewImage &, Error &);
    // Borrows the light values for the render. A zero count disables the authored lights.
    bool RenderWithLightValues(const PreviewView &, const MMPreviewLightValues *, std::size_t count, PreviewImage &, Error &);
    void Close();
    // BMDL models, TMPL actors/props/text, ACTR instances, SCEN scenes and
    // BKGD backgrounds. Failed selection clears the old asset.
    // Playback can preserve the inspection view across scene changes.
    bool Select(Id asset, Error &error, bool preserveFraming = false);
    const PreviewInfo &Info() const;
    std::vector<PreviewObject> Objects() const;
    bool ObjectAt(int index, PreviewObject &) const;
    bool ObjectText(int object, PreviewObjectText &, Error &) const;
    bool ObjectCostumes(int object, std::vector<PreviewCostume> &, Error &) const;
    bool ObjectActions(int object, std::vector<PreviewAction> &, Error &) const;
    bool WriteObjectAnimation(int object, const PreviewAnimationEdit &, const std::string &path,
                              int &lastActorFrame, int &lastEditedFrame, Error &);
    // Write a private edited event track, leaving the live actor unchanged.
    bool WriteObjectCostumes(int object, const std::vector<PreviewCostume> &, const std::string &path, Error &);
    bool SetObjectEventTrack(int object, std::int32_t source, Error &);
    bool RenderObjectCostumes(int object, const std::vector<PreviewCostume> &, const PreviewView &, PreviewImage &, Error &, int bodySet = -1);
    // An empty clipBounds is filled with the action's framing for reuse on later cels.
    bool RenderObjectAction(int object, int action, int cel, const PreviewView &, PreviewImage &, Error &,
                            PreviewBounds *clipBounds = nullptr);
    // Change only this instance's text template, retaining its authored track.
    bool SetObjectTextTemplate(int object, std::int32_t source, std::uint32_t number, Error &);
    bool TextMaterial(int object, bool preset, std::int32_t source, std::uint32_t number, PreviewTextMaterial &, Error &);
    bool WriteTextMaterial(int object, bool preset, std::int32_t source, std::uint32_t number,
        const PreviewTextMaterial &, const std::string &path, Error &);
    bool SetObjectTextMaterial(int object, std::int32_t source, std::uint32_t number, Error &);
    bool PrepareTextPreview(const std::string &path, std::int32_t fontSource, std::uint32_t font, Error &);
    bool UseTextPreviewMaterial(Error &);
    // Current posed bounds in actor-local axes, transformed with the actor.
    // An off-stage actor succeeds with invalid bounds.
    // Temporary, silent whole-track translation of an actor, prop or 3D text.
    bool TranslateObject(int object, const float delta[3], Error &error);
    // Idempotent placement relative to the loaded actor header, including off-stage objects.
    bool SetObjectOffset(int object, const float offset[3], Error &error);
    bool SetObjectShape(int object, const PreviewObjectShape &, Error &);
    bool ObjectShape(int object, PreviewObjectShape &, float left[9], float right[9], Error &) const;
    bool ObjectTrackPose(int object, PreviewTrackPose &) const;
    bool ObjectBounds(int object, PreviewBounds &bounds, Error &error);
    // Evaluate a private track copy; leaves live frame, pose and sounds unchanged.
    bool ReadAnimationTimeline(int object, PreviewAnimationTimeline &timeline, Error &error);
    bool ReadPresentation(PreviewPresentation &presentation, Error &error);
    bool ReadPresentation(Id scene, PreviewPresentation &presentation, Error &error);
    // Saved scene thumbnail; no scene selection, rendering or playback changes.
    // Missing thumbnails succeed with an empty image.
    bool ReadSceneThumbnail(Id scene, PreviewImage &image, Error &error);
    // Captions, pauses and all scene/actor cues in one read, without seeking.
    bool ReadTimingPresentation(Id scene, PreviewPresentation &presentation, Error &error);
    // Authored scene cues and actor sound events, without seeking or playing.
    // Motion sounds mark the binding event, not every subsequent cel match.
    bool ReadSoundCues(std::vector<PreviewSoundCue> &cues, Error &error);
    // Read another scene's authored sound events without selecting or seeking it.
    bool ReadSoundCues(Id scene, std::vector<PreviewSoundCue> &cues, Error &error);
    std::vector<PreviewSoundCue> FrameSounds() const;
    bool SoundName(const PreviewSoundCue &, std::string &) const;
    bool ReadSound(const PreviewSoundCue &cue, PreviewAudio &audio, Error &error);
    // Resolve stored indexed caption colours against the active movie palette.
    std::uint32_t PresentationColour(std::uint32_t colour) const;
    bool CopyPresentationPalette(unsigned char bgrx[1024]) const;
    // Overlay this background's raw palette entries on the outgoing display palette.
    void OverlayTransitionPalette(unsigned char bgrx[1024]) const;
    bool SetPose(std::uint32_t action, std::uint32_t cel, Error &error);
    bool SetFrame(std::int32_t frame, Error &error);
    // Adjust scene edges in memory. Opening camera cues follow the first frame;
    // actor tracks and other authored event times are retained.
    bool SetSceneRange(std::int32_t first, std::int32_t last, Error &error);
    // Temporary selection leaves the insertion sequence untouched; restore the
    // original camera after rendering a thumbnail.
    bool SetCamera(std::uint32_t camera, Error &error, bool temporary = false);
    // Read an authored background camera without selecting it, rendering, or
    // changing frame/pose state. Metadata is cached for the loaded background.
    bool CameraProjection(std::uint32_t camera, PreviewProjection &projection, Error &error);
    // Default authored object placement for the current background camera.
    bool PlacementPoint(float point[3]) const;
    // Consume default/appended insertion points in retail order. Failed insertions
    // and completed manual moves can release the last point for reuse.
    bool NextPlacementPoint(float point[3]);
    void ReusePlacementPoint();
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
    // Authored views must use the same renderAtOutputResolution flag as Render.
    bool Pick(const PreviewProjection &projection, bool authoredCamera, std::uint32_t width, std::uint32_t height,
              int x, int y, int &object, float &distance, Error &error, bool renderAtOutputResolution = false);
    // Dimensions 64..PreviewMaxDimension, finite camera values, pitch -90..90, zoom .1..10.
    // Free camera coordinates are limited to +/-32700 world units and take
    // precedence over authoredCamera. Returned images include the rendered pose.
    // Output cleared on failure. Camera orbit uses world Y and the supplied roll.
    bool Render(const PreviewView &view, PreviewImage &image, Error &error);
};
} // namespace MM
#endif
