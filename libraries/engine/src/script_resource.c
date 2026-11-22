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

typedef struct ScriptTable
{
    MMCollectionStorage storage;
    MMCollectionHeader header;
} ScriptTable;

struct MMCompiledScript
{
    ScriptTable words, literals;
    MMResourceObject binding;
    MMReference references;
    const MMScriptHost *host;
    int has_literals;
};

struct MMScriptCollection
{
    ScriptTable table;
    MMResourceObject binding;
    MMReference references;
    const MMScriptHost *host;
};

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

static int native_big(void)
{
    const unsigned short one = 1;
    return *(const unsigned char *)&one == 0;
}

static void *allocate_owner(const MMScriptHost *host, MMU32 bytes)
{
    const MMStorageAllocator *objects = host->objects;
    if (objects && (!objects->resize || !objects->release))
        return NULL;
    return objects ? objects->resize(objects->context, NULL, 0, bytes, MM_STORAGE_NORMAL) : malloc(bytes);
}

static void free_owner(const MMScriptHost *host, void *owner, MMU32 bytes)
{
    const MMStorageAllocator *objects = host->objects;
    if (objects)
        objects->release(objects->context, owner, bytes);
    else
        free(owner);
}

static void mark_table(const MMScriptHost *host, const ScriptTable *table)
{
    host->mark_memory(table->storage.buffers[0].data);
    host->mark_memory(table->storage.buffers[1].data);
}

static void mark_owner(const MMScriptHost *host, void *owner, const MMResourceObject *binding)
{
    host->mark_memory(owner);
    if (binding->cache && !binding->attached)
        mm_resource_cache_mark(binding->cache);
}

static int read_table(ScriptTable *table, const MMReadStream *stream, int kind, MMU32 width)
{
    MMCollectionHeader *header = &table->header;
    return mm_collection_header_read(stream, kind, 0, header) == MM_STORAGE_OK &&
           (!width || header->width == width) &&
           mm_storage_read(&table->storage, stream, header->header_bytes, header->first_bytes, header->second_bytes) ==
               MM_STORAGE_OK;
}

static MMResourceObject *collection_binding(void *p) { return &((MMScriptCollection *)p)->binding; }
static MMReference *collection_refs(void *p) { return &((MMScriptCollection *)p)->references; }

static void collection_destroy(void *p)
{
    MMScriptCollection *table = p;
    mm_storage_dispose(&table->table.storage);
    mm_resource_object_dispose(table, mm_script_collection_services());
    free_owner(table->host, table, sizeof(*table));
}

static void collection_mark(void *p)
{
    MMScriptCollection *table = p;
    if (table->host->mark_memory)
    {
        mark_table(table->host, &table->table);
        mark_owner(table->host, table, &table->binding);
    }
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

static MMStringTable strings_view(const ScriptTable *table, unsigned units)
{
    MMStringTable view;
    memset(&view, 0, sizeof(view));
    bind_strings(&view, &table->storage);
    view.bytes = table->header.bytes;
    view.count = table->header.count;
    view.stride = table->header.width;
    view.free_count = table->header.free_count;
    view.unit_bytes = units;
    return view;
}

static int normalize_strings(ScriptTable *table, const MMScriptHost *host, int registry_ids)
{
    MMStringTable view = strings_view(table, host->unit_bytes);
    if (table->header.big_endian != native_big())
    {
        mm_strings_swap_offsets(&view);
        if (registry_ids && view.count)
            swap_words(view.entries + 4, view.count, view.stride);
    }
    view.context = &table->storage;
    view.reserve = reserve_strings;
    if (table->header.system != host->encoding &&
        !mm_strings_translate(&view, table->header.system, host->encoding, host->convert, host->context))
        return 0;
    table->header.bytes = view.bytes;
    return 1;
}

int mm_script_registry_merge(MMScriptCollection *table, MMScriptRegistryPut put, void *context)
{
    MMStringTable view = strings_view(&table->table, table->host->unit_bytes);
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
    *count = table->table.header.count;
    return table->table.storage.buffers[0].data;
}

static int fetch_collection(MMOwnedBlock *block, void **out, MMI32 *bytes, int registry,
                            const MMScriptHost *s)
{
    MMBlockIO io;
    MMReadStream stream;
    MMScriptCollection *table = NULL;
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
    table = allocate_owner(s, sizeof(*table));
    if (!table)
        goto fail;
    memset(table, 0, sizeof(*table));
    table->host = s;
    mm_reference_init(&table->references);
    if (mm_storage_init(&table->table.storage, s->storage) != MM_STORAGE_OK ||
        !read_table(&table->table, &stream, registry ? MM_COLLECTION_STRINGS : MM_COLLECTION_ARRAY,
                    registry ? 8U : 4U) ||
        (registry && !normalize_strings(&table->table, s, 1)))
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

int mm_script_registry_fetch(MMOwnedBlock *block, void **out, MMI32 *bytes, const MMScriptHost *s)
{
    return fetch_collection(block, out, bytes, 1, s);
}

int mm_script_colors_fetch(MMOwnedBlock *block, void **out, MMI32 *bytes, const MMScriptHost *s)
{
    return fetch_collection(block, out, bytes, 0, s);
}

static MMResourceObject *script_binding(void *p) { return &((MMCompiledScript *)p)->binding; }
static MMReference *script_refs(void *p) { return &((MMCompiledScript *)p)->references; }

static void script_destroy(void *p)
{
    MMCompiledScript *script = p;
    mm_storage_dispose(&script->words.storage);
    mm_storage_dispose(&script->literals.storage);
    mm_resource_object_dispose(script, mm_script_object_services());
    free_owner(script->host, script, sizeof(*script));
}

static void script_mark(void *p)
{
    MMCompiledScript *script = p;
    if (script->host->mark_memory)
    {
        mark_table(script->host, &script->words);
        mark_table(script->host, &script->literals);
        mark_owner(script->host, script, &script->binding);
    }
}

const MMResourceObjectServices *mm_script_object_services(void)
{
    static const MMResourceObjectServices services = {script_binding, script_refs, script_destroy, NULL, script_mark};
    return &services;
}

const MMResourceObject *mm_script_binding(const MMCompiledScript *script) { return &script->binding; }

void mm_script_retain(MMCompiledScript *script) { mm_reference_add(&script->references); }

void mm_script_release(MMCompiledScript *script)
{
    if (script)
        mm_resource_object_release(script, mm_script_object_services());
}

MMCompiledScript *mm_script_create(const MMScriptHost *host)
{
    MMCompiledScript *script;
    if (host->unit_bytes != 1 && host->unit_bytes != 2)
        return NULL;
    script = allocate_owner(host, sizeof(*script));
    if (!script)
        return NULL;
    memset(script, 0, sizeof(*script));
    script->host = host;
    mm_reference_init(&script->references);
    if (mm_storage_init(&script->words.storage, host->storage) != MM_STORAGE_OK ||
        mm_storage_init(&script->literals.storage, host->storage) != MM_STORAGE_OK)
    {
        mm_script_release(script);
        return NULL;
    }
    script->words.storage.min_grow[0] = 400;
    script->literals.storage.min_grow[0] = 120;
    script->literals.storage.min_grow[1] = 48;
    script->words.header.kind = MM_COLLECTION_ARRAY;
    script->words.header.width = 4;
    script->words.header.free_count = -1;
    script->literals.header.kind = MM_COLLECTION_STRINGS;
    script->literals.header.width = 4;
    script->literals.header.free_count = -1;
    return script;
}

static int read_literals(void *context, const MMReadStream *stream)
{
    MMCompiledScript *script = context;
    script->has_literals = read_table(&script->literals, stream, MM_COLLECTION_STRINGS, 0) &&
                           normalize_strings(&script->literals, script->host, 0);
    return script->has_literals;
}

static MMCompiledScript *read_script(const ScriptSource *source, MMU32 type, MMU32 number, MMOwnedBlock *block,
                                     MMBlockTransform decode, void *decode_context, const MMScriptHost *host)
{
    MMBlockIO io;
    MMReadStream stream;
    MMCompiledScript *script;
    MMU32 child;
    int status, read = 0;
    if (mm_owned_block_decoded_stream(block, decode_context, decode, &io, &stream) != MM_DATA_OK)
        return NULL;
    script = mm_script_create(host);
    if (!script)
        return NULL;
    if (!read_table(&script->words, &stream, MM_COLLECTION_ARRAY, 4))
        goto fail;
    if (script->words.header.big_endian != native_big())
        swap_words(script->words.storage.buffers[0].data, script->words.header.count, 4);
    if (mm_chunk_link_number(&source->chunks, type, number, 0, GSTX, &child) == MM_CHUNK_OK)
    {
        status = source->cache
                     ? mm_resource_cache_visit_payload(source->cache, GSTX, child, decode, decode_context,
                                                       read_literals, script, &read)
                     : mm_chunk_read_payload(source->payloads, source->archive, GSTX, child, read_literals, script,
                                             &read);
        if (status != MM_CHUNK_OK || !read)
            goto fail;
    }
    return script;
fail:
    mm_script_release(script);
    return NULL;
}

int mm_script_resource_fetch(MMResourceCache *cache, MMU32 type, MMU32 number, MMOwnedBlock *block,
                             void **out, MMI32 *bytes, const MMScriptHost *host)
{
    ScriptSource source;
    *bytes = (MMI32)block->view.length;
    if (!out)
        return 1;
    source.chunks = mm_resource_cache_chunks(cache);
    source.cache = cache;
    source.payloads = NULL;
    source.archive = NULL;
    *out = read_script(&source, type, number, block, host->decode, host->context, host);
    return *out != NULL;
}

MMCompiledScript *mm_script_resource_archive(const MMChunkAccess *chunks, const MMChunkPayloadServices *payloads,
                                             void *archive, MMU32 type, MMU32 number, const MMScriptHost *host)
{
    MMOwnedBlock block;
    ScriptSource source;
    MMCompiledScript *script = NULL;
    if (mm_owned_block_init(&block, payloads->handles, payloads->files) != MM_DATA_OK)
        return NULL;
    source.chunks = *chunks;
    source.cache = NULL;
    source.payloads = payloads;
    source.archive = archive;
    if (mm_chunk_payload_bind_block(payloads, archive, type, number, &block) == MM_CHUNK_PAYLOAD_OK)
        script = read_script(&source, type, number, &block, payloads->decode, payloads->decode_context, host);
    mm_owned_block_clear(&block);
    return script;
}

MMU32 mm_script_word_count(const MMCompiledScript *script) { return script->words.header.count; }

int mm_script_add_word(MMCompiledScript *script, MMI32 word)
{
    MMArray words;
    memset(&words, 0, sizeof(words));
    words.storage = &script->words.storage;
    words.width = 4;
    words.count = script->words.header.count;
    words.free_count = -1;
    if (mm_array_add(&words, &word, NULL) != MM_STORAGE_OK)
        return 0;
    script->words.header.count = words.count;
    return 1;
}

const void *mm_script_word_span(const MMCompiledScript *script, MMU32 first, MMU32 count)
{
    MMU32 words = mm_script_word_count(script);
    if (!count || first >= words || count > words - first)
        return NULL;
    return script->words.storage.buffers[0].data + first * 4;
}

void mm_script_set_word(MMCompiledScript *script, MMU32 index, MMI32 word)
{
    if (index < mm_script_word_count(script))
        memcpy(script->words.storage.buffers[0].data + index * 4, &word, 4);
}

MMI32 mm_script_word(const MMCompiledScript *script, MMU32 index)
{
    const void *span = mm_script_word_span(script, index, 1);
    MMI32 word = 0;
    if (span)
        memcpy(&word, span, 4);
    return word;
}

int mm_script_has_literals(const MMCompiledScript *script) { return script->has_literals; }
MMU32 mm_script_literal_count(const MMCompiledScript *script) { return script->literals.header.count; }

int mm_script_add_literal(MMCompiledScript *script, const void *text, MMU32 count, MMU32 *index)
{
    ScriptTable *table = &script->literals;
    MMStringTable strings = strings_view(table, script->host->unit_bytes);
    strings.context = &table->storage;
    strings.reserve = reserve_strings;
    if (strings.stride != 4 || !mm_strings_add(&strings, text, count, NULL, index))
        return 0;
    table->header.count = strings.count;
    table->header.bytes = strings.bytes;
    script->has_literals = 1;
    return 1;
}

int mm_script_literal(const MMCompiledScript *script, MMU32 index, const void **text, MMU32 *count)
{
    MMStringTable strings = strings_view(&script->literals, script->host->unit_bytes);
    return mm_strings_get(&strings, index, text, count);
}

static int section_header(const MMCompiledScript *script, int literals, MMCollectionHeader *header)
{
    if (literals && !script->has_literals)
        return 0;
    *header = literals ? script->literals.header : script->words.header;
    header->big_endian = native_big();
    header->system = script->host->encoding;
    return mm_collection_layout(header) == MM_STORAGE_OK;
}

MMU32 mm_script_section_size(const MMCompiledScript *script, int literals)
{
    MMCollectionHeader header;
    return section_header(script, literals, &header) ? header.header_bytes + header.first_bytes + header.second_bytes
                                                     : 0;
}

int mm_script_write_section(MMCompiledScript *script, int literals, const MMStorageWriter *writer)
{
    MMCollectionHeader header;
    unsigned char bytes[20];
    ScriptTable *table = literals ? &script->literals : &script->words;
    return section_header(script, literals, &header) &&
           mm_collection_header_write(&header, bytes, sizeof(bytes)) == MM_STORAGE_OK &&
           mm_storage_write(&table->storage, writer, bytes, header.header_bytes, header.first_bytes,
                            header.second_bytes) == MM_STORAGE_OK;
}
