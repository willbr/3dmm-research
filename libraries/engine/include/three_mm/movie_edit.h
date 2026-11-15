/* Movie editing and common undo control converted from engine/movie.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_MOVIE_EDIT_H
#define THREE_MM_ENGINE_MOVIE_EDIT_H
#include "movie_rollcall.h"
#include "movie_scene.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum
    {
        THREE_MM_MOVIE_UNDO_DISABLED = 1,
        THREE_MM_MOVIE_UNDO_READY = 2
    };
    typedef struct ThreeMMMovieUndoServices
    {
        void *context;
        void (*bind_movie)(void *, void *undo), (*bind_scene)(void *undo, ThreeMMI32),
            (*bind_frame)(void *undo, ThreeMMI32);
        ThreeMMI32 (*scene_index)(void *), (*frame)(void *);
        int (*commit)(void *, void *undo);
        void (*clear)(void *), (*notify)(void *, int mode);
    } ThreeMMMovieUndoServices;
    int three_mm_movie_undo_add(void *undo, const ThreeMMMovieUndoServices *);
    void three_mm_movie_undo_clear(const ThreeMMMovieUndoServices *);
    enum
    {
        THREE_MM_MOVIE_EDIT_ROTATE,
        THREE_MM_MOVIE_EDIT_SQUASH,
        THREE_MM_MOVIE_EDIT_TIME,
        THREE_MM_MOVIE_EDIT_SCALE,
        THREE_MM_MOVIE_EDIT_COSTUME,
        THREE_MM_MOVIE_EDIT_ACTOR_SOUND,
        THREE_MM_MOVIE_EDIT_REMOVE,
        THREE_MM_MOVIE_EDIT_ONSTAGE,
        THREE_MM_MOVIE_EDIT_CAMERA,
        THREE_MM_MOVIE_EDIT_PAUSE,
        THREE_MM_MOVIE_EDIT_TRANSITION,
        THREE_MM_MOVIE_EDIT_BACKGROUND_SOUND
    };
    enum
    {
        THREE_MM_MOVIE_EDIT_NO_ACTOR,
        THREE_MM_MOVIE_EDIT_ROLLCALL_FAILED,
        THREE_MM_MOVIE_EDIT_BACKGROUND_FAILED
    };
    typedef struct ThreeMMMovieEditRequest
    {
        const ThreeMMTag *tag;
        unsigned short rotation[3];
        int forward;
        ThreeMMI32 factor, pull[3], frame, shift, part, costume, custom;
        ThreeMMI32 camera, reason, delay, transition, loop, queue, cel, volume, style;
    } ThreeMMMovieEditRequest;
    typedef struct ThreeMMMovieEditServices
    {
        void *context;
        ThreeMMMovieState *data;
        ThreeMMMovieScenes *scenes;
        ThreeMMMovieRollCallServices rollcall;
        ThreeMMMovieUndoServices undo;
        void *(*selected_actor)(void *scene), *(*actor_from_id)(void *scene, ThreeMMI32);
        void (*select_actor)(void *scene, void *actor);
        void *(*first_view)(void *), *(*new_actor)(void *, const ThreeMMTag *);
        int (*actor_visible)(void *actor), (*core_onstage)(void *actor);
        void *(*duplicate_actor)(void *actor, int reset);
        void (*release_actor)(void *actor), (*restore_actor)(void *actor, void *snapshot);
        int (*add_actor)(void *scene, void *actor), (*remove_actor)(void *scene, ThreeMMI32 id);
        int (*apply_actor)(void *actor, int kind, const ThreeMMMovieEditRequest *);
        int (*apply_scene)(void *scene, int kind, const ThreeMMMovieEditRequest *);
        ThreeMMI32 (*frame)(void *scene), (*undo_count)(void *);
        void *(*last_undo)(void *);
        int (*actor_undo)(void *undo), (*time_undo)(void *undo);
        void (*time_undo_frame)(void *undo, ThreeMMI32);
        void (*place_actor)(void *view, int entire_scene), (*placement_undo)(void *view, void *owned_snapshot);
        int (*paste_actor)(void *scene, void *actor), (*paste_route)(void *actor, void *source);
        int (*create_actor_undo)(void *actor, void *snapshot);
        void (*long_op)(void *, int begin);
        int (*cache_tag)(void *, const ThreeMMTag *, int background);
        void (*default_title)(void *);
        int (*empty_scene)(void *scene), (*new_scene)(void *, ThreeMMI32), (*remove_scene)(void *, ThreeMMI32);
        int (*background)(void *, const ThreeMMTag *, int undoable);
        void *(*new_scene_undo)(void *);
        void (*configure_scene_undo)(void *, void *undo, ThreeMMI32 index, const ThreeMMTag *);
        void (*release_undo)(void *undo);
        int (*sounds_enabled)(void *);
        void (*play_sound)(void *), (*flush_sound)(void *);
        void *(*fetch_sound)(void *, const ThreeMMTag *);
        ThreeMMI32 (*sound_volume)(void *sound), (*sound_style)(void *sound);
        void (*release_sound)(void *sound);
        void (*dirty)(void *), (*invalidate)(void *, int scrollbars), (*actor_nuked)(void *),
            (*diagnostic)(void *, int);
    } ThreeMMMovieEditServices;
    /* Initialized, stable owners/handles and synchronous non-reentrant services
     * are required. Scene edits/selected edits require a current scene; selected
     * actor edits require a selected actor. Getter/result tags are borrowed.
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
    ThreeMMI32 three_mm_movie_selected_actor(const ThreeMMMovieEditServices *);
    int three_mm_movie_choose_actor(ThreeMMI32 id, const ThreeMMMovieEditServices *);
    int three_mm_movie_insert_actor(const ThreeMMTag *, const ThreeMMMovieEditServices *);
    int three_mm_movie_onstage(ThreeMMI32 id, const ThreeMMMovieEditServices *);
    int three_mm_movie_edit_actor(int kind, const ThreeMMMovieEditRequest *, const ThreeMMMovieEditServices *);
    int three_mm_movie_edit_scene(int kind, const ThreeMMMovieEditRequest *, const ThreeMMMovieEditServices *);
    int three_mm_movie_paste_actor(void *actor, const ThreeMMMovieEditServices *);
    int three_mm_movie_paste_route(void *source, const ThreeMMMovieEditServices *);
    int three_mm_movie_add_background(const ThreeMMTag *, const ThreeMMMovieEditServices *);
#ifdef __cplusplus
}
#endif
#endif
