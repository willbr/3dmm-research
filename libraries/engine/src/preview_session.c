#include "mm/preview_session.h"
#include "mm/archive_reader.h"
#include "mm/indexed_display.h"
#include "mm/resource_lookup.h"
#include <string.h>
#define PALETTE_TYPE 0x474c4352UL
#define SHADE_TYPE 0x544d4150UL

int mm_preview_archive_resource(MMArchiveReader *reader, MMU32 type, MMU32 number, MMReadStream *out)
{
    const MMChunkAccess access = mm_archive_reader_access(reader);
    MMU32 position;
    const int status = mm_chunk_find(&access, type, number, &position);
    if (status == MM_CHUNK_NOT_FOUND)
        return MM_PREVIEW_RESOURCE_MISSING;
    return status == MM_CHUNK_OK &&
           (!out || mm_archive_reader_payload(reader, position, out) == MM_ARCHIVE_READ_OK);
}

static int runtime_owned;
static int failure(int r)
{
    return r == -1 ? MM_PREVIEW_SESSION_MEMORY : r == -2 ? MM_PREVIEW_SESSION_EXCEPTION : MM_PREVIEW_SESSION_EXTERNAL;
}
static int report(int result, const MMPreviewSessionServices *s)
{
    int r;
    if (result == MM_PREVIEW_SESSION_MEMORY || result == MM_PREVIEW_SESSION_EXCEPTION)
        return result;
    r = s->report(s->context, result);
    return r < 0 ? failure(r) : result;
}
int mm_preview_session_init(MMPreviewSession *p, const MMStorageAllocator *a)
{
    memset(p, 0, sizeof(*p));
    mm_preview_orbit_reset(&p->orbit);
    return mm_storage_init(&p->sources, a) == MM_STORAGE_OK;
}
void mm_preview_session_dispose(MMPreviewSession *p, const MMPreviewSessionServices *s)
{
    MMU32 i;
    MMPreviewSessionSource *sources = (MMPreviewSessionSource *)p->sources.buffers[0].data;
    s->clear_asset(s->context);
    s->dispose_live(s->context);
    for (i = 0; i < p->source_count; ++i)
        s->source_release(s->context, sources[i].owner);
    p->source_count = 0;
    mm_storage_dispose(&p->sources);
    if (p->cache)
        s->cache_release(s->context, p->cache);
    p->cache = 0;
    if (p->running)
    {
        s->shared_material_release(s->context);
        s->shade_release(s->context);
        s->depth_end(s->context);
        s->runtime_end(s->context);
        p->running = runtime_owned = 0;
    }
}
static int palette(void *candidate, const MMPreviewSessionServices *s)
{
    const unsigned short native_order = 1;
    MMReadStream stream;
    MMCollectionHeader header;
    unsigned char bytes[1024];
    int r = s->resource(s->context, candidate, PALETTE_TYPE, 0, &stream);
    if (r == MM_PREVIEW_RESOURCE_MISSING)
        return MM_PREVIEW_SESSION_PALETTE_MISSING;
    if (r < 0)
        return failure(r);
    if (!r || mm_collection_header_read(&stream, MM_COLLECTION_ARRAY, 0, &header) != MM_STORAGE_OK ||
        header.width != 4 || header.count != 256 || !mm_read_exact(&stream, header.header_bytes, bytes, sizeof(bytes)))
        return MM_PREVIEW_SESSION_PALETTE;
    if (header.big_endian != (*(const unsigned char *)&native_order == 0))
        mm_palette_swap_word_order(bytes, header.count);
    memcpy(s->palette_output(s->context, candidate), bytes, sizeof(bytes));
    r = s->display_palette(s->context, candidate);
    if (r <= 0)
        return failure(r);
    r = s->resource(s->context, candidate, SHADE_TYPE, 0, NULL);
    if (r < 0)
        return failure(r);
    return r == 1 ? MM_PREVIEW_SESSION_OK : MM_PREVIEW_SESSION_PALETTE;
}
int mm_preview_session_open(MMPreviewSession *p, const void *path, int require_asset, int preserve,
                            const MMPreviewSessionServices *s)
{
    void *candidate = 0;
    MMPreviewSession *next;
    int r, assets, result;
    if (require_asset && !s->input_present(s->context))
        return report(MM_PREVIEW_SESSION_TRANSFER, s);
    s->begin(s->context);
    r = s->candidate_new(s->context, &candidate);
    if (r <= 0)
    {
        result = r == 0 ? MM_PREVIEW_SESSION_MEMORY : failure(r);
        goto done;
    }
    next = s->state(s->context, candidate);
    s->take_assets(s->context, candidate);
    r = s->open_resources(s->context, candidate, path);
    if (r <= 0)
    {
        result = failure(r);
        goto done;
    }
    assets = s->has_assets(s->context, candidate);
    if (assets)
    {
        r = s->budget(s->context, candidate, 0);
        if (r <= 0)
        {
            result = failure(r);
            goto done;
        }
    }
    r = s->budget(s->context, candidate, 1);
    if (r <= 0)
    {
        result = failure(r);
        goto done;
    }
    result = palette(candidate, s);
    if (result != MM_PREVIEW_SESSION_OK)
        goto done;
    if (runtime_owned && !p->running)
    {
        result = MM_PREVIEW_SESSION_RUNTIME;
        goto done;
    }
    if (!p->running)
    {
        r = s->runtime_begin(s->context);
        if (r <= 0)
        {
            result = failure(r);
            goto done;
        }
        r = s->depth_begin(s->context);
        if (r <= 0)
        {
            result = failure(r);
            goto done;
        }
        next->running = runtime_owned = 1;
    }
    if (assets)
    {
        r = s->cache_new(s->context, candidate, &next->cache);
        if (r < 0)
        {
            result = failure(r);
            goto done;
        }
        if (!r || !next->cache)
        {
            result = MM_PREVIEW_SESSION_CACHE;
            goto done;
        }
    }
    if (!s->healthy(s->context))
    {
        result = MM_PREVIEW_SESSION_CACHE;
        goto done;
    }
    r = s->shade_load(s->context, candidate);
    if (r <= 0)
    {
        result = r < 0 ? failure(r) : MM_PREVIEW_SESSION_SHADE;
        goto done;
    }
    p->running = 0;
    next->running = 1;
    if (preserve)
        next->orbit = p->orbit;
    s->publish(s->context, &candidate);
    result = MM_PREVIEW_SESSION_OK;
done:
    if (result != MM_PREVIEW_SESSION_OK)
        result = report(result, s);
    if (candidate)
        s->candidate_release(s->context, candidate);
    return result;
}
int mm_preview_session_add(MMPreviewSession *p, MMI32 id, const void *path, const MMPreviewSessionServices *s)
{
    MMPreviewSessionSource entry, *sources = (MMPreviewSessionSource *)p->sources.buffers[0].data;
    MMArray array;
    MMU32 i;
    void *source = 0;
    int r, result;
    s->begin(s->context);
    if (!p->running || id <= 0)
        return report(MM_PREVIEW_SESSION_CONTENT_ARGUMENT, s);
    for (i = 0; i < p->source_count; ++i)
        if (sources[i].id == id && s->same_path(s->context, sources[i].owner, path))
            return MM_PREVIEW_SESSION_OK;
    r = s->source_new(s->context, &source);
    if (r <= 0)
    {
        result = r == 0 ? MM_PREVIEW_SESSION_MEMORY : failure(r);
        goto done;
    }
    r = s->source_open(s->context, source, path);
    if (r <= 0)
    {
        result = failure(r);
        goto done;
    }
    r = s->source_budget(s->context, source);
    if (r <= 0)
    {
        result = failure(r);
        goto done;
    }
    r = s->source_cache_new(s->context, source);
    if (r < 0)
    {
        result = failure(r);
        goto done;
    }
    if (!r || !s->healthy(s->context))
    {
        result = MM_PREVIEW_SESSION_CONTENT_CACHE;
        goto done;
    }
    memset(&array, 0, sizeof(array));
    array.storage = &p->sources;
    array.width = sizeof(entry);
    array.count = p->source_count;
    array.free_count = -1;
    entry.id = id;
    entry.owner = source;
    if (mm_array_add(&array, &entry, 0) != MM_STORAGE_OK)
    {
        result = MM_PREVIEW_SESSION_MEMORY;
        goto done;
    }
    p->source_count = array.count;
    return MM_PREVIEW_SESSION_OK;
done:
    result = report(result, s);
    if (source)
        s->source_release(s->context, source);
    return result;
}
typedef struct PreviewLookup
{
    const MMPreviewSession *session;
    const MMPreviewSessionServices *services;
    MMI32 id;
    MMU32 type, number;
} PreviewLookup;

static int next_source_cache(void *context, size_t *cursor, void **out)
{
    const PreviewLookup *search = (const PreviewLookup *)context;
    const MMPreviewSession *p = search->session;
    const MMPreviewSessionServices *s = search->services;
    const MMPreviewSessionSource *sources = (const MMPreviewSessionSource *)p->sources.buffers[0].data;
    const MMPreviewSessionSource *source;
    if (search->id == 0)
    {
        if (*cursor != 0 || !p->cache)
            return MM_RESOURCE_LOOKUP_MISSING;
        ++*cursor;
        *out = p->cache;
        return MM_RESOURCE_LOOKUP_OK;
    }
    while (*cursor < p->source_count)
    {
        source = &sources[*cursor];
        ++*cursor;
        if (source->id == search->id)
        {
            *out = s->source_cache(s->context, source->owner);
            return MM_RESOURCE_LOOKUP_OK;
        }
    }
    return MM_RESOURCE_LOOKUP_MISSING;
}

static int probe_source_cache(void *context, void *cache, void **out)
{
    const PreviewLookup *search = (const PreviewLookup *)context;
    const MMPreviewSessionServices *s = search->services;
    if (!s->contains(s->context, cache, search->type, search->number))
        return MM_RESOURCE_LOOKUP_MISSING;
    *out = cache;
    return MM_RESOURCE_LOOKUP_OK;
}

void *mm_preview_session_resolve(const MMPreviewSession *p, MMI32 id, MMU32 type, MMU32 number,
                                 const MMPreviewSessionServices *s)
{
    PreviewLookup search;
    MMResourceLookup lookup;
    void *cache = 0;
    search.session = p;
    search.services = s;
    search.id = id;
    search.type = type;
    search.number = number;
    lookup.context = &search;
    lookup.next = next_source_cache;
    lookup.probe = probe_source_cache;
    mm_resource_lookup(&lookup, &cache);
    return cache;
}
