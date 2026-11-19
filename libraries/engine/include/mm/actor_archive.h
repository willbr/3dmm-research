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

enum MMActorAuthorResult
{
    MM_ACTOR_AUTHOR_OK,
    MM_ACTOR_AUTHOR_READ_TEMPLATE,
    MM_ACTOR_AUTHOR_TEMPLATE,
    MM_ACTOR_AUTHOR_FRAME,
    MM_ACTOR_AUTHOR_POSITION,
    MM_ACTOR_AUTHOR_ROOT,
    MM_ACTOR_AUTHOR_SOURCE,
    MM_ACTOR_AUTHOR_COPY_TEMPLATE,
    MM_ACTOR_AUTHOR_FONT,
    MM_ACTOR_AUTHOR_READ_ACTOR,
    MM_ACTOR_AUTHOR_WRITE_ACTOR,
    MM_ACTOR_AUTHOR_ACTOR_NUMBER,
    MM_ACTOR_AUTHOR_LINK_TEMPLATE,
    MM_ACTOR_AUTHOR_TRACK,
    MM_ACTOR_AUTHOR_COPY_MATERIAL,
    MM_ACTOR_AUTHOR_LINK_MATERIAL,
    MM_ACTOR_AUTHOR_MATERIAL_EVENTS,
    MM_ACTOR_AUTHOR_REGISTER
};

typedef struct MMActorAuthorHost
{
    const MMMovieArchiveEditor *source, *destination;
    const MMTextHost *text;
    void *context;
    int (*copy)(void *, MMU32 type, MMU32 number, MMU32 *copied);
} MMActorAuthorHost;

typedef struct MMActorInsert
{
    MMU32 scene, number, template_number;
    MMI32 actor_id, first, last;
    const float *point;
    const void *name;
    MMU32 characters;
    int embedded_text;
} MMActorInsert;

typedef struct MMActorTextReplacement
{
    MMU32 actor;
    const void *name;
    MMU32 characters;
    int retail_font, custom_material;
} MMActorTextReplacement;

/* These operations belong to mm-movie-content. The destination is a private
 * archive. Names are counted in the destination's native text units.
 * detail carries the underlying status for root, source, material-event,
 * and roll-call errors. The text replacement source has template zero
 * and, when requested, material zero. */
int mm_actor_archive_insert(const MMActorAuthorHost *, const MMActorInsert *, int *detail);
int mm_actor_archive_replace_text(const MMActorAuthorHost *, const MMActorTextReplacement *, int *detail);

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

typedef struct MMActorArchiveAnimation
{
    MMU32 scene;
    MMI32 actor_id, previous_actor_last, actor_last, edited_last;
    const float *rotation, *scale;
    void *context;
    int (*copy)(void *, MMU32 type, MMU32 number, MMU32 *copied);
    int (*appearance)(void *, MMU32 copied_actor); /* Optional, before shape. */
} MMActorArchiveAnimation;

/* Copy PATH/0 and GGAE/0 into the candidate archive and update the actor
 * and scene lifetimes. If the actor edit succeeds, scene_result receives the
 * scene edit status. Discard the candidate on failure in either stage. */
int mm_actor_archive_animate(const MMMovieArchiveEditor *, const MMActorArchiveAnimation *, int *scene_result);

#ifdef __cplusplus
}
#endif
#endif
