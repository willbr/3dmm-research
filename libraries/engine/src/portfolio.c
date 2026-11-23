/* Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/portfolio.h"
#include "mm/scene_thumbnail.h"
#include "mm/string_wire.h"

MMMaskedOwner *mm_portfolio_preview(void *selection, MMU32 previews, const MMPortfolioPreviewServices *services)
{
    MMMaskedOwner *bitmap = NULL;
    MMResourceCache *cache;
    MMChunkAccess chunks;
    MMU32 thumbnail;
    if (!selection)
        return NULL;
    if (previews & MM_PORTFOLIO_PREVIEW_MOVIE)
    {
        cache = services->open_movie(selection);
        if (cache)
        {
            chunks = mm_resource_cache_chunks(cache);
            if (mm_movie_first_scene_thumbnail(&chunks, &thumbnail) == MM_CHUNK_OK)
                bitmap = mm_resource_cache_fetch_owned_chunk(cache, MM_SCENE_THUMBNAIL_TYPE, thumbnail,
                                                              services->thumbnail_reader, mm_masked_owner_services());
            mm_resource_cache_release(cache);
        }
    }
    if (!bitmap && (previews & MM_PORTFOLIO_PREVIEW_BITMAP))
        bitmap = services->import_bitmap(selection);
    return bitmap;
}

typedef union PortfolioName
{
    unsigned char narrow[MM_STRING_MAX + 2];
    unsigned short wide[MM_STRING_MAX + 2];
} PortfolioName;

int mm_portfolio_append_extension(const MMStringValue *path, const void *extension, MMI32 count)
{
    static const unsigned char narrow_dot = '.';
    static const unsigned short wide_dot = '.';
    PortfolioName name;
    MMStringValue copy = *path;
    const void *dot = path->unit == 1 ? (const void *)&narrow_dot : (const void *)&wide_dot;
    copy.buffer = &name;
    mm_string_value_copy(&copy, path->buffer);
    if (!mm_string_value_append(&copy, dot, 1) || !mm_string_value_append(&copy, extension, count))
        return 0;
    mm_string_value_copy(path, copy.buffer);
    return 1;
}

void mm_portfolio_remove_extension(const MMStringValue *leaf)
{
    const unsigned char *narrow = leaf->buffer;
    const unsigned short *wide = leaf->buffer;
    MMI32 index = leaf->unit == 1 ? narrow[0] : wide[0];
    while (index > 0)
    {
        if ((leaf->unit == 1 ? narrow[index] : wide[index]) == '.')
        {
            mm_string_value_delete(leaf, index - 1, MM_STRING_MAX);
            return;
        }
        --index;
    }
}
