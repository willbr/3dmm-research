/* Background resources converted from engine/bkgd.cpp and preview_background.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_BACKGROUND_RESOURCE_H
#define MM_ENGINE_BACKGROUND_RESOURCE_H
#include "background_camera.h"
#include "collection_storage.h"
#include "preview_light.h"
#include "tag.h"
#include "string_value.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum
    {
        MM_BACKGROUND = 0x424b4744,
        MM_BACKGROUND_SOUND = 0x42445320,
        MM_BACKGROUND_COLORS = 0x474c4352,
        MM_BACKGROUND_LIGHTS = 0x474c4c54
    };
    enum
    {
        MM_BACKGROUND_STUDIO,
        MM_BACKGROUND_PREVIEW
    };
    enum
    {
        MM_BACKGROUND_READ_ERROR,
        MM_BACKGROUND_CAMERA_GAPS
    };
    typedef struct MMBackgroundHeader
    {
        unsigned system, palette_base;
    } MMBackgroundHeader;
    typedef struct MMBackgroundSound
    {
        unsigned system;
        MMI32 volume;
        int loop;
        MMTag tag;
    } MMBackgroundSound;
    typedef struct MMBackgroundLight
    {
        MMActorMatrix matrix;
        MMI32 intensity, type;
    } MMBackgroundLight;
    enum
    {
        MM_BACKGROUND_LIGHT_POINT = 0, MM_BACKGROUND_LIGHT_DIRECT = 1,
        MM_BACKGROUND_LIGHT_BYTES = 56, MM_BACKGROUND_LIGHT_RIG_LIMIT = 16,
        MM_BACKGROUND_LIGHT_TABLE_BYTES = 12 + MM_BACKGROUND_LIGHT_BYTES * MM_BACKGROUND_LIGHT_RIG_LIMIT
    };
    typedef struct MMBackgroundLightRig
    {
        MMBackgroundLight lights[MM_BACKGROUND_LIGHT_RIG_LIMIT];
        MMU32 count;
    } MMBackgroundLightRig;
    typedef void (*MMBackgroundLightRotation)(unsigned short pitch, unsigned short yaw, MMActorMatrix *);
    int mm_background_light_values_valid(const MMPreviewLightValues *);
    /* Prepare a rig in authored order. Zero brightness omits the light.
     * Invalid values leave the destination untouched. */
    int mm_background_light_rig_prepare(const MMPreviewLightValues *, size_t count, MMBackgroundLightRotation,
                                        MMBackgroundLightRig *);
    /* Emit the GLLT array and return the bytes written, or zero on failure. */
    MMU32 mm_background_light_rig_encode(const MMBackgroundLightRig *, int system, int big,
                                        unsigned char *, size_t capacity);
    typedef struct MMBackgroundResource
    {
        void *actors, *lights;
        MMCollectionStorage palette;
        MMU32 palette_count;
        MMI32 light_count;
        unsigned palette_base;
        int enabled, leave_on;
        MMBackgroundSound sound;
    } MMBackgroundResource;
    /* Chunk status. A successful call supplies a decoded stream to the
     * receiver synchronously and publishes the receiver's result. */
    typedef int (*MMBackgroundPayloadRead)(void *, MMU32 type, MMU32 number, MMReadReceiver, void *, int *result);
    typedef struct MMBackgroundSource
    {
        MMChunkAccess chunks;
        void *context;
        MMBackgroundPayloadRead payload;
    } MMBackgroundSource;
    typedef struct MMBackgroundServices
    {
        void *context;
        int host_big_endian, debug_checks, retain_palette;
        MMI32 actor_bytes, light_bytes;
        int (*name)(void *, MMU32 type, MMU32 number);
        int (*allocate)(void *, MMI32 bytes, void **);
        void (*free_memory)(void *, void **);
        int (*configure_light)(void *, MMBackgroundResource *, MMI32, const MMBackgroundLight *);
        void (*add_light)(void *, MMBackgroundResource *, MMI32);
        void (*enable_light)(void *, MMBackgroundResource *, MMI32);
        void (*disable_light)(void *, MMBackgroundResource *, MMI32);
        void (*remove_light)(void *, MMBackgroundResource *, MMI32);
        void (*palette_color)(void *, MMI32, const unsigned char native_bytes[4]);
        void (*resolve_palette)(void *);
        int (*append_camera)(void *, MMU32 number);
        int (*select_camera)(void *, MMI32 index);
        void (*diagnostic)(void *, int issue);
    } MMBackgroundServices;
    typedef struct MMBackgroundOwner MMBackgroundOwner;
    typedef struct MMBackgroundOwnerHost
    {
        const MMStorageAllocator *allocator;
        const MMBackgroundServices *teardown;
        unsigned name_unit; /* Zero for graph-only owners, otherwise 1 or 2. */
        void (*mark)(MMBackgroundOwner *);
    } MMBackgroundOwnerHost;
    /* The host and its teardown services outlive the owner. Graph-only owners
     * omit the counted name and have no native light arrays to dispose. */
    MMBackgroundOwner *mm_background_owner_create(const MMBackgroundOwnerHost *);
    MMBackgroundResource *mm_background_owner_resource(MMBackgroundOwner *);
    MMBackgroundCameraState *mm_background_owner_camera(MMBackgroundOwner *);
    MMStringValue mm_background_owner_name(MMBackgroundOwner *);
    const MMResourceObject *mm_background_owner_binding(const MMBackgroundOwner *);
    const MMResourceObjectServices *mm_background_owner_services(void);
    MMI32 mm_background_owner_references(const MMBackgroundOwner *);
    void mm_background_owner_retain(MMBackgroundOwner *);
    void mm_background_owner_release(MMBackgroundOwner *);
    void mm_background_owner_lights_off(MMBackgroundOwner *);
    /* Exact 8/28/56-byte records. Wire pointer slots and padding are ignored.
     * Header/sound failure leaves output unchanged. Light input is already bounded. */
    int mm_background_header_read(const MMReadStream *, MMBackgroundHeader *);
    int mm_background_sound_read(const MMReadStream *, MMBackgroundSound *);
    void mm_background_light_decode(const unsigned char bytes[MM_BACKGROUND_LIGHT_BYTES], int big, MMBackgroundLight *);
    void mm_background_resource_init(MMBackgroundResource *);
    /* Services are synchronous, non-reentrant and bound to one file/object.
     * Fresh owners are required. Studio retains partial SDK arrays on failure
     * for factory disposal. Studio tolerates failed decode of a located palette.
     * Preview keeps its stricter palette/light admission, skips sound/name,
     * publishes visual entries in order, then selects camera zero. Preview
     * callers can retain the authored palette for transition playback. Camera
     * numbering starts at zero and stops at the first missing ID. */
    int mm_background_read(MMBackgroundResource *, MMBackgroundCameraState *, MMU32 type,
                           MMU32 number, int profile, const MMBackgroundSource *, const MMBackgroundServices *);
    struct MMMovieSessionGraph;
    int mm_background_read_graph(MMBackgroundResource *, MMBackgroundCameraState *, MMU32 type, MMU32 number,
                                 const struct MMMovieSessionGraph *, const MMBackgroundServices *);
    /* Borrow the bound root. Child payloads and names use the cache archive. */
    int mm_background_read_cached(MMBackgroundResource *, MMBackgroundCameraState *, MMU32 type, MMU32 number,
                                  MMResourceCache *, MMOwnedBlock *root, MMBlockTransform, const MMBackgroundServices *);
    int mm_background_graph_payload(const struct MMMovieSessionGraph *, MMU32 type, MMU32 number,
                                    MMReadReceiver, void *, int *result);
    /* Apply a camera and its exact image links through the same cache. */
    int mm_background_select_cached(MMBackgroundCameraState *, MMResourceCache *, MMU32 background, MMU32 slot,
                                    MMBlockTransform, const MMBackgroundCameraServices *);
    void mm_background_lights_on(MMBackgroundResource *, const MMBackgroundServices *);
    void mm_background_lights_off(MMBackgroundResource *, const MMBackgroundServices *);
    /* Caller must clear leave_on before teardown. Dispose after partial construction.
     * Preview can pass NULL services because it owns no native light arrays. */
    void mm_background_resource_dispose(MMBackgroundResource *, const MMBackgroundServices *);
    /* Borrow the native-order four-byte colours until disposal. */
    const unsigned char *mm_background_palette(const MMBackgroundResource *, MMU32 *count, MMI32 *base);
    int mm_background_overlay_palette(const MMBackgroundResource *, unsigned char *, size_t count);
    typedef struct MMBackgroundFactoryServices
    {
        void *context;
        MMI32 object_bytes, actor_bytes, light_bytes;
        void *(*create)(void *);
        int (*initialize)(void *, void *, MMU32 type, MMU32 number);
        MMI32 (*lights)(void *, void *);
        void (*release)(void *, void *);
        void (*size)(void *, MMI32);
        void (*publish)(void *, void *);
        void (*failure)(void *);
    } MMBackgroundFactoryServices;
    /* Estimate publishes even without materialization. Failure invalidates outputs
     * before releasing the partial object; success publishes object before final size. */
    int mm_background_factory(MMU32 type, MMU32 number, int materialize,
                                    const MMBackgroundFactoryServices *);
    typedef struct MMBackgroundTagServices
    {
        void *context;
        int (*child)(void *, MMU32 child_type, MMTag *);
        int (*cache)(void *, const MMTag *, int children);
    } MMBackgroundTagServices;
    /* Preserve BDS/GLLT/GLCR/CAM-zero order. Cache resolves every child before any
     * cache effect. Like the native controller, this does not close returned child
     * tags: cache references acquired by child services retain that lifetime. */
    int mm_background_tags(MMTagList *, const MMTag *parent);
    int mm_background_cache(const MMTag *, const MMBackgroundTagServices *);
    int mm_background_cache_tag(const MMTag *, const MMTagServices *);
#ifdef __cplusplus
}
#endif
#endif
