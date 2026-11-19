/* Movie scene-container control converted from engine/movie.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_MOVIE_SCENE_H
#define MM_ENGINE_MOVIE_SCENE_H
#include "playback.h"
#include "tag.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C"
{
#endif
    enum
    {
        MM_MOVIE_INSERT_SCENE,
        MM_MOVIE_REMOVE_SCENE
    };
    typedef struct MMMovieScenes
    {
        void *open;
        MMI32 index;
    } MMMovieScenes;
    typedef struct MMMovieSceneServices
    {
        void *context;
        MMMovieScenes *state;
        MMI32 *count;
        int (*autosave)(void *), (*make_resources)(void *), (*use_temporary)(void *);
        void (*stop_loops)(void *), (*memory_cleanup)(void *);
        void *(*read)(void *, MMI32), *(*create)(void *);
        int (*start)(void *scene), (*seek_first)(void *scene);
        void (*close)(void *scene), (*retain)(void *scene), (*release)(void *scene);
        void (*sound_frame)(void *scene), (*hide)(void *scene);
        void (*scene_change)(void *), (*invalidate)(void *);
        void *(*file)(void *);
        int (*write)(void *, void *scene, MMU32 *number);
        MMU32 (*scene_number)(void *, void *file, MMI32 index);
        void (*move_child)(void *, void *file, MMI32 from, MMI32 to);
        int (*adopt)(void *, void *file, MMU32 number, MMI32 index);
        void (*delete_child)(void *, void *file, MMU32 number, MMI32 index);
        void (*delete_chunk)(void *, void *file, MMU32 number), (*save)(void *file);
        void (*remove_actors)(void *scene), (*add_actors)(void *scene);
        void (*dirty)(void *), (*rollcall_changed)(void *), (*switch_error)(void *);
        int (*sounds_enabled)(void *);
        void (*play_sound)(void *), (*flush_sound)(void *);
        void *(*new_undo)(void *);
        void (*configure_undo)(void *, void *undo, MMI32 index, void *scene);
        int (*add_undo)(void *, void *undo);
        void (*release_undo)(void *undo), (*clear_undo)(void *);
    } MMMovieSceneServices;
    /* Valid scene indices, an initialized owner, synchronous callbacks and stable
     * service storage are required. Nested scene helpers may use the same state;
     * client notifications must not re-enter editing. close releases one reference
     * and hides the scene; release only releases. read/create return one owner.
     * A failed removal read still calls close with null, including host diagnostics.
     * new_undo returns one reference;
     * configure_undo retains its scene, and successful add_undo retains the undo.
     * The host list releases committed records; C orders commit/release/clear.
     * Native file handles remain borrowed throughout a transaction. start can fail
     * after changing the world. Scene publication precedes notification and index
     * publication follows it, including retry of the old scene after failure.
     * Save and roll-call refresh failures after publication are intentionally ignored.
     * Failed adoption preserves the native incremented count, while failed opening
     * removes the inserted child and decrements the count. No implicit undo/dirty
     * rollback is performed. Removing the last scene returns before marking dirty.
     * Undo allocation failure during remove returns success without removing it. */
    void mm_movie_scenes_init(MMMovieScenes *);
    void mm_movie_scene_close_open(const MMMovieSceneServices *);
    int mm_movie_scene_close(const MMMovieSceneServices *);
    int mm_movie_scene_switch(MMI32 index, const MMMovieSceneServices *);
    void mm_movie_scene_move(MMI32 index, int down, void *file, const MMMovieSceneServices *);
    int mm_movie_scene_insert(MMI32 index, void *scene, const MMMovieSceneServices *);
    int mm_movie_scene_new(MMI32 index, const MMMovieSceneServices *);
    int mm_movie_scene_remove(MMI32 index, const MMMovieSceneServices *);
    int mm_movie_scene_remove_undoable(MMI32 index, const MMMovieSceneServices *);

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

    typedef struct MMMovieSceneUndoServices
    {
        void *context;
        int (*create)(void *, MMI32), (*remove)(void *, MMI32);
        int (*insert)(void *, MMI32, void *scene), (*background)(void *, const MMTag *);
        void (*notify)(void *, int removed), (*flush)(void *), (*clear)(void *), (*unknown)(void *);
    } MMMovieSceneUndoServices;
    /* tag and scene are borrowed native undo payloads. clear may destroy both
     * the undo object and its payload, so neither is accessed after that callback.
     * Successful redo of insertion creates a fresh scene, then sets its background;
     * undo of removal inserts the retained snapshot. Flush precedes clear on failure. */
    int mm_movie_scene_undo(int kind, MMI32 index, void *scene, const MMTag *tag, int redo,
                                  const MMMovieSceneUndoServices *);
#ifdef __cplusplus
}
#endif
#endif
