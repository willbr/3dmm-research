/* Actor sound editing converted from engine actrsnd.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_ACTOR_SOUND_EDIT_H
#define MM_ENGINE_ACTOR_SOUND_EDIT_H
#include "actor_edit.h"
#include "actor_sound.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct MMActorSoundInfo { MMI32 style, volume, no_sound; } MMActorSoundInfo;
typedef struct MMActorSoundEditServices
{
    void *context;
    MMActorSoundServices runtime;
    MMActorEditServices edits;
    void (*write_sound)(void *, MMI32, const MMActorSound *);
    void (*dirty)(void *);
    void *(*query_new)(void *);
    int (*query_add)(void *, void *query, const MMTag *);
    void (*query_release)(void *, void *query);
    int (*resolve_scene)(void *, MMTag *, MMU32 child, MMU32 scene);
    void (*sound_info)(void *, void *resource, MMActorSoundInfo *);
    int (*sound_child)(void *, MMU32 number, MMU32 *child);
    int (*sounds_suppressed)(void *);
    void (*set_sound_frame)(void *);
    void (*midi_error)(void *);
    int (*default_sound)(void *, MMI32 action, MMI32 cel, int *exists, MMTag *);
} MMActorSoundEditServices;
typedef struct MMActorSoundQuery
{
    void *tags; /* Replaced only on success; borrowed tags, owning host table. */
    MMI32 volume;
    int loop;
    void *context;
    void (*publish)(void *, MMI32 volume, int loop);
} MMActorSoundQuery;
/* Valid runtime tables and cursors are required. Mutations are intentionally
 * partial on failure. Queries publish volume/loop after each successful append,
 * even if a later resolve/append fails; no-match leaves these values unchanged.
 * Host table allocations are released on failure or empty success. */
int mm_actor_sound_volume(MMActorState *, MMActorSounds *, void *events, MMI32 style,
                                int motion, MMI32 volume, const MMActorMotionServices *,
                                const MMActorSoundEditServices *);
int mm_actor_sound_query(const MMActorState *, const MMActorSounds *, void *events, MMI32 style,
                               int motion, MMActorSoundQuery *, const MMActorMotionServices *,
                               const MMActorSoundEditServices *);
int mm_actor_sound_delete(MMActorState *, MMActorSounds *, void *events, MMI32 style, int motion,
                                const MMActorMotionServices *, const MMActorSoundEditServices *);
void mm_actor_sound_remove_motion(MMActorState *, void *events, MMI32 action,
                                        const MMActorMotionServices *, const MMActorSoundEditServices *);
int mm_actor_sound_in_frame(const MMActorState *, void *events, MMI32 scene_frame,
                                  const MMActorMotionServices *);
int mm_actor_sound_resolve_all(void *events, MMU32 scene, const MMActorMotionServices *,
                                     const MMActorSoundEditServices *);
int mm_actor_sound_set(MMActorState *, MMActorSounds *, void *events, MMI32 actor,
                             const MMTag *, MMI32 loop, MMI32 queue, int motion,
                             MMI32 volume, MMI32 style, MMU32 external_child,
                             const MMActorMotionServices *, const MMActorSoundEditServices *);
/* Preserves the original default-sound call's positional parameter binding. */
int mm_actor_sound_defaults(MMActorState *, MMActorSounds *, void *events, MMI32 actor,
                                  MMI32 action, MMU32 external_child, const MMActorMotionServices *,
                                  const MMActorSoundEditServices *);
#ifdef __cplusplus
}
#endif
#endif
