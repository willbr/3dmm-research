/* Movie save transactions converted from engine/movie.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_MOVIE_SAVE_H
#define MM_ENGINE_MOVIE_SAVE_H
#include "movie_owner.h"
#include "movie_scene.h"
#include "collections.h"
#include "chunky_file.h"
#include "movie_session.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum
    {
        MM_SAVE_SCENE = 0x5343454e,
        MM_SAVE_TABLE = 0x47535420,
        MM_SAVE_MATERIAL = 0x4d54524c,
        MM_SAVE_TEMPLATE = 0x544d504c
    };
    typedef struct MMMovieSaveState
    {
        void *file;
        int autosaveDirty, nameValid, closing, readOnly, containerNamed;
    } MMMovieSaveState;
    typedef struct MMMovieSaveServices
    {
        void *context;
        MMMovieState *data;
        MMMovieScenes *scenes;
        MMMovieSaveState *state;
        MMStorageAllocator allocator;
        int retry_enabled;
        void *(*temporary_path)(void *), *(*unique_path)(void *, void *destination);
        void *(*file)(void *resources), *(*create_file)(void *, void *path);
        void (*release_archive)(void *file);
        void *(*new_resources)(void *, void *file, MMU32 cache_bytes);
        int (*is_temp)(void *file), (*save)(void *file, void *path);
        void (*set_temp)(void *file, int), (*mark_temporary)(void *file), (*persistent)(void *file);
        MMChunkAccess (*chunks)(void *file);
        int (*create_root)(void *, void *file, MMU32 *);
        void *(*find_file)(void *, void *path);
        void (*retain_saved)(void *file), (*release_saved)(void *file), (*flush_saved)(void *file);
        int (*read_only)(void *, void *path);
        void (*get_name)(void *file, void *output), (*copy_path)(void *input, void *output);
        void *(*container_name)(void *);
        void (*set_container_name)(void *, void *path);
        void (*long_op)(void *, int begin), (*document_dirty)(void *, int);
        int (*write_scene)(void *, void *scene, MMU32 *);
        int (*adopt)(void *file, MMU32 root, MMU32 type, MMU32 number, MMU32 slot);
        int (*preserve_scene_assets)(void *file, MMU32 scene);
        void (*delete_child)(void *file, MMU32 root, MMU32 type, MMU32 number, MMU32 slot);
        void (*delete_chunk)(void *file, MMU32 type, MMU32 number);
        void (*close_tag)(void *, MMTag *);
        void (*rollcall_changed)(void *);
        int (*write_rollcall)(void *, void *file, MMU32 root, MMRollCallTable *, MMU32 *);
        MMU32 (*source_bytes)(void *);
        int (*add_sources)(void *, void *file, MMU32 bytes, MMU32 *), (*write_sources)(void *);
        void (*release_sources)(void *);
        MMChunkTitleIO titles;
        int (*has_error)(void *file);
        void (*reset_error)(void *file), (*clear_errors)(void *), (*save_error)(void *), (*bad_file)(void *);
        void (*update_title)(void *, void *path), (*clear_undo)(void *);
        int (*has_container)(void *), (*bundled)(void *), (*is_vmm)(void *path);
        int (*package)(void *, void *working, void *destination), (*source_archive)(void *, MMI32 source);
        MMMovieSessionGraph (*tag_graph)(void *file);
        void (*actor_tag_issue)(void *, int issue), (*scene_tag_issue)(void *, int start, int missing_background);
        void (*toss_cache)(void *, MMU32 type, MMU32 number);
        int (*purge_sounds)(void *);
        void (*bad_child)(void *), (*bad_sound_index)(void *), (*missing_scene)(void *), (*missing_root)(void *);
    } MMMovieSaveServices;
    /* State owns the saved-file reference; resources remain owned by MovieState.
     * States must be initialized and invoked callbacks present. Temporary-file,
     * tag and asset collection operations require existing working resources.
     * A zero allocator selects the C heap; otherwise both callbacks are required.
     * Service storage and borrowed paths/files survive each synchronous transaction.
     * Callbacks cannot re-enter editing. Separate temporary/unique path scratch is
     * required: a named VMM save autosaves into its temporary path and may retry.
     * File creation returns one reference, new_resources retains it (zero cache).
     * New resources remain published if their initial autosave fails.
     * Registry lookup returns borrowed values. Named saving releases/reacquires a matching saved file even on
     * save failure; unsuccessful bind retains the old nameValid/readOnly flags.
     * Source-block allocation publishes host scratch consumed by write_sources;
     * release_sources clears it after the long operation ends, including failure,
     * before nested autosave returns to packaging/format-selection/publication.
     * Cleanup preserves completed destructive prefixes, including actor pruning.
     * After old children are removed, physical save failure keeps the new graph.
     * Native debug/physical I/O diagnostics remain host operations. */
    void mm_movie_save_init(MMMovieSaveState *);
    void mm_movie_save_release(int clear_name, const MMMovieSaveServices *);
    int mm_movie_save_bind(void *path, const MMMovieSaveServices *);
    void mm_movie_save_flush(const MMMovieSaveServices *);
    int mm_movie_save_name(void *output, const MMMovieSaveServices *);
    MMI32 mm_movie_save_command(MMI32 command, MMI32 save, MMI32 save_as,
                                           const MMMovieSaveState *);
    int mm_movie_save_temporary(const MMMovieSaveServices *);
    int mm_movie_save_resources(const MMMovieSaveServices *);
    int mm_movie_save_ensure(void *output, void (*publish)(void *, void *resources),
                                   const MMMovieSaveServices *);
    int mm_movie_autosave(void *path, int clean_rollcall, const MMMovieSaveServices *);
    int mm_movie_save_to(void *path, int remember, const MMMovieSaveServices *);
    int mm_movie_needs_container(const MMMovieSaveServices *);
    /* Collect into an uninitialized list. Success transfers the list to the
     * caller. Failure disposes partial tags. Cache pointers remain borrowed. */
    int mm_movie_save_tags(MMTagList *, const MMMovieSaveServices *);
    int mm_movie_save_assets(void *file, const MMMovieSaveServices *);
    void mm_movie_save_sounds(int purge_all, const MMMovieSaveServices *);
    int mm_movie_save_gc(void *file, const MMMovieSaveServices *);
#ifdef __cplusplus
}
#endif
#endif
