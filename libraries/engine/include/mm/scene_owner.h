/* Scene ownership and lifecycle converted from engine/scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_SCENE_OWNER_H
#define MM_ENGINE_SCENE_OWNER_H
#include "movie_frame.h"
#include "scene_sound.h"
#include "scene_track.h"
#include "tag.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum
    {
        MM_SCENE_FRAMES,
        MM_SCENE_STARTS,
        MM_SCENE_TRACKS
    };
    enum
    {
        MM_SCENE_BACKGROUND,
        MM_SCENE_THUMBNAIL,
        MM_SCENE_BACKGROUND_SOUND,
        MM_SCENE_BACKGROUND_CACHE,
        MM_SCENE_OWNERS
    };
    enum
    {
        MM_SCENE_ACTORS,
        MM_SCENE_TEXTS,
        MM_SCENE_SELECTION,
        MM_SCENE_MEMBER_LISTS
    };
    enum
    {
        MM_SCENE_BAD_START,
        MM_SCENE_BAD_FRAME,
        MM_SCENE_UNKNOWN_EVENT,
        MM_SCENE_BAD_SOUND,
        MM_SCENE_BAD_PAYLOAD,
        MM_SCENE_TEXT_HIDE_FAILED
    };
    typedef struct MMSceneMemberList
    {
        MMCollectionStorage storage;
        MMU32 count;
    } MMSceneMemberList;
    typedef struct MMSceneRuntime
    {
        MMI32 next_event; /* First frame event after the current frame. */
        MMI32 background_frame;
        MMU32 disabled;
        MMMovieLiveFrame publication;
    } MMSceneRuntime;
    typedef struct MMSceneOwner
    {
        void *movie; /* Borrowed; no reference cycle. */
        MMSceneState data;
        MMSceneRuntime runtime;
        void *owned[MM_SCENE_OWNERS];
        MMTag background_tag; /* Borrowed tag of the last successfully applied background. */
        void *selected_actor, *selected_text; /* Borrowed primary selections. */
        MMSceneTrack tracks[MM_SCENE_TRACKS];
        MMSceneMemberList members[MM_SCENE_MEMBER_LISTS];
    } MMSceneOwner;
    typedef struct MMSceneSoundServices
    {
        void *context;
        void (*close)(void *, MMSceneSoundTag *);
        void (*free)(void *, void *);
        void (*diagnostic)(void *, int issue);
    } MMSceneSoundServices;
    typedef struct MMSceneOwnerServices
    {
        void *context;
        void *(*create_scene)(void *, void *movie);
        MMSceneOwner *(*state)(void *, void *scene);
        int (*can_notify)(void *);
        void (*loaded)(void *, void *scene);
        void (*release)(void *, void *object);
        void (*release_background)(void *, void *background);
        void (*release_thumbnail)(void *, void *bitmap);
        void (*release_cache)(void *, void *cache);
        void (*actor_visibility)(void *, void *actor, int show), (*lights_off)(void *, void *background);
        int (*text_lifetime)(void *, void *text, MMI32 *first);
        int (*text_seek)(void *, void *text, MMI32 frame);
        void (*diagnostic)(void *, int issue);
        MMSceneSoundServices sound;
    } MMSceneOwnerServices;
    /* The factory initializes the owner before returning the scene.
     * Assigning a vacant resource slot transfers one reference. Drop retains
     * the slot value until release returns. Lifecycle callbacks must not
     * reenter or change ownership. Both embedded tracks are initialized
     * with the owner and start empty. Initialization accepts 1- or 2-byte title
     * units and starts at frame one with pause effects disabled. */
    int mm_scene_owner_init(MMSceneOwner *, void *movie, unsigned text_unit, const MMStorageAllocator *);
    void mm_scene_owner_drop(MMSceneOwner *, int slot, const MMSceneOwnerServices *);
    void *mm_scene_owner_new(void *movie, const MMSceneOwnerServices *);
    void mm_scene_owner_dispose(MMSceneOwner *, const MMSceneOwnerServices *);
    /* Closes a sound row's native tag records before deleting it. The caller updates its cursor. */
    int mm_scene_owner_erase_frame(MMSceneOwner *, MMI32 index, const MMSceneOwnerServices *);
    /* Read-failure cleanup walks each track's inserted rows in reverse order.
     * Valid sound payload size is variable bytes, excluding fixed event bytes. */
    void mm_scene_owner_unwind(MMSceneOwner *, const MMSceneOwnerServices *);
    void mm_scene_owner_actors(MMSceneOwner *, int show, const MMSceneOwnerServices *);
    void mm_scene_owner_hide_texts(MMSceneOwner *, const MMSceneOwnerServices *);
    void mm_scene_owner_close(void **scene, const MMSceneOwnerServices *);
    /* Rosters borrow objects from the start events. Selection borrows actors
     * from the roster. Append allocates on demand and leaves the list
     * unchanged on failure. Clear keeps storage for the next selection. */
    MMI32 mm_scene_member_count(const MMSceneOwner *, int list);
    void *mm_scene_member_at(const MMSceneOwner *, int list, MMI32 index);
    int mm_scene_member_append(MMSceneOwner *, int list, void *);
    void mm_scene_member_replace(MMSceneOwner *, int list, MMI32 index, void *);
    void mm_scene_member_remove(MMSceneOwner *, int list, MMI32 index);
    void mm_scene_member_clear(MMSceneOwner *, int list);
    /* Owned background/undo SSEs close only opened tags, then free and publish
     * NULL. Frame SSEs are embedded and close all tags without freeing payloads.
     * Sound allocations must contain their header and all declared tag records. */
    void mm_scene_sound_release(void **sound, const MMSceneSoundServices *);
#ifdef __cplusplus
}
#endif
#endif
