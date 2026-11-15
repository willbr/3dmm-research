/* Movie composition converted from engine/movie.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_MOVIE_COMPOSE_H
#define THREE_MM_ENGINE_MOVIE_COMPOSE_H
#include "movie_owner.h"
#include "movie_scene.h"
#include "chunk_graph.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct ThreeMMComposeScene
    {
        ThreeMMI32 movie;
        ThreeMMU32 number, child;
        void *thumbnail;
        ThreeMMI32 transition;
        int deleted;
    } ThreeMMComposeScene;
    typedef struct ThreeMMComposeMovie
    {
        void *resources;
        ThreeMMU32 number;
        ThreeMMI32 actor_limit;
    } ThreeMMComposeMovie;
    typedef struct ThreeMMCompositeMovie
    {
        void *scenes, *movies;
    } ThreeMMCompositeMovie;
    enum
    {
        THREE_MM_COMPOSE_MISSING_SCENE,
        THREE_MM_COMPOSE_BAD_BASE,
        THREE_MM_COMPOSE_IMPORT_ACTORS,
        THREE_MM_COMPOSE_REMOVE_ACTORS
    };
    typedef struct ThreeMMMovieComposeServices
    {
        void *context;
        ThreeMMMovieState *data;
        ThreeMMMovieScenes *scenes;
        ThreeMMMovieSceneServices scene_control;
        ThreeMMStorageAllocator allocator;
        ThreeMMChunkGraph graph;
        int temporary_before_save;
        int (*use_temporary)(void *), (*autosave)(void *);
        void *(*new_list)(int movies), *(*duplicate_movies)(void *list);
        ThreeMMI32 (*count)(void *list);
        void (*scene)(void *list, ThreeMMI32, ThreeMMComposeScene *),
            (*movie)(void *list, ThreeMMI32, ThreeMMComposeMovie *);
        int (*insert_scene)(void *list, ThreeMMI32, const ThreeMMComposeScene *),
            (*append_movie)(void *list, const ThreeMMComposeMovie *, ThreeMMI32 *index);
        void (*put_movie)(void *list, ThreeMMI32, const ThreeMMComposeMovie *), (*erase)(void *list, ThreeMMI32);
        void (*retain)(void *), (*release)(void *);
        void *(*file)(void *resources);
        int (*scene_child)(void *file, ThreeMMU32 movie, ThreeMMU32 child, ThreeMMU32 *number);
        int (*read_transition)(void *resources, ThreeMMU32 number, ThreeMMI32 *);
        ThreeMMI32 (*transition)(void *scene);
        void *(*read_thumbnail)(void *file, ThreeMMU32 number), *(*thumbnail)(void *scene);
        int (*clone_movie)(void *source, ThreeMMU32 number, void *destination, ThreeMMU32 *number_out);
        void (*delete_movie)(void *file, ThreeMMU32 number);
        int (*adopt_scene)(void *file, ThreeMMU32 movie, ThreeMMU32 scene, ThreeMMU32 child),
            (*adopt_sounds)(void *, void *file, ThreeMMU32 scene);
        void *(*read_scene)(void *, void *resources, ThreeMMU32 scene);
        int (*start_scene)(void *scene), (*add_actors)(void *scene);
        void (*remove_actors)(void *scene), (*actor_tools)(void *), (*text_tools)(void *);
        void (*delete_scene_child)(void *file, ThreeMMU32 movie, ThreeMMU32 scene, ThreeMMU32 child),
            (*change_child)(void *file, ThreeMMU32 movie, ThreeMMU32 scene, ThreeMMU32 old_child, ThreeMMU32 new_child);
        int (*write_transition)(void *resources, ThreeMMU32 scene, ThreeMMI32);
        void (*set_transition)(void *scene, ThreeMMI32);
        void (*dirty)(void *), (*scene_nuked)(void *), (*invalidate)(void *), (*rollcall_changed)(void *),
            (*diagnostic)(void *, int), (*validate_children)(void *);
        void *(*read_rollcall)(void *resources, ThreeMMU32 movie);
        ThreeMMI32 (*actor_count)(void *table);
        void (*actor)(void *table, ThreeMMI32, ThreeMMRollCallActor *);
        int (*append_actor)(void *destination, void *source, ThreeMMI32 source_index, const ThreeMMRollCallActor *);
        void (*erase_actor)(void *table, ThreeMMI32), (*close_tag)(ThreeMMTag *);
        int (*adjust_actor)(void *file, ThreeMMU32 actor, ThreeMMI32 offset);
    } ThreeMMMovieComposeServices;
    /* Initialized synchronous services, stable owners and valid descriptor indexes
     * are required. Callbacks cannot re-enter editing. Lists own one thumbnail/cache
     * reference per descriptor; duplicate_movies copies borrowed rows without retain.
     * new_list/read_thumbnail/read_scene/read_rollcall return one reference, thumbnail
     * is borrowed. Table destruction releases storage only, never embedded tags.
     * Add requires working resources when temporary_before_save is enabled. On failure
     * it rolls back inserted descriptors and releases empty lists, preserving autosave.
     * Apply keeps completed scene/roll-call/file prefixes on failure; copied Movie roots
     * and cache references are temporary and always cleaned, including failed merge.
     * Import actor-start/registration failures are diagnosed but do not abort apply.
     * The current scene follows its live child slot, excluding deleted descriptors.
     * Merge transfers loaded tag references to appended rows. Failure removes those
     * rows and closes every loaded tag once; adjusted actor-file IDs are not undone.
     * C storage owns the visited-ID set and traversal stack. All actor descendants are
     * walked, matching the native loop's unused skip variable. Traversal error flags
     * retain the native non-fatal policy; visited-set/actor-write failures abort.
     * Empty accepts partial/empty state and releases scenes, then movies, in reverse. */
    int three_mm_composite_insert_scene(void *list, ThreeMMI32, const ThreeMMComposeScene *,
                                        const ThreeMMMovieComposeServices *);
    void three_mm_composite_delete_scene(void *list, ThreeMMI32, const ThreeMMMovieComposeServices *);
    void three_mm_composite_empty(ThreeMMCompositeMovie *, const ThreeMMMovieComposeServices *);
    int three_mm_movie_compose_add(ThreeMMCompositeMovie *, ThreeMMI32 *insertion, const ThreeMMMovieComposeServices *);
    int three_mm_movie_compose_apply(const ThreeMMCompositeMovie *, const ThreeMMMovieComposeServices *);
    int three_mm_movie_compose_merge(ThreeMMU32 movie, ThreeMMI32 actor_offset, const ThreeMMMovieComposeServices *);
#ifdef __cplusplus
}
#endif
#endif
