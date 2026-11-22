#include "caption_raster.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef struct MarcusCaptionRasterEntry
{
    MarcusCaptionRaster raster;
    MMCaptionFont font;
    const unsigned short *text;
    size_t length, bytes;
    MMI32 pixels, flags;
} MarcusCaptionRasterEntry;

enum { COPY_MASK, ENLARGE_MASK, HALF_MASK };

static void discard(MarcusCaptionRasterCache *cache)
{
    while (cache->count)
        free(cache->entries[--cache->count]);
    cache->bytes = 0;
}

void marcus_caption_raster_clear(MarcusCaptionRasterCache *cache)
{
    discard(cache);
    free(cache->entries);
    cache->entries = NULL;
    cache->capacity = 0;
}

static int compare(const MarcusCaptionRasterEntry *entry, const MarcusCaptionRun *run, MMI32 pixels)
{
    size_t bytes;
    int order;
    if (entry->pixels != pixels)
        return entry->pixels < pixels ? -1 : 1;
    if (entry->flags != run->style.flags)
        return entry->flags < run->style.flags ? -1 : 1;
    if (entry->font.length != run->style.font.length)
        return entry->font.length < run->style.font.length ? -1 : 1;
    bytes = entry->font.length;
    order = bytes ? memcmp(entry->font.name, run->style.font.name, bytes) : 0;
    if (order)
        return order;
    if (entry->length != (size_t)run->length)
        return entry->length < (size_t)run->length ? -1 : 1;
    return entry->length ? memcmp(entry->text, run->text, entry->length * sizeof(*entry->text)) : 0;
}

static const MarcusCaptionRasterEntry *find(const MarcusCaptionRasterCache *cache, const MarcusCaptionRun *run,
                                            MMI32 pixels, size_t *row)
{
    size_t low = 0, high = cache->count, mid;
    int order;
    while (low < high)
    {
        mid = low + (high - low) / 2;
        order = compare(cache->entries[mid], run, pixels);
        if (order < 0)
            low = mid + 1;
        else if (order > 0)
            high = mid;
        else
            return cache->entries[mid];
    }
    *row = low;
    return NULL;
}

static int reserve(MarcusCaptionRasterCache *cache)
{
    MarcusCaptionRasterEntry **new_entries;
    size_t capacity;
    if (cache->count < cache->capacity)
        return 1;
    capacity = cache->capacity ? cache->capacity * 2 : 8;
    if (capacity <= cache->capacity || capacity > (size_t)-1 / sizeof(*new_entries))
        return 0;
    new_entries = realloc(cache->entries, capacity * sizeof(*new_entries));
    if (!new_entries)
        return 0;
    cache->entries = new_entries;
    cache->capacity = capacity;
    return 1;
}

static int expand(MMCaptionMask *mask, int scale)
{
    if (mask->width < 0 || mask->height < 0 ||
        mask->width > INT_MAX / scale || mask->height > INT_MAX / scale ||
        mask->left > INT_MAX / scale || mask->left < INT_MIN / scale ||
        mask->top > INT_MAX / scale || mask->top < INT_MIN / scale)
        return 0;
    mask->left *= scale;
    mask->top *= scale;
    mask->width *= scale;
    mask->height *= scale;
    mask->pitch = mask->width;
    return 1;
}

static const unsigned char *make_plane(unsigned char **next, const MarcusCaptionRaster *source,
                                       const unsigned char *plane, const MMCaptionMask *output,
                                       size_t bytes, int mode, int scale)
{
    unsigned char *result;
    if (!plane || !bytes)
        return NULL;
    result = *next;
    if (mode == HALF_MASK)
        mm_caption_mask_half(&source->geometry, plane, output, result);
    else if (mode == ENLARGE_MASK)
        mm_caption_mask_expand(&source->geometry, plane, output, result, scale);
    else
        memcpy(result, plane, bytes);
    *next += bytes;
    return result;
}

static MarcusCaptionRasterEntry *copy_raster(const MarcusCaptionRun *run, MMI32 pixels,
                                             const MarcusCaptionRaster *source, const MarcusCaptionRaster *shape,
                                             int mode, int scale)
{
    const MMCaptionMask *geometry = &shape->geometry;
    size_t text_bytes, plane_bytes, mask_bytes, total;
    const size_t planes = (source->ink != NULL) + (source->paper != NULL);
    MarcusCaptionRasterEntry *entry;
    unsigned char *data;
    if (geometry->width < 0 || geometry->height < 0 || geometry->pitch < geometry->width ||
        (geometry->height && (size_t)geometry->pitch > (size_t)-1 / geometry->height) ||
        (size_t)run->length > ((size_t)-1 - sizeof(*entry)) / sizeof(*run->text))
        return NULL;
    text_bytes = (size_t)run->length * sizeof(*run->text);
    plane_bytes = (size_t)geometry->pitch * geometry->height;
    total = sizeof(*entry) + text_bytes;
    if (run->style.font.length > (size_t)-1 - total)
        return NULL;
    total += run->style.font.length;
    if (planes && plane_bytes > ((size_t)-1 - total) / planes)
        return NULL;
    mask_bytes = plane_bytes * planes;
    entry = malloc(total + mask_bytes);
    if (!entry)
        return NULL;
    data = (unsigned char *)(entry + 1);
    entry->text = (const unsigned short *)data;
    entry->length = (size_t)run->length;
    entry->pixels = pixels;
    entry->flags = run->style.flags;
    entry->bytes = mask_bytes;
    if (text_bytes)
        memcpy(data, run->text, text_bytes);
    data += text_bytes;
    entry->font.name = (const char *)data;
    entry->font.length = run->style.font.length;
    if (entry->font.length)
        memcpy(data, run->style.font.name, entry->font.length);
    data += entry->font.length;
    entry->raster = *shape;
    entry->raster.ink = make_plane(&data, source, source->ink, geometry, plane_bytes, mode, scale);
    entry->raster.paper = make_plane(&data, source, source->paper, geometry, plane_bytes, mode, scale);
    return entry;
}

static int load(const MarcusCaptionRun *run, int scale, MMI32 pixels, size_t budget,
                 const MarcusCaptionFonts *fonts, MarcusCaptionRasterEntry **out)
{
    MMCaptionStyle style = run->style;
    MarcusCaptionMaskOwner sampled = {0};
    const MarcusCaptionRaster *source = &run->mask.raster;
    MarcusCaptionRaster shape = *source;
    MarcusCaptionMetrics metrics;
    double estimate;
    int mode = COPY_MASK;
    style.pixels = pixels;
    if (!fonts->face(fonts->context, &style) || !fonts->measure(fonts->context, run->text, run->length, &metrics))
        return MARCUS_CAPTION_RASTER_METRICS;
    estimate = ((double)metrics.advance + pixels * 2.) * ((double)metrics.cell_height + pixels * 2.) * 2.;
    if (estimate > (double)budget)
    {
        /* Keep the copied native strike when supersampling would exceed the budget. */
        shape.ascent = run->ascent * 2;
        shape.density = 1.f;
    }
    else
    {
        if (!fonts->mask(fonts->context, run->text, run->length, &sampled))
            return MARCUS_CAPTION_RASTER_SOURCE;
        shape.density = (float)scale;
        if (metrics.cell_height < (double)run->height * scale)
        {
            /* The selected bitmap face has no larger strike. */
            mode = ENLARGE_MASK;
            shape.ascent = run->ascent * scale * 2;
            if (!expand(&shape.geometry, scale))
            {
                if (sampled.release)
                    sampled.release(sampled.owner);
                return MARCUS_CAPTION_RASTER_MEMORY;
            }
        }
        else
        {
            mode = HALF_MASK;
            source = &sampled.raster;
            mm_caption_mask_half_size(&source->geometry, &shape.geometry);
            shape.ascent = metrics.ascent;
        }
    }
    *out = copy_raster(run, pixels, source, &shape, mode, scale);
    if (sampled.release)
        sampled.release(sampled.owner);
    return *out ? MARCUS_CAPTION_RASTER_OK : MARCUS_CAPTION_RASTER_MEMORY;
}

int marcus_caption_raster_get(MarcusCaptionRasterCache *cache, const MarcusCaptionRun *run, int scale,
                              size_t budget, const MarcusCaptionFonts *fonts, MarcusCaptionRaster *out)
{
    const MarcusCaptionRasterEntry *found;
    MarcusCaptionRasterEntry *entry;
    MMI32 pixels;
    size_t row;
    int status;
    if (scale <= 1 || run->style.pixels <= 0 || run->style.pixels > INT_MAX / scale / 2)
        return MARCUS_CAPTION_RASTER_METRICS;
    pixels = run->style.pixels * scale * 2;
    found = find(cache, run, pixels, &row);
    if (found)
    {
        *out = found->raster;
        return MARCUS_CAPTION_RASTER_OK;
    }
    status = load(run, scale, pixels, budget, fonts, &entry);
    if (status != MARCUS_CAPTION_RASTER_OK)
        return status;
    if (cache->bytes > budget || entry->bytes > budget - cache->bytes)
    {
        discard(cache);
        row = 0;
    }
    if (!reserve(cache))
    {
        free(entry);
        return MARCUS_CAPTION_RASTER_MEMORY;
    }
    memmove(cache->entries + row + 1, cache->entries + row, (cache->count - row) * sizeof(*cache->entries));
    cache->entries[row] = entry;
    ++cache->count;
    cache->bytes += entry->bytes;
    *out = entry->raster;
    return MARCUS_CAPTION_RASTER_OK;
}
