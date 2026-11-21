/* Actor paste policy converted from engine actredit.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_ACTOR_PASTE_H
#define MM_ENGINE_ACTOR_PASTE_H
#include "actor_create.h"
#include "movie_sound.h"
#ifdef __cplusplus
extern "C" {
#endif
enum MMActorPasteDiagnostic
{
    MM_PASTE_TEXT_NOT_DOCUMENT,
    MM_PASTE_EMPTY,
    MM_PASTE_MISSING_SOUND,
    MM_PASTE_SAVE_SOUND_FAILED
};
typedef struct MMActorPasteServices
{
    void *context;
    MMActorCreateServices create;
    MMActorEditServices edits;
    int migrate_text;
    int (*is_text)(void *,void *template_object);
    int (*ensure_autosave)(void *,void *scene,void **cache);
    void (*placement)(void *,MMActorPoint *);
    MMMovieSoundServices (*movie_sound)(void *scene);
    unsigned short (*camera_yaw)(void *,void *scene);
    void (*dirty)(void *,void *scene);
    void (*diagnostic)(void *,int);
} MMActorPasteServices;
/* Paste an already copied actor. Tables must contain an initial route point and
 * a first Add event with native actor payload layouts. A non-Add first event
 * fails after publishing lifetime/frame/scene changes. An empty event table
 * diagnoses the legacy assertion at that point. migrate_text selects BUG1888.
 *
 * C owns template transfer/ownership, positioning, retiming and sound adoption.
 * The tag service redirects a duplicated tag to the destination cache. Failed
 * save/fetch closes that duplicate; successful transfer closes the old tag and
 * releases the old template before publishing the new template pointer.
 * Sound removal uses the actor's table and the shared event editor, with cursor
 * updates and tag disposal enabled. The supplied edit hooks provide state
 * publication and the scene sound indicator. Callbacks must not reenter or
 * independently change the actor/tables.
 *
 * Failure is deliberately partial: earlier frame writes/deletions remain. A
 * failed sound-child lookup does not write its locally adopted tag into the row
 * or perform additional cleanup, matching the native operation. dirty and final
 * route-translation publication happen only on success.
 */
int mm_actor_paste(MMActorOwner *, MMI32 frame, void *scene, const MMActorPasteServices *);
#ifdef __cplusplus
}
#endif
#endif
