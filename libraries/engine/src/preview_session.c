#include "mm/preview_session.h"
#include <string.h>
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
    MMPreviewSessionPalette table;
    unsigned char *bytes;
    unsigned char swap;
    MMU32 i;
    int r, valid;
    if (!s->palette_find(s->context, candidate))
        return MM_PREVIEW_SESSION_PALETTE_MISSING;
    memset(&table, 0, sizeof(table));
    r = s->palette_read(s->context, candidate, &table);
    valid = r > 0 && table.handle && table.width == 4 && table.count == 256;
    if (valid)
    {
        bytes = s->palette_bytes(s->context, table.handle);
        if (table.other_order)
            for (i = 0; i < 1024; i += 4)
            {
                swap = bytes[i];
                bytes[i] = bytes[i + 3];
                bytes[i + 3] = swap;
                swap = bytes[i + 1];
                bytes[i + 1] = bytes[i + 2];
                bytes[i + 2] = swap;
            }
        memcpy(s->palette_output(s->context, candidate), bytes, 1024);
    }
    if (table.handle)
        s->palette_release(s->context, &table);
    if (r < 0)
        return failure(r);
    if (valid)
    {
        r = s->display_palette(s->context, candidate);
        if (r <= 0)
            return failure(r);
    }
    return !valid || !s->shade_find(s->context, candidate) ? MM_PREVIEW_SESSION_PALETTE : MM_PREVIEW_SESSION_OK;
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
void *mm_preview_session_resolve(const MMPreviewSession *p, MMI32 id, MMU32 type, MMU32 number,
                                 const MMPreviewSessionServices *s)
{
    MMU32 i;
    void *cache;
    const MMPreviewSessionSource *sources = (const MMPreviewSessionSource *)p->sources.buffers[0].data;
    if (!id && p->cache && s->contains(s->context, p->cache, type, number))
        return p->cache;
    for (i = 0; i < p->source_count; ++i)
        if (sources[i].id == id)
        {
            cache = s->source_cache(s->context, sources[i].owner);
            if (s->contains(s->context, cache, type, number))
                return cache;
        }
    return 0;
}
