/* Movie editing and common undo control converted from engine/movie.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_MOVIE_EDIT_H
#define MM_ENGINE_MOVIE_EDIT_H
#include "movie_rollcall.h"
#include "movie_scene.h"
#include "movie_undo.h"
#ifdef __cplusplus
extern "C"
{
#endif
    struct MMActorUndoState;
    enum
    {
        MM_MOVIE_EDIT_ROTATE,
        MM_MOVIE_EDIT_SQUASH,
        MM_MOVIE_EDIT_TIME,
        MM_MOVIE_EDIT_SCALE,
        MM_MOVIE_EDIT_COSTUME,
        MM_MOVIE_EDIT_ACTOR_SOUND,
        MM_MOVIE_EDIT_REMOVE,
        MM_MOVIE_EDIT_ONSTAGE,
        MM_MOVIE_EDIT_CAMERA,
        MM_MOVIE_EDIT_PAUSE,
        MM_MOVIE_EDIT_TRANSITION,
        MM_MOVIE_EDIT_BACKGROUND_SOUND
    };
    enum
    {
        MM_MOVIE_EDIT_NO_ACTOR,
        MM_MOVIE_EDIT_ROLLCALL_FAILED,
        MM_MOVIE_EDIT_BACKGROUND_FAILED
    };
    typedef struct MMMovieEditRequest
    {
        const MMTag *tag;
        unsigned short rotation[3];
        int forward;
        MMI32 factor, pull[3], frame, shift, part, costume, custom;
        MMI32 camera, reason, delay, transition, loop, queue, cel, volume, style;
    } MMMovieEditRequest;
    typedef struct MMMovieEditServices
    {
        void *context;
        MMMovieState *data;
        MMMovieScenes *scenes;
        MMMovieRollCallServices rollcall;
        MMMovieUndoServices undo;
        void *(*selected_actor)(void *scene), *(*actor_from_id)(void *scene, MMI32);
        void (*select_actor)(void *scene, void *actor);
        void *(*first_view)(void *), *(*new_actor)(void *, const MMTag *);
        int (*actor_visible)(void *actor), (*core_onstage)(void *actor);
        void *(*duplicate_actor)(void *actor, int reset);
        void (*release_actor)(void *actor), (*restore_actor)(void *actor, void *snapshot);
        int (*add_actor)(void *scene, void *actor), (*remove_actor)(void *scene, MMI32 id);
        int (*apply_actor)(void *actor, int kind, const MMMovieEditRequest *);
        int (*apply_scene)(void *scene, int kind, const MMMovieEditRequest *);
        MMI32 (*frame)(void *scene);
        struct MMActorUndoState *(*last_actor_undo)(void *);
        void (*place_actor)(void *view, int entire_scene), (*placement_undo)(void *view, void *owned_snapshot);
        int (*paste_actor)(void *scene, void *actor), (*paste_route)(void *actor, void *source);
        int (*create_actor_undo)(void *actor, void *snapshot);
        void (*long_op)(void *, int begin);
        int (*cache_tag)(void *, const MMTag *, int background);
        void (*default_title)(void *);
        int (*empty_scene)(void *scene), (*new_scene)(void *, MMI32), (*remove_scene)(void *, MMI32);
        int (*background)(void *, const MMTag *, int undoable);
        void *(*new_scene_undo)(void *);
        void (*configure_scene_undo)(void *, void *undo, MMI32 index, const MMTag *);
        void (*release_undo)(void *undo);
        int (*sounds_enabled)(void *);
        void (*play_sound)(void *), (*flush_sound)(void *);
        void *(*fetch_sound)(void *, const MMTag *);
        MMI32 (*sound_volume)(void *sound), (*sound_style)(void *sound);
        void (*release_sound)(void *sound);
        void (*dirty)(void *), (*invalidate)(void *, int scrollbars), (*actor_nuked)(void *),
            (*diagnostic)(void *, int);
    } MMMovieEditServices;
    /* Initialized, stable owners/handles and synchronous non-reentrant services
     * are required. Scene edits/selected edits require a current scene; selected
     * actor edits require a selected actor. Time edits return failure when
     * there is no open scene or primary actor. Getter/result tags are borrowed.
     * new_actor/duplicate_actor/new_scene_undo return one reference. Placement undo
     * takes that reference; scene-add/paste/undo factories manage their own retains.
     * Callbacks for Scene/Actor operations may use the existing C controllers and
     * publish their documented state. No implicit rollback of failed edits occurs.
     * Path-paste rereads selection after duplicate/paste/failed-undo callbacks and
     * restores only after undo-creation failure. Offstage choice retains
     * its two selection notifications, roll-call refresh and snapshot transfer.
     * Sound defaults are fetched/released only when either field is missing
     * (volume -1, style 0). Scene creation removes its new scene after background/
     * undo failure, but preserves preceding title, sound and mutation prefixes.
     * Common undo publishes movie/scene/frame before committing and notifying. */
    MMI32 mm_movie_selected_actor(const MMMovieEditServices *);
    int mm_movie_choose_actor(MMI32 id, const MMMovieEditServices *);
    int mm_movie_insert_actor(const MMTag *, const MMMovieEditServices *);
    int mm_movie_onstage(MMI32 id, const MMMovieEditServices *);
    int mm_movie_edit_actor(int kind, const MMMovieEditRequest *, const MMMovieEditServices *);
    int mm_movie_step_actor_time(MMI32 frames, const MMMovieEditServices *);
    int mm_movie_edit_scene(int kind, const MMMovieEditRequest *, const MMMovieEditServices *);
    int mm_movie_paste_actor(void *actor, const MMMovieEditServices *);
    int mm_movie_paste_route(void *source, const MMMovieEditServices *);
    int mm_movie_add_background(const MMTag *, const MMMovieEditServices *);
#ifdef __cplusplus
}
#endif
#endif
