/* Live Preview definitions, actor ownership and timeline control. */
#ifndef MM_ENGINE_PREVIEW_LIVE_H
#define MM_ENGINE_PREVIEW_LIVE_H
#include "collections.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMPreviewLiveScene
    {
        MMU32 number, background;
        MMI32 source;
    } MMPreviewLiveScene;
    typedef struct MMPreviewLiveProp
    {
        MMU32 scene, number, template_number;
        MMI32 source, actor_id, first, last;
        float point[3];
    } MMPreviewLiveProp;
    typedef struct MMPreviewLiveCamera
    {
        MMI32 frame;
        MMU32 camera;
    } MMPreviewLiveCamera;
    typedef struct MMPreviewLiveArray
    {
        MMCollectionStorage storage;
        MMU32 width, count;
    } MMPreviewLiveArray;
    typedef struct MMPreviewLiveStatus
    {
        int ready, scene, timeline, background, prop;
        MMU32 asset_type, asset_number, camera_count, current_camera, reported_actors;
        MMI32 first, last, frame;
    } MMPreviewLiveStatus;
    typedef struct MMPreviewLive
    {
        MMPreviewLiveArray scenes, props, actors, cameras, original_cameras;
        MMPreviewLiveStatus status;
        MMU32 original_actors, initial_camera;
        MMI32 original_first, original_last;
        int virtual_scene;
    } MMPreviewLive;
    typedef struct MMPreviewLiveDefinitions
    {
        void *context;
        MMU32 scene_count, prop_count;
        void (*scene)(void *, MMU32, MMPreviewLiveScene *);
        void (*prop)(void *, MMU32, MMPreviewLiveProp *);
    } MMPreviewLiveDefinitions;
    typedef struct MMPreviewLiveCameraEdits
    {
        void *context;
        MMU32 count;
        void (*get)(void *, MMU32, MMPreviewLiveCamera *);
    } MMPreviewLiveCameraEdits;
    typedef struct MMPreviewLiveServices
    {
        void *context;
        void *(*actor_new)(void *);
        int (*actor_create)(void *, void *, const MMPreviewLiveProp *);
        int (*actor_seek)(void *, void *, MMI32);
        MMU32 (*actor_number)(void *, void *);
        void (*actor_release)(void *, void *);
        int (*camera)(void *, MMU32);
        void (*begin_seek)(void *);
        int (*healthy)(void *);
        void (*publish)(void *, const MMPreviewLive *), (*stats)(void *), (*clear_asset)(void *);
    } MMPreviewLiveServices;
    typedef struct MMPreviewLiveBackgroundServices
    {
        void *context;
        int (*load)(void *, MMU32 *camera_count);
        int (*select)(void *, MMU32 camera);
        void (*commit)(void *);
    } MMPreviewLiveBackgroundServices;
    enum
    {
        MM_PREVIEW_LIVE_OK,
        MM_PREVIEW_LIVE_MEMORY,
        MM_PREVIEW_LIVE_CREATE,
        MM_PREVIEW_LIVE_FRAME,
        MM_PREVIEW_LIVE_SEEK,
        MM_PREVIEW_LIVE_CAMERA,
        MM_PREVIEW_LIVE_RANGE,
        MM_PREVIEW_LIVE_SCENE,
        MM_PREVIEW_LIVE_CAMERA_INDEX,
        MM_PREVIEW_LIVE_EXCEPTION,
        MM_PREVIEW_LIVE_SEEK_MEMORY,
        MM_PREVIEW_LIVE_BACKGROUND
    };
    /* Unique C owner, never copied. Services are synchronous, non-reentrant and
     * never throw; effects return 1, 0, -1 (allocation exception), -2 (other exception).
     * actor_new returns NULL on allocation failure. The owner releases actors in
     * forward order. Adopting transfers ownership only on success. Borrowed actors
     * and scene records remain valid until mutation/clear. Clear retains definitions
     * and storage capacity; dispose releases everything. clear_asset releases the
     * remaining typed asset resources after C live state has already been cleared.
     * publish marshals status; camera publishes its own camera index/error and ready.
     * Input getters are immutable and do not alias owner storage. Snapshot
     * preparation failure leaves old records. Assignment publishes scenes before
     * props; allocation failure retains that completed prefix. Ordinary reconciliation
     * failure restores definitions and retains live actors. Exceptions after definition
     * publication retain new definitions, as before. Created/seek effects and clocks
     * are not rolled back. Matching prop numbers retain the existing added prefix;
     * other definition fields do not edit retained actor instances. */
    int mm_preview_live_init(MMPreviewLive *, const MMStorageAllocator *);
    void mm_preview_live_clear(MMPreviewLive *, const MMPreviewLiveServices *);
    void mm_preview_live_dispose(MMPreviewLive *, const MMPreviewLiveServices *);
    void *mm_preview_live_actor(const MMPreviewLive *, MMU32);
    const MMPreviewLiveScene *mm_preview_live_scene(const MMPreviewLive *, MMU32 number);
    int mm_preview_live_adopt(MMPreviewLive *, void *actor);
    int mm_preview_live_camera_add(MMPreviewLive *, MMI32 frame, MMU32 camera);
    int mm_preview_live_seal(MMPreviewLive *);
    /* Install the camera state of an already loaded background. */
    void mm_preview_live_background_loaded(MMPreviewLive *, MMU32 count, MMU32 camera);
    /* Prepare a new background and select its camera before installing it.
     * Camera indices beyond the replacement count clamp to its last camera.
     * Failure preserves the live background status. The caller owns the
     * uncommitted candidate. Effects have the same status contract as above. */
    int mm_preview_live_replace_background(MMPreviewLive *, const MMPreviewLiveBackgroundServices *);
    int mm_preview_live_sync(MMPreviewLive *, const MMPreviewLiveServices *);
    int mm_preview_live_definitions(MMPreviewLive *, const MMPreviewLiveDefinitions *, const MMPreviewLiveServices *);
    int mm_preview_live_range(MMPreviewLive *, MMI32 first, MMI32 last);
    /* Camera edits replace equal-frame cues in input order; last edit wins. MIN_I32
     * denotes the current first frame. Stable sorting retains remaining equal ties;
     * scratch allocation failure uses an allocation-free stable fallback. */
    int mm_preview_live_cameras(MMPreviewLive *, const MMPreviewLiveCameraEdits *, const MMPreviewLiveServices *);
    /* Seek retains completed actor prefixes after failure. Actor read/allocation
     * failure clears ready; camera errors preserve the camera service's ready state.
     * Frame publication and stats occur only after all effects succeed. */
    int mm_preview_live_seek(MMPreviewLive *, MMI32 frame, const MMPreviewLiveServices *);
#ifdef __cplusplus
}
#endif
#endif
