/* Movie editing and common undo control converted from engine/movie.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_MOVIE_EDIT_H
#define MM_ENGINE_MOVIE_EDIT_H
#include "actor_group_undo.h"
#include "actor_route_paste.h"
#include "actor_transaction.h"
#include "movie_rollcall.h"
#include "movie_scene.h"
#include "movie_undo.h"
#include "scene_control.h"
#include "scene_edit.h"
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
        MMI32 factor, frame, part, costume, custom;
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
        int (*actor_visible)(void *actor);
        MMActorStageBind stage;
        MMActorOwnerServices actors;
        MMActorUndoCreateServices (*actor_undo)(void *actor);
        MMActorTransformEditServices (*transform)(void *actor);
        MMActorCostumeEditServices (*costume)(void *actor);
        MMActorSoundEditServices (*actor_sound)(void *actor);
        MMU32 sound_external_child; /* Unused child field for external sound sources. */
        int (*add_actor)(void *scene, void *actor), (*remove_actor)(void *scene, MMI32 id);
        MMSceneEditServices (*scene_edit)(void *scene);
        MMScenePropertyServices (*scene_properties)(void *scene);
        MMI32 (*frame)(void *scene);
        struct MMActorUndoState *(*last_actor_undo)(void *);
        void (*place_actor)(void *view, int entire_scene), (*placement_undo)(void *view, void *owned_snapshot);
        int (*paste_actor)(void *scene, void *actor);
        MMActorRoutePasteServices (*route_paste)(void *actor);
        void (*long_op)(void *, int begin);
        int (*cache_tag)(void *, const MMTag *, int background);
        void (*default_title)(void *);
        int (*new_scene)(void *, MMI32), (*remove_scene)(void *, MMI32);
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

    enum
    {
        MM_MOVIE_DRAG_COMPOSE,
        MM_MOVIE_DRAG_ROTATE_X,
        MM_MOVIE_DRAG_ROTATE_Y,
        MM_MOVIE_DRAG_ROTATE_Z,
        MM_MOVIE_DRAG_RESIZE,
        MM_MOVIE_DRAG_SQUASH
    };
    typedef struct MMMovieActorDrag
    {
        MMActorDragUndo undo;
        int size_direction;
    } MMMovieActorDrag;
    typedef struct MMMovieActorDragInput
    {
        int tool, command, shift, ground;
        MMActorPoint mouse, world;
    } MMMovieActorDragInput;
    struct MMActorGroupTransformServices;
    typedef struct MMMovieActorDragServices
    {
        const struct MMSceneSelectionServices *selection;
        const struct MMActorGroupTransformServices *actors;
        const MMMovieEditServices *movie;
        const MMActorDragUndoServices *undo;
        void *view;
        void (*adjust_cursor)(void *);
        void (*size_sound)(void *, int alternate);
        void (*world_dirty)(void *);
        void (*mark_views)(void *);
    } MMMovieActorDragServices;
    /* Use the pending undo and pivot captured on pointer down. Start each
     * resize or squash gesture with size_direction zero. View services run
     * synchronously in the original cursor, sound and invalidation order. */
    void mm_movie_actor_drag_sample(MMMovieActorDrag *, const MMMovieActorDragInput *,
                                    const MMMovieActorDragServices *);

    enum
    {
        MM_MOVIE_CLIP_UNKNOWN,
        MM_MOVIE_CLIP_CUT,
        MM_MOVIE_CLIP_COPY,
        MM_MOVIE_CLIP_PASTE,
        MM_MOVIE_CLIP_ROUTE,
        MM_MOVIE_CLIP_CUT_TOOL,
        MM_MOVIE_CLIP_COPY_TOOL,
        MM_MOVIE_CLIP_PASTE_TOOL,
        MM_MOVIE_CLIP_SHIFT_CUT,
        MM_MOVIE_CLIP_SHIFT_COPY
    };
    enum
    {
        MM_MOVIE_CLIP_ACTOR,
        MM_MOVIE_CLIP_TEXT_BOX
    };
    enum
    {
        MM_MOVIE_CLIP_NO_ACTOR,
        MM_MOVIE_CLIP_NO_SCENE,
        MM_MOVIE_CLIP_UNSUPPORTED,
        MM_MOVIE_CLIP_BAD_COMMAND
    };
    typedef struct MMMovieClipboardServices
    {
        void *context;
        void *(*scene)(void *);
        int (*text_mode)(void *), (*shift)(void *);
        void (*set_shift)(void *, int);
        void *(*selected_text)(void *scene), *(*selected_actor)(void *scene);
        int (*text_visible)(void *text), (*text_selected)(void *text), (*actor_visible)(void *actor);
        int (*empty)(void *), (*format)(void *, int kind, void **owned);
        int (*route_only)(void *document);
        void *(*copy_actor)(void *actor, int route_only, int through_scene_end);
        void (*publish)(void *, void *document), (*release)(void *document);
        int (*scene_sounds_enabled)(void *scene);
        void (*scene_sounds)(void *scene, int enabled);
        int (*delete_actor)(void *actor, int through_scene_end, int *alive);
        void (*remove_actor)(void *scene, void *actor);
        MMActorStageBind stage;
        void (*dirty)(void *scene), (*invalidate)(void *);
        void (*paste_actor)(void *, void *document);
        int (*paste_text)(void *scene, void *document);
        void (*enable_text)(void *);
        void (*cue)(void *, int tool), (*set_tool)(void *, int tool);
        void (*queue_text)(void *, void *text), (*queue_generic)(void *, int command);
        void (*diagnostic)(void *, int);
    } MMMovieClipboardServices;

    /* The host translates command/tool IDs and forwards queued native commands.
     * A successful format with an out pointer and each successful copy_actor
     * return one owned document reference. Publish retains the document.
     * Scene, selection and pointer hit are borrowed synchronous handles. */
    void mm_movie_clipboard_command(int command, const MMMovieClipboardServices *);
    void mm_movie_clipboard_click(int tool, int hit_actor, const MMMovieClipboardServices *);

    /* Initialized, stable owners/handles and synchronous non-reentrant services
     * are required. Scene edits/selected edits require a current scene; selected
     * actor edits require a selected actor. Time edits return failure when
     * there is no open scene or primary actor. Getter/result tags are borrowed.
     * Costume and sound requests require a tag.
     * new_actor/new_scene_undo return one reference. Placement undo takes ownership
     * of the reset actor snapshot. Scene-add/paste/undo manage their own retains.
     * Callbacks for Scene/Actor operations may use the existing C controllers and
     * publish their documented state. No implicit rollback of failed edits occurs.
     * Path-paste rereads selection after capture, paste and failed undo creation.
     * It restores only after undo creation fails. Offstage choice retains its two
     * selection notifications, roll-call refresh and snapshot transfer.
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
