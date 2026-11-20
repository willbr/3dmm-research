/* Scene ownership and lifecycle converted from engine/scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_SCENE_OWNER_H
#define MM_ENGINE_SCENE_OWNER_H
#include "scene_track.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum
    {
        MM_SCENE_FRAMES,
        MM_SCENE_STARTS,
        MM_SCENE_BACKGROUND,
        MM_SCENE_THUMBNAIL,
        MM_SCENE_BACKGROUND_SOUND,
        MM_SCENE_BACKGROUND_CACHE,
        MM_SCENE_OWNERS,
        MM_SCENE_DEFAULT_DISABLED = 2
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
    typedef struct MMSceneOwner
    {
        void *movie; /* Borrowed; no reference cycle. */
        void *owned[MM_SCENE_OWNERS];
        MMSceneTrack tracks[2]; /* Live frame and start tables. */
        MMSceneMemberList members[MM_SCENE_MEMBER_LISTS];
    } MMSceneOwner;
    typedef struct MMSceneSoundServices
    {
        void *context;
        MMI32 (*count)(void *, void *), (*style)(void *, void *);
        int (*open)(void *, void *, MMI32 index);
        void (*close)(void *, void *, MMI32 index), (*free)(void *, void *);
        void (*diagnostic)(void *, int issue);
    } MMSceneSoundServices;
    typedef struct MMSceneOwnerServices
    {
        void *context;
        size_t sound_tag_bytes;
        void *(*create_scene)(void *, void *movie);
        MMSceneOwner *(*state)(void *, void *scene);
        void (*publish)(void *, void *scene, const MMSceneOwner *);
        void (*reset_cursor)(void *, void *scene);
        int (*can_notify)(void *);
        void (*loaded)(void *, void *scene);
        void (*release)(void *, void *object);
        void (*actor_visibility)(void *, void *actor, int show), (*lights_off)(void *, void *background);
        int (*text_lifetime)(void *, void *text, MMI32 *first);
        int (*text_seek)(void *, void *text, MMI32 frame);
        void (*diagnostic)(void *, int issue);
        MMSceneSoundServices sound;
    } MMSceneOwnerServices;
    /* Unique owning state; callbacks cannot reenter lifecycle operations or
     * replace the same scene's owners. Native fields are published borrowed
     * views. Set transfers a new reference into a vacant slot; drop releases
     * just that owner, not embedded event references. Publish follows release,
     * retaining legacy visibility during destruction callbacks. Dispose/unwind
     * support partial construction; close/visibility require initialized lists. */
    void mm_scene_owner_init(MMSceneOwner *, void *movie, const MMStorageAllocator *);
    void mm_scene_owner_set(MMSceneOwner *, void *scene, int slot, void *,
                                  const MMSceneOwnerServices *);
    void mm_scene_owner_drop(MMSceneOwner *, void *scene, int slot, const MMSceneOwnerServices *);
    MMSceneTrack *mm_scene_owner_open_events(MMSceneOwner *, void *scene, int slot,
                                            const MMSceneOwnerServices *);
    void *mm_scene_owner_new(void *movie, const MMSceneOwnerServices *);
    void mm_scene_owner_dispose(MMSceneOwner *, void *scene, const MMSceneOwnerServices *);
    /* Closes a sound row's native tag records before deleting it. The caller updates its cursor. */
    int mm_scene_owner_erase_frame(MMSceneOwner *, MMI32 index, const MMSceneOwnerServices *);
    /* Read-failure cleanup walks only completed prefixes, in reverse order.
     * Valid sound payload size is variable bytes, excluding fixed event bytes. */
    void mm_scene_owner_unwind(MMSceneOwner *, void *scene, MMI32 starts, MMI32 frames,
                                     const MMSceneOwnerServices *);
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
     * Sound allocations must match their header count and native tag stride. */
    void mm_scene_sound_release(void **sound, const MMSceneSoundServices *);
#ifdef __cplusplus
}
#endif
#endif
