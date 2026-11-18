/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/action_definition.h"
#include "mm/movie_session.h"
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

void mm_action_table_init(MMActionTable *table, const MMStorageAllocator *allocator, void *owner,
                          void (*release_owner)(void *))
{
    memset(table, 0, sizeof(*table));
    mm_storage_init(&table->storage, allocator);
    table->owner = owner;
    table->release_owner = release_owner;
}

void mm_action_table_dispose(MMActionTable *table)
{
    if (table)
    {
        mm_storage_dispose(&table->storage);
        if (table->release_owner)
            table->release_owner(table->owner);
    }
}

static MMGroup cel_group(const MMActionTable *table)
{
    MMGroup group = {NULL, MM_ACTION_CEL_BYTES, 0, 0, -1, 0};
    group.storage = (MMCollectionStorage *)&table->storage;
    group.count = table->header.count;
    group.bytes = table->header.first_bytes;
    return group;
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

static MMActionTable *copy_table(const MMActionTableSource *source, int kind, const MMActionDefinitionServices *s)
{
    MMCollectionHeader header = {0};
    MMActionTable *table;
    header.kind = kind == MM_ACTION_CELS ? MM_COLLECTION_GROUP : MM_COLLECTION_ARRAY;
    header.width = kind == MM_ACTION_CELS ? MM_ACTION_CEL_BYTES : MM_ACTION_MATRIX_BYTES;
    header.big_endian = native_big_endian();
    header.count = source->count;
    header.bytes = source->bytes;
    header.free_count = -1;
    if (mm_collection_layout(&header) != MM_STORAGE_OK)
        return NULL;
    table = s->new_table(s->context, kind);
    if (!table)
        return NULL;
    table->storage.min_grow[0] = 128;
    table->storage.min_grow[1] = kind == MM_ACTION_CELS ? 128 : 16;
    if (mm_storage_copy(&table->storage, source->storage, header.first_bytes, header.second_bytes) != MM_STORAGE_OK)
    {
        mm_action_table_dispose(table);
        return NULL;
    }
    table->header = header;
    return table;
}

void *mm_action_create(const MMActionTableSource *cels, const MMActionTableSource *matrices, MMU32 flags,
                       const MMActionDefinitionServices *s)
{
    void *object=s->create(s->context); MMActionDefinition *a;
    if (!object) return NULL;
    a=s->state(s->context,object); a->flags=flags;
    a->cels=copy_table(cels,MM_ACTION_CELS,s);
    if (a->cels)
    {
        a->matrices=copy_table(matrices,MM_ACTION_MATRICES,s);
        if (a->matrices) return object;
    }
    s->release(s->context,object); return NULL;
}

static MMI32 table_size(const MMActionTable *table)
{
    return (MMI32)(table->header.header_bytes + table->header.first_bytes + table->header.second_bytes);
}

int mm_action_fetch(void *file,MMU32 type,MMU32 number,void *block,void *out,void *size,const MMActionDefinitionServices *s)
{
    MMI32 bytes; void *object; MMActionDefinition *a;
    bytes=s->block_size(s->context,block,1); memcpy(size,&bytes,sizeof(bytes));
    if (!out) return 1;
    if (!s->unpack(s->context,block)) goto fail;
    bytes=s->block_size(s->context,block,0); memcpy(size,&bytes,sizeof(bytes));
    object=s->create(s->context);
    if (!object || !s->initialize(s->context,object,file,type,number))
    { if (object) s->release(s->context,object); goto fail; }
    memcpy(out,&object,sizeof(object)); a=s->state(s->context,object);
    bytes=s->object_size(s->context)+table_size(a->cels)+table_size(a->matrices);
    memcpy(size,&bytes,sizeof(bytes)); return 1;
fail:
    s->invalidate(s->context,out,sizeof(object)); s->invalidate(s->context,size,sizeof(bytes)); return 0;
}
typedef struct NativeActionRead
{
    const MMActionDefinitionServices *services;
    void *file;
    MMU32 type, number;
} NativeActionRead;

static int native_header(void *context, unsigned char *bytes)
{
    NativeActionRead *read = (NativeActionRead *)context;
    const MMActionDefinitionServices *s = read->services;
    void *block = s->find(s->context, read->file, read->type, read->number);
    if (!block || !s->unpack(s->context, block) || s->block_size(s->context, block, 0) < MM_ACTION_HEADER_BYTES ||
        !s->block_read(s->context, block, bytes, MM_ACTION_HEADER_BYTES))
        return MM_MOVIE_BAD_FILE;
    return MM_MOVIE_OK;
}

static int native_table(void *context, int kind, MMActionTable **out)
{
    NativeActionRead *read = (NativeActionRead *)context;
    const MMActionDefinitionServices *s = read->services;
    MMU32 child, type = mm_action_table_type(kind);
    void *block;
    MMActionTable *table;
    MMReadStream stream;
    int status = MM_MOVIE_BAD_FILE;
    if (!s->child(s->context, read->file, read->type, read->number, type, &child))
        return MM_MOVIE_ASSET_NOT_FOUND;
    block = s->find(s->context, read->file, type, child);
    if (!block)
        return MM_MOVIE_BAD_FILE;
    table = s->new_table(s->context, kind);
    if (!table)
        return MM_MOVIE_MEMORY;
    if (s->unpack(s->context, block))
    {
        stream = s->stream(s->context, block);
        status = mm_action_table_read(table, &stream, kind);
    }
    if (status == MM_MOVIE_OK)
        *out = table;
    else
        mm_action_table_dispose(table);
    return status;
}

static void native_bad_order(void *context)
{
    const MMActionDefinitionServices *s = ((NativeActionRead *)context)->services;
    if (s->bad_order)
        s->bad_order(s->context);
}

int mm_action_initialize(MMActionDefinition *a,void *file,MMU32 type,MMU32 number,const MMActionDefinitionServices *s)
{
    NativeActionRead native;
    MMActionRead read = {NULL, 0, native_header, native_table, native_bad_order};
    const unsigned char *p;
    MMActionTable *wire, *sounds;
    int big,status;
    MMU32 i,count,sid;
    MMTag tag;
    int ok = 0;
    native.services = s;
    native.file = file;
    native.type = type;
    native.number = number;
    read.context = &native;
    read.host_big_endian = s->host_big_endian;
    if (mm_action_read_cels(a, &read) != MM_MOVIE_OK || mm_action_read_matrices(a, &read) != MM_MOVIE_OK)
        return 0;
    status = native_table(&native, MM_ACTION_WIRE_SOUNDS, &wire);
    if (status == MM_MOVIE_ASSET_NOT_FOUND) return 1;
    if (status != MM_MOVIE_OK) return 0;
    count = wire->header.count;
    big = wire->header.big_endian;
    sounds = s->new_table(s->context, MM_ACTION_SOUNDS);
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
    return ok;
}
void mm_action_dispose(MMActionDefinition *a)
{
    mm_action_table_dispose(a->cels); a->cels=NULL;
    mm_action_table_dispose(a->matrices); a->matrices=NULL;
    mm_action_table_dispose(a->sounds); a->sounds=NULL;
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

void mm_action_get_sound(const MMActionDefinition *a,MMI32 cel,void *out,const MMActionDefinitionServices *s)
{
    MMU32 fields[2];
    MMTag tag;
    s->clear_sound_type(s->context,out);
    if (a->sounds && mm_action_get_cel(a, cel, fields) && fields[0] < 0x80000000UL &&
        mm_action_sound_tag(a->sounds, fields[0], &tag))
        s->publish_sound(s->context,out,&tag);
}
