/* Licensed under the MIT License. */
#ifndef MM_ENGINE_MOVIE_PUBLISH_H
#define MM_ENGINE_MOVIE_PUBLISH_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum MMMovieCopyFormat
{
    MM_MOVIE_COPY_NATIVE,
    MM_MOVIE_COPY_BUNDLE
} MMMovieCopyFormat;

typedef enum MMMovieCopyResult
{
    MM_MOVIE_COPY_OK,
    MM_MOVIE_COPY_BAD_EXTENSION,
    MM_MOVIE_COPY_BUNDLE_REQUIRED
} MMMovieCopyResult;

/* The suffix includes its leading dot or is empty. Publish the format and
 * optional suffix to append only on success. */
MMMovieCopyResult mm_movie_copy_format(const char *suffix, int needs_bundle,
                                       MMMovieCopyFormat *, const char **append_suffix);

typedef enum MMMovieStageResult
{
    MM_MOVIE_STAGE_OK,
    MM_MOVIE_STAGE_COLLISION,
    MM_MOVIE_STAGE_ERROR
} MMMovieStageResult;

typedef enum MMMoviePublishResult
{
    MM_MOVIE_PUBLISH_OK,
    MM_MOVIE_PUBLISH_STAGE_FAILED,
    MM_MOVIE_PUBLISH_WRITE_FAILED,
    MM_MOVIE_PUBLISH_REPLACE_FAILED
} MMMoviePublishResult;

enum { MM_MOVIE_STAGE_ATTEMPTS = 16 };

typedef struct MMMoviePublishServices
{
    void *context;
    /* Only OK grants ownership of an exclusive staging location. */
    MMMovieStageResult (*reserve)(void *);
    /* Write and close a complete candidate, then atomically replace the output. */
    int (*write)(void *);
    int (*replace)(void *);
    /* Remove the owned staging location, including any partial candidate.
     * After successful replacement the published file belongs to the caller. */
    void (*discard)(void *);
} MMMoviePublishServices;

/* Retry collisions only. Write, replace and discard are synchronous. Discard
 * follows every successful reservation, including failed writes/replacements. */
MMMoviePublishResult mm_movie_publish_save(const MMMoviePublishServices *);

#ifdef __cplusplus
}
#endif
#endif
