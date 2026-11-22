/* Script resource loading converted from Kauai screxe.cpp and screxeg.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/script_resource.h"
#include <stdlib.h>
#include <string.h>

#define GSTX 0x47535458UL

typedef struct ScriptSource
{
    MMChunkAccess chunks;
    MMResourceCache *cache;
    const MMChunkPayloadServices *payloads;
    void *archive;
} ScriptSource;

typedef struct LiteralRead
{
    const MMScriptResourceServices *services;
    void *table;
} LiteralRead;

static void swap_words(unsigned char *data, MMU32 count, MMU32 stride)
{
    unsigned char first, second;
    while (count)
    {
        first = data[0];
        second = data[1];
        data[0] = data[3];
        data[1] = data[2];
        data[2] = second;
        data[3] = first;
        if (--count)
            data += stride;
    }
}

static int read_literals(void *context, const MMReadStream *stream)
{
    LiteralRead *read = (LiteralRead *)context;
    MMStringTable view;
    int other;
    read->table = read->services->read_strings(read->services->context, stream, &view, &other);
    return read->table != NULL;
}

static void *read_script(const ScriptSource *source, MMU32 type, MMU32 number, MMOwnedBlock *block,
                         MMBlockTransform decode, void *decode_context, const MMScriptResourceServices *s)
{
    MMBlockIO io;
    MMReadStream stream;
    MMArray words;
    LiteralRead literals;
    void *rows, *object;
    MMU32 child;
    int other, status, read = 0;
    if (mm_owned_block_decoded_stream(block, decode_context, decode, &io, &stream) != MM_DATA_OK)
        return NULL;
    rows = s->read_rows(s->context, &stream, &words, &other);
    if (!rows)
        return NULL;
    literals.services = s;
    literals.table = NULL;
    if (words.width != 4)
        goto fail;
    if (mm_chunk_link_number(&source->chunks, type, number, 0, GSTX, &child) == MM_CHUNK_OK)
    {
        status = source->cache
                     ? mm_resource_cache_visit_payload(source->cache, GSTX, child, decode, decode_context,
                                                       read_literals, &literals, &read)
                     : mm_chunk_read_payload(source->payloads, source->archive, GSTX, child, read_literals, &literals,
                                             &read);
        if (status != MM_CHUNK_OK || !read)
            goto fail;
    }
    object = s->create_script(s->context, rows, literals.table);
    if (!object)
        goto fail;
    if (other)
        swap_words(words.storage->buffers[0].data, words.count, words.width);
    return object;
fail:
    if (literals.table)
        s->release(s->context, literals.table);
    s->release(s->context, rows);
    return NULL;
}

int mm_script_resource_fetch(MMResourceCache *cache, MMU32 type, MMU32 number, MMOwnedBlock *block,
                             void **out, MMI32 *bytes, const MMScriptResourceServices *s)
{
    ScriptSource source;
    *bytes = (MMI32)block->view.length;
    if (!out)
        return 1;
    source.chunks = mm_resource_cache_chunks(cache);
    source.cache = cache;
    source.payloads = NULL;
    source.archive = NULL;
    *out = read_script(&source, type, number, block, s->decode, s->context, s);
    return *out != NULL;
}

void *mm_script_resource_archive(const MMChunkAccess *chunks, const MMChunkPayloadServices *payloads,
                                 void *archive, MMU32 type, MMU32 number, const MMScriptResourceServices *s)
{
    MMOwnedBlock block;
    ScriptSource source;
    void *object = NULL;
    if (mm_owned_block_init(&block, payloads->handles, payloads->files) != MM_DATA_OK)
        return NULL;
    source.chunks = *chunks;
    source.cache = NULL;
    source.payloads = payloads;
    source.archive = archive;
    if (mm_chunk_payload_bind_block(payloads, archive, type, number, &block) == MM_CHUNK_PAYLOAD_OK)
        object = read_script(&source, type, number, &block, payloads->decode, payloads->decode_context, s);
    mm_owned_block_clear(&block);
    return object;
}

struct MMScriptCollection
{
    MMCollectionStorage storage;
    MMCollectionHeader header;
    MMResourceObject binding;
    MMReference references;
    const MMScriptCollectionHost *host;
};

static MMResourceObject *collection_binding(void *p) { return &((MMScriptCollection *)p)->binding; }
static MMReference *collection_refs(void *p) { return &((MMScriptCollection *)p)->references; }

static void collection_destroy(void *p)
{
    MMScriptCollection *table = p;
    const MMStorageAllocator *objects = table->host->objects;
    mm_storage_dispose(&table->storage);
    mm_resource_object_dispose(table, mm_script_collection_services());
    if (objects)
        objects->release(objects->context, table, sizeof(*table));
    else
        free(table);
}

static void collection_mark(void *p)
{
    MMScriptCollection *table = p;
    if (table->host->mark)
        table->host->mark(table, &table->storage, &table->binding);
}

const MMResourceObjectServices *mm_script_collection_services(void)
{
    static const MMResourceObjectServices services = {
        collection_binding, collection_refs, collection_destroy, NULL, collection_mark};
    return &services;
}

void mm_script_collection_release(MMScriptCollection *table)
{
    if (table)
        mm_resource_object_release(table, mm_script_collection_services());
}

void mm_script_collection_priority(MMScriptCollection *table, MMI32 priority)
{
    mm_resource_object_priority(table, priority, mm_script_collection_services());
}

static void bind_strings(MMStringTable *view, const MMCollectionStorage *storage)
{
    view->strings = storage->buffers[0].data;
    view->entries = storage->buffers[1].data;
    view->string_capacity = storage->buffers[0].capacity;
    view->entry_capacity = storage->buffers[1].capacity;
}

static int reserve_strings(void *context, MMStringTable *view, MMU32 strings, MMU32 entries)
{
    MMCollectionStorage *storage = context;
    int result = mm_storage_ensure(storage, strings, entries, 0);
    bind_strings(view, storage);
    return result == MM_STORAGE_OK;
}

static MMStringTable registry_view(MMScriptCollection *table)
{
    MMStringTable view;
    memset(&view, 0, sizeof(view));
    bind_strings(&view, &table->storage);
    view.bytes = table->header.bytes;
    view.count = table->header.count;
    view.stride = table->header.width;
    view.free_count = table->header.free_count;
    view.unit_bytes = table->host->unit_bytes;
    view.context = &table->storage;
    view.reserve = reserve_strings;
    return view;
}

static int normalize_registry(MMScriptCollection *table)
{
    const unsigned short one = 1;
    const MMScriptCollectionHost *host = table->host;
    MMStringTable view = registry_view(table);
    if (table->header.big_endian != (*(const unsigned char *)&one == 0))
    {
        mm_strings_swap_offsets(&view);
        if (view.count)
            swap_words(view.entries + 4, view.count, view.stride);
    }
    if (table->header.system != host->encoding &&
        !mm_strings_translate(&view, table->header.system, host->encoding, host->convert, host->context))
        return 0;
    table->header.bytes = view.bytes;
    return 1;
}

int mm_script_registry_merge(MMScriptCollection *table, MMScriptRegistryPut put, void *context)
{
    MMStringTable view = registry_view(table);
    MMU32 i = view.count, count;
    const void *text;
    MMI32 id;
    int ok = 1;
    while (i--)
    {
        if (!mm_strings_get(&view, i, &text, &count))
        {
            ok = 0;
            continue;
        }
        memcpy(&id, view.entries + i * view.stride + 4, 4);
        if (!put(context, id, text, count))
            ok = 0;
    }
    return ok;
}

const void *mm_script_color_rows(const MMScriptCollection *table, MMU32 *count)
{
    *count = table->header.count;
    return table->storage.buffers[0].data;
}

static int fetch_collection(MMOwnedBlock *block, void **out, MMI32 *bytes, int registry,
                            const MMScriptCollectionHost *s)
{
    MMBlockIO io;
    MMReadStream stream;
    MMCollectionHeader header;
    MMScriptCollection *table = NULL;
    const MMStorageAllocator *objects = s->objects;
    *bytes = (MMI32)block->view.length;
    if (!out)
        return 1;
    if (mm_owned_block_code(block, 0, s->context, s->decode, NULL) != MM_DATA_OK)
    {
        if (registry)
            return 0;
        goto fail;
    }
    *bytes = (MMI32)block->view.length;
    if (mm_owned_block_io(block, &io) != MM_DATA_OK || mm_block_io_stream(&io, &stream) != MM_DATA_OK ||
        mm_collection_header_read(&stream, registry ? MM_COLLECTION_STRINGS : MM_COLLECTION_ARRAY, 0, &header) !=
            MM_STORAGE_OK || header.width != (registry ? 8U : 4U))
        goto fail;
    if (objects && (!objects->resize || !objects->release))
        goto fail;
    table = objects ? objects->resize(objects->context, NULL, 0, sizeof(*table), MM_STORAGE_NORMAL)
                    : malloc(sizeof(*table));
    if (!table)
        goto fail;
    memset(table, 0, sizeof(*table));
    table->host = s;
    table->header = header;
    mm_reference_init(&table->references);
    mm_resource_object_init(&table->binding);
    if (mm_storage_init(&table->storage, s->storage) != MM_STORAGE_OK ||
        mm_storage_read(&table->storage, &stream, header.header_bytes, header.first_bytes, header.second_bytes) !=
            MM_STORAGE_OK || (registry && !normalize_registry(table)))
        goto fail;
    *out = table;
    return 1;
fail:
    mm_script_collection_release(table);
    if (s->invalidate)
    {
        s->invalidate(s->context, out, sizeof(*out));
        s->invalidate(s->context, bytes, sizeof(*bytes));
    }
    return 0;
}

int mm_script_registry_fetch(MMOwnedBlock *block, void **out, MMI32 *bytes, const MMScriptCollectionHost *s)
{
    return fetch_collection(block, out, bytes, 1, s);
}

int mm_script_colors_fetch(MMOwnedBlock *block, void **out, MMI32 *bytes, const MMScriptCollectionHost *s)
{
    return fetch_collection(block, out, bytes, 0, s);
}
