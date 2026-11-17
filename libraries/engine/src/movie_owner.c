/* Movie ownership and persistence converted from engine movie_data.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/movie_owner.h"
#include "mm/movie_session.h"
#include <string.h>
void mm_movie_state_init(MMMovieState *m)
{
    memset(m, 0, sizeof(*m));
    m->movieNumber = 0xffffffffUL;
    mm_movie_title_init(&m->title, 1);
}
static void close_rows(void *table, const MMMovieOwnerServices *s)
{
    MMU32 i;
    MMRollCallActor actor;
    for (i = 0; i < s->count(table); ++i)
    {
        s->actor(table, i, &actor);
        s->close_actor(s->context, &actor);
    }
}
void mm_movie_clear_actors(MMMovieState *m, const MMMovieOwnerServices *s)
{
    if (!m->actors)
        return;
    close_rows(m->actors, s);
    s->release_table(m->actors);
    m->actors = NULL;
}
void mm_movie_release_resources(MMMovieState *m, const MMMovieOwnerServices *s)
{
    mm_movie_session_close(m->session);
    m->session = NULL;
    if (m->resources)
    {
        s->release_resources(m->resources);
        m->resources = NULL;
    }
}
void mm_movie_state_dispose(MMMovieState *m, const MMMovieOwnerServices *s)
{
    mm_movie_clear_actors(m, s);
    mm_movie_release_resources(m, s);
}
static void replace(MMMovieState *m, MMMovieState *fresh, const MMMovieOwnerServices *s)
{
    mm_movie_state_dispose(m, s);
    *m = *fresh;
    fresh->resources = fresh->actors = NULL;
    fresh->session = NULL;
}
int mm_movie_init_empty(MMMovieState *m, const MMMovieOwnerServices *s)
{
    MMMovieState fresh;
    void *file;
    mm_movie_state_init(&fresh);
    mm_movie_title_init(&fresh.title, m->title.unit);
    file = s->create_file(s->context);
    if (!file)
        return 0;
    fresh.resources = s->new_resources(s->context, file);
    s->release_file(file);
    fresh.actors = s->new_actors(s->context, 0);
    if (!fresh.resources || !fresh.actors || !s->create_root(s->context, s->file(fresh.resources), &fresh.movieNumber))
    {
        mm_movie_state_dispose(&fresh, s);
        return 0;
    }
    replace(m, &fresh, s);
    return 1;
}
int mm_movie_load(MMMovieState *m, MMU32 number, const MMMovieOwnerServices *s)
{
    MMMovieState loaded;
    void *file;
    MMU32 scene_number;
    int valid;
    mm_movie_state_init(&loaded);
    mm_movie_title_init(&loaded.title, m->title.unit);
    file = s->open_file(s->context);
    if (!file)
        return 0;
    valid = s->open_session(s->context, file, &number, &loaded.session);
    if (valid)
        loaded.resources = s->new_resources(s->context, file);
    if (!valid || !loaded.resources)
    {
        mm_movie_state_dispose(&loaded, s);
        s->release_file(file);
        return 0;
    }
    s->release_file(file);
    loaded.movieNumber = number;
    if (!s->read_rollcall(s->context, loaded.resources, number, &loaded.actors, &loaded.actorIdLimit))
    {
        mm_movie_state_dispose(&loaded, s);
        return 0;
    }
    file = s->file(loaded.resources);
    s->load_name(s->context, file, number, &loaded.title);
    while (mm_movie_session_scene_number(loaded.session, (MMU32)loaded.sceneCount, &scene_number) == MM_MOVIE_OK)
        ++loaded.sceneCount;
    replace(m, &loaded, s);
    return 1;
}
int mm_movie_state_scene_number(MMMovieState *m, MMU32 slot, MMU32 *number, const MMMovieOwnerServices *s)
{
    const MMMovieSessionRoot *root = mm_movie_session_root(m->session);
    MMMovieSession *opened;
    MMU32 requested = m->movieNumber;
    if (!m->resources)
        return 0;
    if (!root || root->number != requested)
    {
        if (!s->open_session(s->context, s->file(m->resources), &requested, &opened))
            return 0;
        mm_movie_session_close(m->session);
        m->session = opened;
        m->movieNumber = requested;
    }
    return mm_movie_session_scene_number(m->session, slot, number) == MM_MOVIE_OK;
}
int mm_movie_save(MMMovieState *m, const MMMovieOwnerServices *s)
{
    void *file;
    MMU32 previous, written;
    int had;
    if (!m->resources || !m->actors)
        return 0;
    file = s->file(m->resources);
    had = s->previous_rollcall(s->context, file, m->movieNumber, &previous);
    if (!s->write_rollcall(s->context, file, m->movieNumber, m->actors, &written))
        return 0;
    if (had)
        s->delete_previous(s->context, file, m->movieNumber, previous);
    if (!s->set_name(s->context, file, m->movieNumber, &m->title) || !s->save(s->context, file))
        return 0;
    s->make_persistent(file);
    return 1;
}
int mm_movie_read_rollcall(void *resources, MMU32 movie, void **actors, MMI32 *limit,
                                 const MMRollCallServices *s)
{
    void *wire, *runtime = NULL;
    int big;
    MMU32 i, count;
    MMI32 next = limit ? *limit : 0;
    unsigned char bytes[28];
    MMMemoryStream memory;
    MMRollCallActor actor;
    wire = s->read_wire(s->context, resources, movie, &big);
    if (!wire || s->extra_width(wire) != MM_MOVIE_ACTOR_BYTES)
        goto fail;
    runtime = s->new_table(s->context, 0);
    if (!runtime)
        goto fail;
    count = s->count(wire);
    for (i = 0; i < count; ++i)
    {
        s->wire_row(s->context, wire, i, bytes);
        mm_memory_stream_init(&memory, bytes, sizeof(bytes));
        if (mm_movie_read_actor(&memory.stream, big, &actor.data, &next) != MM_MOVIE_OK)
            goto fail;
        actor.cache = NULL;
        if (!s->open_actor(s->context, &actor, resources))
            goto fail;
        if (!s->append_runtime(s->context, runtime, &actor))
        {
            s->close_actor(s->context, &actor);
            goto fail;
        }
    }
    s->release_table(wire);
    *actors = runtime;
    if (limit)
        *limit = next;
    return 1;
fail:
    if (wire)
        s->release_table(wire);
    if (runtime)
    {
        for (i = 0; i < s->count(runtime); ++i)
        {
            s->actor(runtime, i, &actor);
            s->close_actor(s->context, &actor);
        }
        s->release_table(runtime);
    }
    *actors = NULL;
    return 0;
}
int mm_movie_write_rollcall(void *file, MMU32 movie, void *actors, MMU32 *written,
                                  const MMRollCallServices *s)
{
    void *wire;
    MMU32 i, count;
    MMRollCallActor actor;
    unsigned char bytes[28];
    if (!actors || s->extra_width(actors) != s->runtime_width)
        return 0;
    wire = s->new_table(s->context, 1);
    if (!wire)
        return 0;
    count = s->count(actors);
    for (i = 0; i < count; ++i)
    {
        s->runtime_row(s->context, actors, i, &actor);
        /* Native GST text/extra payloads are native endian. */
        {
            const unsigned short one = 1;
            mm_movie_write_actor(&actor.data, bytes, *(const unsigned char *)&one == 0);
        }
        if (!s->append_wire(s->context, wire, bytes))
        {
            s->release_table(wire);
            return 0;
        }
    }
    if (!s->add_chunk(s->context, file, s->wire_size(wire), written))
    {
        s->release_table(wire);
        return 0;
    }
    if (!s->write_table(s->context, wire) || !s->adopt(s->context, file, movie, *written))
    {
        s->release_table(wire);
        s->delete_chunk(s->context, file, *written);
        return 0;
    }
    s->release_table(wire);
    return 1;
}
