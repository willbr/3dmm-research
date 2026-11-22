/* Archive listing policy converted from Kauai chcm.cpp by ShonK.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/chunky_decompile.h"
#include <string.h>

#define GLOP 0x474c4f50UL
#define GSTX 0x47535458UL

static void line(const MMChunkyDecompile *source, const char *text)
{
    source->listing->write_line(source->listing->context, text, strlen(text), 1);
}

static void issue(const MMChunkyDecompile *source, MMChunkyDecompileIssue kind)
{
    if (source->issue)
        source->issue(source->listing->context, kind);
}

typedef struct CollectionBody
{
    const MMChunkyCollectionListing *listing;
    MMChunkyCollectionMode *mode;
    MMChunkyListKind kind;
    int packed;
    MMU32 format;
} CollectionBody;

static int read_format(void *context, const MMReadStream *stream)
{
    MMI32 *format = context;
    return mm_packed_format(stream, format) == MM_DATA_OK;
}

static int dump_collection(void *context, const MMReadStream *stream)
{
    const CollectionBody *body = context;
    return mm_chunky_list_collection(stream, body->kind, body->packed, body->format, body->listing, body->mode);
}

static int dump_stored(void *context, const MMReadStream *stream)
{
    const MMChunkyCollectionListing *listing = context;
    return mm_chunky_list_bytes(stream, 1, listing->write_line, listing->context);
}

static void standard_body(const MMChunkyDecompile *source, const MMChunkEntry *entry, MMChunkyCollectionMode *mode)
{
    CollectionBody body;
    MMI32 format = 0;
    int status, result = 0;

    if (entry->type == GLOP && source->hooks && source->hooks->script &&
        source->hooks->script(source->hooks->context, entry))
        return;

    body.kind = mm_chunky_list_kind(entry->type);
    body.packed = (entry->flags & MM_CHUNK_PACKED) != 0;
    if (body.kind != MM_CHUNKY_LIST_NONE)
    {
        body.listing = source->listing;
        body.mode = mode;
        if (body.packed)
            mm_chunk_visit_stored(source->payloads, source->archive, entry->type, entry->number,
                                  read_format, &format, &result);
        body.format = (MMU32)format;
        status = mm_chunk_read_payload(source->payloads, source->archive, entry->type, entry->number,
                                       dump_collection, &body, &result);
        if (status == MM_CHUNK_OK)
        {
            if (result == MM_STORAGE_OK)
                return;
            if (result == MM_STORAGE_ARGUMENT)
                issue(source, MM_CHUNKY_DECOMPILE_COLLECTION);
        }
    }
    if (body.packed)
        line(source, "PREPACKED");
    if (mm_chunk_visit_stored(source->payloads, source->archive, entry->type, entry->number,
                              dump_stored, (void *)source->listing, &result) != MM_CHUNK_OK || !result)
        issue(source, MM_CHUNKY_DECOMPILE_PAYLOAD);
}

int mm_chunky_decompile(const MMChunkyDecompile *source)
{
    MMChunkEntry entry;
    MMChunkChild child;
    MMMovieTitle name;
    MMChunkyCollectionMode mode;
    MMU32 index, link;
    MMChunkyBodyResult result;
    const MMChunkyCollectionListing *listing;

    if (!source || !source->payloads || !source->archive || !source->names || !source->names->read ||
        !(listing = source->listing) || !listing->write_line || !mm_movie_title_init(&name, listing->native_unit))
        return 0;
    mode.foreign_order = 0;
    mode.system = listing->native_system;
    line(source, "BYTE");
    line(source, "");
    for (index = 0; index < source->chunks.count; ++index)
    {
        if (mm_chunk_at(&source->chunks, index, &entry) != MM_CHUNK_OK)
            return 0;
        if (entry.type == GSTX)
            continue;
        mm_chunk_title_read(&name, source->archive, entry.type, entry.number, source->names);
        mm_chunky_list_header(&entry, mm_movie_title_text(&name), name.count, name.unit,
                             listing->write_line, listing->context);
        result = source->hooks && source->hooks->special ? source->hooks->special(source->hooks->context, &entry)
                                                        : MM_CHUNKY_BODY_FALLBACK;
        if (result == MM_CHUNKY_BODY_STOP || result == MM_CHUNKY_BODY_ERROR)
            return result == MM_CHUNKY_BODY_STOP;
        if (result == MM_CHUNKY_BODY_FALLBACK)
            standard_body(source, &entry, &mode);
        line(source, "ENDCHUNK");
        line(source, "");
    }
    for (index = 0; index < source->chunks.count; ++index)
    {
        if (mm_chunk_at(&source->chunks, index, &entry) != MM_CHUNK_OK)
            return 0;
        for (link = 0; link < entry.child_count; ++link)
        {
            if (mm_chunk_child_at(&source->chunks, index, link, &child) != MM_CHUNK_OK)
                return 0;
            if (entry.type != GLOP || child.type != GSTX)
                mm_chunky_list_adopt(&entry, &child, listing->write_line, listing->context);
        }
    }
    return 1;
}
