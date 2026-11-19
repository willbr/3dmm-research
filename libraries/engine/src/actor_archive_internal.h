#ifndef MM_ACTOR_ARCHIVE_INTERNAL_H
#define MM_ACTOR_ARCHIVE_INTERNAL_H
#include "mm/actor_archive.h"
#include "mm/actor_events.h"

MMActorEventTable *mm_actor_archive_read_event_group(const MMMovieArchiveEditor *, MMU32 actor, MMU32 *source_number);
int mm_actor_archive_write_event_group(const MMMovieArchiveEditor *, MMActorEventTable *, MMU32 *number,
                                       const MMU32 *attach_to_actor);
int mm_actor_archive_replace_event_group(const MMMovieArchiveEditor *, MMU32 actor, MMActorEventTable *);
int mm_actor_archive_read_actor(const MMMovieArchiveEditor *, MMU32 actor, MMActorHeader *);
int mm_actor_archive_write_route(const MMMovieArchiveEditor *, MMU32 actor, const void *points, MMU32 count);
int mm_actor_archive_write_stationary(const MMMovieArchiveEditor *, MMU32 actor, MMI32 frame,
                                      const unsigned short angles[3]);
int mm_actor_archive_set_font_source(const MMMovieArchiveEditor *, MMU32 text_template, MMI32 source);
int mm_actor_archive_copy_actor(const MMMovieArchiveEditor *, MMU32 actor, const MMActorHeader *,
                                int share_track, MMU32 *copy);
#endif
