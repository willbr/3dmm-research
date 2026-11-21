/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/action_definition.h"
#include "mm/movie_session.h"
#include <stdlib.h>
#include <string.h>
enum { MM_ACTION_WIRE_TAG_BYTES = 16 };
static MMU32 word(const unsigned char *p,int big)
{
    MMU32 value=0; int i;
    for (i=0;i<4;++i) value=(value<<8)|p[big?i:3-i];
    return value;
}
static void swap(unsigned char *p,MMI32 count,int width)
{
    MMI32 i; int j; unsigned char t;
    for (i=0;i<count;++i,p+=width) for (j=0;j<width/2;++j)
    { t=p[j]; p[j]=p[width-1-j]; p[width-1-j]=t; }
}

static void decode_words(const void *bytes, int other, void *out, int count, int width)
{
    if (out != bytes)
        memcpy(out, bytes, (size_t)(count * width));
    if (other)
        swap((unsigned char *)out, count, width);
}

void mm_action_decode_cel(const void *bytes, int other, void *out)
{
    decode_words(bytes, other, out, 2, 4);
}

void mm_action_decode_part(const void *bytes, int other, void *out)
{
    decode_words(bytes, other, out, 2, 2);
}

void mm_action_decode_matrix(const void *bytes, int other, void *out)
{
    decode_words(bytes, other, out, 12, 4);
}

MMU32 mm_action_table_type(int kind)
{
    static const MMU32 types[] = {MM_ACTION_GGCL, MM_ACTION_GLXF, MM_ACTION_GLMS};
    return types[kind];
}

static int native_big_endian(void)
{
    const unsigned short one = 1;
    return *(const unsigned char *)&one == 0;
}

int mm_action_table_init(MMActionTable *table, const MMStorageAllocator *allocator)
{
    memset(table, 0, sizeof(*table));
    return mm_storage_init(&table->storage, allocator) == MM_STORAGE_OK;
}

MMActionTable *mm_action_table_new(const MMStorageAllocator *allocator)
{
    MMActionTable empty, *table;
    if (!mm_action_table_init(&empty, allocator))
        return NULL;
    table = (MMActionTable *)empty.storage.allocator.resize(empty.storage.allocator.context, NULL, 0,
                                                           sizeof(*table), MM_STORAGE_NORMAL);
    if (table)
    {
        *table = empty;
        table->allocated = 1;
    }
    return table;
}

void mm_action_table_dispose(MMActionTable *table)
{
    if (!table || mm_storage_dispose(&table->storage) != MM_STORAGE_OK)
        return;
    if (table->allocated)
        table->storage.allocator.release(table->storage.allocator.context, table, sizeof(*table));
}

static MMGroup cel_group(const MMActionTable *table)
{
    MMGroup group = {NULL, MM_ACTION_CEL_BYTES, 0, 0, -1, 0};
    group.storage = (MMCollectionStorage *)&table->storage;
    group.count = table->header.count;
    group.bytes = table->header.first_bytes;
    return group;
}

void mm_action_cels_prepare(MMActionTable *table)
{
    table->header.kind = MM_COLLECTION_GROUP;
    table->header.width = MM_ACTION_CEL_BYTES;
    table->header.big_endian = native_big_endian();
    table->header.free_count = -1;
    mm_collection_layout(&table->header);
    table->storage.min_grow[0] = 128;
    table->storage.min_grow[1] = 128;
}

unsigned char *mm_action_cel_append(MMActionTable *table, MMU32 sound, MMI32 distance, MMU32 parts)
{
    MMGroup group = cel_group(table);
    MMU32 offset = group.bytes, fields[2];
    fields[0] = sound;
    fields[1] = (MMU32)distance;
    if (parts >= MM_STORAGE_LIMIT / MM_ACTION_PART_BYTES ||
        mm_group_add(&group, parts * MM_ACTION_PART_BYTES, NULL, fields, NULL) != MM_STORAGE_OK)
        return NULL;
    table->header.count = group.count;
    table->header.bytes = table->header.first_bytes = group.bytes;
    table->header.second_bytes = group.count * 8;
    return table->storage.buffers[0].data + offset + MM_ACTION_CEL_BYTES;
}

int mm_action_matrices_prepare(MMActionTable *table, MMU32 count)
{
    MMCollectionHeader header = {0};
    header.kind = MM_COLLECTION_ARRAY;
    header.width = MM_ACTION_MATRIX_BYTES;
    header.big_endian = native_big_endian();
    header.count = count;
    header.free_count = -1;
    table->storage.min_grow[0] = 128;
    table->storage.min_grow[1] = 16;
    if (mm_collection_layout(&header) != MM_STORAGE_OK ||
        mm_storage_ensure(&table->storage, header.first_bytes, 0, 0) != MM_STORAGE_OK)
        return 0;
    table->header = header;
    return 1;
}

void mm_action_matrix_put(MMActionTable *table, MMU32 index, const void *matrix)
{
    memcpy(table->storage.buffers[0].data + index * MM_ACTION_MATRIX_BYTES, matrix, MM_ACTION_MATRIX_BYTES);
}

int mm_action_table_read(MMActionTable *table, const MMReadStream *stream, int kind)
{
    static const MMU32 widths[] = {MM_ACTION_CEL_BYTES, MM_ACTION_MATRIX_BYTES, MM_ACTION_WIRE_TAG_BYTES};
    MMCollectionHeader header;
    MMGroup group;
    int status;
    if (kind < MM_ACTION_CELS || kind > MM_ACTION_WIRE_SOUNDS)
        return MM_MOVIE_BAD_FILE;
    status = mm_collection_header_read(stream, kind == MM_ACTION_CELS ? MM_COLLECTION_GROUP : MM_COLLECTION_ARRAY,
                                       0, &header);
    if (status != MM_STORAGE_OK)
        return status == MM_STORAGE_IO ? MM_MOVIE_IO : MM_MOVIE_BAD_FILE;
    if (header.width != widths[kind])
        return MM_MOVIE_BAD_FILE;
    table->storage.min_grow[0] = kind == MM_ACTION_CELS ? 0 : 128;
    table->storage.min_grow[1] = kind == MM_ACTION_CELS ? 128 : 16;
    status = mm_storage_read(&table->storage, stream, header.header_bytes, header.first_bytes, header.second_bytes);
    if (status != MM_STORAGE_OK)
        return status == MM_STORAGE_IO ? MM_MOVIE_IO : MM_MOVIE_MEMORY;
    table->header = header;
    if (kind == MM_ACTION_CELS && header.big_endian != native_big_endian())
    {
        group = cel_group(table);
        mm_group_swap_locations(&group);
    }
    return MM_MOVIE_OK;
}

static MMArray sound_array(MMActionTable *table)
{
    MMArray array = {NULL, sizeof(MMTag), 0, -1, 0, 0};
    array.storage = &table->storage;
    array.count = table->header.count;
    return array;
}

int mm_action_sounds_reserve(MMActionTable *table, MMU32 count)
{
    MMArray array = sound_array(table);
    table->storage.min_grow[0] = 128;
    table->storage.min_grow[1] = 16;
    return mm_array_reserve(&array, count, 0) == MM_STORAGE_OK;
}

int mm_action_sounds_append(MMActionTable *table, const MMTag *tag)
{
    MMArray array = sound_array(table);
    if (mm_array_add(&array, tag, NULL) != MM_STORAGE_OK)
        return 0;
    table->header.count = array.count;
    return 1;
}

int mm_action_sound_tag(const MMActionTable *table, MMU32 index, MMTag *out)
{
    if (!table || index >= table->header.count)
        return 0;
    memcpy(out, table->storage.buffers[0].data + index * sizeof(*out), sizeof(*out));
    return 1;
}

int mm_action_read_cels(MMActionDefinition *action, const MMActionRead *read)
{
    unsigned char bytes[MM_ACTION_HEADER_BYTES];
    int big, status = read->header(read->context, bytes);
    if (status != MM_MOVIE_OK)
        return status;
    if (bytes[0] == 1 && bytes[1] == 0)
        big = 0;
    else if (bytes[0] == 0 && bytes[1] == 1)
        big = 1;
    else
    {
        if (read->bad_order)
            read->bad_order(read->context);
        big = read->host_big_endian;
    }
    action->flags = word(bytes + 4, big);
    return read->table(read->context, MM_ACTION_CELS, &action->cels);
}

int mm_action_read_matrices(MMActionDefinition *action, const MMActionRead *read)
{
    return read->table(read->context, MM_ACTION_MATRICES, &action->matrices);
}

void *mm_action_create(MMActionTable *cels, MMActionTable *matrices, MMU32 flags,
                       const MMActionDefinitionServices *s)
{
    void *object = s->create(s->context);
    MMActionDefinition *action;
    if (!object)
        return NULL;
    action = s->state(s->context, object);
    action->flags = flags;
    action->cels = cels;
    action->matrices = matrices;
    return object;
}

static MMI32 table_size(const MMActionTable *table)
{
    return (MMI32)(table->header.header_bytes + table->header.first_bytes + table->header.second_bytes);
}

int mm_action_fetch(MMResourceCache *cache, MMU32 type, MMU32 number, MMOwnedBlock *block, void *out, void *size,
                    const MMActionDefinitionServices *s)
{
    MMI32 bytes = (MMI32)block->view.length;
    void *object = NULL;
    MMActionDefinition *a;
    memcpy(size, &bytes, sizeof(bytes));
    if (!out)
        return 1;
    if (mm_owned_block_code(block, 0, s->context, s->decode, NULL) != MM_DATA_OK)
        goto fail;
    bytes = (MMI32)block->view.length;
    memcpy(size, &bytes, sizeof(bytes));
    object = s->create(s->context);
    if (!object || !mm_action_initialize(s->state(s->context, object), cache, type, number, block, s))
        goto fail;
    memcpy(out, &object, sizeof(object));
    a = s->state(s->context, object);
    bytes = s->object_size(s->context) + table_size(a->cels) + table_size(a->matrices);
    memcpy(size, &bytes, sizeof(bytes));
    return 1;
fail:
    if (object)
        s->release(s->context, object);
    if (s->invalidate)
    {
        s->invalidate(s->context, out, sizeof(object));
        s->invalidate(s->context, size, sizeof(bytes));
    }
    return 0;
}
typedef struct LiveActionRead
{
    const MMActionDefinitionServices *services;
    MMResourceCache *cache;
    MMOwnedBlock block, *header;
    MMChunkAccess chunks;
    MMU32 type, number;
} LiveActionRead;

static int live_header(void *context, unsigned char *bytes)
{
    LiveActionRead *read = (LiveActionRead *)context;
    const MMActionDefinitionServices *s = read->services;
    MMOwnedBlock *block = read->header;
    MMBlockIO io;
    if (!block)
    {
        block = &read->block;
        if (mm_resource_cache_read_block(read->cache, read->type, read->number, block) != MM_CHUNK_OK ||
            mm_owned_block_code(block, 0, s->context, s->decode, NULL) != MM_DATA_OK)
            return MM_MOVIE_BAD_FILE;
    }
    if (mm_owned_block_io(block, &io) != MM_DATA_OK ||
        mm_block_read(&io, 0, bytes, MM_ACTION_HEADER_BYTES, 0) != MM_DATA_OK)
        return MM_MOVIE_BAD_FILE;
    return MM_MOVIE_OK;
}

static int live_table(void *context, int kind, MMActionTable **out)
{
    LiveActionRead *read = (LiveActionRead *)context;
    const MMActionDefinitionServices *s = read->services;
    MMU32 child, type = mm_action_table_type(kind);
    MMActionTable *table;
    MMBlockIO io;
    MMReadStream stream;
    int status = MM_MOVIE_BAD_FILE;
    if (mm_chunk_link_number(&read->chunks, read->type, read->number, 0, type, &child) != MM_CHUNK_OK)
        return MM_MOVIE_ASSET_NOT_FOUND;
    if (mm_resource_cache_read_block(read->cache, type, child, &read->block) != MM_CHUNK_OK)
        return MM_MOVIE_BAD_FILE;
    table = mm_action_table_new(s->table_allocator.resize ? &s->table_allocator : NULL);
    if (!table)
        return MM_MOVIE_MEMORY;
    if (mm_owned_block_code(&read->block, 0, s->context, s->decode, NULL) == MM_DATA_OK &&
        mm_owned_block_io(&read->block, &io) == MM_DATA_OK && mm_block_io_stream(&io, &stream) == MM_DATA_OK)
        status = mm_action_table_read(table, &stream, kind);
    if (status == MM_MOVIE_OK)
        *out = table;
    else
        mm_action_table_dispose(table);
    return status;
}

static void live_bad_order(void *context)
{
    const MMActionDefinitionServices *s = ((LiveActionRead *)context)->services;
    if (s->bad_order)
        s->bad_order(s->context);
}

int mm_action_initialize(MMActionDefinition *a, MMResourceCache *cache, MMU32 type, MMU32 number, MMOwnedBlock *header,
                         const MMActionDefinitionServices *s)
{
    LiveActionRead live;
    MMActionRead read = {NULL, 0, live_header, live_table, live_bad_order};
    const unsigned char *p;
    MMActionTable *wire = NULL, *sounds;
    int big,status;
    MMU32 i,count,sid;
    MMTag tag;
    int ok = 0;
    if (mm_owned_block_init(&live.block, cache->services.handles, cache->services.block_files) != MM_DATA_OK)
        return 0;
    live.services = s;
    live.cache = cache;
    live.header = header;
    live.chunks = mm_resource_cache_chunks(cache);
    live.type = type;
    live.number = number;
    read.context = &live;
    read.host_big_endian = s->host_big_endian;
    if (mm_action_read_cels(a, &read) != MM_MOVIE_OK || mm_action_read_matrices(a, &read) != MM_MOVIE_OK)
        goto done;
    status = live_table(&live, MM_ACTION_WIRE_SOUNDS, &wire);
    if (status != MM_MOVIE_OK)
    {
        ok = status == MM_MOVIE_ASSET_NOT_FOUND;
        goto done;
    }
    count = wire->header.count;
    big = wire->header.big_endian;
    sounds = mm_action_table_new(s->table_allocator.resize ? &s->table_allocator : NULL);
    if (!sounds)
        goto done;
    if (!mm_action_sounds_reserve(sounds, count))
    {
        mm_action_table_dispose(sounds);
        goto done;
    }
    a->sounds = sounds;
    for (i=0;i<count;++i)
    {
        p=wire->storage.buffers[0].data+i*wire->header.width;
        sid=word(p,big); memcpy(&tag.sid,&sid,sizeof(sid));
        tag.cache=NULL; tag.type=word(p+8,big); tag.number=word(p+12,big);
        if (!mm_action_sounds_append(sounds,&tag))
            goto done;
    }
    ok = 1;
done:
    mm_action_table_dispose(wire);
    mm_owned_block_clear(&live.block);
    return ok;
}
void mm_action_dispose(MMActionDefinition *a)
{
    mm_action_table_dispose(a->cels); a->cels=NULL;
    mm_action_table_dispose(a->matrices); a->matrices=NULL;
    mm_action_table_dispose(a->sounds); a->sounds=NULL;
}

struct MMActionResource
{
    MMResourceObject binding;
    MMReference references;
    MMActionDefinition action;
    MMStorageAllocator allocator;
    void (*mark_memory)(void *);
};

static MMResourceObject *action_binding(void *object)
{
    return &((MMActionResource *)object)->binding;
}

static MMReference *action_references(void *object)
{
    return &((MMActionResource *)object)->references;
}

static MMActionDefinition *action_definition(void *context, void *object)
{
    (void)context;
    return &((MMActionResource *)object)->action;
}

static void action_destroy(void *object)
{
    MMActionResource *resource = object;
    const MMStorageAllocator allocator = resource->allocator;
    mm_action_dispose(&resource->action);
    mm_resource_object_dispose(object, mm_action_resource_services());
    if (allocator.release)
        allocator.release(allocator.context, resource, sizeof(*resource));
    else
        free(resource);
}

static void mark_action_table(const MMActionTable *table, void (*mark)(void *))
{
    if (table)
    {
        mark((void *)table);
        mark(table->storage.buffers[0].data);
        mark(table->storage.buffers[1].data);
    }
}

static void action_mark_memory(void *object)
{
    const MMActionResource *resource = object;
    void (*mark)(void *) = resource->mark_memory;
    if (mark)
    {
        mark(object);
        mark_action_table(resource->action.cels, mark);
        mark_action_table(resource->action.matrices, mark);
        mark_action_table(resource->action.sounds, mark);
    }
}

const MMResourceObjectServices *mm_action_resource_services(void)
{
    static const MMResourceObjectServices services = {
        action_binding, action_references, action_destroy, NULL, action_mark_memory
    };
    return &services;
}

const MMActionDefinition *mm_action_resource_definition(const MMActionResource *resource)
{
    return &resource->action;
}

static void *action_allocate(void *context)
{
    const MMActionResourceHost *host = context;
    const MMStorageAllocator *allocator = host->objects;
    MMActionResource *resource = allocator ?
        allocator->resize(allocator->context, NULL, 0, sizeof(*resource), MM_STORAGE_NORMAL) :
        malloc(sizeof(*resource));
    if (resource)
    {
        memset(resource, 0, sizeof(*resource));
        if (allocator)
            resource->allocator = *allocator;
        resource->mark_memory = host->mark_memory;
        mm_reference_init(&resource->references);
    }
    return resource;
}

static void action_release(void *context, void *object)
{
    (void)context;
    mm_resource_object_release(object, mm_action_resource_services());
}

static MMI32 action_size(void *context)
{
    (void)context;
    return sizeof(MMActionResource);
}

int mm_action_resource_read(MMResourceCache *cache, MMU32 type, MMU32 number, MMOwnedBlock *block,
                            void **object, MMI32 *bytes, const MMActionResourceHost *host)
{
    MMActionDefinitionServices services = {0};
    services.context = (void *)host;
    services.host_big_endian = native_big_endian();
    services.state = action_definition;
    services.create = action_allocate;
    services.release = action_release;
    if (host->tables)
        services.table_allocator = *host->tables;
    services.decode = host->decode;
    services.object_size = action_size;
    services.bad_order = host->bad_order;
    return mm_action_fetch(cache, type, number, block, object, bytes, &services);
}

MMI32 mm_action_cel_count(const MMActionDefinition *a)
{ return a->cels ? (MMI32)a->cels->header.count : 0; }

static const unsigned char *cel_row(const MMActionTable *table, MMI32 cel, MMI32 *parts)
{
    MMGroup group;
    MMGroupLocation location;
    if (!table || cel < 0)
        return NULL;
    group = cel_group(table);
    if (mm_group_location(&group, (MMU32)cel, &location) != MM_STORAGE_OK)
        return NULL;
    *parts = (location.bytes - MM_ACTION_CEL_BYTES) / MM_ACTION_PART_BYTES;
    return table->storage.buffers[0].data + location.offset;
}

MMI32 mm_action_part_count(const MMActionDefinition *a, MMI32 cel)
{
    MMI32 count;
    return cel_row(a->cels, cel, &count) ? count : -1;
}

int mm_action_get_cel(const MMActionDefinition *a, MMI32 cel, void *out)
{
    MMI32 parts;
    const unsigned char *row = cel_row(a->cels, cel, &parts);
    if (!row)
        return 0;
    mm_action_decode_cel(row, a->cels->header.big_endian != native_big_endian(), out);
    return 1;
}

int mm_action_get_part(const MMActionDefinition *a, MMI32 cel, MMI32 part, void *out)
{
    MMI32 parts;
    const unsigned char *row = cel_row(a->cels, cel, &parts);
    if (!row || part < 0 || part >= parts)
        return 0;
    mm_action_decode_part(row + MM_ACTION_CEL_BYTES + part * MM_ACTION_PART_BYTES,
                          a->cels->header.big_endian != native_big_endian(), out);
    return 1;
}

int mm_action_get_matrix(const MMActionDefinition *a, MMI32 i, void *out)
{
    const MMActionTable *table = a->matrices;
    if (!table || i < 0 || (MMU32)i >= table->header.count)
        return 0;
    mm_action_decode_matrix(table->storage.buffers[0].data + i * MM_ACTION_MATRIX_BYTES,
                            table->header.big_endian != native_big_endian(), out);
    return 1;
}

int mm_action_cel_sound(const MMActionDefinition *a, MMI32 cel, MMTag *tag)
{
    MMU32 fields[2];
    return a->sounds && mm_action_get_cel(a, cel, fields) && fields[0] < 0x80000000UL &&
        mm_action_sound_tag(a->sounds, fields[0], tag);
}

void mm_action_get_sound(const MMActionDefinition *a,MMI32 cel,void *out,const MMActionDefinitionServices *s)
{
    MMTag tag;
    s->clear_sound_type(s->context,out);
    if (mm_action_cel_sound(a, cel, &tag))
        s->publish_sound(s->context,out,&tag);
}
