/* Movie composition converted from engine/movie.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_MOVIE_COMPOSE_H
#define MM_ENGINE_MOVIE_COMPOSE_H
#include "movie_owner.h"
#include "movie_scene.h"
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
        MMCollectionStorage rows; /* Scene descriptors in buffer 0, movies in buffer 1. */
        MMU32 scene_count, movie_count;
    } MMCompositeMovie;
    enum
    {
        MM_COMPOSE_MISSING_SCENE,
        MM_COMPOSE_BAD_BASE,
        MM_COMPOSE_IMPORT_ACTORS,
        MM_COMPOSE_REMOVE_ACTORS,
        MM_COMPOSE_MISSING_THUMBNAIL,
        MM_COMPOSE_SCENE_ENUM,
        MM_COMPOSE_SCENE_ORDER
    };
    typedef struct MMMovieComposeServices
    {
        void *context;
        MMMovieState *data;
        MMMovieScenes *scenes;
        MMMovieSceneServices scene_control;
        MMStorageAllocator allocator;
        MMArchiveWriter (*archive)(void *file);
        MMChunkTitleIO titles;
        MMRollCallBinding rollcall;
        int temporary_before_save, validate_children;
        int (*use_temporary)(void *), (*autosave)(void *);
        void (*retain)(void *), (*release)(void *);
        MMI32 (*transition)(void *scene);
        void *(*decode_thumbnail)(void *, const MMReadStream *);
        void *(*thumbnail)(void *scene);
        int (*clone_movie)(void *source, MMU32 number, void *destination, MMU32 *number_out);
        void (*sound_issue)(void *, int);
        int (*start_scene)(void *scene), (*add_actors)(void *scene);
        void (*remove_actors)(void *scene), (*actor_tools)(void *), (*text_tools)(void *);
        void (*set_transition)(void *scene, MMI32);
        void (*dirty)(void *), (*scene_nuked)(void *), (*invalidate)(void *), (*rollcall_changed)(void *),
            (*diagnostic)(void *, int);
        int (*copy_reference)(void *source, void *destination, MMU32 type, MMU32 number,
                              MMU32 *copied, int *created);
    } MMMovieComposeServices;
    /* Start with a zeroed composite. It owns one thumbnail/cache reference per
     * descriptor. Accessors copy the row with borrowed reference fields.
     * Move inserts before the target in the original order. Remove marks the scene
     * and moves it behind the remaining rows for later application.
     * Empty releases scene refs, then movie refs in reverse order and frees storage. */
    int mm_composite_scene(const MMCompositeMovie *, MMU32 index, MMComposeScene *);
    int mm_composite_movie(const MMCompositeMovie *, MMU32 index, MMComposeMovie *);
    int mm_composite_move_scene(MMCompositeMovie *, MMU32 from, MMU32 before);
    int mm_composite_remove_scene(MMCompositeMovie *, MMU32 index);
    int mm_composite_set_transition(MMCompositeMovie *, MMU32 index, MMI32 transition);
    void mm_composite_empty(MMCompositeMovie *, void (*release)(void *));
    /* Initialized synchronous services and stable owners are required. Callbacks
     * cannot re-enter editing. Decode_thumbnail/read return one reference,
     * thumbnail is borrowed.
     * The roll-call binding reads the cloned root from the working cache.
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
     * actor-write failures abort. */
    int mm_movie_compose_add(MMCompositeMovie *, MMI32 *insertion, const MMMovieComposeServices *);
    int mm_movie_compose_apply(const MMCompositeMovie *, const MMMovieComposeServices *);
    int mm_movie_compose_merge(MMU32 movie, MMI32 actor_offset, const MMMovieComposeServices *);
#ifdef __cplusplus
}
#endif
#endif
