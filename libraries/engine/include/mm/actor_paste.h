/* Actor paste policy converted from engine actredit.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_ACTOR_PASTE_H
#define MM_ENGINE_ACTOR_PASTE_H
#include "actor_owner.h"
#include "actor_effect.h"
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
    int (*is_text)(void *,void *template_object);
    int (*ensure_autosave)(void *,void *scene,void **cache);
    void (*duplicate_tag)(void *,const MMTag *);
    void (*close_tag)(void *,MMTag *);
    int (*save_template)(void *,MMTag *,void *cache);
    void *(*fetch_template)(void *,const MMTag *);
    void (*release_template)(void *,void *template_object);
    void (*bind_scene)(void *,void *scene);
    void (*placement)(void *,MMActorPoint *);
    int (*sound_exists)(void *,const MMTag *);
    int (*save_sound)(void *,void *scene,MMTag *);
    int (*sound_child)(void *,void *scene,MMU32 number,MMU32 *child);
    void (*write_event)(void *events,MMI32,const MMActorEvent *);
    void (*write_payload)(void *events,MMI32,const void *);
    void (*remove_event)(void *,MMI32);
    unsigned short (*camera_yaw)(void *,void *scene);
    void (*dirty)(void *,void *scene);
    void (*diagnostic)(void *,int);
} MMActorPasteServices;
/* Paste an already copied actor. Tables must contain an initial route point and
 * a first Add event with native actor payload layouts. A non-Add first event
 * fails after publishing lifetime/frame/scene changes. Empty events are a legacy
 * assertion precondition, not a recoverable input. migrate_text selects BUG1888.
 *
 * C owns template transfer/ownership, positioning, retiming and sound adoption.
 * save_template redirects a duplicated tag to the destination cache. Failed
 * save/fetch closes that duplicate; successful transfer closes the old tag and
 * releases the old template before publishing the new template pointer.
 * remove_event must use the actor edit removal primitive with state updates and
 * tag disposal enabled. bind_scene and remove_event may update the runtime;
 * other callbacks must not reenter or independently change the actor/tables.
 *
 * Failure is deliberately partial: earlier frame writes/deletions remain. A
 * failed sound-child lookup does not write its locally adopted tag into the row
 * or perform additional cleanup, matching the native operation. dirty and final
 * route-translation publication happen only on success. This is a controller,
 * not a declaration that native resource/movie/template adapters are converted.
 */
int mm_actor_paste(MMActorRuntime *,void *route,void *events,MMI32 frame,void *scene,int migrate_text,
    const MMActorMotionServices *,const MMActorPasteServices *);
#ifdef __cplusplus
}
#endif
#endif
