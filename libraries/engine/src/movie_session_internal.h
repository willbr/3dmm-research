#ifndef MM_MOVIE_SESSION_INTERNAL_H
#define MM_MOVIE_SESSION_INTERNAL_H

#include "mm/movie_session.h"
#include "mm/collections.h"

struct MMMovieSession
{
    MMReadStream stream;
    MMChunkIndex *index;
    MMChunkPolicy policy;
    MMU32 root_position;
    MMMovieSessionRoot root;
};

typedef struct MMMoviePayload
{
    MMReadSlice slice;
    MMMemoryStream memory;
    unsigned char *decoded;
    const MMReadStream *stream;
} MMMoviePayload;

typedef struct MMMovieGroup
{
    MMCollectionHeader header;
    unsigned char *bytes;
    const unsigned char *records, *locations;
} MMMovieGroup;

MMU32 mm_session_word(const unsigned char *, int big);
int mm_session_payload(const MMReadStream *, const MMChunkIndex *, const MMChunkEntry *, MMU32 exact_size,
                       MMMoviePayload *);
int mm_session_index(const MMReadStream *, const MMChunkPolicy *, MMChunkIndex **);
int mm_session_child(const MMChunkAccess *, MMU32 parent, MMU32 slot, MMU32 type, MMChunkEntry *, MMU32 *position);
int mm_session_scene(const MMMovieSession *, const MMChunkAccess *, MMU32 slot, MMU32 *position, MMMovieSessionScene *);
int mm_session_group(const MMReadStream *, const MMChunkIndex *, const MMChunkEntry *, MMMovieGroup *);
const unsigned char *mm_session_group_row(const MMMovieGroup *, MMU32 row, MMI32 *size);
int mm_session_scene_group(const MMMovieSession *, const MMChunkAccess *, MMU32 scene, int start, MMMovieGroup *);

typedef struct MMMovieBodyInput
{
    const MMMovieSession *movie;
    MMMovieSourceRead source;
    void *context;
    const MMReadStream *stream;
    MMChunkIndex *index;
    MMU32 template_position, action_position;
    int text;
    const MMMovieGroup *cels;
    MMI32 cel;
} MMMovieBodyInput;

int mm_session_body_parts(const MMMovieBodyInput *, MMMovieEvaluatedBody *);

#endif
