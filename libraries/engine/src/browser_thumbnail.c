/* Thumbnail archive handling converted from Studio browser.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/browser_thumbnail.h"

#define GRAPHIC_TYPE 0x474f4b44UL

int mm_browser_thumbnail_first(const MMChunkAccess *chunks, MMU32 type, MMU32 number, MMU32 *position)
{
    MMChunkEntry entry;
    if (number != MM_BROWSER_THUMBNAIL_ANY)
        return mm_chunk_find(chunks, type, number, position);
    return mm_chunk_type_at(chunks, type, 0, &entry, position);
}

int mm_browser_thumbnail_visit(const MMChunkAccess *chunks, MMU32 first, MMU32 child_type,
                               MMBrowserThumbnailVisit visit, void *context)
{
    MMChunkEntry entry;
    MMChunkChild child;
    MMU32 i, type;
    if (mm_chunk_at(chunks, first, &entry) != MM_CHUNK_OK)
        return 0;
    if (child_type)
    {
        for (i = 0; i < entry.child_count; ++i)
        {
            if (mm_chunk_child_at(chunks, first, i, &child) != MM_CHUNK_OK)
                return 0;
            if (child.type == child_type)
                visit(context, child.type, child.number, child.slot);
        }
        return 1;
    }
    type = entry.type;
    for (i = first; i < chunks->count; ++i)
    {
        if (mm_chunk_at(chunks, i, &entry) != MM_CHUNK_OK)
            return 0;
        if (entry.type != type)
            break;
        if (!visit(context, type, entry.number, MM_BROWSER_THUMBNAIL_ANY))
            return 0;
    }
    return 1;
}

static MMU32 word(const unsigned char *bytes, int big_endian)
{
    if (!big_endian)
        return (MMU32)bytes[0] | (MMU32)bytes[1] << 8 | (MMU32)bytes[2] << 16 | (MMU32)bytes[3] << 24;
    return (MMU32)bytes[0] << 24 | (MMU32)bytes[1] << 16 | (MMU32)bytes[2] << 8 | (MMU32)bytes[3];
}

int mm_browser_thumbnail_decode(const MMReadStream *stream, MMBrowserThumbnail *thumbnail)
{
    unsigned char bytes[12];
    int big_endian;
    if (!stream || stream->length != sizeof(bytes) || !mm_read_exact(stream, 0, bytes, sizeof(bytes)))
        return 0;
    big_endian = bytes[0] == 0 && bytes[1] == 1;
    if (!big_endian && (bytes[0] != 1 || bytes[1] != 0))
        return 0;
    thumbnail->type = word(bytes + 4, big_endian);
    thumbnail->value = word(bytes + 8, big_endian);
    thumbnail->graphic = thumbnail->value;
    thumbnail->has_graphic = 0;
    return 1;
}

static int read_thumbnail(void *context, const MMReadStream *stream)
{
    return mm_browser_thumbnail_decode(stream, context);
}

int mm_browser_thumbnail_read(MMResourceCache *cache, MMU32 type, MMU32 number, MMBlockTransform decode,
                              MMBrowserThumbnail *thumbnail)
{
    MMChunkAccess chunks;
    int result;
    if (mm_resource_cache_visit_payload(cache, type, number, decode, NULL,
                                        read_thumbnail, thumbnail, &result) != MM_CHUNK_OK || !result)
        return 0;
    chunks = mm_resource_cache_chunks(cache);
    thumbnail->has_graphic = mm_chunk_link_number(&chunks, type, number, 0, GRAPHIC_TYPE,
                                                  &thumbnail->graphic) == MM_CHUNK_OK;
    return 1;
}
