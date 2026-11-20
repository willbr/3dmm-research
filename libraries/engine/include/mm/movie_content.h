/* Movie content transactions converted from engine/movie.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_MOVIE_CONTENT_H
#define MM_ENGINE_MOVIE_CONTENT_H
#include "movie_edit.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMMovieContentServices
    {
        MMMovieEditServices edit;
        int (*ensure)(void *, void **resources), (*make_valid)(void *), (*use_temp)(void *);
        void *(*file)(void *resources);
        int (*write_material)(void *material, void *file, MMU32 *number);
        void *(*new_text)(const void *name, MMI32 shape, const void *font);
        int (*write_text)(void *text, void *file, MMU32 *number);
        void (*release_text)(void *text);
        int (*open_tag)(MMTag *, void *resources);
        void (*close_tag)(MMTag *);
        const void *(*name_units)(const void *name, MMU32 *count);
        MMI32 (*character)(const void *name, MMI32 index);
        int (*change_template)(void *actor, const MMTag *);
        void (*restore_template_edit)(void *actor, void *snapshot); /* Studio template/body checks on rollback. */
        void *(*new_caption)(void *scene, const void *bounds, int story);
        MMI32 (*default_font)(void *);
        void (*caption_font)(void *caption, MMI32);
        int (*add_caption)(void *scene, void *caption), (*remove_caption)(void *scene, void *caption);
        void (*select_caption)(void *scene, void *caption), (*attach_caption)(void *caption),
            (*release_caption)(void *caption);
        void *(*selected_caption)(void *scene), *(*caption_at)(void *scene, MMI32 index);
        int (*hide_caption)(void *caption), (*visible_caption)(void *caption);
        void (*missing_caption)(void *);
    } MMMovieContentServices;
    /* Initialized synchronous services and stable Movie/Scene owners are required.
     * Names, bounds and font tags are borrowed host representations; new_text and
     * new_caption return one owned reference. Scene add retains its own reference.
     * open_tag returns one owned tag on success and no reference on failure.
     * Material output is defined once writing succeeds, including failed open_tag;
     * before that it is untouched and written=0 (native hosts may poison output).
     * Text creation captures the working file before use_temp, preserving the native
     * transaction order. Failed writes/edits keep their authored chunk prefixes, but
     * release temporary templates, tags and actor snapshots on every exit.
     * Text change requires actor to be selected. Only ASCII spaces count as blank;
     * blank deletion does not inspect the font input.
     * Failed new roll-call insertion restores the actor but keeps the Movie ID
     * limit increment, so the consumed ID is not reused.
     * Indexed caption selection scans visible captions in storage order and leaves
     * selection unchanged for negative/out-of-range indexes. */
    int mm_movie_insert_material(void *material, MMTag *output, int *written,
                                       const MMMovieContentServices *);
    int mm_movie_insert_text(const void *name, MMI32 shape, const void *font,
                                   const MMMovieContentServices *);
    int mm_movie_change_text(void *actor, const void *name, MMI32 shape, const void *font,
                                   const MMMovieContentServices *);
    int mm_movie_insert_caption(const void *bounds, int story, const MMMovieContentServices *);
    int mm_movie_remove_caption(int hide, const MMMovieContentServices *);
    void mm_movie_select_caption(MMI32 index, const MMMovieContentServices *);
#ifdef __cplusplus
}
#endif
#endif
