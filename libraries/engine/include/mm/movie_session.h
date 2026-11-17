#ifndef MM_ENGINE_MOVIE_SESSION_H
#define MM_ENGINE_MOVIE_SESSION_H

#include "chunky_file.h"
#include "background_camera.h"
#include "movie_data.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MMMovieSession MMMovieSession;

typedef struct MMMovieSessionRoot
{
    MMU32 number;
    MMMovieHeader header;
} MMMovieSessionRoot;

typedef struct MMMovieSessionScene
{
    MMU32 number;
    MMI32 first_frame, last_frame, transition;
} MMMovieSessionScene;

enum
{
    MM_MOVIE_SCENE_NOT_FOUND = MM_MOVIE_MEMORY + 1,
    MM_MOVIE_FRAME_OUT_OF_RANGE,
    MM_MOVIE_ASSET_NOT_FOUND,
    MM_MOVIE_ACTOR_NOT_FOUND
};

typedef struct MMMovieAsset
{
    MMI32 source;
    MMU32 type, number;
} MMMovieAsset;

typedef struct MMMovieCamera
{
    MMMovieAsset background;
    MMU32 slot, number;
    MMBackgroundCameraData data;
} MMMovieCamera;

typedef struct MMMovieEvaluatedActor
{
    MMU32 number;
    MMActorHeader header;
    MMActorState state;
    MMActorPoint translation;
    MMActorMatrix orientation;
} MMMovieEvaluatedActor;

typedef struct MMMovieBodyPart
{
    MMI32 parent, set;
    MMMovieAsset model; /* Zero type denotes a cel that keeps its previous model. */
    MMActorMatrix matrix; /* Local to the parent, or to the actor for parent -1. */
} MMMovieBodyPart;

typedef struct MMMovieEvaluatedBody
{
    MMMovieEvaluatedActor actor;
    MMMovieAsset model_owner; /* Resolve this template or font before reading models from its archive. */
    MMU32 part_count;
    MMMovieBodyPart *parts;
} MMMovieEvaluatedBody;

/* Resolve a positive source and specific asset to the appropriate content
 * archive. On MM_MOVIE_OK, the stream's context stays valid through the query.
 * Source zero always uses the movie's own archive. */
typedef int (*MMMovieSourceRead)(void *, const MMMovieAsset *, MMReadStream *);

/* Select the first MVIE by chunk number and validate its decoded header.
 * The session owns the index and copies the stream descriptor. Its underlying
 * storage/context must stay valid and unmodified until close. The session
 * performs synchronous reads and has the same concurrency limits as the stream.
 * Open and scene lookup publish outputs only on MM_MOVIE_OK. */
int mm_movie_session_open(const MMReadStream *, const MMChunkPolicy *, MMMovieSession **);
void mm_movie_session_close(MMMovieSession *);
const MMMovieSessionRoot *mm_movie_session_root(const MMMovieSession *);

enum { MM_MOVIE_SOURCE_NAME_BYTES = 766 };
/* Read the first matching SID from MVIE's source table as UTF-8. A movie
 * with no source table uses the retail source name for SID 2. Failure leaves
 * the caller's MM_MOVIE_SOURCE_NAME_BYTES-byte buffer unchanged. */
int mm_movie_session_source_name(const MMMovieSession *, MMI32 source, char *name);

/* Look up an exact SCEN child at the authored slot. A missing slot returns
 * MM_MOVIE_SCENE_NOT_FOUND. A link to an absent or invalid chunk is an error. */
int mm_movie_session_scene(const MMMovieSession *, MMU32 slot, MMMovieSessionScene *);

/* Evaluate the scene's authored camera at frame and decode its resource.
 * Never modifies the session or publishes output on failure. */
int mm_movie_session_camera(const MMMovieSession *, MMU32 scene_slot, MMI32 frame,
                            MMMovieSourceRead, void *context, MMMovieCamera *);

/* Actor ordinals follow the ADD_ACTOR rows in the scene's start group. Replay
 * from a fresh state using the actor's stored route, events and action metadata.
 * The result describes pose and motion without constructing a renderer body.
 * The pose fields apply when state.onStage is true. Failures preserve output. */
int mm_movie_session_actor(const MMMovieSession *, MMU32 scene_slot, MMI32 frame, MMU32 ordinal,
                           MMMovieSourceRead, void *context, MMMovieEvaluatedActor *);

/* Evaluate the selected cel's model bindings and local part matrices. An
 * off-stage actor returns its actor state with zero parts. Each successful
 * query publishes a newly owned result. On failure the output is unchanged. */
int mm_movie_session_body(const MMMovieSession *, MMU32 scene_slot, MMI32 frame, MMU32 ordinal,
                          MMMovieSourceRead, void *context, MMMovieEvaluatedBody **);
void mm_movie_body_dispose(MMMovieEvaluatedBody *);

#ifdef __cplusplus
}
#endif
#endif
