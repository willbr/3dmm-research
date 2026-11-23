/* Movie content transactions converted from engine/movie.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#ifndef MM_ENGINE_MOVIE_CONTENT_H
#define MM_ENGINE_MOVIE_CONTENT_H
#include "material_resource.h"
#include "movie_edit.h"
#include "text_resource.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct MMMovieContentServices
    {
        MMMovieEditServices edit;
        const struct MMMovieSaveServices *working_save;
        int (*ensure)(void *, MMResourceCache **resources);
        int (*make_valid)(void *), (*use_temp)(void *);
        const MMTemplateOwnerHost *text_host;
        MMTextGenerationServices text_generation;
        const MMTagServices *tags;
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
        const struct MMSceneContentServices *caption_scene;
    } MMMovieContentServices;
    /* Initialized synchronous services and stable Movie/Scene owners are required.
     * Counted names, caption bounds and font tags are borrowed. New captions
     * return one owned reference. Scene add retains its own reference.
     * Authored local tags retain the active document cache. Material output is
     * defined once writing succeeds. Before that it is untouched and written=0
     * (native hosts may poison output).
     * Material and text use the working save or the alternate preparation
     * callbacks. Both capture the working cache before making it temporary.
     * Failed writes/edits keep their authored chunk prefixes, but
     * release temporary templates, tags and actor snapshots on every exit.
     * Text change requires actor to be selected. Only ASCII spaces count as blank;
     * blank deletion does not inspect the font input.
     * Failed new roll-call insertion restores the actor but keeps the Movie ID
     * limit increment, so the consumed ID is not reused.
     * Caption scene edits and selection use the live scene binding when supplied.
     * Indexed caption selection scans visible captions in storage order and leaves
     * selection unchanged for negative/out-of-range indexes. */
    int mm_movie_insert_material(MMMaterialOwner *material, MMTag *output, int *written,
                                       const MMMovieContentServices *);
    int mm_movie_insert_text(const MMStringValue *name, MMI32 shape, const MMTag *font,
                                   const MMMovieContentServices *);
    int mm_movie_change_text(void *actor, const MMStringValue *name, MMI32 shape, const MMTag *font,
                                   const MMMovieContentServices *);
    int mm_movie_insert_caption(const void *bounds, int story, const MMMovieContentServices *);
    int mm_movie_remove_caption(int hide, const MMMovieContentServices *);
    void mm_movie_select_caption(MMI32 index, const MMMovieContentServices *);
#ifdef __cplusplus
}
#endif
#endif
