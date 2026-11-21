/* Script resource loading converted from Kauai screxe.cpp and screxeg.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/script_resource.h"

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

static int open_stream(MMOwnedBlock *block, MMBlockTransform decode, void *context, MMBlockIO *io,
                       MMReadStream *stream)
{
    return mm_owned_block_code(block, 0, context, decode, NULL) == MM_DATA_OK &&
           mm_owned_block_io(block, io) == MM_DATA_OK && mm_block_io_stream(io, stream) == MM_DATA_OK;
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
    if (!open_stream(block, decode, decode_context, &io, &stream))
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

static int fetch_collection(MMOwnedBlock *block, void **out, MMI32 *bytes, int registry,
                            const MMScriptResourceServices *s)
{
    MMBlockIO io;
    MMReadStream stream;
    MMStringTable strings;
    MMArray rows;
    void *collection = NULL, *object;
    int other;
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
    if (mm_owned_block_io(block, &io) != MM_DATA_OK || mm_block_io_stream(&io, &stream) != MM_DATA_OK)
        goto fail;
    if (registry)
    {
        collection = s->read_strings(s->context, &stream, &strings, &other);
        if (!collection || strings.stride != 8)
            goto fail;
        if (other && strings.count)
            swap_words(strings.entries + 4, strings.count, strings.stride);
    }
    else
    {
        collection = s->read_rows(s->context, &stream, &rows, &other);
        if (!collection || rows.width != 4)
            goto fail;
    }
    object = s->wrap_collection(s->context, collection);
    if (!object)
        goto fail;
    *out = object;
    return 1;
fail:
    if (collection)
        s->release(s->context, collection);
    if (s->invalidate)
    {
        s->invalidate(s->context, out, sizeof(*out));
        s->invalidate(s->context, bytes, sizeof(*bytes));
    }
    return 0;
}

int mm_script_registry_fetch(MMOwnedBlock *block, void **out, MMI32 *bytes, const MMScriptResourceServices *s)
{
    return fetch_collection(block, out, bytes, 1, s);
}

int mm_script_colors_fetch(MMOwnedBlock *block, void **out, MMI32 *bytes, const MMScriptResourceServices *s)
{
    return fetch_collection(block, out, bytes, 0, s);
}
