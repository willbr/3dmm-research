/* Scene ownership and lifecycle converted from engine/scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_SCENE_OWNER_H
#define THREE_MM_ENGINE_SCENE_OWNER_H
#include "scene_data.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum
    {
        THREE_MM_SCENE_FRAMES,
        THREE_MM_SCENE_STARTS,
        THREE_MM_SCENE_ACTORS,
        THREE_MM_SCENE_TEXTS,
        THREE_MM_SCENE_SELECTION,
        THREE_MM_SCENE_BACKGROUND,
        THREE_MM_SCENE_THUMBNAIL,
        THREE_MM_SCENE_BACKGROUND_SOUND,
        THREE_MM_SCENE_OWNERS,
        THREE_MM_SCENE_DEFAULT_DISABLED = 2
    };
    enum
    {
        THREE_MM_SCENE_BAD_START,
        THREE_MM_SCENE_BAD_FRAME,
        THREE_MM_SCENE_UNKNOWN_EVENT,
        THREE_MM_SCENE_BAD_SOUND,
        THREE_MM_SCENE_BAD_PAYLOAD,
        THREE_MM_SCENE_TEXT_HIDE_FAILED
    };
    typedef struct ThreeMMSceneOwner
    {
        void *movie; /* Borrowed; no reference cycle. */
        void *owned[THREE_MM_SCENE_OWNERS];
    } ThreeMMSceneOwner;
    typedef struct ThreeMMSceneSoundServices
    {
        void *context;
        ThreeMMI32 (*count)(void *, void *), (*style)(void *, void *);
        int (*open)(void *, void *, ThreeMMI32 index);
        void (*close)(void *, void *, ThreeMMI32 index), (*free)(void *, void *);
        void (*diagnostic)(void *, int issue);
    } ThreeMMSceneSoundServices;
    typedef struct ThreeMMSceneOwnerServices
    {
        void *context;
        ThreeMMI32 pointer_bytes, sound_header_bytes, sound_tag_bytes;
        void *(*create_scene)(void *, void *movie), *(*create_slot)(void *, int kind);
        ThreeMMSceneOwner *(*state)(void *, void *scene);
        void (*publish)(void *, void *scene, const ThreeMMSceneOwner *);
        void (*reset_cursor)(void *, void *scene);
        int (*can_notify)(void *);
        void (*loaded)(void *, void *scene);
        void (*release)(void *, void *object);
        ThreeMMI32 (*count)(void *, void *group), (*kind)(void *, void *group, ThreeMMI32 index);
        void *(*data)(void *, void *group, ThreeMMI32 index, ThreeMMI32 *bytes);
        void *(*object)(void *, void *group, ThreeMMI32 index);
        void *(*list_at)(void *, void *list, ThreeMMI32 index);
        void (*actor_visibility)(void *, void *actor, int show), (*lights_off)(void *, void *background);
        int (*text_lifetime)(void *, void *text, ThreeMMI32 *first);
        int (*text_seek)(void *, void *text, ThreeMMI32 frame);
        void (*diagnostic)(void *, int issue);
        ThreeMMSceneSoundServices sound;
    } ThreeMMSceneOwnerServices;
    /* Unique owning state; callbacks cannot reenter lifecycle operations or
     * replace the same scene's owners. Native fields are published borrowed
     * views. Set transfers a new reference into a vacant slot; drop releases
     * just that owner, not embedded event references. Publish follows release,
     * retaining legacy visibility during destruction callbacks. Dispose/unwind
     * support partial construction; close/visibility require initialized lists. */
    void three_mm_scene_owner_init(ThreeMMSceneOwner *, void *movie);
    void three_mm_scene_owner_set(ThreeMMSceneOwner *, void *scene, int slot, void *,
                                  const ThreeMMSceneOwnerServices *);
    void three_mm_scene_owner_drop(ThreeMMSceneOwner *, void *scene, int slot, const ThreeMMSceneOwnerServices *);
    void *three_mm_scene_owner_new(void *movie, const ThreeMMSceneOwnerServices *);
    void three_mm_scene_owner_dispose(ThreeMMSceneOwner *, void *scene, const ThreeMMSceneOwnerServices *);
    /* Read-failure cleanup walks only completed prefixes, in reverse order.
     * Valid sound payload size is variable bytes, excluding fixed event bytes. */
    void three_mm_scene_owner_unwind(ThreeMMSceneOwner *, void *scene, ThreeMMI32 starts, ThreeMMI32 frames,
                                     const ThreeMMSceneOwnerServices *);
    void three_mm_scene_owner_actors(ThreeMMSceneOwner *, int show, const ThreeMMSceneOwnerServices *);
    void three_mm_scene_owner_hide_texts(ThreeMMSceneOwner *, const ThreeMMSceneOwnerServices *);
    void three_mm_scene_owner_close(void **scene, const ThreeMMSceneOwnerServices *);
    /* Owned background/undo SSEs close only opened tags, then free and publish
     * NULL. Frame SSEs are embedded and close all tags without freeing payloads.
     * Sound allocations must match their header count and native tag stride. */
    void three_mm_scene_sound_release(void **sound, const ThreeMMSceneSoundServices *);
#ifdef __cplusplus
}
#endif
#endif
