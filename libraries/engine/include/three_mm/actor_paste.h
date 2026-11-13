/* Actor paste policy converted from engine actredit.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_ACTOR_PASTE_H
#define THREE_MM_ENGINE_ACTOR_PASTE_H
#include "actor_owner.h"
#include "actor_effect.h"
#ifdef __cplusplus
extern "C" {
#endif
enum ThreeMMActorPasteDiagnostic
{
    THREE_MM_PASTE_TEXT_NOT_DOCUMENT,
    THREE_MM_PASTE_EMPTY,
    THREE_MM_PASTE_MISSING_SOUND,
    THREE_MM_PASTE_SAVE_SOUND_FAILED
};
typedef struct ThreeMMActorPasteServices
{
    void *context;
    void (*read)(void *,ThreeMMActorRuntime *);
    void (*write)(void *,const ThreeMMActorRuntime *);
    int (*is_text)(void *,void *template_object);
    int (*ensure_autosave)(void *,void *scene,void **cache);
    void (*duplicate_tag)(void *,const ThreeMMTag *);
    void (*close_tag)(void *,ThreeMMTag *);
    int (*save_template)(void *,ThreeMMTag *,void *cache);
    void *(*fetch_template)(void *,const ThreeMMTag *);
    void (*release_template)(void *,void *template_object);
    void (*bind_scene)(void *,void *scene);
    void (*placement)(void *,ThreeMMActorPoint *);
    int (*sound_exists)(void *,const ThreeMMTag *);
    int (*save_sound)(void *,void *scene,ThreeMMTag *);
    int (*sound_child)(void *,void *scene,ThreeMMU32 number,ThreeMMU32 *child);
    void (*write_event)(void *events,ThreeMMI32,const ThreeMMActorEvent *);
    void (*write_payload)(void *events,ThreeMMI32,const void *);
    void (*remove_event)(void *,ThreeMMI32);
    unsigned short (*camera_yaw)(void *,void *scene);
    void (*dirty)(void *,void *scene);
    void (*diagnostic)(void *,int);
} ThreeMMActorPasteServices;
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
int three_mm_actor_paste(void *route,void *events,ThreeMMI32 frame,void *scene,int migrate_text,
    const ThreeMMActorMotionServices *,const ThreeMMActorPasteServices *);
#ifdef __cplusplus
}
#endif
#endif
