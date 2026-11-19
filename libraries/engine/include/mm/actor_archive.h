#ifndef MM_ENGINE_ACTOR_ARCHIVE_H
#define MM_ENGINE_ACTOR_ARCHIVE_H
#include "actor_data.h"
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
    MMChunkAccess source_chunks;
    MMChunkAccess (*destination_chunks)(void *);
    int (*clone_actor)(void *, MMU32 actor, MMActorHeader *, MMU32 *copied);
    int (*copy_asset)(void *, MMU32 type, MMU32 number, MMU32 *copied);
    int (*adopt)(void *, MMU32 parent_type, MMU32 parent, MMU32 type, MMU32 number, MMU32 slot);
    int (*map_source)(void *, MMI32 source, MMI32 *mapped);
    int (*read_text)(void *, MMU32 number, unsigned char *descriptor);
    int (*write_text)(void *, MMU32 number, unsigned char *descriptor);
    int (*write_header)(void *, MMU32 actor, const MMActorHeader *);
    void *(*read_events)(void *, MMU32 actor);
    int (*write_events)(void *, MMU32 actor, void *events);
    MMActorEventServices events;
} MMActorArchiveHost;

/* The next slot follows the highest slot of the requested type. */
int mm_actor_archive_next_child(const MMChunkAccess *, MMU32 parent_type, MMU32 parent,
                                MMU32 type, MMU32 *slot);

/* Copy the complete actor track into a private destination. read_events returns
 * host-owned, writable, decoded native rows as used by mm_actor_read_events.
 * Only count, fixed and data are used from the event services. The destination
 * graph view must reflect preceding clone/copy/adopt calls. Header and actor
 * outputs are published after all writes succeed. On failure the host discards
 * the candidate archive and its event group. */
int mm_actor_archive_transfer(const MMActorArchiveRequest *, const MMActorArchiveHost *,
                              MMU32 *actor, MMActorHeader *);

#ifdef __cplusplus
}
#endif
#endif
