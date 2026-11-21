/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/sound_resource.h"
#include "mm/actor_sound.h"
#include "mm/block_io.h"
#include "mm/midi.h"
#include "mm/movie_session.h"
#include "mm/string_wire.h"
#include <stdlib.h>
#include <string.h>
static MMU32 word(const unsigned char *p,int n,int big)
{ MMU32 v=0; int i; for (i=0;i<n;++i) v=(v<<8)|p[big?i:n-1-i]; return v; }
static void put(unsigned char *p,int n,MMU32 v,int big)
{ int i; for (i=0;i<n;++i) { p[big?n-1-i:i]=(unsigned char)(v&255U); v>>=8; } }
typedef struct SoundHeaderRead
{
    MMSoundHeader header;
    int host_big_endian, loaded;
} SoundHeaderRead;

static int read_header(void *context, const MMReadStream *stream)
{
    SoundHeaderRead *read = (SoundHeaderRead *)context;
    MMSoundHeader *out = &read->header;
    unsigned char b[MM_SOUND_HEADER_BYTES];
    MMU32 value;
    int status = mm_movie_read_fixed(stream, b, sizeof(b));
    if (status != MM_MOVIE_OK)
        return status;
    value = word(b + 4, 4, read->host_big_endian);
    memcpy(&out->style, &value, 4);
    value = word(b + 8, 4, read->host_big_endian);
    memcpy(&out->volume, &value, 4);
    out->valid_order = (b[0] == 1 && b[1] == 0) || (b[0] == 0 && b[1] == 1);
    out->invalid = b[12] != 0;
    out->reserved_nonzero = (b[13] | b[14] | b[15]) != 0;
    read->loaded = 1;
    return MM_MOVIE_OK;
}

int mm_sound_read_header(const MMMovieSessionGraph *graph, const MMChunkEntry *entry, int host_big_endian,
                         MMSoundHeader *out)
{
    SoundHeaderRead read;
    int status;
    read.host_big_endian = host_big_endian;
    read.loaded = 0;
    status = graph->payload(graph->context, entry, read_header, &read);
    if (status != MM_MOVIE_OK)
        return status;
    if (!read.loaded)
        return MM_MOVIE_BAD_FILE;
    *out = read.header;
    return MM_MOVIE_OK;
}

int mm_sound_catalog_style(const MMMovieSessionGraph *graph, MMU32 number, int host_big_endian, MMI32 *style)
{
    MMChunkAccess chunks;
    MMChunkEntry entry;
    MMChunkChild child;
    MMSoundHeader header;
    MMU32 parent, i;
    graph->access(graph->context, &chunks);
    if (mm_chunk_find(&chunks, MM_SOUND_TYPE, number, &parent) != MM_CHUNK_OK ||
        mm_chunk_at(&chunks, parent, &entry) != MM_CHUNK_OK ||
        mm_sound_read_header(graph, &entry, host_big_endian, &header) != MM_MOVIE_OK ||
        header.invalid || header.style < MM_SOUND_STYLE_EFFECT || header.style > MM_SOUND_STYLE_MUSIC)
        return 0;
    for (i = 0; i < entry.child_count; ++i)
    {
        if (mm_chunk_child_at(&chunks, parent, i, &child) != MM_CHUNK_OK)
            return 0;
        if (child.type == MM_SOUND_WAVE || child.type == MM_SOUND_MIDI)
        {
            *style = header.style;
            return 1;
        }
    }
    return 0;
}

static void encode_header(unsigned char b[MM_SOUND_HEADER_BYTES], MMI32 style, MMI32 volume,
                          int invalid, int host_big_endian, MMU32 system)
{
    memset(b, 0, MM_SOUND_HEADER_BYTES);
    put(b, 2, 1, host_big_endian);
    put(b + 2, 2, system, host_big_endian);
    put(b + 4, 4, (MMU32)style, host_big_endian);
    put(b + 8, 4, (MMU32)volume, host_big_endian);
    b[12] = (unsigned char)(invalid != 0);
}
static int read_block_header(MMOwnedBlock *block, MMSoundHeader *out, const MMSoundResourceServices *s)
{
    SoundHeaderRead read = {0};
    MMBlockIO io;
    MMReadStream stream;
    int status;
    read.host_big_endian = s->host_big_endian;
    if (mm_owned_block_decoded_stream(block, s->context, s->decode, &io, &stream) != MM_DATA_OK)
        return MM_MOVIE_BAD_FILE;
    status = read_header(&read, &stream);
    if (status == MM_MOVIE_OK)
        *out = read.header;
    return status;
}

static int read_cached_header(MMResourceCache *cache, MMU32 type, MMU32 number, MMOwnedBlock *root,
                              MMSoundHeader *out, const MMSoundResourceServices *s)
{
    MMOwnedBlock block;
    int status = MM_MOVIE_BAD_FILE;
    if (root)
        return read_block_header(root, out, s);
    if (mm_owned_block_init(&block, cache->services.handles, cache->services.block_files) != MM_DATA_OK)
        return status;
    if (mm_resource_cache_read_block(cache, type, number, &block) == MM_CHUNK_OK)
        status = read_block_header(&block, out, s);
    mm_owned_block_clear(&block);
    return status;
}

struct MMSoundOwner
{
    MMResourceObject binding;
    MMReference references;
    const MMSoundResourceHost *host;
    MMSoundState state;
};

static MMU32 owner_bytes(const MMSoundResourceHost *host)
{
    return sizeof(MMSoundOwner) + (MM_STRING_MAX + 2) * host->name_unit;
}

MMSoundOwner *mm_sound_owner_create(const MMSoundResourceHost *host)
{
    const MMStorageAllocator *allocator = host->objects;
    MMSoundOwner *owner;
    MMU32 bytes;
    if (host->name_unit != 1 && host->name_unit != 2)
        return NULL;
    bytes = owner_bytes(host);
    owner = allocator ? allocator->resize(allocator->context, NULL, 0, bytes, MM_STORAGE_NORMAL) : malloc(bytes);
    if (owner)
    {
        memset(owner, 0, bytes);
        mm_reference_init(&owner->references);
        owner->host = host;
    }
    return owner;
}

MMSoundState *mm_sound_owner_state(MMSoundOwner *owner) { return &owner->state; }
const MMResourceObject *mm_sound_owner_binding(const MMSoundOwner *owner) { return &owner->binding; }
MMI32 mm_sound_owner_references(const MMSoundOwner *owner) { return owner->references.count; }

MMStringValue mm_sound_owner_name(MMSoundOwner *owner)
{
    MMStringValue name = {0};
    name.buffer = owner + 1;
    name.unit = owner->host->name_unit;
    return name;
}

static MMResourceObject *sound_binding(void *p) { return &((MMSoundOwner *)p)->binding; }
static MMReference *sound_references(void *p) { return &((MMSoundOwner *)p)->references; }

static void destroy_sound(void *object)
{
    MMSoundOwner *owner = object;
    const MMSoundResourceHost *host = owner->host;
    const MMStorageAllocator *allocator = host->objects;
    mm_resource_object_dispose(owner, mm_sound_owner_services());
    if (allocator)
        allocator->release(allocator->context, owner, owner_bytes(host));
    else
        free(owner);
}

static void mark_sound(void *object)
{
    const MMSoundOwner *owner = object;
    const MMSoundResourceHost *host = owner->host;
    if (host->mark_memory)
        host->mark_memory(object);
    if (host->mark_cache && owner->binding.cache && !owner->binding.attached)
        host->mark_cache(owner->binding.cache);
}

const MMResourceObjectServices *mm_sound_owner_services(void)
{
    static const MMResourceObjectServices services = {sound_binding, sound_references, destroy_sound, NULL, mark_sound};
    return &services;
}

void mm_sound_owner_retain(MMSoundOwner *owner)
{
    mm_reference_add(&owner->references);
}

void mm_sound_owner_release(MMSoundOwner *owner)
{
    if (owner)
        mm_resource_object_release(owner, mm_sound_owner_services());
}

static void publish_sound(MMSoundState *state, const MMChunkAccess *chunks, MMU32 position, const MMSoundHeader *header)
{
    MMChunkChild child;
    state->resource.style = header->style;
    state->resource.volume = header->volume;
    state->invalid = (unsigned char)header->invalid;
    if (header->invalid)
        return;
    if (mm_chunk_child_type(chunks, position, 0, 0, &child) == MM_CHUNK_OK)
    {
        state->resource.number = child.number;
        state->resource.type = child.type;
        state->silent = 0;
    }
    else
        state->silent = 1;
}

static int graph_header(const MMMovieSessionGraph *graph, MMU32 type, MMU32 number, int host_big,
                        MMChunkAccess *chunks, MMU32 *position, MMSoundHeader *header)
{
    MMChunkEntry entry;
    graph->access(graph->context, chunks);
    return mm_chunk_find(chunks, type, number, position) == MM_CHUNK_OK &&
           mm_chunk_at(chunks, *position, &entry) == MM_CHUNK_OK &&
           mm_sound_read_header(graph, &entry, host_big, header) == MM_MOVIE_OK;
}

int mm_sound_read_graph(MMSoundState *state, const MMMovieSessionGraph *graph, MMU32 type, MMU32 number, int host_big)
{
    MMChunkAccess chunks;
    MMSoundHeader header;
    MMU32 position;
    if (!graph_header(graph, type, number, host_big, &chunks, &position, &header))
        return 0;
    publish_sound(state, &chunks, position, &header);
    return 1;
}

static int read_sound(MMSoundOwner *owner, void *file, MMResourceCache *cache, MMU32 type, MMU32 number,
                      MMOwnedBlock *root)
{
    const MMSoundResourceServices *s = owner->host->services;
    MMMovieSessionGraph graph;
    MMChunkAccess chunks;
    MMSoundHeader header;
    MMU32 position;
    if (cache)
    {
        chunks = mm_resource_cache_chunks(cache);
        if (mm_chunk_find(&chunks, type, number, &position) != MM_CHUNK_OK ||
            read_cached_header(cache, type, number, root, &header, s) != MM_MOVIE_OK)
            return 0;
    }
    else
    {
        s->graph(s->context, file, &graph);
        if (!graph_header(&graph, type, number, s->host_big_endian, &chunks, &position, &header))
            return 0;
    }
    if (!header.valid_order && s->bad_order)
        s->bad_order(s->context);
    if (!s->read_name(s->context, file, cache, type, number, owner))
        return 0;
    publish_sound(&owner->state, &chunks, position, &header);
    return 1;
}

int mm_sound_read(MMSoundOwner *owner, void *file, MMU32 type, MMU32 number)
{
    return read_sound(owner, file, NULL, type, number, NULL);
}

int mm_sound_fetch(MMResourceCache *cache, MMU32 type, MMU32 number, MMOwnedBlock *root, void **out,
                   MMI32 *bytes, const MMSoundResourceHost *host)
{
    const MMSoundResourceServices *s = host->services;
    MMSoundOwner *owner;
    *bytes = host->cache_bytes;
    if (!out)
        return 1;
    owner = mm_sound_owner_create(host);
    if (!owner || !read_sound(owner, NULL, cache, type, number, root))
    {
        s->invalidate_output(s->context, out, sizeof(*out));
        s->invalidate_output(s->context, bytes, sizeof(*bytes));
        mm_sound_owner_release(owner);
        return 0;
    }
    owner->state.resource.cache = cache;
    *out = owner;
    return 1;
}

int mm_sound_info(void *file, MMU32 type, MMU32 number, void *invalid_out, void *style_out, void *volume_out,
                  const MMSoundResourceHost *host)
{
    const MMSoundResourceServices *s = host->services;
    MMSoundOwner *owner = mm_sound_owner_create(host);
    int result;
    if (!owner)
        return 0;
    result = mm_sound_read(owner, file, type, number);
    if (result)
    {
        if (invalid_out)
            s->publish_invalid(s->context, invalid_out, owner->state.invalid != 0);
        if (style_out)
            memcpy(style_out, &owner->state.resource.style, sizeof(owner->state.resource.style));
        if (volume_out)
            memcpy(volume_out, &owner->state.resource.volume, sizeof(owner->state.resource.volume));
    }
    mm_sound_owner_release(owner);
    return result;
}
int mm_sound_write(const MMReadStream *payload, int wave, MMI32 style, const MMChunkNameValue *name,
                   MMU32 *number, const MMArchiveWriter *writer)
{
    const MMChunkEdit *edit = &writer->edit;
    const MMChunkPayloadServices *files = writer->payloads;
    unsigned char header[MM_SOUND_HEADER_BYTES];
    MMChunkChild child = {0};
    MMOwnedBlock block;
    MMBlockIO io;
    MMStorageWriter output;
    int initialized, ok = 0;
    if (!payload || !payload->read_at || payload->length >= MM_STORAGE_LIMIT)
        return 0;
    encode_header(header, wave ? style : MM_SOUND_STYLE_MUSIC, 65536L, 0, writer->big_endian, writer->system);
    if (mm_chunk_add_bytes(edit, files->files, MM_SOUND_TYPE, header, sizeof(header), number) != MM_CHUNK_OK)
    {
        if (writer->invalidate_number)
            writer->invalidate_number(number);
        return 0;
    }
    child.type = wave ? MM_SOUND_WAVE : MM_SOUND_MIDI;
    initialized = mm_owned_block_init(&block, files->handles, files->files) == MM_DATA_OK;
    if (initialized &&
        mm_chunk_add_child_block(edit, MM_SOUND_TYPE, *number, &child, payload->length, &block) == MM_CHUNK_OK)
        ok = mm_owned_block_io(&block, &io) == MM_DATA_OK && mm_block_io_writer(&io, &output) == MM_DATA_OK &&
             mm_stream_copy(payload, &output, writer->transfer_allocator) == MM_DATA_OK &&
             mm_chunk_name_set_key(edit, &writer->names, MM_SOUND_TYPE, *number, name) == MM_CHUNK_OK;
    if (!ok)
        mm_chunk_delete(edit, MM_SOUND_TYPE, *number);
    if (initialized)
        mm_owned_block_clear(&block);
    return ok;
}

int mm_sound_import_midi(const MMReadStream *source, const MMChunkNameValue *name, MMU32 *number,
                         const MMArchiveWriter *s)
{
    MMCollectionStorage input, decoded;
    MMMemoryStream payload;
    MMU32 bytes;
    int parsed, result = MM_SOUND_BAD_MIDI;
    if (!source || !source->length || source->length >= MM_STORAGE_LIMIT)
        return result;
    mm_storage_init(&input, NULL);
    mm_storage_init(&decoded, NULL);
    parsed = mm_storage_resize_exact(&input, 0, source->length, MM_STORAGE_NORMAL) == MM_STORAGE_OK &&
             mm_read_exact(source, 0, input.buffers[0].data, source->length) &&
             mm_midi_import(input.buffers[0].data, source->length, &decoded, &bytes) &&
             mm_memory_stream_init(&payload, decoded.buffers[0].data, bytes);
    mm_storage_dispose(&input);
    if (parsed)
        result = mm_sound_write(&payload.stream, 0, MM_SOUND_STYLE_MUSIC, name, number, s);
    mm_storage_dispose(&decoded);
    return result;
}

int mm_sound_invalidate(MMSoundState *state, const MMResourceObject *binding, const MMChunkEdit *edit,
                        const MMSoundResourceServices *s)
{
    MMResourceCache *cache = binding->cache;
    MMChunkAccess chunks;
    MMChunkChild child;
    MMU32 position;
    unsigned char b[MM_SOUND_HEADER_BYTES];
    if (!cache || !cache->file)
        return 0;
    chunks = mm_resource_cache_chunks(cache);
    if (mm_chunk_find(&chunks, MM_SOUND_TYPE, binding->number, &position) != MM_CHUNK_OK ||
        mm_chunk_child_type(&chunks, position, 0, 0, &child) != MM_CHUNK_OK)
        return 0;
    encode_header(b, state->resource.style, state->resource.volume, 1, s->host_big_endian, s->system);
    if (mm_chunk_put_bytes(edit, cache->services.block_files, binding->type, binding->number, b, sizeof(b)) != MM_CHUNK_OK)
        return 0;
    mm_chunk_unlink(edit, binding->type, binding->number, &child);
    state->invalid = 1;
    return 1;
}
MMI32 mm_sound_class(MMI32 style,int loop)
{
    return loop ? (style == MM_SOUND_STYLE_MUSIC ? MM_SOUND_CLASS_LOOP_MIDI : MM_SOUND_CLASS_LOOP_WAVE)
                : MM_SOUND_CLASS_NONLOOP;
}
MMI32 mm_sound_priority(MMI32 style,int tool)
{ if (style==4) return 1; if (style==3) return tool==0?1:tool==2?5:3; if (style==2) return tool==0?2:tool==2?6:4; return 0; }
void mm_sound_play(const MMSoundState *state, MMSoundLastMidi *last, MMI32 object_id, int loop, int queue,
                   MMI32 volume, MMI32 priority, int actor, MMU32 start, const MMSoundResourceServices *s)
{
    const MMSoundResource *resource = &state->resource;
    MMI32 number, sound_class, instance = 0;
    if (state->invalid)
        return;
    number = mm_sound_queue_number(resource->style, object_id, actor);
    sound_class = mm_sound_class(resource->style, loop);
    if (resource->style == MM_SOUND_STYLE_MUSIC && resource->type == last->type &&
        resource->number == last->number && s->playing(s->context, last->instance))
        return;
    if (!queue || state->silent)
        s->stop(s->context, number);
    if (!state->silent)
        instance = s->play(s->context, resource, number, volume, loop ? 2147483647L : 1, start, priority, sound_class);
    if (resource->style == MM_SOUND_STYLE_MUSIC)
    {
        last->instance = instance;
        last->type = resource->type;
        last->number = resource->number;
    }
}
