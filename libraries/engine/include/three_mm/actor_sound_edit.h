/* Actor sound editing converted from engine actrsnd.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_ACTOR_SOUND_EDIT_H
#define THREE_MM_ENGINE_ACTOR_SOUND_EDIT_H
#include "actor_edit.h"
#include "actor_sound.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct ThreeMMActorSoundInfo { ThreeMMI32 style, volume, no_sound; } ThreeMMActorSoundInfo;
typedef struct ThreeMMActorSoundEditServices
{
    void *context;
    ThreeMMActorSoundServices runtime;
    ThreeMMActorEditServices edits;
    void (*write_sound)(void *, ThreeMMI32, const ThreeMMActorSound *);
    void (*dirty)(void *);
    void *(*query_new)(void *);
    int (*query_add)(void *, void *query, const ThreeMMTag *);
    void (*query_release)(void *, void *query);
    int (*resolve_scene)(void *, ThreeMMTag *, ThreeMMU32 child, ThreeMMU32 scene);
    void (*sound_info)(void *, void *resource, ThreeMMActorSoundInfo *);
    int (*reserve_events)(void *, ThreeMMU32 entries, ThreeMMU32 bytes);
    int (*sound_child)(void *, ThreeMMU32 number, ThreeMMU32 *child);
    int (*sounds_suppressed)(void *);
    void (*set_sound_frame)(void *);
    void (*midi_error)(void *);
    int (*default_sound)(void *, ThreeMMI32 action, ThreeMMI32 cel, int *exists, ThreeMMTag *);
} ThreeMMActorSoundEditServices;
typedef struct ThreeMMActorSoundQuery
{
    void *tags; /* Replaced only on success; borrowed tags, owning host table. */
    ThreeMMI32 volume;
    int loop;
    void *context;
    void (*publish)(void *, ThreeMMI32 volume, int loop);
} ThreeMMActorSoundQuery;
/* Valid runtime tables and cursors are required. Mutations are intentionally
 * partial on failure. Queries publish volume/loop after each successful append,
 * even if a later resolve/append fails; no-match leaves these values unchanged.
 * Host table allocations are released on failure or empty success. */
int three_mm_actor_sound_volume(ThreeMMActorState *, ThreeMMActorSounds *, void *events, ThreeMMI32 style,
                                int motion, ThreeMMI32 volume, const ThreeMMActorMotionServices *,
                                const ThreeMMActorSoundEditServices *);
int three_mm_actor_sound_query(const ThreeMMActorState *, const ThreeMMActorSounds *, void *events, ThreeMMI32 style,
                               int motion, ThreeMMActorSoundQuery *, const ThreeMMActorMotionServices *,
                               const ThreeMMActorSoundEditServices *);
int three_mm_actor_sound_delete(ThreeMMActorState *, ThreeMMActorSounds *, void *events, ThreeMMI32 style, int motion,
                                const ThreeMMActorMotionServices *, const ThreeMMActorSoundEditServices *);
void three_mm_actor_sound_remove_motion(ThreeMMActorState *, void *events, ThreeMMI32 action,
                                        const ThreeMMActorMotionServices *, const ThreeMMActorSoundEditServices *);
int three_mm_actor_sound_in_frame(const ThreeMMActorState *, void *events, ThreeMMI32 scene_frame,
                                  const ThreeMMActorMotionServices *);
int three_mm_actor_sound_resolve_all(void *events, ThreeMMU32 scene, const ThreeMMActorMotionServices *,
                                     const ThreeMMActorSoundEditServices *);
int three_mm_actor_sound_set(ThreeMMActorState *, ThreeMMActorSounds *, void *events, ThreeMMI32 actor,
                             const ThreeMMTag *, ThreeMMI32 loop, ThreeMMI32 queue, int motion,
                             ThreeMMI32 volume, ThreeMMI32 style, ThreeMMU32 external_child,
                             const ThreeMMActorMotionServices *, const ThreeMMActorSoundEditServices *);
/* Preserves the original default-sound call's positional parameter binding. */
int three_mm_actor_sound_defaults(ThreeMMActorState *, ThreeMMActorSounds *, void *events, ThreeMMI32 actor,
                                  ThreeMMI32 action, ThreeMMU32 external_child, const ThreeMMActorMotionServices *,
                                  const ThreeMMActorSoundEditServices *);
#ifdef __cplusplus
}
#endif
#endif
