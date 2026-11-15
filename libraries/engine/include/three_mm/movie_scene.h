/* Movie scene-container control converted from engine/movie.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_MOVIE_SCENE_H
#define THREE_MM_ENGINE_MOVIE_SCENE_H
#include "tag.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum
    {
        THREE_MM_MOVIE_INSERT_SCENE,
        THREE_MM_MOVIE_REMOVE_SCENE
    };
    typedef struct ThreeMMMovieScenes
    {
        void *open;
        ThreeMMI32 index;
    } ThreeMMMovieScenes;
    typedef struct ThreeMMMovieSceneServices
    {
        void *context;
        ThreeMMMovieScenes *state;
        ThreeMMI32 *count;
        int (*autosave)(void *), (*make_resources)(void *), (*use_temporary)(void *);
        void (*stop_loops)(void *), (*memory_cleanup)(void *);
        void *(*read)(void *, ThreeMMI32), *(*create)(void *);
        int (*start)(void *scene), (*seek_first)(void *scene);
        void (*close)(void *scene), (*retain)(void *scene), (*release)(void *scene);
        void (*sound_frame)(void *scene), (*hide)(void *scene);
        void (*scene_change)(void *), (*invalidate)(void *);
        void *(*file)(void *);
        int (*write)(void *, void *scene, ThreeMMU32 *number);
        ThreeMMU32 (*scene_number)(void *, void *file, ThreeMMI32 index);
        void (*move_child)(void *, void *file, ThreeMMI32 from, ThreeMMI32 to);
        int (*adopt)(void *, void *file, ThreeMMU32 number, ThreeMMI32 index);
        void (*delete_child)(void *, void *file, ThreeMMU32 number, ThreeMMI32 index);
        void (*delete_chunk)(void *, void *file, ThreeMMU32 number), (*save)(void *file);
        void (*remove_actors)(void *scene), (*add_actors)(void *scene);
        void (*dirty)(void *), (*rollcall_changed)(void *), (*switch_error)(void *);
        int (*sounds_enabled)(void *);
        void (*play_sound)(void *), (*flush_sound)(void *);
        void *(*new_undo)(void *);
        void (*configure_undo)(void *, void *undo, ThreeMMI32 index, void *scene);
        int (*add_undo)(void *, void *undo);
        void (*release_undo)(void *undo), (*clear_undo)(void *);
    } ThreeMMMovieSceneServices;
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
    void three_mm_movie_scenes_init(ThreeMMMovieScenes *);
    void three_mm_movie_scene_close_open(const ThreeMMMovieSceneServices *);
    int three_mm_movie_scene_close(const ThreeMMMovieSceneServices *);
    int three_mm_movie_scene_switch(ThreeMMI32 index, const ThreeMMMovieSceneServices *);
    void three_mm_movie_scene_move(ThreeMMI32 index, int down, void *file, const ThreeMMMovieSceneServices *);
    int three_mm_movie_scene_insert(ThreeMMI32 index, void *scene, const ThreeMMMovieSceneServices *);
    int three_mm_movie_scene_new(ThreeMMI32 index, const ThreeMMMovieSceneServices *);
    int three_mm_movie_scene_remove(ThreeMMI32 index, const ThreeMMMovieSceneServices *);
    int three_mm_movie_scene_remove_undoable(ThreeMMI32 index, const ThreeMMMovieSceneServices *);

    typedef struct ThreeMMMovieSceneUndoServices
    {
        void *context;
        int (*create)(void *, ThreeMMI32), (*remove)(void *, ThreeMMI32);
        int (*insert)(void *, ThreeMMI32, void *scene), (*background)(void *, const ThreeMMTag *);
        void (*notify)(void *, int removed), (*flush)(void *), (*clear)(void *), (*unknown)(void *);
    } ThreeMMMovieSceneUndoServices;
    /* tag and scene are borrowed native undo payloads. clear may destroy both
     * the undo object and its payload, so neither is accessed after that callback.
     * Successful redo of insertion creates a fresh scene, then sets its background;
     * undo of removal inserts the retained snapshot. Flush precedes clear on failure. */
    int three_mm_movie_scene_undo(int kind, ThreeMMI32 index, void *scene, const ThreeMMTag *tag, int redo,
                                  const ThreeMMMovieSceneUndoServices *);
#ifdef __cplusplus
}
#endif
#endif
