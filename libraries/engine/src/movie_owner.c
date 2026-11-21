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
static void close_rows(MMRollCallTable *table, void *context, void (*close_actor)(void *, MMRollCallActor *))
{
    MMU32 i;
    MMRollCallActor actor;
    for (i = 0; i < table->count; ++i)
        if (mm_rollcall_table_extra(table, i, &actor))
            close_actor(context, &actor);
}
int mm_movie_init_actors(MMMovieState *m, const MMMovieOwnerServices *s)
{
    const MMStorageAllocator *allocator = &s->actor_allocator;
    if (!allocator->resize && !allocator->release)
        allocator = NULL;
    return m->actors.stride || mm_rollcall_table_init(&m->actors, sizeof(MMRollCallActor), m->title.unit,
               s->actor_encoding, s->actor_trash, allocator) == MM_STORAGE_OK;
}
void mm_movie_clear_actors(MMMovieState *m, const MMMovieOwnerServices *s)
{
    if (!m->actors.stride)
        return;
    close_rows(&m->actors, s->context, s->close_actor);
    mm_rollcall_table_dispose(&m->actors);
    memset(&m->actors, 0, sizeof(m->actors));
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
    mm_movie_state_init(fresh);
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
    if (!fresh.resources || !mm_movie_init_actors(&fresh, s) ||
        !s->create_root(s->context, s->file(fresh.resources), &fresh.movieNumber))
    {
        mm_movie_state_dispose(&fresh, s);
        return 0;
    }
    replace(m, &fresh, s);
    return 1;
}
int mm_movie_load(MMMovieState *m, void *path, MMU32 number, const MMMovieOwnerServices *s)
{
    MMMovieState loaded;
    void *file;
    MMU32 scene_number;
    int valid;
    mm_movie_state_init(&loaded);
    mm_movie_title_init(&loaded.title, m->title.unit);
    file = s->open_file(s->context, path);
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
    if (!mm_movie_init_actors(&loaded, s) ||
        !s->read_rollcall(s->context, loaded.resources, number, &loaded.actors, &loaded.actorIdLimit))
    {
        mm_movie_state_dispose(&loaded, s);
        return 0;
    }
    file = s->file(loaded.resources);
    mm_chunk_title_read(&loaded.title, file, MM_MOVIE_CHUNK, number, &s->titles);
    while (mm_movie_session_scene_number(loaded.session, (MMU32)loaded.sceneCount, &scene_number) == MM_MOVIE_OK)
        ++loaded.sceneCount;
    replace(m, &loaded, s);
    return 1;
}
int mm_movie_state_scene_number(MMMovieState *m, MMU32 slot, MMU32 *number, void *file,
                                 void *context, MMMovieOpenSession open_session)
{
    const MMMovieSessionRoot *root = mm_movie_session_root(m->session);
    MMMovieSession *opened;
    MMU32 requested = m->movieNumber;
    if (!m->resources)
        return 0;
    if (!root || root->number != requested)
    {
        if (!open_session(context, file, &requested, &opened))
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
    if (!m->resources || !m->actors.stride)
        return 0;
    file = s->file(m->resources);
    had = s->previous_rollcall(s->context, file, m->movieNumber, &previous);
    if (!s->write_rollcall(s->context, file, m->movieNumber, &m->actors, &written))
        return 0;
    if (had)
        s->delete_previous(s->context, file, m->movieNumber, previous);
    if (!mm_chunk_title_write(&m->title, file, MM_MOVIE_CHUNK, m->movieNumber, &s->titles) ||
        !s->save(s->context, file))
        return 0;
    s->make_persistent(file);
    return 1;
}
int mm_movie_read_rollcall(const MMReadStream *stream, void *resources, MMRollCallTable *actors, MMI32 *limit,
                                 const MMRollCallReader *s)
{
    MMRollCallTable wire;
    int big;
    MMU32 i, count;
    MMI32 next = limit ? *limit : 0;
    unsigned char bytes[MM_MOVIE_ACTOR_BYTES];
    const void *name;
    MMMemoryStream memory;
    MMRollCallActor actor;
    if (!actors || actors->count || actors->bytes || actors->stride != sizeof(actor) + 4 ||
        mm_rollcall_table_init(&wire, 0, actors->unit, actors->encoding, actors->trash_unused,
                               &actors->storage.allocator) != MM_STORAGE_OK)
        return 0;
    if (mm_rollcall_table_read(&wire, stream, s->convert, s->context, &big) != MM_STORAGE_OK ||
        wire.stride != sizeof(bytes) + 4)
        goto fail;
    for (i = 0; i < wire.count; ++i)
    {
        if (!mm_rollcall_table_name(&wire, i, &name, &count) || !mm_rollcall_table_extra(&wire, i, bytes))
            goto fail;
        mm_memory_stream_init(&memory, bytes, sizeof(bytes));
        if (mm_movie_read_actor(&memory.stream, big, &actor.data, &next) != MM_MOVIE_OK)
            goto fail;
        actor.cache = NULL;
        if (!s->open_actor(s->context, &actor, resources))
            goto fail;
        if (!mm_rollcall_table_add(actors, name, count, &actor))
        {
            s->close_actor(s->context, &actor);
            goto fail;
        }
    }
    mm_rollcall_table_dispose(&wire);
    if (limit)
        *limit = next;
    return 1;
fail:
    mm_rollcall_table_dispose(&wire);
    close_rows(actors, s->context, s->close_actor);
    mm_rollcall_table_dispose(actors);
    return 0;
}
typedef struct RollCallLoad
{
    void *resources;
    MMRollCallTable *actors;
    MMI32 *limit;
    const MMRollCallReader *reader;
} RollCallLoad;

static int read_rollcall_payload(void *context, const MMReadStream *stream)
{
    const RollCallLoad *load = context;
    return mm_movie_read_rollcall(stream, load->resources, load->actors, load->limit, load->reader);
}

int mm_movie_load_rollcall(MMResourceCache *cache, MMU32 movie, void *resources, MMRollCallTable *actors,
                           MMI32 *limit, MMBlockTransform decode, const MMRollCallReader *reader)
{
    const MMChunkAccess chunks = mm_resource_cache_chunks(cache);
    RollCallLoad load;
    MMU32 number;
    int status, read;
    if (mm_chunk_link_number(&chunks, MM_MOVIE_CHUNK, movie, MM_ROLLCALL_SLOT, MM_ROLLCALL_CHUNK,
                             &number) != MM_CHUNK_OK)
        return MM_CHUNK_NOT_FOUND;
    load.resources = resources;
    load.actors = actors;
    load.limit = limit;
    load.reader = reader;
    status = mm_resource_cache_visit_payload(cache, MM_ROLLCALL_CHUNK, number, decode, NULL,
                                             read_rollcall_payload, &load, &read);
    return status == MM_CHUNK_OK && !read ? MM_CHUNK_BAD_FILE : status;
}

int mm_movie_write_rollcall(const MMArchiveWriter *archive, MMU32 movie, MMRollCallTable *actors, MMU32 *written)
{
    MMChunkChild child = {MM_ROLLCALL_CHUNK, 0, MM_ROLLCALL_SLOT};
    MMRollCallTable wire;
    MMOwnedBlock block;
    MMBlockIO io;
    MMStorageWriter writer;
    const void *name;
    MMU32 i, count, size;
    MMRollCallActor actor;
    unsigned char bytes[MM_MOVIE_ACTOR_BYTES];
    int added = 0, ok = 0;
    if (!archive || !archive->payloads || !written || !actors || actors->stride != sizeof(actor) + 4 ||
        mm_owned_block_init(&block, archive->payloads->handles, archive->payloads->files) != MM_DATA_OK)
        return 0;
    if (mm_rollcall_table_init(&wire, sizeof(bytes), actors->unit, actors->encoding, actors->trash_unused,
                               &actors->storage.allocator) != MM_STORAGE_OK)
        return 0;
    for (i = 0; i < actors->count; ++i)
    {
        if (!mm_rollcall_table_name(actors, i, &name, &count) || !mm_rollcall_table_extra(actors, i, &actor))
            goto done;
        mm_movie_write_actor(&actor.data, bytes, archive->big_endian);
        if (!mm_rollcall_table_add(&wire, name, count, bytes))
            goto done;
    }
    size = mm_rollcall_table_wire_size(&wire);
    if (!size)
        goto done;
    if (mm_archive_add_block(archive, child.type, size, written, &block) != MM_CHUNK_OK)
        goto done;
    added = 1;
    child.number = *written;
    ok = mm_owned_block_io(&block, &io) == MM_DATA_OK && mm_block_io_writer(&io, &writer) == MM_DATA_OK &&
         mm_rollcall_table_write(&wire, &writer, archive->big_endian) == MM_STORAGE_OK &&
         mm_chunk_adopt(&archive->edit, MM_MOVIE_CHUNK, movie, &child, 1) == MM_CHUNK_OK;
done:
    mm_owned_block_clear(&block);
    mm_rollcall_table_dispose(&wire);
    if (added && !ok)
        mm_chunk_delete(&archive->edit, child.type, child.number);
    return ok;
}
