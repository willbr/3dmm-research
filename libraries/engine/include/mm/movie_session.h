#ifndef MM_ENGINE_MOVIE_SESSION_H
#define MM_ENGINE_MOVIE_SESSION_H

#include "chunky_file.h"
#include "chunk_storage.h"
#include "background_camera.h"
#include "model_resource.h"
#include "movie_data.h"
#include "movie_frame.h"

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
    MM_MOVIE_ACTOR_NOT_FOUND,
    MM_MOVIE_ROOT_NOT_FOUND
};

/* A borrowed view of the current chunk graph, including edits. The reader
 * must decode the selected entry and read a record of exactly the given size.
 * Its result is a movie status. Positions refer to the given chunk access. */
typedef struct MMMovieChunkView
{
    MMChunkAccess chunks;
    void *context;
    int (*read)(void *, MMU32 position, const MMChunkEntry *, void *bytes, MMU32 size);
} MMMovieChunkView;

/* On success the provider calls the receiver once, synchronously, with a
 * decoded chunk or serialized chunk name and returns its status. The stream
 * only has to live through the call. */
typedef MMReadReceiver MMMovieReceiveBytes;
typedef int (*MMMovieGraphBytes)(void *, const MMChunkEntry *, MMMovieReceiveBytes, void *);

/* Open and decode the live extent through the archive provider. Fixed records
 * require the exact decoded size, even when the stored chunk is packed. */
int mm_movie_block_payload(const MMChunkPayloadServices *, void *archive, const MMChunkEntry *,
                           MMMovieReceiveBytes, void *out);
int mm_movie_block_read(const MMChunkPayloadServices *, void *archive, const MMChunkEntry *,
                        void *bytes, MMU32 size);
int mm_movie_read_fixed(const MMReadStream *, void *bytes, MMU32 size);

typedef struct MMMovieSessionGraph
{
    void *context;
    void (*access)(void *, MMChunkAccess *);
    int (*read)(void *, void *request, MMU32 position, const MMChunkEntry *, void *bytes, MMU32 size);
    MMMovieGraphBytes payload, name;
} MMMovieSessionGraph;

/* Read an ACTR and the PATH/GGAE children at slot zero from their owning
 * graph. Header storage follows mm_actor_read_header. Track outputs transfer
 * ownership only on success. */
int mm_movie_actor_header(const MMMovieSessionGraph *, MMU32 number, void *header);
int mm_movie_actor_route(const MMMovieSessionGraph *, MMU32 number, const MMActorRouteReader *, void **out);
int mm_movie_actor_events(const MMMovieSessionGraph *, MMU32 number, const MMActorEventServices *, void **out);

typedef struct MMMovieSceneSource
{
    const MMMovieSession *movie;
    MMMovieSessionGraph graph; /* Used for individual scenes in archives without a retained movie. */
} MMMovieSceneSource;

typedef struct MMMovieSceneGroup
{
    void *handle;
    MMU32 count;
} MMMovieSceneGroup;

typedef struct MMMovieSceneEvent
{
    MMI32 frame, kind, bytes;
    unsigned char *data;
    int big_endian;
} MMMovieSceneEvent;

typedef struct MMMovieSceneReader
{
    void *context;
    int (*header)(void *, MMU32 number, MMMovieSessionScene *);
    int (*group)(void *, MMU32 number, int start, MMMovieSceneGroup *);
    void (*event)(void *, const MMMovieSceneGroup *, MMU32 index, MMMovieSceneEvent *);
    void (*close)(void *, MMMovieSceneGroup *);
} MMMovieSceneReader;

/* Read an exact SCEN chunk number and its frame or start group from the
 * current graph. Reader and graph contexts are borrowed. Reads return movie
 * statuses, with MM_MOVIE_ASSET_NOT_FOUND for missing event children. Output
 * changes only on success. Each loaded group owns its decoded buffer. Read
 * event indices below the returned count. Payloads may be normalized by the
 * consumer and live until close. */
MMMovieSceneReader mm_movie_scene_reader(MMMovieSceneSource *);
/* Read a complete 32-bit word within a validated event payload. */
MMU32 mm_movie_scene_event_word(const MMMovieSceneEvent *, MMU32 offset);

/* A null number or the nil chunk number selects the first MVIE. A successful
 * lookup publishes its number before validation, even if the header is invalid. */
int mm_movie_view_root(const MMMovieChunkView *, MMU32 *number, MMMovieHeader *);

/* Resolve a scene by its authored slot, without reading its payload. */
int mm_movie_view_scene_number(const MMChunkAccess *, MMU32 root, MMU32 slot, MMU32 *number);

/* Start the cursor at zero. Traverse existing scene links in child order,
 * preserving gaps in their authored slot numbers. On error the cursor and
 * outputs are unchanged. End of traversal returns MM_MOVIE_SCENE_NOT_FOUND. */
int mm_movie_view_next_scene(const MMMovieChunkView *, MMU32 root, MMU32 *cursor,
                             MMMovieSessionScene *, MMU32 *slot);

/* Resolve the first GST linked at the movie's source-table slot. */
int mm_movie_view_source_table(const MMChunkAccess *, MMU32 root, MMChunkEntry *, MMU32 *position);

typedef struct MMMovieEventView
{
    void *context, *group;
    MMI32 count;
    void (*read)(void *, void *group, MMI32 index, MMI32 *frame, int *kind);
} MMMovieEventView;

/* Return the last qualifying row in group order. Equal-frame events retain
 * their authored order and a later row wins. Use include_frame=0 when restoring
 * state before replaying the target frame. Returns -1 on a miss and leaves the
 * optional event_frame untouched. The view borrows an immutable event prefix. */
MMI32 mm_movie_view_last_event(const MMMovieEventView *, int kind, MMI32 frame, int include_frame,
                              MMI32 *event_frame);

typedef struct MMMovieAsset
{
    MMI32 source;
    MMU32 type, number;
} MMMovieAsset;

typedef struct MMMovieCelPart
{
    MMMovieAsset model; /* Zero type means retain the current model. */
    MMActorMatrix matrix;
} MMMovieCelPart;

typedef struct MMMovieCelRead
{
    void *context;
    void (*indices)(void *, MMU32 part, MMI32 *model, MMI32 *matrix);
    int (*model)(void *, MMU32 part, MMU32 slot, MMMovieAsset *);
    int (*matrix)(void *, MMI32 index, MMActorMatrix *);
} MMMovieCelRead;

/* Evaluate one part of the selected cel from cached or wire action data.
 * Model and matrix readers return movie statuses. A keep-model directive
 * reads only its matrix. Output is published only on success. */
int mm_movie_cel_part(const MMMovieCelRead *, MMU32 part, MMMovieCelPart *);
/* Resolve a replacement model by exact type and ID under its template. */
int mm_movie_cel_model(const MMChunkAccess *, MMU32 parent_position, MMU32 slot, MMI32 source,
                       MMMovieAsset *);
/* Resolve the exact CMTL child of a template. Position is optional. */
int mm_movie_costume_link(const MMChunkAccess *, MMU32 template_position, MMU32 costume_id,
                          MMChunkEntry *, MMU32 *position);

typedef struct MMMovieCamera
{
    MMMovieAsset background;
    MMU32 slot, number;
    MMBackgroundCameraData data;
    MMBackgroundCameraImages images; /* Slot-zero links from the CAM's own graph. */
} MMMovieCamera;

typedef int (*MMMovieConsumeCamera)(void *, const MMBackgroundCameraBinding *, const MMReadStream *);

/* Evaluate a selected slot in its owning, already loaded background graph.
 * Slots through the selected slot must be contiguous. The binding and
 * placement stream are borrowed only during consume. Its movie status is
 * returned unchanged. An optional diagnostic reports a short CAM or tail. */
int mm_movie_camera_graph(const MMMovieSessionGraph *, MMU32 background, MMU32 slot,
                          MMMovieConsumeCamera consume, void *context, void (*diagnostic)(void *, int));

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
    MMMovieAsset active_model; /* Effective binding after action and costume replay. */
    MMMovieAsset material; /* Zero type denotes the generated default for 3D text. */
    MMMovieAsset material_owner; /* Archive lookup key for an authored material. */
    MMActorMatrix matrix; /* Local to the parent, or to the actor for parent -1. */
} MMMovieBodyPart;

typedef struct MMMovieEvaluatedBody
{
    MMMovieEvaluatedActor actor;
    MMMovieAsset model_owner; /* Resolve this template or font before reading models from its archive. */
    MMU32 part_count;
    MMMovieBodyPart *parts;
} MMMovieEvaluatedBody;

typedef struct MMMovieEvaluatedFrame
{
    MMMovieSessionScene scene;
    MMI32 frame;
    MMMovieCamera camera;
    MMU32 actor_count;
    MMMovieEvaluatedActor *actors;
    MMMovieEvaluatedBody **bodies; /* Optional, in the same order as actors. */
} MMMovieEvaluatedFrame;

/* Resolve a positive source and specific asset to the appropriate content
 * archive. External streams use the policy of a stream-backed movie session.
 * On MM_MOVIE_OK, the stream's context stays valid through the query. Source
 * zero always uses the movie's own archive. */
typedef int (*MMMovieSourceRead)(void *, const MMMovieAsset *, MMReadStream *);

/* Resolve an asset through a loaded archive. The graph and its context stay
 * valid through the enclosing query. Asset graphs supply access and payload,
 * and name when serving named templates. Source zero always uses the movie. */
typedef int (*MMMovieSourceGraph)(void *, const MMMovieAsset *, MMMovieSessionGraph *);

/* Select the first MVIE by chunk number and validate its decoded header.
 * The session owns the index and copies the stream descriptor. Its underlying
 * storage/context must stay valid and unmodified until close. The session
 * performs synchronous reads and has the same concurrency limits as the stream.
 * Open and scene lookup publish outputs only on MM_MOVIE_OK. */
int mm_movie_session_open(const MMReadStream *, const MMChunkPolicy *, MMMovieSession **);
/* Validate and retain a movie root over an already-open graph. The session
 * borrows the graph context until close. The provider supplies fresh access
 * on each query, so edits may move records between calls. The optional
 * request belongs to this read only. On failure out is unchanged, but a
 * root found before validation is reported through number. Evaluation needs
 * the payload callback, and embedded 3D text also needs name. */
int mm_movie_session_open_graph(const MMMovieSessionGraph *, void *request, MMU32 *number, MMMovieSession **);
void mm_movie_session_close(MMMovieSession *);
const MMMovieSessionRoot *mm_movie_session_root(const MMMovieSession *);
int mm_movie_session_scene_number(const MMMovieSession *, MMU32 slot, MMU32 *number);
int mm_movie_session_next_scene(const MMMovieSession *, void *request, MMU32 *cursor,
                                MMMovieSessionScene *, MMU32 *slot);
int mm_movie_session_source_table(const MMMovieSession *, MMChunkEntry *, MMU32 *position);

enum { MM_MOVIE_SOURCE_NAME_BYTES = 766, MM_MOVIE_RETAIL_SOURCE_ID = 2 };
#define MM_MOVIE_RETAIL_SOURCE_NAME "3dmovie"
typedef struct MMMovieSourceTable
{
    const MMReadStream *stream;
    MMU32 count, strings_offset, rows_offset;
    unsigned unit;
    int big_endian, encoding;
} MMMovieSourceTable;

typedef struct MMMovieSourceName
{
    MMI32 id;
    int encoding;
    MMU32 size;
    unsigned char bytes[510];
} MMMovieSourceName;

/* Open a dense GST of counted source names and four-byte SID extras. Keep the
 * decoded stream stable throughout iteration. Rows retain wire order and
 * encoding; duplicate IDs are published for the caller's lookup/merge policy.
 * The table and row outputs are unchanged on failure. */
int mm_movie_source_table_open(const MMReadStream *, MMU32 max_bytes, MMMovieSourceTable *);
int mm_movie_source_table_at(const MMMovieSourceTable *, MMU32 row, MMMovieSourceName *);
int mm_movie_source_name_utf8(const MMMovieSourceName *, char name[MM_MOVIE_SOURCE_NAME_BYTES]);

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

/* Evaluate an authored frame's camera and all actors in start-event order.
 * Success transfers an owned snapshot, including off-stage actor states.
 * A missing/invalid camera or actor, or any other failure, leaves out unchanged. */
int mm_movie_session_seek(const MMMovieSession *, MMU32 scene_slot, MMI32 frame,
                          MMMovieSourceRead, void *context, MMMovieEvaluatedFrame **out);
/* Evaluate the camera, actors and their effective body bindings in one seek.
 * Off-stage actors have empty bodies. The frame owns every body and its part
 * data, and remains readable after the session and source archives close.
 * Failure leaves out unchanged. */
int mm_movie_session_seek_with_bodies(const MMMovieSession *, MMU32 scene_slot, MMI32 frame,
                                      MMMovieSourceRead, void *context, MMMovieEvaluatedFrame **out);
/* Resolve external assets from live chunk graphs. Result ownership and
 * publication match mm_movie_session_seek_with_bodies. */
int mm_movie_session_seek_graph(const MMMovieSession *, MMU32 scene_slot, MMI32 frame,
                                MMMovieSourceGraph, void *context, MMMovieEvaluatedFrame **out);
void mm_movie_frame_dispose(MMMovieEvaluatedFrame *);

/* Actor ordinals follow the ADD_ACTOR rows in the scene's start group. Replay
 * from a fresh state using the actor's stored route, events and action metadata.
 * The result describes pose and motion without constructing a renderer body.
 * The pose fields apply when state.onStage is true. Failures preserve output. */
int mm_movie_session_actor(const MMMovieSession *, MMU32 scene_slot, MMI32 frame, MMU32 ordinal,
                           MMMovieSourceRead, void *context, MMMovieEvaluatedActor *);

/* Replay costume and action effects from a fresh body, then evaluate the
 * selected cel's local matrices and effective model and material bindings.
 * An off-stage actor returns its actor state with zero parts. Each successful
 * query publishes a newly owned result. On failure the output is unchanged. */
int mm_movie_session_body(const MMMovieSession *, MMU32 scene_slot, MMI32 frame, MMU32 ordinal,
                          MMMovieSourceRead, void *context, MMMovieEvaluatedBody **);
void mm_movie_body_dispose(MMMovieEvaluatedBody *);

typedef struct MMMoviePartMesh
{
    MMI32 model_index; /* -1 when replay leaves the part without a model. */
    MMActorMatrix world;
} MMMoviePartMesh;

typedef struct MMMovieBodyMeshes
{
    MMU32 part_count, model_count;
    MMMoviePartMesh *parts;
    MMModelMesh *models;
} MMMovieBodyMeshes;

/* Read each distinct active BMDL from the body owner's exact archive and
 * accumulate the body hierarchy into world transforms. The newly owned result
 * is published only on success. The input body is borrowed during the query. */
int mm_movie_session_body_meshes(const MMMovieSession *, const MMMovieEvaluatedBody *, MMMovieSourceRead,
                                void *context, MMMovieBodyMeshes **);
int mm_movie_session_body_meshes_graph(const MMMovieSession *, const MMMovieEvaluatedBody *, MMMovieSourceGraph,
                                       void *context, MMMovieBodyMeshes **);
void mm_movie_body_meshes_dispose(MMMovieBodyMeshes *);

#ifdef __cplusplus
}
#endif
#endif
