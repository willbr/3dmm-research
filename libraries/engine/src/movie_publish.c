/* Licensed under the MIT License. */
#include "mm/movie_publish.h"
#include "mm/vxp_package.h"
#include <stddef.h>

MMMovieCopyResult mm_movie_copy_format(const char *suffix, int needs_bundle,
                                       MMMovieCopyFormat *format, const char **append_suffix)
{
    MMMovieCopyFormat selected;
    const char *append = NULL;
    if (!*suffix)
    {
        selected = needs_bundle ? MM_MOVIE_COPY_BUNDLE : MM_MOVIE_COPY_NATIVE;
        append = needs_bundle ? ".vmm" : ".3mm";
    }
    else if (!mm_vxp_compare_names(suffix, ".vmm"))
        selected = MM_MOVIE_COPY_BUNDLE;
    else if (!mm_vxp_compare_names(suffix, ".3mm"))
    {
        if (needs_bundle) return MM_MOVIE_COPY_BUNDLE_REQUIRED;
        selected = MM_MOVIE_COPY_NATIVE;
    }
    else return MM_MOVIE_COPY_BAD_EXTENSION;
    *format = selected;
    *append_suffix = append;
    return MM_MOVIE_COPY_OK;
}

MMMoviePublishResult mm_movie_publish_save(const MMMoviePublishServices *services)
{
    void *context = services->context;
    MMMovieStageResult stage = MM_MOVIE_STAGE_COLLISION;
    MMMoviePublishResult result;
    int attempt;
    for (attempt = 0; attempt < MM_MOVIE_STAGE_ATTEMPTS && stage == MM_MOVIE_STAGE_COLLISION; ++attempt)
        stage = services->reserve(context);
    if (stage != MM_MOVIE_STAGE_OK) return MM_MOVIE_PUBLISH_STAGE_FAILED;
    if (!services->write(context)) result = MM_MOVIE_PUBLISH_WRITE_FAILED;
    else if (!services->replace(context)) result = MM_MOVIE_PUBLISH_REPLACE_FAILED;
    else result = MM_MOVIE_PUBLISH_OK;
    services->discard(context);
    return result;
}
