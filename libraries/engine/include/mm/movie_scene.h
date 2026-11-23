/* Movie scene-container control converted from engine/movie.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_MOVIE_SCENE_H
#define MM_ENGINE_MOVIE_SCENE_H
#include "chunk_edit.h"
#include "movie_archive_edit.h"
#include "movie_owner.h"
#include "playback.h"
#include "scene_edit.h"
#include "scene_snapshot.h"
#include "tag.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C"
{
#endif
    enum
    {
        MM_MOVIE_SCENE_UNDO_EMPTY,
        MM_MOVIE_INSERT_SCENE,
        MM_MOVIE_REMOVE_SCENE
    };
    typedef struct MMMovieSceneUndoRecord
    {
        int operation;
        MMI32 index;
        union
        {
            MMTag background;
            MMSceneSnapshot snapshot;
        } saved;
    } MMMovieSceneUndoRecord;
    enum
    {
        MM_MOVIE_ARCHIVE_MISSING_CHILD,
        MM_MOVIE_ARCHIVE_UNLINK_FAILED,
        MM_MOVIE_ARCHIVE_DELETE_FAILED,
        MM_MOVIE_ARCHIVE_MOVE_FAILED
    };
    typedef struct MMMovieScenes
    {
        void *open;
        MMI32 index;
    } MMMovieScenes;
    struct MMMovieSaveServices;
    typedef struct MMMovieSceneServices
    {
        void *context;
        MMMovieScenes *state;
        MMMovieState *movie;
        const struct MMMovieSaveServices *working_save;
        /* Alternate archive operations when a working save provider is absent. */
        int (*autosave)(void *), (*make_resources)(void *), (*use_temporary)(void *);
        void (*stop_loops)(void *), (*memory_cleanup)(void *);
        MMMovieSessionBinding session;
        void *(*read)(void *, void *resources, MMU32 number, const struct MMMovieSession *);
        void *(*create)(void *);
        int (*start)(void *scene), (*seek_first)(void *scene);
        void (*close)(void *scene), (*retain)(void *scene), (*release)(void *scene);
        void (*sound_frame)(void *scene);
        void (*scene_change)(void *), (*invalidate)(void *);
        MMChunkEdit (*edit)(void *resources);
        int (*write)(void *, void *scene, MMU32 *number);
        MMMovieArchiveText snapshot_text;
        int (*copy_snapshot)(void *resources, const MMSceneSnapshot *, MMU32 *number);
        void (*save)(void *resources);
        void (*archive_issue)(void *, int issue);
        void (*missing_link)(void *);
        void (*remove_actors)(void *scene), (*add_actors)(void *scene);
        void (*dirty)(void *), (*rollcall_changed)(void *), (*switch_error)(void *);
        int (*sounds_enabled)(void *);
        void (*play_sound)(void *), (*flush_sound)(void *);
        void *(*new_undo)(void *, MMMovieSceneUndoRecord **record);
        const MMTagServices *undo_tags;
        MMSceneSnapshotServices snapshot;
        int (*add_undo)(void *, void *undo);
        void (*release_undo)(void *undo), (*clear_undo)(void *);
    } MMMovieSceneServices;
    /* Valid scene indices, an initialized owner, synchronous callbacks and stable
     * service storage are required. Nested scene helpers may use the same state;
     * client notifications must not re-enter editing. close releases one reference
     * and hides the scene; release only releases. read/create return one owner.
     * new_undo returns one reference and a fresh record inside it. The C record
     * owns a private scene snapshot or local background tag. A failed snapshot
     * leaves the scene in the movie. Successful add_undo retains the undo.
     * undo_tags is required for a local tag with an open cache.
     * The host list releases committed records; C orders commit/release/clear.
     * Reads receive a resolved scene number. The live session is borrowed for
     * scene switching and NULL for direct reads during composition.
     * The C89 editor works on exact chunk identities. Resources remain borrowed
     * throughout a transaction. start can fail
     * after changing the world. Scene publication precedes notification and index
     * publication follows it, including retry of the old scene after failure.
     * Save and roll-call refresh failures after publication are intentionally ignored.
     * Failed adoption preserves the native incremented count, while failed opening
     * removes the inserted child and decrements the count. No implicit undo/dirty
     * rollback is performed. Removing the last scene returns before marking dirty.
     * Undo allocation failure during remove returns success without removing it. */
    void mm_movie_scenes_init(MMMovieScenes *);
    int mm_movie_scene_autosave(const MMMovieSceneServices *);
    int mm_movie_scene_temporary(const MMMovieSceneServices *);
    MMChunkEdit mm_movie_scene_editor(void *resources, const MMMovieSceneServices *);
    void mm_movie_scene_close_open(const MMMovieSceneServices *);
    int mm_movie_scene_close(const MMMovieSceneServices *);
    int mm_movie_scene_switch(MMI32 index, const MMMovieSceneServices *);
    int mm_movie_scene_insert(MMI32 index, void *scene, const MMMovieSceneServices *);
    int mm_movie_scene_insert_snapshot(MMI32 index, const MMSceneSnapshot *, const MMMovieSceneServices *);
    int mm_movie_scene_new(MMI32 index, const MMMovieSceneServices *);
    int mm_movie_scene_remove(MMI32 index, const MMMovieSceneServices *);
    int mm_movie_scene_remove_undoable(MMI32 index, const MMMovieSceneServices *);
    int mm_movie_scene_record_insertion(MMI32 index, const MMTag *, const MMMovieSceneServices *);
    void mm_movie_scene_undo_dispose(MMMovieSceneUndoRecord *, const MMTagServices *);

    enum
    {
        MM_MOVIE_SCENE_INITIAL = 1,
        MM_MOVIE_SCENE_KEEP_FRAMING = 2,
        MM_MOVIE_SCENE_KEEP_AUDIO = 4
    };
    typedef enum MMMovieSceneCursorEffect
    {
        MM_MOVIE_SCENE_CLEAR_VIEW,
        MM_MOVIE_SCENE_STOP_PLAYBACK,
        MM_MOVIE_SCENE_LOAD,
        MM_MOVIE_SCENE_BACKGROUND,
        MM_MOVIE_SCENE_READ_RANGE,
        MM_MOVIE_SCENE_SET_RANGE,
        MM_MOVIE_SCENE_READ_FRAME,
        MM_MOVIE_SCENE_SET_FRAME,
        MM_MOVIE_SCENE_APPLY_CAMERAS,
        MM_MOVIE_SCENE_APPLY_PLACEMENTS,
        MM_MOVIE_SCENE_RESET_FRAMING,
        MM_MOVIE_SCENE_SET_PLAYBACK,
        MM_MOVIE_SCENE_SYNC_OBJECTS,
        MM_MOVIE_SCENE_READ_SOUNDS,
        MM_MOVIE_SCENE_CANCEL_PREVIEWS,
        MM_MOVIE_SCENE_SEEK_PLAYBACK
    } MMMovieSceneCursorEffect;
    typedef struct MMMovieSceneCursorRequest
    {
        MMI32 scene, frame, first, last;
        int keep_framing, keep_audio;
    } MMMovieSceneCursorRequest;
    typedef struct MMMovieSceneCursorServices
    {
        void *context;
        MMI32 *selected;
        MMPolledMovie *playback;
        /* READ_RANGE writes first/last. READ_FRAME writes frame. */
        int (*effect)(void *, MMMovieSceneCursorEffect, MMMovieSceneCursorRequest *);
    } MMMovieSceneCursorServices;
    /* The preview session supplies typed view and playback effects. Effects
     * return 1/0 or a negative exception status without unwinding through C.
     * Initial admission uses the loaded scene's authored range and camera.
     * A switch clears the old selection first and publishes the new index
     * only after playback, objects and sounds are ready. A failed switch
     * retains the effects already applied. Seek cancels temporary previews
     * before validation and resets playback after successful frame evaluation. */
    int mm_movie_scene_cursor_select(MMI32 scene, size_t count, unsigned flags, const MMMovieSceneCursorServices *);
    int mm_movie_scene_cursor_seek(long frame, const MMMovieSceneCursorServices *);

    typedef struct MMMovieRangeEdit
    {
        MMI32 first, last, frame;
    } MMMovieRangeEdit;
    typedef enum MMMovieRangeEffect
    {
        MM_MOVIE_RANGE_CANCEL_PREVIEWS,
        MM_MOVIE_RANGE_PREPARE,
        MM_MOVIE_RANGE_SET_BOUNDS,
        MM_MOVIE_RANGE_SET_FRAME,
        MM_MOVIE_RANGE_PREPARE_FRAME,
        MM_MOVIE_RANGE_ABANDON
    } MMMovieRangeEffect;
    typedef struct MMMovieRangeServices
    {
        void *context;
        MMI32 *selected;
        MMPolledMovie *playback;
        void (*position)(void *, MMMovieRangeEdit *);
        int (*effect)(void *, MMMovieRangeEffect, const MMMovieRangeEdit *, int restoring);
        void (*publish)(void *);
    } MMMovieRangeServices;
    enum { MM_MOVIE_RANGE_DIRECTION = -1, MM_MOVIE_RANGE_BOUNDARY = -2 };
    /* Reserve undo and prepare the replacement schedule before changing the
     * live view. Failed evaluation restores the prior range and frame. If
     * restoring fails, clear selection and abandon the view. Effects return
     * 1/0 and callbacks never unwind. Publish commits the staged edit. */
    int mm_movie_scene_extend(int direction, const MMMovieRangeServices *);

    typedef struct MMMovieSceneUndoServices
    {
        const MMMovieSceneServices *scenes;
        MMSceneEditServices (*scene_edit)(void *scene);
        void (*notify)(void *movie, int removed), (*unknown)(void *movie);
    } MMMovieSceneUndoServices;
    /* Clear may destroy the undo object and its record, so neither is accessed
     * after that callback.
     * Successful redo of insertion creates a fresh scene, then sets its background;
     * undo of removal copies and inserts the archived scene. Flush precedes clear on failure. */
    int mm_movie_scene_undo(const MMMovieSceneUndoRecord *, int redo, const MMMovieSceneUndoServices *);
#ifdef __cplusplus
}
#endif
#endif
