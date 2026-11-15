/* Preview session replacement, runtime lease and content source ownership. */
#ifndef MM_ENGINE_PREVIEW_SESSION_H
#define MM_ENGINE_PREVIEW_SESSION_H
#include "preview_camera.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMPreviewSessionSource
    {
        MMI32 id;
        void *owner;
    } MMPreviewSessionSource;
    typedef struct MMPreviewSession
    {
        MMCollectionStorage sources;
        MMU32 source_count;
        void *cache;
        int running;
        MMPreviewOrbit orbit;
    } MMPreviewSession;
    typedef struct MMPreviewSessionPalette
    {
        void *handle;
        MMU32 width, count;
        int other_order;
    } MMPreviewSessionPalette;
    typedef struct MMPreviewSessionServices
    {
        void *context;
        void (*begin)(void *);
        int (*healthy)(void *);
        int (*report)(void *, int result);
        int (*input_present)(void *);
        int (*candidate_new)(void *, void **);
        MMPreviewSession *(*state)(void *, void *candidate);
        void (*take_assets)(void *, void *candidate);
        int (*open_resources)(void *, void *candidate, const void *path);
        int (*has_assets)(void *, void *candidate);
        int (*budget)(void *, void *candidate, int resources);
        int (*palette_find)(void *, void *candidate);
        int (*palette_read)(void *, void *candidate, MMPreviewSessionPalette *);
        unsigned char *(*palette_bytes)(void *, void *table);
        unsigned char *(*palette_output)(void *, void *candidate);
        void (*palette_release)(void *, MMPreviewSessionPalette *);
        int (*display_palette)(void *, void *candidate);
        int (*shade_find)(void *, void *candidate), (*shade_load)(void *, void *candidate);
        int (*runtime_begin)(void *), (*depth_begin)(void *);
        int (*cache_new)(void *, void *candidate, void **cache);
        void (*publish)(void *, void **candidate);
        void (*candidate_release)(void *, void *);
        int (*same_path)(void *, void *source, const void *path);
        int (*source_new)(void *, void **);
        int (*source_open)(void *, void *, const void *path), (*source_budget)(void *, void *);
        int (*source_cache_new)(void *, void *);
        void *(*source_cache)(void *, void *);
        int (*contains)(void *, void *cache, MMU32 type, MMU32 number);
        void (*source_release)(void *, void *);
        void (*clear_asset)(void *), (*dispose_live)(void *), (*cache_release)(void *, void *);
        void (*shared_material_release)(void *), (*shade_release)(void *);
        void (*depth_end)(void *), (*runtime_end)(void *);
    } MMPreviewSessionServices;
    enum
    {
        MM_PREVIEW_SESSION_OK,
        MM_PREVIEW_SESSION_EXTERNAL,
        MM_PREVIEW_SESSION_MEMORY,
        MM_PREVIEW_SESSION_EXCEPTION,
        MM_PREVIEW_SESSION_TRANSFER,
        MM_PREVIEW_SESSION_PALETTE_MISSING,
        MM_PREVIEW_SESSION_PALETTE,
        MM_PREVIEW_SESSION_RUNTIME,
        MM_PREVIEW_SESSION_CACHE,
        MM_PREVIEW_SESSION_SHADE,
        MM_PREVIEW_SESSION_CONTENT_ARGUMENT,
        MM_PREVIEW_SESSION_CONTENT_CACHE
    };
    /* Unique owner of source handles and the root cache. Native files/paths/palette
     * representations remain typed providers. All calls are synchronous, confined to
     * one thread and non-reentrant except candidate_release may dispose that distinct
     * session. Fallible services return 1/0, -1 allocation, -2 other exception.
     * report formats errors before candidate disposal; it does nothing for EXTERNAL.
     * Acquired palettes and candidates are released on every failure. publish swaps
     * the candidate with the previous owner, then C releases that previous owner.
     * Runtime ownership transfers only after cache/shade setup. Failed replacements
     * preserve the old session, without rolling back completed global SDK effects.
     * Source IDs/paths append in registration order; duplicate ID/path is a no-op.
     * Root sid zero precedes registered sources; a matching ID without the requested
     * chunk does not stop lookup. Returned caches are borrowed until session disposal.
     * Disposal clears live assets/tables, releases sources forward, root cache, shared
     * material/shade state, depth backend and runtime, in that order. */
    int mm_preview_session_init(MMPreviewSession *, const MMStorageAllocator *);
    void mm_preview_session_dispose(MMPreviewSession *, const MMPreviewSessionServices *);
    int mm_preview_session_open(MMPreviewSession *, const void *resources_path, int require_asset, int preserve_framing,
                                const MMPreviewSessionServices *);
    int mm_preview_session_add(MMPreviewSession *, MMI32 source_id, const void *path, const MMPreviewSessionServices *);
    void *mm_preview_session_resolve(const MMPreviewSession *, MMI32 source_id, MMU32 type, MMU32 number,
                                     const MMPreviewSessionServices *);
#ifdef __cplusplus
}
#endif
#endif
