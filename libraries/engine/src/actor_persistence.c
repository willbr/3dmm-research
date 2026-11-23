/* Actor persistence converted from actrsave.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/actor_persistence.h"
#include "mm/wide.h"

int mm_actor_movie_path(const MMMovieSessionGraph *graph, MMU32 number, const MMActorPathFactory *factory,
                        MMActorPath **out)
{
    MMActorRouteReader reader = mm_actor_path_reader(factory);
    void *path;
    int status = mm_movie_actor_route(graph, number, &reader, &path);
    if (status == MM_MOVIE_OK)
        *out = (MMActorPath *)path;
    return status;
}

int mm_actor_movie_events(const MMMovieSessionGraph *graph, MMU32 number, MMActorEventFactory *factory,
                          MMActorEventTable **out)
{
    MMActorEventServices services = mm_actor_events_services(factory);
    void *events;
    int status = mm_movie_actor_events(graph, number, &services, &events);
    if (status == MM_MOVIE_OK)
        *out = (MMActorEventTable *)events;
    return status;
}

static int read_runtime(const MMMovieSessionGraph *graph, MMU32 number, MMActorRuntime *runtime,
                        const MMActorLoadServices *s)
{
    MMActorHeader header;
    MMChunkAccess chunks;
    if (mm_movie_actor_header(graph, number, &header) != MM_MOVIE_OK)
        return 0;
    runtime->origin.dxr = mm_signed_word(header.translation[0]);
    runtime->origin.dyr = mm_signed_word(header.translation[1]);
    runtime->origin.dzr = mm_signed_word(header.translation[2]);
    runtime->actor = header.actor_id;
    runtime->first = header.first_frame;
    runtime->last = header.last_frame;
    runtime->life_dirty = runtime->first == MM_ACTOR_FRAME_UNPLACED || runtime->last == MM_ACTOR_FRAME_UNPLACED;
    runtime->template_tag.sid = header.source_id;
    runtime->template_tag.cache = NULL;
    runtime->template_tag.type = header.template_type;
    runtime->template_tag.number = header.template_number;
    if (!header.source_id)
    {
        graph->access(graph->context, &chunks);
        if (mm_actor_template_link(&chunks, number, 0, 0, &runtime->template_tag.number) != MM_CHUNK_OK)
            s->diagnostic(s->context, MM_ACTOR_LOAD_TEMPLATE_LINK);
    }
    return 1;
}

void *mm_actor_load(const MMMovieSessionGraph *graph, MMU32 number, void *cache, const MMActorLoadServices *s)
{
    MMActorPathFactory paths = {NULL, 0};
    MMActorEventFactory events = {NULL, 0};
    MMActorOwner *owner;
    MMActorRuntime *runtime;
    MMActorTrack *track;
    MMActorPath *path;
    MMActorEventTable *table;
    void *actor = s->create(s->context, &owner);
    if (!actor)
        goto fail;
    runtime = &owner->runtime;
    track = &owner->track;
    if (!read_runtime(graph, number, runtime, s))
        goto fail;
    paths.allocator = events.allocator = s->allocator;
    paths.trash_unused = events.trash_unused = s->trash_unused;
    if (mm_actor_movie_path(graph, number, &paths, &path) != MM_MOVIE_OK)
        goto fail;
    mm_actor_track_adopt_route(track, path);
    if (mm_actor_movie_events(graph, number, &events, &table) != MM_MOVIE_OK)
        goto fail;
    mm_actor_track_adopt_events(track, table, s->tags);
    if (runtime->template_tag.sid < 0)
        s->diagnostic(s->context, MM_ACTOR_LOAD_TEMPLATE_SOURCE);
    if (!mm_tag_open(&runtime->template_tag, cache, NULL, s->tags) ||
        !mm_actor_track_open_tags(track, cache, NULL, s->tags) ||
        !mm_actor_sounds_reserve(&owner->sounds, MM_ACTOR_SOUND_GROW))
        goto fail;
    runtime->template_object = s->fetch_template(s->context, &runtime->template_tag);
    if (!runtime->template_object)
        goto fail;
    if (runtime->last == MM_ACTOR_FRAME_UNPLACED && runtime->first != MM_ACTOR_FRAME_UNPLACED)
    {
        const MMActorTimelineServices timeline = s->timeline(s->context, actor);
        MMI32 last;
        mm_actor_owner_frame_range(owner, NULL, &last, &timeline);
    }
    return actor;

fail:
    s->diagnostic(s->context, MM_ACTOR_LOAD_FAILED);
    if (actor)
        s->release(s->context, actor);
    return NULL;
}
