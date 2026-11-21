/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/template_create.h"
#include "mm/collections.h"
#include "mm/movie_session.h"
#include "mm/wide.h"
#include <string.h>
void mm_template_state_init(MMTemplateState *state)
{ memset(state, 0, sizeof(*state)); }
static void diagnostic(const MMTemplateCreateServices *s,int n) { if (s->diagnostic) s->diagnostic(s->context,n); }
static MMU32 word(const unsigned char *p,int n,int big)
{ MMU32 v=0; int i; for (i=0;i<n;++i) v=(v<<8)|p[big?i:n-1-i]; return v; }
static void put(unsigned char *p,int n,MMU32 value,int big)
{ int i; for (i=0;i<n;++i) { p[big?n-1-i:i]=(unsigned char)(value&255U); value>>=8; } }

MMU32 mm_template_table_type(int kind)
{
    static const MMU32 types[] = {MM_GLPI, MM_GLBS, MM_GGCM};
    return types[kind];
}

MMI32 mm_template_decode_costume_word(const unsigned char *bytes, int big)
{
    return mm_signed_word(word(bytes, MM_TEMPLATE_COSTUME_WORD_BYTES, big));
}

static MMGroup costume_group(MMTemplateCostumeTable *table)
{
    MMGroup group = {NULL, MM_TEMPLATE_COSTUME_WORD_BYTES, 0, 0, -1, 0};
    group.storage = &table->storage;
    group.count = table->rows;
    group.bytes = table->bytes;
    return group;
}

int mm_template_costumes_init(MMTemplateCostumeTable *table, const MMStorageAllocator *allocator)
{
    const unsigned short one = 1;
    memset(table, 0, sizeof(*table));
    if (mm_storage_init(&table->storage, allocator) != MM_STORAGE_OK)
        return 0;
    table->storage.min_grow[0] = 16 * MM_TEMPLATE_COSTUME_WORD_BYTES;
    table->storage.min_grow[1] = 16 * 8;
    table->big_endian = *(const unsigned char *)&one == 0;
    return 1;
}

MMTemplateCostumeTable *mm_template_costumes_new(const MMStorageAllocator *allocator)
{
    MMTemplateCostumeTable empty, *table;
    if (!mm_template_costumes_init(&empty, allocator))
        return NULL;
    table = (MMTemplateCostumeTable *)empty.storage.allocator.resize(empty.storage.allocator.context, NULL, 0,
                                                                   sizeof(*table), MM_STORAGE_NORMAL);
    if (table)
    {
        *table = empty;
        table->allocated = 1;
    }
    return table;
}

void mm_template_costumes_dispose(MMTemplateCostumeTable *table)
{
    if (!table || mm_storage_dispose(&table->storage) != MM_STORAGE_OK)
        return;
    if (table->allocated)
        table->storage.allocator.release(table->storage.allocator.context, table, sizeof(*table));
    else
        table->rows = table->bytes = 0;
}

static const unsigned char *costume_row(const MMTemplateCostumeTable *table, MMI32 set, MMI32 *count)
{
    MMGroupLocation location;
    const unsigned char *row;
    /* The location reader borrows storage without editing it. */
    MMGroup group = costume_group((MMTemplateCostumeTable *)table);
    if (set < 0 || mm_group_location(&group, (MMU32)set, &location) != MM_STORAGE_OK)
        return NULL;
    row = table->storage.buffers[0].data + location.offset;
    *count = mm_template_decode_costume_word(row, table->big_endian);
    if (*count < 0 || *count > (location.bytes - MM_TEMPLATE_COSTUME_WORD_BYTES) / MM_TEMPLATE_COSTUME_WORD_BYTES)
        return NULL;
    return row + MM_TEMPLATE_COSTUME_WORD_BYTES;
}

int mm_template_costumes_read(MMTemplateCostumeTable *table, const MMReadStream *stream, MMI32 expected)
{
    MMCollectionHeader header;
    MMGroup group;
    MMI32 count, i;
    int status = mm_collection_header_read(stream, MM_COLLECTION_GROUP, 0, &header);
    if (status != MM_STORAGE_OK)
        return status == MM_STORAGE_IO ? MM_MOVIE_IO : MM_MOVIE_BAD_FILE;
    if (header.width != MM_TEMPLATE_COSTUME_WORD_BYTES || header.count != (MMU32)expected)
        return MM_MOVIE_BAD_FILE;
    /* The legacy reader allocates the records at their exact decoded size. */
    table->storage.min_grow[0] = 0;
    status = mm_storage_read(&table->storage, stream, header.header_bytes, header.first_bytes, header.second_bytes);
    if (status != MM_STORAGE_OK)
        return status == MM_STORAGE_IO ? MM_MOVIE_IO : MM_MOVIE_MEMORY;
    table->rows = header.count;
    table->bytes = header.first_bytes;
    if (table->big_endian != header.big_endian)
    {
        group = costume_group(table);
        mm_group_swap_locations(&group);
        table->big_endian = header.big_endian;
    }
    for (i = 0; i < expected; ++i)
        if (!costume_row(table, i, &count))
            return MM_MOVIE_BAD_FILE;
    return MM_MOVIE_OK;
}

int mm_template_costumes_reserve(MMTemplateCostumeTable *table, MMU32 rows, MMU32 bytes)
{
    MMGroup group = costume_group(table);
    return mm_group_reserve(&group, rows, bytes, 0) == MM_STORAGE_OK;
}

int mm_template_costumes_append(MMTemplateCostumeTable *table, const MMI32 *ids, MMI32 count)
{
    const unsigned short one = 1;
    MMGroup group = costume_group(table);
    if (table->big_endian != (*(const unsigned char *)&one == 0) || count < 0 ||
        (MMU32)count >= MM_STORAGE_LIMIT / MM_TEMPLATE_COSTUME_WORD_BYTES ||
        mm_group_add(&group, (MMU32)count * MM_TEMPLATE_COSTUME_WORD_BYTES, ids, &count, NULL) != MM_STORAGE_OK)
        return 0;
    table->rows = group.count;
    table->bytes = group.bytes;
    return 1;
}

MMI32 mm_template_costumes_count(const MMTemplateCostumeTable *table, MMI32 set)
{
    MMI32 count;
    return table && costume_row(table, set, &count) ? count : -1;
}

int mm_template_costumes_id(const MMTemplateCostumeTable *table, MMI32 set, MMI32 index, MMI32 *id)
{
    MMI32 count;
    const unsigned char *ids = table ? costume_row(table, set, &count) : NULL;
    if (!ids || index < 0 || index >= count)
        return 0;
    *id = mm_template_decode_costume_word(ids + (MMU32)index * MM_TEMPLATE_COSTUME_WORD_BYTES, table->big_endian);
    return 1;
}

MMI32 mm_template_costumes_wire_size(const MMTemplateCostumeTable *table)
{
    return (MMI32)(20 + table->bytes + table->rows * 8);
}

int mm_template_read_shape(MMTemplateState *state, const MMTemplateRead *reader)
{
    int status = reader->table(reader->context, MM_TEMPLATE_PARENTS, &state->parents);
    if (status != MM_MOVIE_OK)
        return status;
    return reader->table(reader->context, MM_TEMPLATE_SETS, &state->sets);
}

int mm_template_read_costumes(MMTemplateState *state, const MMTemplateRead *reader)
{
    return reader->costumes(reader->context, state->set_count, &state->costumes);
}

int mm_template_count_sets(MMTemplateState *state, const MMTemplateParts *parts)
{
    MMI32 i, set;
    int status;
    state->set_count = -1;
    for (i = 0; i < parts->count; ++i)
    {
        status = parts->part_set(parts->context, i, &set);
        if (status != MM_MOVIE_OK)
            return status;
        if (set >= 0x7fffffffL)
            return MM_MOVIE_BAD_FILE;
        if (set > state->set_count)
            state->set_count = set;
    }
    ++state->set_count;
    return MM_MOVIE_OK;
}

int mm_template_scan_costumes(MMI32 set, const MMTemplateCostumeScan *s)
{
    MMTemplateChild child;
    MMU32 ordinal;
    MMI32 candidate;
    int step;
    for (ordinal = 0;; ++ordinal)
    {
        step = s->child(s->context, ordinal, &child);
        if (step <= 0)
            return step == 0;
        if (child.type != MM_CMTL)
            continue;
        if (!s->costume_set(s->context, &child, &candidate))
            return 0;
        if (set >= 0 && candidate != set)
            continue;
        step = s->visit(s->context, &child, candidate);
        if (step <= 0)
            return step == 0;
    }
}

int mm_template_bind_defaults(MMI32 sets, const MMTemplateDefaultServices *s)
{
    MMI32 set, id;
    for (set = 0; set < sets; ++set)
        if (!s->costume_id(s->context, set, 0, &id) || !s->prepare(s->context, set, id))
            return 0;
    if (s->apply)
        for (set = 0; set < sets; ++set)
            s->apply(s->context, set);
    return 1;
}

int mm_template_decode_header(const unsigned char *bytes, int host_big_endian, MMTemplateState *state)
{
    int big, valid = 1;
    if (bytes[0] == 1 && bytes[1] == 0)
        big = 0;
    else if (bytes[0] == 0 && bytes[1] == 1)
        big = 1;
    else
    {
        valid = 0;
        big = host_big_endian;
    }
    state->x = (unsigned short)word(bytes + 4, 2, big);
    state->y = (unsigned short)word(bytes + 6, 2, big);
    state->z = (unsigned short)word(bytes + 8, 2, big);
    state->flags = word(bytes + 12, 4, big);
    return valid;
}

static int template_stream(MMOwnedBlock *block, MMBlockIO *io, MMReadStream *stream,
                           const MMTemplateCreateServices *s)
{
    return mm_owned_block_code(block, 0, s->context, s->decode, NULL) == MM_DATA_OK &&
           mm_owned_block_io(block, io) == MM_DATA_OK && mm_block_io_stream(io, stream) == MM_DATA_OK;
}

int mm_template_read_header(void *object, MMResourceCache *cache, MMU32 type, MMU32 number, MMOwnedBlock *block,
                            const MMTemplateCreateServices *s)
{
    MMOwnedBlock scratch;
    MMBlockIO io;
    MMReadStream stream;
    unsigned char bytes[MM_TEMPLATE_HEADER_BYTES];
    MMTemplateState header, *state;
    int owned = block == NULL, ok = 0;
    if (owned)
    {
        if (mm_owned_block_init(&scratch, cache->services.handles, cache->services.block_files) != MM_DATA_OK)
            return 0;
        block = &scratch;
    }
    if ((!owned || mm_resource_cache_read_block(cache, type, number, block) == MM_CHUNK_OK) &&
        template_stream(block, &io, &stream, s) && mm_read_exact(&stream, 0, bytes, sizeof(bytes)))
    {
        if (!mm_template_decode_header(bytes, s->host_big_endian, &header))
            diagnostic(s, MM_TEMPLATE_BAD_ORDER);
        state = s->state(s->context, object);
        state->x = header.x;
        state->y = header.y;
        state->z = header.z;
        state->flags = header.flags;
        ok = s->read_name(s->context, object, type, number);
    }
    if (owned)
        mm_owned_block_clear(&scratch);
    return ok;
}
void mm_template_encode_header(unsigned char *bytes, int big, MMU32 system, const MMTemplateState *state)
{
    put(bytes,2,1,big); put(bytes+2,2,system,big);
    put(bytes+4,2,state->x,big); put(bytes+6,2,state->y,big); put(bytes+8,2,state->z,big);
    bytes[10]=bytes[11]=0; put(bytes+12,4,state->flags,big);
}
int mm_template_write_header(const MMTemplateState *state, const MMChunkNameValue *name, MMU32 type,
                             MMU32 *number, const MMArchiveWriter *writer)
{
    unsigned char bytes[MM_TEMPLATE_HEADER_BYTES];
    mm_template_encode_header(bytes, writer->big_endian, writer->system, state);
    if (mm_chunk_add_bytes(&writer->edit, writer->payloads->files, type, bytes, sizeof(bytes), number) != MM_CHUNK_OK)
    {
        if (writer->invalidate_number)
            writer->invalidate_number(number);
        return 0;
    }
    if (mm_chunk_name_set_key(&writer->edit, &writer->names, MM_TMPL, *number, name) != MM_CHUNK_OK)
    {
        mm_chunk_delete(&writer->edit, type, *number);
        return 0;
    }
    return 1;
}
int mm_template_fetch(MMResourceCache *cache, MMU32 type, MMU32 number, MMOwnedBlock *block, void **out,
                      MMI32 *bytes, const MMTemplateCreateServices *s)
{
    void *object;
    MMChunkAccess chunks;
    MMU32 child;
    MMTemplateState t;
    int text;
    *bytes = (MMI32)block->view.length;
    if (!out)
        return 1;
    chunks = mm_resource_cache_chunks(cache);
    text = mm_chunk_link_number(&chunks, type, number, 0, MM_TDT, &child) == MM_CHUNK_OK;
    object=s->create(s->context,text);
    if (!object || !s->initialize(s->context,object,type,number,block))
    {
        if (object) s->release(s->context,object);
        s->invalidate(s->context,out,sizeof(*out)); s->invalidate(s->context,bytes,sizeof(*bytes)); return 0;
    }
    *out=object; t=*s->state(s->context,object);
    if (t.flags&2U) *bytes=s->object_size(s->context,1);
    else *bytes=s->object_size(s->context,0)+mm_part_indices_wire_size(t.parents)+
        mm_part_indices_wire_size(t.sets)+mm_template_costumes_wire_size(t.costumes);
    return 1;
}
typedef struct LegacyCostumeTable
{
    const MMTemplateCreateServices *services;
    const MMChunkAccess *chunks;
    void *cache;
    MMU32 parent;
    MMI32 ids[50], count;
} LegacyCostumeTable;

static int table_child(void *context, MMU32 ordinal, MMTemplateChild *out)
{
    LegacyCostumeTable *table = (LegacyCostumeTable *)context;
    MMChunkChild child;
    if (mm_chunk_child_at(table->chunks, table->parent, ordinal, &child) != MM_CHUNK_OK)
        return 0;
    out->type = child.type;
    out->number = child.number;
    out->id = child.slot;
    return 1;
}

static int table_costume_set(void *context, const MMTemplateChild *child, MMI32 *set)
{
    LegacyCostumeTable *table = (LegacyCostumeTable *)context;
    const MMTemplateCreateServices *s = table->services;
    void *costume = s->fetch_custom(s->context, table->cache, child->type, child->number);
    if (!costume)
        return 0;
    *set = s->custom_set(s->context, costume);
    s->release(s->context, costume);
    return 1;
}

static int table_add_costume(void *context, const MMTemplateChild *child, MMI32 set)
{
    LegacyCostumeTable *table = (LegacyCostumeTable *)context;
    (void)set;
    if (table->count == (MMI32)(sizeof(table->ids) / sizeof(table->ids[0])))
    {
        diagnostic(table->services, MM_TEMPLATE_COSTUME_LIMIT);
        return -1;
    }
    table->ids[table->count++] = (MMI32)child->id;
    return 1;
}

static int build_costumes(void *object, const MMChunkAccess *chunks, MMU32 parent, MMTemplateState *state,
                          const MMTemplateCreateServices *s)
{
    LegacyCostumeTable table;
    MMTemplateCostumeScan scan;
    MMI32 set;
    int ok = 0;
    diagnostic(s, MM_TEMPLATE_BUILD_COSTUMES);
    table.cache = s->create_cache(s->context);
    if (!table.cache)
        return 0;
    state->costumes = mm_template_costumes_new(&s->table_allocator);
    *s->state(s->context, object) = *state;
    if (!state->costumes)
        goto done;
    table.services = s;
    table.chunks = chunks;
    table.parent = parent;
    scan.context = &table;
    scan.child = table_child;
    scan.costume_set = table_costume_set;
    scan.visit = table_add_costume;
    for (set = 0; set < state->set_count; ++set)
    {
        table.count = 0;
        if (!mm_template_scan_costumes(set, &scan) ||
            !s->append_costumes(s->context, state->costumes, table.ids, table.count))
            goto done;
    }
    ok = 1;
done:
    s->release(s->context, table.cache);
    return ok;
}

typedef struct LiveTemplateRead
{
    const MMTemplateCreateServices *services;
    MMTemplateState *state;
    MMResourceCache *cache;
    MMOwnedBlock block;
    MMChunkAccess chunks;
    MMU32 parent;
} LiveTemplateRead;

static int read_live_table(void *context, int kind, MMPartIndexList **out)
{
    LiveTemplateRead *read = (LiveTemplateRead *)context;
    const MMTemplateCreateServices *s = read->services;
    MMChunkChild child;
    MMPartIndexList *list;
    MMBlockIO io;
    MMReadStream stream;
    int status = MM_MOVIE_BAD_FILE;
    if (mm_chunk_child_exact(&read->chunks, read->parent, 0, mm_template_table_type(kind), &child) != MM_CHUNK_OK)
        return MM_MOVIE_ASSET_NOT_FOUND;
    if (mm_resource_cache_read_block(read->cache, child.type, child.number, &read->block) != MM_CHUNK_OK)
        return MM_MOVIE_BAD_FILE;
    list = mm_part_indices_new(&s->table_allocator);
    if (!list)
        return MM_MOVIE_MEMORY;
    if (template_stream(&read->block, &io, &stream, s))
        status = mm_part_indices_read(list, &stream);
    if (status == MM_MOVIE_OK)
        *out = list;
    else
        mm_part_indices_dispose(list);
    return status;
}

static int read_live_costumes(void *context, MMI32 expected, MMTemplateCostumeTable **out)
{
    LiveTemplateRead *read = (LiveTemplateRead *)context;
    const MMTemplateCreateServices *s = read->services;
    MMChunkChild child;
    MMTemplateCostumeTable *table;
    MMBlockIO io;
    MMReadStream stream;
    int status = MM_MOVIE_BAD_FILE;
    if (mm_chunk_child_exact(&read->chunks, read->parent, 0, MM_GGCM, &child) != MM_CHUNK_OK)
        return MM_TEMPLATE_COSTUMES_ABSENT;
    if (mm_resource_cache_read_block(read->cache, child.type, child.number, &read->block) != MM_CHUNK_OK)
        return MM_MOVIE_BAD_FILE;
    table = mm_template_costumes_new(&s->table_allocator);
    if (!table)
        return MM_MOVIE_MEMORY;
    if (template_stream(&read->block, &io, &stream, s))
        status = mm_template_costumes_read(table, &stream, expected);
    if (status == MM_MOVIE_OK)
        *out = table;
    else
        mm_template_costumes_dispose(table);
    return status;
}

static int live_part_set(void *context, MMI32 index, MMI32 *set)
{
    const LiveTemplateRead *read = (const LiveTemplateRead *)context;
    *set = mm_part_index_get(&read->state->sets->view, index);
    return MM_MOVIE_OK;
}

int mm_template_initialize(void *object, MMResourceCache *cache, MMU32 type, MMU32 number, MMOwnedBlock *block,
                           const MMTemplateCreateServices *s)
{
    LiveTemplateRead live;
    MMTemplateRead reader = {NULL, read_live_table, read_live_costumes};
    MMTemplateParts parts = {NULL, 0, live_part_set};
    MMTemplateState *state;
    MMChunkChild child;
    int status, ok = 0;
    if (!mm_template_read_header(object, cache, type, number, block, s))
        return 0;
    state = s->state(s->context, object);
    live.services = s;
    live.state = state;
    live.cache = cache;
    live.chunks = mm_resource_cache_chunks(cache);
    if (mm_chunk_find(&live.chunks, type, number, &live.parent) != MM_CHUNK_OK ||
        mm_owned_block_init(&live.block, cache->services.handles, cache->services.block_files) != MM_DATA_OK)
        return 0;
    reader.context = &live;
    if (mm_template_read_shape(state, &reader) != MM_MOVIE_OK)
        goto done;
    if (s->debug_checks && mm_chunk_child_exact(&live.chunks, live.parent, 0, MM_GLDC, &child) == MM_CHUNK_OK)
        diagnostic(s, MM_TEMPLATE_OBSOLETE);
    parts.context = &live;
    parts.count = state->sets->view.count;
    if (mm_template_count_sets(state, &parts) != MM_MOVIE_OK)
        goto done;
    state->custom_count = 0;
    while (mm_chunk_child_exact(&live.chunks, live.parent, (MMU32)state->custom_count, MM_CMTL, &child) == MM_CHUNK_OK)
        ++state->custom_count;
    state->action_count = 0;
    while (mm_chunk_child_exact(&live.chunks, live.parent, (MMU32)state->action_count, MM_ACTN, &child) == MM_CHUNK_OK)
        ++state->action_count;
    status = mm_template_read_costumes(state, &reader);
    if (status == MM_TEMPLATE_COSTUMES_ABSENT)
        ok = build_costumes(object, &live.chunks, live.parent, state, s);
    else
        ok = status == MM_MOVIE_OK;
done:
    mm_owned_block_clear(&live.block);
    return ok;
}
void mm_template_dispose(void *object,const MMTemplateCreateServices *s)
{
    MMTemplateState t; t=*s->state(s->context,object);
    mm_part_indices_dispose(t.parents); t.parents=NULL; *s->state(s->context,object)=t;
    mm_part_indices_dispose(t.sets); t.sets=NULL; *s->state(s->context,object)=t;
    mm_template_costumes_dispose(t.costumes); t.costumes=NULL; *s->state(s->context,object)=t;
}
void *mm_template_create_body(void *object,const MMTemplateCreateServices *s)
{
    MMTemplateState t; void *body; t=*s->state(s->context,object);
    body=s->create_body(s->context,&t.parents->view,&t.sets->view);
    if (!body || !s->default_costume(s->context,object,body)) { if (body) s->release(s->context,body); return NULL; }
    return body;
}
typedef struct DefaultCostumes
{
    const MMTemplateCreateServices *services;
    void *object, *body, **refs;
} DefaultCostumes;

static int live_costume_id(void *context, MMI32 set, MMI32 index, MMI32 *id)
{
    DefaultCostumes *costumes = (DefaultCostumes *)context;
    const MMTemplateCreateServices *s = costumes->services;
    *id = s->costume_id(s->context, costumes->object, set, index);
    return 1;
}

static int live_costume_prepare(void *context, MMI32 set, MMI32 id)
{
    DefaultCostumes *costumes = (DefaultCostumes *)context;
    const MMTemplateCreateServices *s = costumes->services;
    void *costume = s->custom(s->context, costumes->object, id);
    if (!costume)
        return 0;
    costumes->refs[set] = costume;
    if (s->custom_set(s->context, costume) != set)
        diagnostic(s, MM_TEMPLATE_WRONG_SET);
    return 1;
}

static void live_costume_apply(void *context, MMI32 set)
{
    DefaultCostumes *costumes = (DefaultCostumes *)context;
    const MMTemplateCreateServices *s = costumes->services;
    s->apply_custom(s->context, costumes->body, costumes->refs[set]);
}

int mm_template_default_costume(void *object, void *body, const MMTemplateCreateServices *s)
{
    DefaultCostumes costumes;
    MMTemplateDefaultServices defaults;
    void *storage = NULL;
    MMI32 i, sets = s->state(s->context, object)->set_count;
    int ok;
    if (!s->allocate_refs(s->context, sets, &storage))
        return 0;
    costumes.services = s;
    costumes.object = object;
    costumes.body = body;
    costumes.refs = storage;
    defaults.context = &costumes;
    defaults.costume_id = live_costume_id;
    defaults.prepare = live_costume_prepare;
    defaults.apply = live_costume_apply;
    ok = mm_template_bind_defaults(sets, &defaults);
    for (i = 0; i < sets; ++i)
        if (costumes.refs[i])
        {
            s->release(s->context, costumes.refs[i]);
            costumes.refs[i] = NULL;
        }
    s->free_memory(s->context, &storage);
    return ok;
}
