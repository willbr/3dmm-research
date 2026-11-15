/* Movie save transactions converted from engine/movie.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_MOVIE_SAVE_H
#define THREE_MM_ENGINE_MOVIE_SAVE_H
#include "movie_owner.h"
#include "movie_scene.h"
#include "collections.h"
#include "chunky_file.h"
#ifdef __cplusplus
extern "C"
{
#endif
    enum
    {
        THREE_MM_SAVE_SCENE = 0x5343454e,
        THREE_MM_SAVE_TABLE = 0x47535420,
        THREE_MM_SAVE_MATERIAL = 0x4d54524c,
        THREE_MM_SAVE_TEMPLATE = 0x544d504c,
        THREE_MM_SAVE_SOUND = 0x4d534e44
    };
    typedef struct ThreeMMMovieSaveState
    {
        void *file;
        int autosaveDirty, nameValid, closing, readOnly, containerNamed;
    } ThreeMMMovieSaveState;
    typedef struct ThreeMMMovieSaveServices
    {
        void *context;
        ThreeMMMovieState *data;
        ThreeMMMovieScenes *scenes;
        ThreeMMMovieSaveState *state;
        ThreeMMStorageAllocator allocator;
        int retry_enabled;
        void *(*temporary_path)(void *), *(*unique_path)(void *, void *destination);
        void *(*file)(void *resources), *(*create_file)(void *, void *path);
        void (*release_archive)(void *file);
        void *(*new_resources)(void *, void *file, ThreeMMU32 cache_bytes);
        int (*is_temp)(void *file), (*save)(void *file, void *path);
        void (*set_temp)(void *file, int), (*mark_temporary)(void *file), (*persistent)(void *file);
        ThreeMMU32 (*first_root)(void *file);
        int (*create_root)(void *, void *file, ThreeMMU32 *);
        void *(*find_file)(void *, void *path);
        void (*retain_saved)(void *file), (*release_saved)(void *file), (*flush_saved)(void *file);
        int (*read_only)(void *, void *path);
        void (*get_name)(void *file, void *output), (*copy_path)(void *input, void *output);
        void *(*container_name)(void *);
        void (*set_container_name)(void *, void *path);
        void (*long_op)(void *, int begin), (*document_dirty)(void *, int);
        int (*write_scene)(void *, void *scene, ThreeMMU32 *);
        int (*child)(void *file, ThreeMMU32 root, ThreeMMU32 type, ThreeMMU32 slot, ThreeMMU32 *);
        int (*adopt)(void *file, ThreeMMU32 root, ThreeMMU32 type, ThreeMMU32 number, ThreeMMU32 slot);
        void (*delete_child)(void *file, ThreeMMU32 root, ThreeMMU32 type, ThreeMMU32 number, ThreeMMU32 slot);
        void (*delete_chunk)(void *file, ThreeMMU32 type, ThreeMMU32 number);
        ThreeMMI32 (*actor_count)(void *table), (*actor_refs)(void *table, ThreeMMI32 index, ThreeMMTag *);
        void (*erase_actor)(void *table, ThreeMMI32 index), (*close_tag)(void *, ThreeMMTag *);
        void (*rollcall_changed)(void *);
        int (*write_rollcall)(void *, void *file, ThreeMMU32 root, void *table, ThreeMMU32 *);
        ThreeMMU32 (*source_bytes)(void *);
        int (*add_sources)(void *, void *file, ThreeMMU32 bytes, ThreeMMU32 *), (*write_sources)(void *);
        void (*release_sources)(void *);
        int (*write_title)(void *, void *file);
        int (*has_error)(void *file);
        void (*reset_error)(void *file), (*clear_errors)(void *), (*save_error)(void *), (*bad_file)(void *);
        void (*update_title)(void *, void *path), (*clear_undo)(void *);
        int (*has_container)(void *), (*bundled)(void *), (*is_vmm)(void *path);
        int (*package)(void *, void *working, void *destination), (*source_archive)(void *, ThreeMMI32 source);
        void *(*new_tags)(void *);
        int (*scene_tags)(void *file, ThreeMMU32 scene, void *tags);
        ThreeMMI32 (*tag_count)(void *tags);
        void (*tag)(void *tags, ThreeMMI32 index, ThreeMMTag *), (*release_tags)(void *tags);
        int (*chunk)(void *file, ThreeMMU32 type, ThreeMMI32 index, ThreeMMU32 *);
        void (*toss_cache)(void *, ThreeMMU32 type, ThreeMMU32 number);
        ThreeMMI32 (*child_count)(void *file, ThreeMMU32 root),
            (*references)(void *file, ThreeMMU32 type, ThreeMMU32 number);
        int (*child_at)(void *file, ThreeMMU32 root, ThreeMMI32 index, ThreeMMChunkChild *);
        int (*sound_child)(void *file, ThreeMMU32 number), (*unused_sounds)(void *, int *valid),
            (*purge_sounds)(void *);
        void (*bad_child)(void *), (*missing_scene)(void *);
    } ThreeMMMovieSaveServices;
    /* State owns the saved-file reference; resources remain owned by MovieState.
     * States must be initialized and invoked callbacks present. Temporary-file,
     * tag and asset collection operations require existing working resources.
     * A zero allocator selects the C heap; otherwise both callbacks are required.
     * Service storage and borrowed paths/files survive each synchronous transaction.
     * Callbacks cannot re-enter editing. Separate temporary/unique path scratch is
     * required: a named VMM save autosaves into its temporary path and may retry.
     * File creation returns one reference, new_resources retains it (zero cache).
     * New resources remain published if their initial autosave fails.
     * Registry lookup and tag traversal return borrowed values; new_tags owns one
     * reference. Named saving releases/reacquires a matching saved file even on
     * save failure; unsuccessful bind retains the old nameValid/readOnly flags.
     * Source-block allocation publishes host scratch consumed by write_sources;
     * release_sources clears it after the long operation ends, including failure,
     * before nested autosave returns to packaging/format-selection/publication.
     * Cleanup preserves completed destructive prefixes, including actor pruning.
     * After old children are removed, physical save failure keeps the new graph.
     * Native debug/physical I/O diagnostics remain host operations. */
    void three_mm_movie_save_init(ThreeMMMovieSaveState *);
    void three_mm_movie_save_release(int clear_name, const ThreeMMMovieSaveServices *);
    int three_mm_movie_save_bind(void *path, const ThreeMMMovieSaveServices *);
    void three_mm_movie_save_flush(const ThreeMMMovieSaveServices *);
    int three_mm_movie_save_name(void *output, const ThreeMMMovieSaveServices *);
    ThreeMMI32 three_mm_movie_save_command(ThreeMMI32 command, ThreeMMI32 save, ThreeMMI32 save_as,
                                           const ThreeMMMovieSaveState *);
    int three_mm_movie_save_temporary(const ThreeMMMovieSaveServices *);
    int three_mm_movie_save_resources(const ThreeMMMovieSaveServices *);
    int three_mm_movie_save_ensure(void *output, void (*publish)(void *, void *resources),
                                   const ThreeMMMovieSaveServices *);
    int three_mm_movie_autosave(void *path, int clean_rollcall, const ThreeMMMovieSaveServices *);
    int three_mm_movie_save_to(void *path, int remember, const ThreeMMMovieSaveServices *);
    int three_mm_movie_needs_container(const ThreeMMMovieSaveServices *);
    void *three_mm_movie_save_tags(const ThreeMMMovieSaveServices *);
    int three_mm_movie_save_assets(void *file, const ThreeMMMovieSaveServices *);
    void three_mm_movie_save_sounds(int purge_all, const ThreeMMMovieSaveServices *);
    int three_mm_movie_save_gc(void *file, const ThreeMMMovieSaveServices *);
#ifdef __cplusplus
}
#endif
#endif
