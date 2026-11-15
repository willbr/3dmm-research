/* Movie content transactions converted from engine/movie.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef THREE_MM_ENGINE_MOVIE_CONTENT_H
#define THREE_MM_ENGINE_MOVIE_CONTENT_H
#include "movie_edit.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct ThreeMMMovieContentServices
    {
        ThreeMMMovieEditServices edit;
        int (*ensure)(void *, void **resources), (*make_valid)(void *), (*use_temp)(void *);
        void *(*file)(void *resources);
        int (*write_material)(void *material, void *file, ThreeMMU32 *number);
        void *(*new_text)(const void *name, ThreeMMI32 shape, const void *font);
        int (*write_text)(void *text, void *file, ThreeMMU32 *number);
        void (*release_text)(void *text);
        int (*open_tag)(ThreeMMTag *, void *resources);
        void (*close_tag)(ThreeMMTag *);
        ThreeMMI32 (*length)(const void *name), (*character)(const void *name, ThreeMMI32 index);
        int (*change_template)(void *actor, const ThreeMMTag *);
        void *(*new_caption)(void *scene, const void *bounds, int story);
        ThreeMMI32 (*default_font)(void *);
        void (*caption_font)(void *caption, ThreeMMI32);
        int (*add_caption)(void *scene, void *caption), (*remove_caption)(void *scene, void *caption);
        void (*select_caption)(void *scene, void *caption), (*attach_caption)(void *caption),
            (*release_caption)(void *caption);
        void *(*selected_caption)(void *scene), *(*caption_at)(void *scene, ThreeMMI32 index);
        int (*hide_caption)(void *caption), (*visible_caption)(void *caption);
        void (*missing_caption)(void *);
    } ThreeMMMovieContentServices;
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
    int three_mm_movie_insert_material(void *material, ThreeMMTag *output, int *written,
                                       const ThreeMMMovieContentServices *);
    int three_mm_movie_insert_text(const void *name, ThreeMMI32 shape, const void *font,
                                   const ThreeMMMovieContentServices *);
    int three_mm_movie_change_text(void *actor, const void *name, ThreeMMI32 shape, const void *font,
                                   const ThreeMMMovieContentServices *);
    int three_mm_movie_insert_caption(const void *bounds, int story, const ThreeMMMovieContentServices *);
    int three_mm_movie_remove_caption(int hide, const ThreeMMMovieContentServices *);
    void three_mm_movie_select_caption(ThreeMMI32 index, const ThreeMMMovieContentServices *);
#ifdef __cplusplus
}
#endif
#endif
