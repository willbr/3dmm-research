/* Chunky archive assembly converted from Kauai chcm.cpp by ShonK.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/chunky_assembly.h"
#include <string.h>

typedef struct Subfile
{
    void *parent;
    MMChunkRecordKey key;
    int pack;
} Subfile;

typedef struct Assembly
{
    const MMChunkySource *source;
    const MMChunkyAssemblyHost *host;
    void *current;
    MMCollectionStorage subfile_storage, loner_storage;
    MMArray subfiles, loners;
} Assembly;

static void report(const Assembly *a, int error)
{
    a->source->report(a->source->context, error);
}

static int failed(const Assembly *a)
{
    return a->host->error_count(a->host->context) != 0;
}

static int next(const Assembly *a, MMChunkySourceToken *token, int mode)
{
    return a->source->next(a->source->context, token, mode);
}

static void skip_end(const Assembly *a)
{
    MMChunkySourceToken token;
    while (next(a, &token, MM_CHUNKY_READ_CLEAN) && token.kind != ttEndChunk)
        ;
}

static MMChunkRecordKey read_key(const MMChunkyHeaderField *fields)
{
    MMChunkRecordKey key;
    key.ctg = (MMU32)fields[0].number;
    key.cno = (MMU32)fields[1].number;
    return key;
}

static MMChunkRecordKey chunk_header(const Assembly *a)
{
    const MMChunkyAssemblyHost *host = a->host;
    MMChunkyHeaderField fields[3] = {{0, NULL}, {0, NULL}, {0, NULL}};
    MMLexText name;
    MMChunkRecordKey key = {0, 0};
    name.length = 0;
    fields[2].text = &name;
    if (mm_chunky_parse_header(a->source, fields, 3) < 2)
    {
        report(a, ertChunkHead);
        skip_end(a);
        return key;
    }
    key = read_key(fields);
    if (host->exists(host->context, key))
    {
        report(a, ertDupChunk);
        skip_end(a);
        key.ctg = key.cno = 0;
        return key;
    }
    if (!host->create(host->context, key) ||
        (name.length && !failed(a) && !host->set_name(host->context, key, &name)))
        report(a, ertOom);
    return key;
}

static void link_chunks(const Assembly *a, int kind, MMChunkRecordKey key)
{
    const MMChunkyAssemblyHost *host = a->host;
    MMChunkyHeaderField fields[5];
    MMChunkRecordKey parent, child;
    MMU32 slot;
    int error;
    memset(fields, 0, sizeof(fields));
    if (kind == ttAdopt)
    {
        if (mm_chunky_parse_header(a->source, fields, 5) < 4)
        {
            report(a, ertAdoptHead);
            return;
        }
        parent = read_key(fields);
        child = read_key(fields + 2);
        slot = (MMU32)fields[4].number;
    }
    else
    {
        if (mm_chunky_parse_header(a->source, fields, 3) < 2)
        {
            error = kind == ttChild ? ertBodyChildHead : ertBodyParentHead;
            report(a, error);
            return;
        }
        parent = kind == ttChild ? key : read_key(fields);
        child = kind == ttChild ? read_key(fields) : key;
        slot = (MMU32)fields[2].number;
    }

    if (kind != ttChild && !host->exists(host->context, parent))
        report(a, ertParentMissing);
    else if (kind != ttParent && !host->exists(host->context, child))
        report(a, ertChildMissing);
    else if (host->reaches(host->context, child, parent))
        report(a, ertCycle);
    else if (!failed(a) && !host->adopt(host->context, parent, child, slot))
        report(a, ertOom);
}

static void pack_format(const Assembly *a)
{
    const MMChunkyAssemblyHost *host = a->host;
    MMChunkyHeaderField field = {0, NULL};
    if (mm_chunky_parse_header(a->source, &field, 1) != 1)
        report(a, ertSyntax);
    else if (!host->pack_format(host->context, field.number))
        report(a, ertBadPackFmt);
}

static void packed_file(const Assembly *a, int *prepacked)
{
    const MMChunkySource *source = a->source;
    const MMChunkyAssemblyHost *host = a->host;
    MMChunkySourceToken token;
    unsigned char prefix[4];
    const char *error = NULL;
    source->import_file(source->context);
    if (source->size(source->context) < sizeof(prefix))
        error = "bad packed file";
    else
    {
        host->read_prefix(host->context, prefix, sizeof(prefix));
        if (!memcmp(prefix, "kapa", 4) || !memcmp(prefix, "apak", 4))
            *prepacked = 1;
        else if (!memcmp(prefix, "kaup", 4) || !memcmp(prefix, "puak", 4))
            *prepacked = 0;
        else
            error = "not a packed file";
    }
    if (error)
    {
        host->error_detail(host->context, ertPackedFile, error);
        skip_end(a);
        return;
    }

    host->remove_prefix(host->context, sizeof(prefix));
    if (!next(a, &token, MM_CHUNKY_READ_CLEAN) || token.kind != ttEndChunk)
    {
        report(a, ertNeedEndChunk);
        skip_end(a);
    }
}

static void begin_subfile(Assembly *a, MMChunkRecordKey key, int pack)
{
    const MMChunkyAssemblyHost *host = a->host;
    Subfile parent;
    void *child;
    parent.parent = a->current;
    parent.key = key;
    parent.pack = pack;
    if (mm_array_add(&a->subfiles, &parent, NULL) != MM_STORAGE_OK)
    {
        report(a, ertOom);
        return;
    }
    child = host->create_subfile(host->context);
    if (!child)
    {
        mm_array_pop(&a->subfiles, NULL);
        report(a, ertOom);
        return;
    }
    a->current = child;
    host->activate(host->context, child);
}

static void end_subfile(Assembly *a, int finish)
{
    const MMChunkyAssemblyHost *host = a->host;
    Subfile parent;
    void *child = a->current;
    if (mm_array_pop(&a->subfiles, &parent) != MM_STORAGE_OK)
    {
        report(a, ertSyntax);
        return;
    }
    if (finish && !failed(a) && !host->finish_subfile(host->context, parent.parent, parent.key, parent.pack))
        report(a, ertOom);
    a->current = parent.parent;
    host->activate(host->context, a->current);
    host->release(host->context, child);
}

static const char *body_label(int kind)
{
    switch (kind)
    {
    case ttPackedFile: return "Packed File";
    case ttMeta: return "Metafile";
    case ttBitmap:
    case ttMask: return "Bitmap";
    case ttPalette: return "Palette";
    case ttMidi: return "Midi";
    case ttCursor: return "Cursor";
    case ttAl:
    case ttGl: return "List";
    case ttAg:
    case ttGg: return "Group";
    case ttAst:
    case ttGst: return "String Table";
    case ttScript:
    case ttScriptP: return "Script";
    case ttSubFile: return "Sub File";
    default: return NULL;
    }
}

static void finish_data(const Assembly *a, MMChunkRecordKey key, int pack, int prepacked)
{
    const MMChunkyAssemblyHost *host = a->host;
    if (!failed(a) && !host->finish_data(host->context, key, pack, prepacked))
        report(a, ertOom);
    host->clear_data(host->context);
}

static void chunk_body(Assembly *a, MMChunkRecordKey key)
{
    const MMChunkyAssemblyHost *host = a->host;
    const MMChunkySource *source = a->source;
    MMChunkySourceToken token;
    const char *label;
    int fetch = 1, pack = 0, prepacked = 0;
    host->clear_data(host->context);
    for (;;)
    {
        if (fetch && !next(a, &token, MM_CHUNKY_READ_CLEAN))
            return;
        fetch = 1;
        label = body_label(token.kind);
        if (label)
        {
            if (source->size(source->context))
            {
                host->error_detail(host->context, ertBufData, label);
                host->clear_data(host->context);
            }
            if (token.kind == ttSubFile)
                begin_subfile(a, key, pack);
            else if (token.kind == ttPackedFile)
            {
                packed_file(a, &prepacked);
                finish_data(a, key, 0, prepacked);
            }
            else
                host->special_body(host->context, key, token.kind, pack);
            return;
        }

        switch (token.kind)
        {
        case ttChild:
        case ttParent: link_chunks(a, token.kind, key); break;
        case ttLoner:
            if (a->subfiles.count)
                report(a, ertLonerInSub);
            else if (mm_array_add(&a->loners, &key, NULL) != MM_STORAGE_OK)
                report(a, ertOom);
            break;
        case ttPrePacked: prepacked = 1; break;
        case ttPack: pack = 1; break;
        case ttPackFmt: pack_format(a); break;
        case ttEndChunk:
            finish_data(a, key, pack, prepacked);
            return;
        default:
            if (!mm_chunky_parse_data(source, &token))
            {
                report(a, ertBadToken);
                skip_end(a);
                return;
            }
            fetch = 0;
            break;
        }
    }
}

static void init_array(MMArray *array, MMCollectionStorage *storage, MMU32 width)
{
    array->storage = storage;
    array->width = width;
    array->free_count = -1;
    storage->min_grow[0] = width * 8;
}

void mm_chunky_assemble(const MMChunkySource *source, const MMChunkyAssemblyHost *host, void *root)
{
    Assembly a;
    MMChunkySourceToken token;
    MMChunkRecordKey key = {0, 0};
    int report_bad = 1;
    memset(&a, 0, sizeof(a));
    a.source = source;
    a.host = host;
    a.current = root;
    if (mm_storage_init(&a.subfile_storage, host->allocator) != MM_STORAGE_OK ||
        mm_storage_init(&a.loner_storage, host->allocator) != MM_STORAGE_OK)
    {
        report(&a, ertOom);
        return;
    }
    init_array(&a.subfiles, &a.subfile_storage, sizeof(Subfile));
    init_array(&a.loners, &a.loner_storage, sizeof(MMChunkRecordKey));

    while (next(&a, &token, MM_CHUNKY_READ_CLEAN_EOF))
    {
        switch (token.kind)
        {
        case ttChunk:
            key = chunk_header(&a);
            chunk_body(&a, key);
            report_bad = 1;
            break;
        case ttEndChunk: end_subfile(&a, 1); report_bad = 1; break;
        case ttAdopt: link_chunks(&a, ttAdopt, key); report_bad = 1; break;
        case ttPackFmt: pack_format(&a); report_bad = 1; break;
        default:
            if (report_bad)
                report(&a, ertNeedChunk);
            report_bad = 0;
            break;
        }
        if (host->error_count(host->context) > 100)
        {
            host->error_limit(host->context);
            break;
        }
    }

    host->clear_data(host->context);
    if (a.subfiles.count)
        report(&a, ertNoEndSubFile);
    while (a.subfiles.count)
        end_subfile(&a, 0);
    if (!failed(&a))
    {
        while (a.loners.count && mm_array_pop(&a.loners, &key) == MM_STORAGE_OK)
            host->set_loner(host->context, key);
        if (!host->finish_archive(host->context))
            report(&a, ertOom);
    }
    mm_storage_dispose(&a.loner_storage);
    mm_storage_dispose(&a.subfile_storage);
}

int mm_chunky_write_roots(const MMChunkyForestHost *host, MMU32 offset, MMU32 *bytes)
{
    MMU32 index, references, tree_bytes, end = offset;
    MMChunkRecordKey key;
    if (offset > 0x7fffffffUL)
        return 0;
    for (index = 0; host->entry(host->context, index, &key, &references); ++index)
    {
        if (references)
            continue;
        if (!host->tree(host->context, key, end, &tree_bytes) || tree_bytes > 0x7fffffffUL - end)
            return 0;
        end += tree_bytes;
    }
    *bytes = end - offset;
    return 1;
}
