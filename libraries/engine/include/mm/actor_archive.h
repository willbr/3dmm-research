#ifndef MM_ENGINE_ACTOR_ARCHIVE_H
#define MM_ENGINE_ACTOR_ARCHIVE_H
#include "actor_data.h"
#include "movie_archive_edit.h"
#include "movie_data.h"
#ifdef __cplusplus
extern "C" {
#endif

enum MMActorArchiveResult
{
    MM_ACTOR_ARCHIVE_OK,
    MM_ACTOR_ARCHIVE_CLONE,
    MM_ACTOR_ARCHIVE_FRAME,
    MM_ACTOR_ARCHIVE_TEMPLATE,
    MM_ACTOR_ARCHIVE_READ_TEXT,
    MM_ACTOR_ARCHIVE_TEXT_ORDER,
    MM_ACTOR_ARCHIVE_WRITE_TEXT,
    MM_ACTOR_ARCHIVE_COPY_ASSET,
    MM_ACTOR_ARCHIVE_SLOT,
    MM_ACTOR_ARCHIVE_ADOPT_ASSET,
    MM_ACTOR_ARCHIVE_ADOPT_SOUND,
    MM_ACTOR_ARCHIVE_SOURCE,
    MM_ACTOR_ARCHIVE_WRITE_HEADER,
    MM_ACTOR_ARCHIVE_READ_EVENTS,
    MM_ACTOR_ARCHIVE_WRITE_EVENTS,
    MM_ACTOR_ARCHIVE_MEMORY
};

enum { MM_ACTOR_ARCHIVE_TEXT_BYTES = 24 };

typedef struct MMActorArchiveRequest
{
    MMU32 actor, source_scene, destination_scene;
    MMI32 actor_id, source_frame, destination_frame;
} MMActorArchiveRequest;

typedef struct MMActorArchiveHost
{
    void *context;
    const MMMovieArchiveEditor *source, *destination;
    int (*clone_actor)(void *, MMU32 actor, MMU32 *copied);
    int (*copy_asset)(void *, MMU32 type, MMU32 number, MMU32 *copied);
    void *source_map;
    int (*map_source)(void *, MMI32 source, MMI32 *mapped);
} MMActorArchiveHost;

/* Copy the complete actor track into a private destination. The destination
 * graph view reflects preceding clone/copy/adopt calls. A null map_source
 * keeps external source IDs. Publish the actor and optional header after all writes. */
int mm_actor_archive_transfer(const MMActorArchiveRequest *, const MMActorArchiveHost *,
                              MMU32 *actor, MMActorHeader *);

/* Find the first readable actor header matching the ID in the selected scene. */
int mm_actor_archive_find(const MMMovieArchiveEditor *, MMU32 scene, MMI32 actor_id, MMU32 *actor);

enum MMActorRegistrationResult
{
    MM_ACTOR_REGISTER_OK,
    MM_ACTOR_REGISTER_SLOT,
    MM_ACTOR_REGISTER_SCENE_READ,
    MM_ACTOR_REGISTER_SCENE_DATA,
    MM_ACTOR_REGISTER_SCENE_WRITE,
    MM_ACTOR_REGISTER_SCENE_LINK,
    MM_ACTOR_REGISTER_ROLL_READ,
    MM_ACTOR_REGISTER_ROLL_DATA,
    MM_ACTOR_REGISTER_ROLL_WRITE,
    MM_ACTOR_REGISTER_MEMORY
};

typedef struct MMActorArchivePlacement
{
    MMU32 scene, actor;
    MMI32 first_frame;
    int invalidate_thumbnail;
} MMActorArchivePlacement;

typedef struct MMActorArchiveName
{
    MMMovieActor actor;
    const void *text;
    MMU32 characters;
    int replace_existing;
} MMActorArchiveName;

/* NULL placement updates the roll call only. Matching rows keep their order and
 * reference counts when replace_existing is set. */
int mm_actor_archive_register(const MMMovieArchiveEditor *, MMU32 movie,
                              const MMActorArchivePlacement *, const MMActorArchiveName *);

enum MMActorArchiveEditResult
{
    MM_ACTOR_EDIT_ARCHIVE_OK,
    MM_ACTOR_EDIT_ARCHIVE_SCENE,
    MM_ACTOR_EDIT_ARCHIVE_HEADER,
    MM_ACTOR_EDIT_ARCHIVE_RANGE,
    MM_ACTOR_EDIT_ARCHIVE_COPY,
    MM_ACTOR_EDIT_ARCHIVE_CHILD,
    MM_ACTOR_EDIT_ARCHIVE_APPEARANCE,
    MM_ACTOR_EDIT_ARCHIVE_READ_EVENTS,
    MM_ACTOR_EDIT_ARCHIVE_EDIT_EVENTS,
    MM_ACTOR_EDIT_ARCHIVE_WRITE_EVENTS,
    MM_ACTOR_EDIT_ARCHIVE_LINK_EVENTS,
    MM_ACTOR_EDIT_ARCHIVE_LINK_ACTOR
};

typedef struct MMActorArchivePose
{
    MMU32 scene, actor;
    const float *translation, *rotation, *scale; /* 3, 9 and 3 elements. */
    void *context;
    int (*appearance)(void *, MMU32 copied_actor); /* Optional, before shape. */
} MMActorArchivePose;

/* Keep the actor's native ID and immutable child chunks. Replace only the
 * selected scene's link, and clear its thumbnail after successful adoption. */
int mm_actor_archive_place(const MMMovieArchiveEditor *, const MMActorArchivePose *);
/* Replace the whole-word material after every Add event on the actor track. */
int mm_actor_archive_set_material(const MMMovieArchiveEditor *, MMU32 actor, MMU32 material);
/* Install a copied GGAE as the actor's track. */
int mm_actor_archive_link_events(const MMMovieArchiveEditor *, MMU32 actor, MMU32 events);

#ifdef __cplusplus
}
#endif
#endif
