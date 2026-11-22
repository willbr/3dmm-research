#include "caption_faces.h"
#include "mm/text.h"
#include <stdlib.h>
#include <string.h>

typedef struct MarcusCaptionFaceEntry
{
    MMI32 pixels, flags;
    size_t length;
    void *face;
    void (*release)(void *);
} MarcusCaptionFaceEntry;

void marcus_caption_faces_clear(MarcusCaptionFaces *cache)
{
    while (cache->count)
    {
        MarcusCaptionFaceEntry *entry = cache->entries[--cache->count];
        if (entry->face)
            entry->release(entry->face);
        free(entry);
    }
}

static int compare(const MarcusCaptionFaceEntry *entry, const MMCaptionStyle *style)
{
    if (entry->pixels != style->pixels)
        return entry->pixels < style->pixels ? -1 : 1;
    if (entry->flags != style->flags)
        return entry->flags < style->flags ? -1 : 1;
    if (entry->length != style->font.length)
        return entry->length < style->font.length ? -1 : 1;
    return entry->length ? memcmp(entry + 1, style->font.name, entry->length) : 0;
}

static const MarcusCaptionFaceEntry *find(const MarcusCaptionFaces *cache, const MMCaptionStyle *style, size_t *row)
{
    size_t low = 0, high = cache->count, mid;
    int order;
    while (low < high)
    {
        mid = low + (high - low) / 2;
        order = compare(cache->entries[mid], style);
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

static int installed(const MMCaptionFont *family, const MarcusCaptionFaceOptions *options,
                     const MarcusCaptionFaceServices *services, void **out)
{
    static const char system[] = "System";
    unsigned short scratch[256], *units = scratch;
    size_t count;
    int status = MARCUS_CAPTION_FACE_MISSING;
    if (family->length > sizeof(scratch) / sizeof(*scratch))
    {
        if (family->length > (size_t)-1 / sizeof(*units))
            return MARCUS_CAPTION_FACE_MEMORY;
        units = malloc(family->length * sizeof(*units));
        if (!units)
            return MARCUS_CAPTION_FACE_MEMORY;
    }
    if (mm_text_read_utf8(family->name, family->length, units, family->length, &count) == 1)
        status = services->installed(services->context, units, count, 1, options, out);
    if (units != scratch)
        free(units);
    if (status == MARCUS_CAPTION_FACE_MISSING)
        status = services->installed(services->context, system, sizeof(system) - 1, 0, options, out);
    return status;
}

int marcus_caption_face_get(MarcusCaptionFaces *cache, const MMCaptionStyle *style,
                            const MarcusCaptionFaceServices *services, void **out)
{
    static const MMCaptionFont arial = {"Arial", 5};
    const MMCaptionFont *family = style->font.length ? &style->font : &arial;
    const MarcusCaptionFaceEntry *found;
    MarcusCaptionFaceOptions options;
    MarcusCaptionFaceEntry *entry;
    void *face = NULL;
    size_t row;
    int status;
    found = find(cache, style, &row);
    if (found)
    {
        if (found->face)
            *out = found->face;
        return found->face ? MARCUS_CAPTION_FACE_READY : MARCUS_CAPTION_FACE_MISSING;
    }
    options.pixels = style->pixels;
    options.bold = (style->flags & 1) != 0;
    options.italic = (style->flags & 2) != 0;
    options.underline = (style->flags & 4) != 0;
    status = services->bundled ? services->bundled(services->context, family, &options, &face) : MARCUS_CAPTION_FACE_MISSING;
    if (status == MARCUS_CAPTION_FACE_MISSING)
        status = installed(family, &options, services, &face);
    if (status < 0)
        return status;
    entry = style->font.length > (size_t)-1 - sizeof(*entry) ? NULL : malloc(sizeof(*entry) + style->font.length);
    if (!entry)
    {
        if (face)
            services->release(face);
        return MARCUS_CAPTION_FACE_MEMORY;
    }
    entry->pixels = style->pixels;
    entry->flags = style->flags;
    entry->length = style->font.length;
    entry->face = face;
    entry->release = services->release;
    if (entry->length)
        memcpy(entry + 1, style->font.name, style->font.length);
    if (cache->count == MARCUS_CAPTION_FACE_LIMIT)
    {
        marcus_caption_faces_clear(cache);
        row = 0;
    }
    memmove(cache->entries + row + 1, cache->entries + row, (cache->count - row) * sizeof(*cache->entries));
    cache->entries[row] = entry;
    ++cache->count;
    if (face)
        *out = face;
    return status;
}
