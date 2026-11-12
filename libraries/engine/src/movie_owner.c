/* Movie ownership and persistence converted from engine movie_data.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/movie_owner.h"
#include <string.h>
void three_mm_movie_state_init(ThreeMMMovieState *m)
{
    memset(m, 0, sizeof(*m));
    m->movieNumber = 0xffffffffUL;
    three_mm_movie_title_init(&m->title, 1);
}
static void close_rows(void *table, const ThreeMMMovieOwnerServices *s)
{
    ThreeMMU32 i;
    ThreeMMRollCallActor actor;
    for (i = 0; i < s->count(table); ++i)
    {
        s->actor(table, i, &actor);
        s->close_actor(s->context, &actor);
    }
}
void three_mm_movie_clear_actors(ThreeMMMovieState *m, const ThreeMMMovieOwnerServices *s)
{
    if (!m->actors)
        return;
    close_rows(m->actors, s);
    s->release_table(m->actors);
    m->actors = NULL;
}
void three_mm_movie_release_resources(ThreeMMMovieState *m, const ThreeMMMovieOwnerServices *s)
{
    if (m->resources)
    {
        s->release_resources(m->resources);
        m->resources = NULL;
    }
}
void three_mm_movie_state_dispose(ThreeMMMovieState *m, const ThreeMMMovieOwnerServices *s)
{
    three_mm_movie_clear_actors(m, s);
    three_mm_movie_release_resources(m, s);
}
static void replace(ThreeMMMovieState *m, ThreeMMMovieState *fresh, const ThreeMMMovieOwnerServices *s)
{
    three_mm_movie_state_dispose(m, s);
    *m = *fresh;
    fresh->resources = fresh->actors = NULL;
}
int three_mm_movie_init_empty(ThreeMMMovieState *m, const ThreeMMMovieOwnerServices *s)
{
    ThreeMMMovieState fresh;
    void *file;
    three_mm_movie_state_init(&fresh);
    three_mm_movie_title_init(&fresh.title, m->title.unit);
    file = s->create_file(s->context);
    if (!file)
        return 0;
    fresh.resources = s->new_resources(s->context, file);
    s->release_file(file);
    fresh.actors = s->new_actors(s->context, 0);
    if (!fresh.resources || !fresh.actors || !s->create_root(s->context, s->file(fresh.resources), &fresh.movieNumber))
    {
        three_mm_movie_state_dispose(&fresh, s);
        return 0;
    }
    replace(m, &fresh, s);
    return 1;
}
int three_mm_movie_load(ThreeMMMovieState *m, ThreeMMU32 number, const ThreeMMMovieOwnerServices *s)
{
    ThreeMMMovieState loaded;
    void *file;
    int valid;
    three_mm_movie_state_init(&loaded);
    three_mm_movie_title_init(&loaded.title, m->title.unit);
    file = s->open_file(s->context);
    if (!file)
        return 0;
    valid = s->verify(s->context, file, &number);
    if (valid)
        loaded.resources = s->new_resources(s->context, file);
    s->release_file(file);
    if (!valid || !loaded.resources)
        return 0;
    loaded.movieNumber = number;
    if (!s->read_rollcall(s->context, loaded.resources, number, &loaded.actors, &loaded.actorIdLimit))
    {
        three_mm_movie_state_dispose(&loaded, s);
        return 0;
    }
    file = s->file(loaded.resources);
    s->load_name(s->context, file, number, &loaded.title);
    while (s->scene(s->context, file, number, loaded.sceneCount))
        ++loaded.sceneCount;
    replace(m, &loaded, s);
    return 1;
}
int three_mm_movie_save(ThreeMMMovieState *m, const ThreeMMMovieOwnerServices *s)
{
    void *file;
    ThreeMMU32 previous, written;
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
int three_mm_movie_read_rollcall(void *resources, ThreeMMU32 movie, void **actors, ThreeMMI32 *limit,
                                 const ThreeMMRollCallServices *s)
{
    void *wire, *runtime = NULL;
    int big;
    ThreeMMU32 i, count;
    ThreeMMI32 next = limit ? *limit : 0;
    unsigned char bytes[28];
    ThreeMMMemoryStream memory;
    ThreeMMRollCallActor actor;
    wire = s->read_wire(s->context, resources, movie, &big);
    if (!wire || s->extra_width(wire) != THREE_MM_MOVIE_ACTOR_BYTES)
        goto fail;
    runtime = s->new_table(s->context, 0);
    if (!runtime)
        goto fail;
    count = s->count(wire);
    for (i = 0; i < count; ++i)
    {
        s->wire_row(s->context, wire, i, bytes);
        three_mm_memory_stream_init(&memory, bytes, sizeof(bytes));
        if (three_mm_movie_read_actor(&memory.stream, big, &actor.data, &next) != THREE_MM_MOVIE_OK)
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
int three_mm_movie_write_rollcall(void *file, ThreeMMU32 movie, void *actors, ThreeMMU32 *written,
                                  const ThreeMMRollCallServices *s)
{
    void *wire;
    ThreeMMU32 i, count;
    ThreeMMRollCallActor actor;
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
            three_mm_movie_write_actor(&actor.data, bytes, *(const unsigned char *)&one == 0);
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
