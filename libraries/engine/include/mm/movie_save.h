/* Movie save transactions converted from engine/movie.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_MOVIE_SAVE_H
#define MM_ENGINE_MOVIE_SAVE_H
#include "movie_owner.h"
#include "chunk_owner.h"
#include "movie_scene.h"
#include "collections.h"
#include "chunky_file.h"
#include "movie_session.h"
#include "source_names.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum
    {
        MM_SAVE_SCENE = 0x5343454e,
        MM_SAVE_TABLE = 0x47535420,
        MM_SAVE_MATERIAL = 0x4d54524c,
        MM_SAVE_TEMPLATE = 0x544d504c,
        MM_SAVE_BUNDLE = 0x564d4d
    };
    typedef struct MMMovieContainerServices
    {
        void (*release)(void *document);
        int (*bundled)(void *document);
        int (*package)(void *document, void *working, void *destination);
        int (*commit)(void *document);
    } MMMovieContainerServices;
    /* The movie owns the document. Its service table must outlive the movie. */
    typedef struct MMMovieContainer
    {
        void *document;
        const MMMovieContainerServices *services;
    } MMMovieContainer;
    typedef struct MMMovieSaveState
    {
        void *file;
        MMMovieContainer container;
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
        void *(*create_resources)(void *, void *path);
        int (*save)(void *resources, void *path);
        /* NULL storage, archive and tag_graph bindings use the live resource cache. */
        MMChunkOwnerStorage *(*archive_storage)(void *resources);
        MMArchiveWriter (*archive)(void *resources);
        const MMChunkOwnerFileFlags *file_flags;
        const MMChunkErrorServices *file_errors;
        void (*archive_options_issue)(void *);
        void *(*find_file)(void *, void *path);
        void (*retain_saved)(void *file), (*release_saved)(void *file), (*flush_saved)(void *file);
        int (*read_only)(void *, void *path);
        void (*get_name)(void *file, void *output), (*copy_path)(void *input, void *output);
        void *(*container_name)(void *);
        void (*set_container_name)(void *, void *path);
        void (*long_op)(void *, int begin), (*document_dirty)(void *, int);
        int (*write_scene)(void *, void *scene, MMU32 *);
        void (*archive_issue)(void *, int issue);
        const MMTagServices *tags;
        void (*rollcall_changed)(void *);
        MMSourceNames *source_names;
        MMChunkTitleIO titles;
        void (*clear_errors)(void *), (*save_error)(void *), (*bad_file)(void *);
        void (*update_title)(void *, void *path), (*clear_undo)(void *);
        int (*is_vmm)(void *path), (*source_archive)(void *, MMI32 source);
        MMMovieSessionGraph (*tag_graph)(void *resources);
        void (*actor_tag_issue)(void *, int issue), (*scene_tag_issue)(void *, int start, int missing_background);
        MMResourceBlockReader material_reader, template_reader;
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
     * The resource factory transfers one working-cache reference.
     * New resources remain published if their initial autosave fails.
     * Registry lookup returns borrowed values. Named saving releases/reacquires a matching saved file even on
     * save failure; unsuccessful bind retains the old nameValid/readOnly flags.
     * Source names are borrowed throughout autosave. Each write owns the
     * destination block until its payload has been serialized.
     * Cleanup preserves completed destructive prefixes, including actor pruning.
     * Pruned rows release their tags after removal. Asset cleanup uses the readers
     * that identify cached resources in the active document.
     * After old children are removed, physical save failure keeps the new graph.
     * Native debug/physical I/O diagnostics remain host operations. */
    void mm_movie_save_init(MMMovieSaveState *);
    /* True when the host committed a change to the active source set. */
    int mm_movie_container_commit(const MMMovieContainer *);
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
    int mm_movie_save_assets(void *resources, const MMMovieSaveServices *);
    void mm_movie_save_sounds(int purge_all, const MMMovieSaveServices *);
    int mm_movie_save_gc(void *resources, const MMMovieSaveServices *);
#ifdef __cplusplus
}
#endif
#endif
