#ifndef MM_ACTOR_ARCHIVE_INTERNAL_H
#define MM_ACTOR_ARCHIVE_INTERNAL_H
#include "mm/actor_archive.h"
#include "mm/collections.h"

void *mm_actor_archive_event(MMGroup *, MMU32 row);
void *mm_actor_archive_payload(MMGroup *, MMU32 row);
MMGroup *mm_actor_archive_read_event_group(const MMMovieArchiveEditor *, MMU32 actor, MMU32 *source_number);
void mm_actor_archive_release_event_group(MMGroup *);
int mm_actor_archive_write_event_group(const MMMovieArchiveEditor *, MMGroup *, MMU32 *number,
                                       const MMU32 *attach_to_actor);
int mm_actor_archive_replace_event_group(const MMMovieArchiveEditor *, MMU32 actor, MMGroup *);
int mm_actor_archive_read_actor(const MMMovieArchiveEditor *, MMU32 actor, MMActorHeader *);
int mm_actor_archive_copy_actor(const MMMovieArchiveEditor *, MMU32 actor, const MMActorHeader *,
                                int share_track, MMU32 *copy);
#endif
