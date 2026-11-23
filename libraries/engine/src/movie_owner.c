/* Movie ownership and persistence converted from engine movie_data.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/movie_owner.h"
#include "mm/movie_rollcall.h"
#include "mm/movie_root.h"
#include "mm/movie_session.h"
#include <string.h>
void mm_movie_state_init(MMMovieState *m, unsigned title_unit)
{
    memset(m, 0, sizeof(*m));
    m->movieNumber = 0xffffffffUL;
    mm_movie_title_init(&m->title, title_unit == 2 ? 2 : 1);
}
static void close_rows(MMRollCallTable *table, const MMTagServices *tags)
{
    MMU32 i;
    MMRollCallActor actor;
    for (i = 0; i < table->count; ++i)
        if (mm_rollcall_table_extra(table, i, &actor))
        {
            MMTag tag = mm_movie_rollcall_tag(&actor);
            mm_tag_close(&tag, tags);
        }
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
    close_rows(&m->actors, s->rollcall.reader.tags);
    mm_rollcall_table_dispose(&m->actors);
    memset(&m->actors, 0, sizeof(m->actors));
}
void mm_movie_release_resources(MMMovieState *m, const MMMovieOwnerServices *s)
{
    mm_movie_session_close(m->session);
    m->session = NULL;
    if (m->resources)
    {
        if (s->release_resources)
            s->release_resources(m->resources);
        else
            mm_resource_cache_release(m->resources);
        m->resources = NULL;
    }
}
void mm_movie_state_dispose(MMMovieState *m, const MMMovieOwnerServices *s)
{
    mm_movie_clear_actors(m, s);
    mm_movie_release_resources(m, s);
}
static void replace(MMMovieState *m, const MMMovieState *fresh, const MMMovieOwnerServices *s)
{
    mm_movie_state_dispose(m, s);
    *m = *fresh;
}
void mm_movie_bound_graph(void *resources, MMMovieSessionGraph *out, const MMMovieSessionBinding *binding)
{
    if (binding->graph)
        binding->graph(resources, out);
    else
        *out = mm_movie_cache_graph(resources);
}

static int open_session(void *resources, MMU32 *number, MMMovieSession **out, const MMMovieSessionBinding *s)
{
    MMMovieSessionGraph graph;
    int status;
    mm_movie_bound_graph(resources, &graph, s);
    status = mm_movie_session_open_graph(&graph, NULL, number, out);
    if (status != MM_MOVIE_OK && s->open_failed)
        s->open_failed(resources, status);
    return status == MM_MOVIE_OK;
}

static MMArchiveWriter movie_archive(void *resources, const MMMovieOwnerServices *s)
{
    return s->archive ? s->archive(resources) : mm_resource_cache_writer(resources);
}

int mm_movie_init_empty(MMMovieState *m, const MMMovieOwnerServices *s)
{
    MMMovieState fresh;
    MMArchiveWriter archive;
    mm_movie_state_init(&fresh, m->title.unit);
    fresh.resources = s->create_resources(s->context);
    if (!fresh.resources || !mm_movie_init_actors(&fresh, s))
        goto fail;
    archive = movie_archive(fresh.resources, s);
    if (!mm_movie_create_root(&archive, &fresh.movieNumber))
        goto fail;
    replace(m, &fresh, s);
    return 1;
fail:
    mm_movie_state_dispose(&fresh, s);
    return 0;
}
int mm_movie_load(MMMovieState *m, void *path, MMU32 number, const MMMovieOwnerServices *s)
{
    MMMovieState loaded;
    MMU32 scene_number;
    mm_movie_state_init(&loaded, m->title.unit);
    loaded.resources = s->open_resources(s->context, path);
    if (!loaded.resources)
        return 0;
    loaded.movieNumber = number;
    if (!open_session(loaded.resources, &loaded.movieNumber, &loaded.session, &s->session) ||
        !mm_movie_init_actors(&loaded, s) ||
        !mm_movie_load_bound_rollcall(loaded.resources, loaded.movieNumber, &loaded.actors, &loaded.actorIdLimit,
                                      &s->rollcall))
    {
        mm_movie_state_dispose(&loaded, s);
        return 0;
    }
    mm_chunk_title_read(&loaded.title, loaded.resources, MM_MOVIE_CHUNK, loaded.movieNumber, &s->titles);
    while (mm_movie_session_scene_number(loaded.session, (MMU32)loaded.sceneCount, &scene_number) == MM_MOVIE_OK)
        ++loaded.sceneCount;
    replace(m, &loaded, s);
    return 1;
}
int mm_movie_state_scene_number(MMMovieState *m, MMU32 slot, MMU32 *number, const MMMovieSessionBinding *binding)
{
    const MMMovieSessionRoot *root = mm_movie_session_root(m->session);
    MMMovieSession *opened;
    MMU32 requested = m->movieNumber;
    if (!m->resources)
        return 0;
    if (!root || root->number != requested)
    {
        if (!open_session(m->resources, &requested, &opened, binding))
            return 0;
        mm_movie_session_close(m->session);
        m->session = opened;
        m->movieNumber = requested;
    }
    return mm_movie_session_scene_number(m->session, slot, number) == MM_MOVIE_OK;
}
int mm_movie_save(MMMovieState *m, const MMMovieOwnerServices *s)
{
    MMArchiveWriter archive;
    MMChunkAccess chunks;
    MMChunkChild previous = {MM_ROLLCALL_CHUNK, 0, MM_ROLLCALL_SLOT};
    MMU32 written;
    int had;
    if (!m->resources || !m->actors.stride)
        return 0;
    archive = movie_archive(m->resources, s);
    chunks = mm_chunk_edit_access(&archive.edit);
    had = mm_chunk_link_number(&chunks, MM_MOVIE_CHUNK, m->movieNumber, previous.slot, previous.type,
                               &previous.number) == MM_CHUNK_OK;
    if (!mm_movie_write_rollcall(&archive, m->movieNumber, &m->actors, &written))
        return 0;
    if (had && mm_chunk_unlink(&archive.edit, MM_MOVIE_CHUNK, m->movieNumber, &previous) != MM_CHUNK_OK &&
        s->unlink_failed)
        s->unlink_failed(s->context);
    if (!mm_chunk_title_write(&m->title, m->resources, MM_MOVIE_CHUNK, m->movieNumber, &s->titles) ||
        !s->save(s->context, m->resources))
        return 0;
    s->make_persistent(m->resources);
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
    MMTag tag;
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
        tag = mm_movie_rollcall_tag(&actor);
        if (!mm_tag_open(&tag, resources, NULL, s->tags))
            goto fail;
        actor.cache = tag.cache;
        if (!mm_rollcall_table_add(actors, name, count, &actor))
        {
            mm_tag_close(&tag, s->tags);
            goto fail;
        }
    }
    mm_rollcall_table_dispose(&wire);
    if (limit)
        *limit = next;
    return 1;
fail:
    mm_rollcall_table_dispose(&wire);
    close_rows(actors, s->tags);
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

int mm_movie_load_bound_rollcall(void *resources, MMU32 movie, MMRollCallTable *actors, MMI32 *limit,
                                const MMRollCallBinding *binding)
{
    MMResourceCache *cache = binding->cache ? binding->cache(resources) : resources;
    const int status = mm_movie_load_rollcall(cache, movie, resources, actors, limit,
                                             binding->decode, &binding->reader);
    if (status == MM_CHUNK_NOT_FOUND && binding->missing)
        binding->missing(binding->reader.context);
    return status == MM_CHUNK_OK;
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
