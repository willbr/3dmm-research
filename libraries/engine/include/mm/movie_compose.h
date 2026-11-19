/* Movie composition converted from engine/movie.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_MOVIE_COMPOSE_H
#define MM_ENGINE_MOVIE_COMPOSE_H
#include "movie_owner.h"
#include "movie_scene.h"
#include "chunk_graph.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMComposeScene
    {
        MMI32 movie;
        MMU32 number, child;
        void *thumbnail;
        MMI32 transition;
        int deleted;
    } MMComposeScene;
    typedef struct MMComposeMovie
    {
        void *resources;
        MMU32 number;
        MMI32 actor_limit;
    } MMComposeMovie;
    typedef struct MMCompositeMovie
    {
        void *scenes, *movies;
    } MMCompositeMovie;
    enum
    {
        MM_COMPOSE_MISSING_SCENE,
        MM_COMPOSE_BAD_BASE,
        MM_COMPOSE_IMPORT_ACTORS,
        MM_COMPOSE_REMOVE_ACTORS
    };
    typedef struct MMMovieComposeServices
    {
        void *context;
        MMMovieState *data;
        MMMovieScenes *scenes;
        MMMovieSceneServices scene_control;
        MMStorageAllocator allocator;
        MMChunkGraph graph;
        int temporary_before_save;
        int (*use_temporary)(void *), (*autosave)(void *);
        void *(*new_list)(int movies), *(*duplicate_movies)(void *list);
        MMI32 (*count)(void *list);
        void (*scene)(void *list, MMI32, MMComposeScene *),
            (*movie)(void *list, MMI32, MMComposeMovie *);
        int (*insert_scene)(void *list, MMI32, const MMComposeScene *),
            (*append_movie)(void *list, const MMComposeMovie *, MMI32 *index);
        void (*put_movie)(void *list, MMI32, const MMComposeMovie *), (*erase)(void *list, MMI32);
        void (*retain)(void *), (*release)(void *);
        void *(*file)(void *resources);
        int (*scene_child)(void *file, MMU32 movie, MMU32 child, MMU32 *number);
        int (*read_transition)(void *resources, MMU32 number, MMI32 *);
        MMI32 (*transition)(void *scene);
        void *(*read_thumbnail)(void *file, MMU32 number), *(*thumbnail)(void *scene);
        int (*clone_movie)(void *source, MMU32 number, void *destination, MMU32 *number_out);
        int (*link_imported_sounds)(void *file, MMU32 movie);
        void (*delete_movie)(void *file, MMU32 number);
        int (*adopt_scene)(void *file, MMU32 movie, MMU32 scene, MMU32 child),
            (*adopt_sounds)(void *, void *file, MMU32 scene);
        void *(*read_scene)(void *, void *resources, MMU32 scene);
        int (*start_scene)(void *scene), (*add_actors)(void *scene);
        void (*remove_actors)(void *scene), (*actor_tools)(void *), (*text_tools)(void *);
        int (*preserve_scene_assets)(void *file, MMU32 scene);
        void (*delete_scene_child)(void *file, MMU32 movie, MMU32 scene, MMU32 child),
            (*change_child)(void *file, MMU32 movie, MMU32 scene, MMU32 old_child, MMU32 new_child);
        int (*write_transition)(void *resources, MMU32 scene, MMI32);
        void (*set_transition)(void *scene, MMI32);
        void (*dirty)(void *), (*scene_nuked)(void *), (*invalidate)(void *), (*rollcall_changed)(void *),
            (*diagnostic)(void *, int), (*validate_children)(void *);
        int (*read_rollcall)(void *resources, MMU32 movie, MMRollCallTable *);
        void (*close_tag)(MMTag *);
        int (*adjust_actor)(void *file, MMU32 actor, MMI32 offset);
        int (*rebind_actor_materials)(void *source, void *destination, MMU32 actor);
        int (*rebind_scene_backgrounds)(void *source, void *destination, MMU32 scene);
    } MMMovieComposeServices;
    /* Initialized synchronous services, stable owners and valid descriptor indexes
     * are required. Callbacks cannot re-enter editing. Lists own one thumbnail/cache
     * reference per descriptor; duplicate_movies copies borrowed rows without retain.
     * new_list/read_thumbnail/read_scene return one reference, thumbnail is borrowed.
     * read_rollcall fills the initialized C table and acquires tags on success.
     * Failure leaves it empty. Table disposal releases storage only.
     * Add requires working resources when temporary_before_save is enabled. On failure
     * it rolls back inserted descriptors and releases empty lists, preserving autosave.
     * Apply keeps completed scene/roll-call/file prefixes on failure; copied Movie roots
     * and cache references are temporary and always cleaned, including failed merge.
     * Adds missing imported scene sound links before transferring roll-call rows.
     * Rebinds cloned scenes' document backgrounds and actors' document materials
     * once before adopting scenes. That import pass aborts on traversal failure.
     * Import actor-start/registration failures are diagnosed but do not abort apply.
     * The current scene follows its live child slot, excluding deleted descriptors.
     * Merge transfers loaded tag references to appended rows. Failure removes those
     * rows and closes every loaded tag once; adjusted actor-file IDs are not undone.
     * C storage owns the visited-ID set and traversal stack. All actor descendants are
     * walked, matching the native loop's unused skip variable. Traversal error flags
     * retain the native non-fatal policy for ID-only remapping. Visited-set and
     * actor-write failures abort.
     * Empty accepts partial/empty state and releases scenes, then movies, in reverse. */
    int mm_composite_insert_scene(void *list, MMI32, const MMComposeScene *,
                                        const MMMovieComposeServices *);
    void mm_composite_delete_scene(void *list, MMI32, const MMMovieComposeServices *);
    void mm_composite_empty(MMCompositeMovie *, const MMMovieComposeServices *);
    int mm_movie_compose_add(MMCompositeMovie *, MMI32 *insertion, const MMMovieComposeServices *);
    int mm_movie_compose_apply(const MMCompositeMovie *, const MMMovieComposeServices *);
    int mm_movie_compose_merge(MMU32 movie, MMI32 actor_offset, const MMMovieComposeServices *);
#ifdef __cplusplus
}
#endif
#endif
