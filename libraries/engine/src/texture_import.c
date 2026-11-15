/* Bitmap import and palette matching converted from bren/tmap.cpp. */
#include "mm/texture_resource.h"
#include <string.h>
void *mm_texture_bitmap(void *filename, void *palette, const MMTextureServices *s)
{
    MMTextureBitmap b;
    MMI32 *cache = NULL, count, source_count = 0, n, i, j, index, best, dist, min, dr, dg, db;
    MMTextureColour source, target;
    void *object = NULL, *memory = NULL;
    MMTextureDescription d;
    memset(&b, 0, sizeof(b));
    if (!s->read_bitmap(s->context, filename, &b))
        goto done;
    if (b.upside_down)
        s->diagnostic(s->context, MM_TEXTURE_UPSIDE_DOWN);
    memset(&d, 0, sizeof(d));
    d.width = d.row_bytes = b.width;
    d.height = b.height;
    if (!mm_texture_payload(&d, &n))
        goto done;
    if (palette)
    {
        count = s->palette_count(s->context, palette);
        source_count = s->palette_count(s->context, b.palette);
        if (count < 0 || count > 256 || source_count < 0 || source_count > 256)
            goto done;
        if (source_count && s->allocate(s->context, source_count * 4, 0, &memory))
        {
            cache = (MMI32 *)memory;
            for (i = 0; i < source_count; ++i)
                cache[i] = -1;
        }
        for (i = 0; i < n; ++i)
        {
            index = ((unsigned char *)b.pixels)[i];
            if (index >= source_count)
                goto done;
            best = cache ? cache[index] : -1;
            if (best == -1)
            {
                s->palette_colour(s->context, b.palette, index, &source);
                min = 2147483647;
                for (j = 0; j < count; ++j)
                {
                    s->palette_colour(s->context, palette, j, &target);
                    dr = (MMI32)source.red - target.red;
                    dg = (MMI32)source.green - target.green;
                    db = (MMI32)source.blue - target.blue;
                    dist = dr * dr + dg * dg + db * db;
                    if (dist <= min)
                    {
                        min = dist;
                        best = j;
                    }
                }
                if (best != -1 && cache)
                    cache[index] = best;
            }
            if (best != -1)
                ((unsigned char *)b.pixels)[i] = (unsigned char)best;
        }
    }
    if (memory)
    {
        s->free_memory(s->context, &memory);
        cache = NULL;
    }
    if (b.palette)
    {
        s->release(s->context, b.palette);
        b.palette = NULL;
    }
    object = mm_texture_new(b.pixels, b.width, b.height, s);
    if (object)
        b.pixels = NULL;
done:
    if (memory)
        s->free_memory(s->context, &memory);
    if (b.palette)
        s->release(s->context, b.palette);
    s->free_memory(s->context, &b.pixels);
    return object;
}
