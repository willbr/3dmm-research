#ifndef MM_MOVIE_SESSION_INTERNAL_H
#define MM_MOVIE_SESSION_INTERNAL_H

#include "mm/movie_session.h"
#include "mm/collections.h"
#include "mm/action_definition.h"

struct MMMovieSession
{
    MMMovieSessionGraph graph;
    MMReadStream stream;
    MMChunkIndex *index;
    MMChunkPolicy policy;
    MMMovieSessionRoot root;
    MMStorageBuffer render_bodies;
    const MMMovieLiveFrame *seeking_frame;
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
    unsigned char *bytes, *records;
} MMMovieGroup;

typedef struct MMMovieSource
{
    MMMovieSourceRead stream;
    MMMovieSourceGraph graph;
    void *context;
} MMMovieSource;

typedef struct MMMovieArchive
{
    MMChunkAccess access;
    MMMovieSessionGraph graph;
    MMReadStream stream;
    MMChunkIndex *index, *owned_index;
} MMMovieArchive;

MMU32 mm_session_word(const unsigned char *, int big);
int mm_session_payload(const MMMovieArchive *, const MMChunkEntry *, MMU32 exact_size, MMMoviePayload *);
int mm_session_name(const MMMovieArchive *, MMU32 position, MMMoviePayload *);
int mm_session_archive(const MMMovieSession *, const MMMovieSource *, const MMMovieAsset *, MMMovieArchive *);
void mm_session_archive_close(MMMovieArchive *);
int mm_session_index(const MMReadStream *, const MMChunkPolicy *, MMChunkIndex **);
int mm_session_child(const MMChunkAccess *, MMU32 parent, MMU32 slot, MMU32 type, MMChunkEntry *, MMU32 *position);
int mm_session_scene(const MMMovieSession *, const MMChunkAccess *, MMU32 slot, MMU32 *position, MMMovieSessionScene *);
int mm_session_group(const MMMovieArchive *, const MMChunkEntry *, MMMovieGroup *);
int mm_session_array(const MMMovieArchive *, MMU32 parent, MMU32 type, MMU32 width,
                     MMMoviePayload *, MMCollectionHeader *);
unsigned char *mm_session_group_row(const MMMovieGroup *, MMU32 row, MMI32 *size);
void mm_session_scene_event(const MMMovieGroup *, MMU32 index, MMMovieSceneEvent *);
int mm_session_scene_group(const MMMovieArchive *, MMU32 scene, int start, MMMovieGroup *);
int mm_session_camera_at(const MMMovieSession *, const MMMovieArchive *, MMU32 scene, const MMMovieGroup *starts,
                          MMI32 frame, const MMMovieSource *, MMMovieCamera *);

typedef struct MMMovieAction
{
    const MMMovieArchive *archive;
    MMChunkEntry entry;
    MMU32 position;
    MMActionDefinition definition;
    MMMovieGroup cels;
    MMMoviePayload matrices;
    MMCollectionHeader matrix_header;
    int cel_other, matrix_other;
} MMMovieAction;

int mm_session_action_matrices(MMMovieAction *);

typedef struct MMMovieBodyInput
{
    const MMMovieSession *movie;
    const MMMovieSource *source;
    const MMMovieArchive *archive;
    MMU32 template_position;
    int text;
    MMMovieAction *action;
    MMI32 cel;
} MMMovieBodyInput;

typedef struct MMMovieCustomCostume
{
    MMU32 position;
    MMI32 set;
} MMMovieCustomCostume;

int mm_session_body_shape(const MMMovieBodyInput *, MMMovieEvaluatedBody *, const MMActorHeader *);
int mm_session_body_cel(const MMMovieBodyInput *, MMMovieEvaluatedBody *);
int mm_session_body_parts(const MMMovieBodyInput *, MMMovieEvaluatedBody *);
int mm_session_costume_find(const MMMovieBodyInput *, MMI32 id, MMMovieCustomCostume *);
int mm_session_costume_apply(const MMMovieBodyInput *, MMMovieEvaluatedBody *, const MMMovieCustomCostume *);
int mm_session_costume_defaults(const MMMovieBodyInput *, MMMovieEvaluatedBody *);
void mm_session_apply_material(MMMovieEvaluatedBody *, MMI32 set, const MMMovieAsset *);
int mm_session_costume_material(const MMMovieSession *, const MMMovieSource *, const MMMovieAsset *);

#endif
