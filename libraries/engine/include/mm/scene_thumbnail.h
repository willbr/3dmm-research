/* Scene thumbnails/dirty propagation converted from engine/scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_SCENE_THUMBNAIL_H
#define MM_ENGINE_SCENE_THUMBNAIL_H
#include "scene_runtime.h"
#include "scene_selection.h"
#include "masked_bitmap.h"
#include "archive_reader.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum
    {
        MM_SCENE_THUMBNAIL_WIDTH = 144,
        MM_SCENE_THUMBNAIL_HEIGHT = 81,
        MM_SCENE_THUMBNAIL_TRANSPARENT = 250,
        MM_SCENE_THUMBNAIL_TYPE = 0x5448554d
    };
    enum
    {
        MM_SCENE_THUMBNAIL_OK,
        MM_SCENE_THUMBNAIL_READ,
        MM_SCENE_THUMBNAIL_DECODE,
        MM_SCENE_THUMBNAIL_DIMENSIONS,
        MM_SCENE_THUMBNAIL_MEMORY
    };
    typedef struct MMSceneThumbnailPreview
    {
        MMArchiveReader *archive, *resources;
        const unsigned char *palette; /* 256 native BGR0 colours used as a fallback. */
        void *context;
        unsigned char *(*image)(void *, MMU32 width, MMU32 height);
    } MMSceneThumbnailPreview;
    int mm_scene_thumbnail_link(const MMChunkAccess *, MMU32 scene, MMU32 *number);
    /* Find the slot-zero thumbnail of the first movie's slot-zero scene.
     * This only follows links. The picker can preview a movie of any version. */
    int mm_movie_first_scene_thumbnail(const MMChunkAccess *, MMU32 *number);
    /* Missing links succeed without an image. The 1 MiB stored-size and 1024px
     * dimension limits apply to preview. image supplies width*height*4 bytes
     * synchronously or returns null for an allocation failure. RGBA is opaque. */
    int mm_scene_thumbnail_preview(MMU32 scene, const MMSceneThumbnailPreview *);
    /* The browser picks a stored THUM by number and requires a palette from
     * the resource archive. It tries GLCR 0x20001 and then GLCR 0 if the first
     * cannot be read as an array. A missing palette succeeds without an image.
     * The decoded thumbnail and palette limits are 1 MiB and 8 KiB. */
    int mm_scene_thumbnail_poster(MMU32 number, const MMSceneThumbnailPreview *);
    typedef struct MMSceneDirtyServices
    {
        void *context;
        void (*mark)(void *, void *movie);
    } MMSceneDirtyServices;
    void mm_scene_mark_dirty(const MMSceneOwner *, int dirty, const MMSceneDirtyServices *);
    typedef struct MMSceneThumbnailServices
    {
        void *context;
        MMSceneOwner *owner;
        MMSceneOwnerServices owners;
        MMSceneSelectionServices selection;
        MMI32 (*sound_time)(void *);
        void (*mute)(void *), (*resume)(void *, MMI32 time);
        void *(*view)(void *);
        int (*dirty)(void *);
        void (*dimensions)(void *, MMI32 *width, MMI32 *height);
        void *(*new_port)(void *, const MMMaskedRect *);
        void (*palette)(void *, void *port), (*release_port)(void *, void *port);
        int (*seek)(void *, MMI32 frame), (*text_mode)(void *, void *view);
        void (*draw)(void *, void *view, void *port, const MMMaskedRect *);
        void *copy_context;
        void (*copy)(void *, void *source, void *target, const MMMaskedRect *, const MMMaskedRect *);
        void (*end_copy)(void *);
        MMI32 (*row_bytes)(void *, void *port);
        void *(*lock)(void *, void *port);
        void *(*bitmap)(void *, const void *pixels, MMI32 row_bytes, const MMMaskedRect *, int transparent);
        void (*unlock)(void *, void *port);
    } MMSceneThumbnailServices;
    /* Synchronous, non-reentrant callbacks; valid Scene/Movie/current cursor required.
     * Owner slots are authoritative. Ports are indexed eight-bit surfaces. copy/end_copy
     * delimit graphics environments which retain/lock ports across local port release,
     * frame restoration and mask restoration. copy cannot fail as an operation; pixel
     * backend diagnostics/failure prefixes retain native behavior. The selected objects
     * and view are borrowed throughout. Selection services use the same owner;
     * primary-only restoration (including its extra-selection effects) is preserved.
     * Every exit resumes sound at the saved time. Failed initial seek keeps its partial
     * frame change and restores only masks/ports. Successful rendering drops the old
     * thumbnail before allocation and publishes even NULL before unlock. A saved frame
     * restores only inside (first,last]; restoration failure remains nonfatal. The
     * accessor updates before returning the borrowed owned-thumbnail pointer. */
    void mm_scene_update_thumbnail(const MMSceneThumbnailServices *);
    void *mm_scene_thumbnail(const MMSceneThumbnailServices *);
#ifdef __cplusplus
}
#endif
#endif
