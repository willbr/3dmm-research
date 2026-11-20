/* Live movie lifecycle converted from engine/movie.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_MOVIE_RUNTIME_H
#define MM_ENGINE_MOVIE_RUNTIME_H
#include "movie_save.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMMovieRuntime
    {
        void *client, *world, *sounds, *thumbnail_palette;
        int user_paused;
    } MMMovieRuntime;
    typedef struct MMMovieRuntimeServices
    {
        void *context;
        const MMMovieOwnerServices *owner;
        const MMMovieSaveServices *save;
        const MMMovieSceneServices *scenes;
        int (*prepare)(void *, void *original, void **working);
        int (*init_undo)(void *);
        void *(*create_world)(void *, void *client, int half), *(*create_sounds)(void *);
        void (*retain)(void *), (*release)(void *);
        void (*discard_document)(void *);
        void (*retain_archive)(void *file);
        void (*missing_saved_file)(void *);
        void (*merge_sources)(void *file, MMU32 root);
        int (*cache_tags)(void *, MMTagList *);
        void (*resume_audio)(void *), (*stop_playback)(void *client);
        int (*audio_fades_disabled)(void *);
        void (*set_volume)(void *, MMI32);
        void (*update_roll_call)(void *client);
    } MMMovieRuntimeServices;
    /* Runtime starts zeroed. Factories transfer a single reference and prepare
     * returns a borrowed working path (NULL for a blank document). Open retains
     * the client. On failure it releases the temporary archive reference and
     * discards the partial document before ending the long operation. Service
     * storage and the long-op provider must survive document destruction. */
    int mm_movie_runtime_open(MMMovieRuntime *, void *client, void *path, MMU32 root, int half,
                              const MMMovieRuntimeServices *);
    /* Drop the archive/cache and save reference before stopping playback.
     * Scene and roll-call teardown still have access to the host references.
     * Callbacks may observe state but must not replace its owned references. */
    void mm_movie_runtime_dispose(MMMovieRuntime *, MMPlaybackState *, const MMMovieRuntimeServices *);
    void mm_movie_runtime_set_ref(void **slot, void *value, void (*retain)(void *), void (*release)(void *));
#ifdef __cplusplus
}
#endif
#endif
