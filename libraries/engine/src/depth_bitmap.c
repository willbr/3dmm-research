/* Depth bitmap policy converted from bren/zbmp.cpp. */
#include "mm/depth_bitmap.h"
#include <string.h>
#define MAXIMUM 2147483647
#define MINIMUM (-2147483647 - 1)
static MMU32 word(const unsigned char *b, int big)
{
    return big ? ((MMU32)b[0] << 8) | b[1] : ((MMU32)b[1] << 8) | b[0];
}
static MMI32 signed_word(MMU32 n)
{
    return n < 32768U ? (MMI32)n : (MMI32)n - 65536L;
}
static void put(unsigned char *b, MMU32 n, int big)
{
    b[big ? 1 : 0] = (unsigned char)n;
    b[big ? 0 : 1] = (unsigned char)(n >> 8);
}
static int layout(MMI32 w, MMI32 h, MMI32 *row, MMI32 *bytes)
{
    if (w < 0 || h < 0 || w > MAXIMUM / 2)
        return 0;
    *row = w * 2;
    if (*row && h > (MAXIMUM - 12) / *row)
        return 0;
    *bytes = *row * h;
    return 1;
}
int mm_depth_header(const unsigned char b[12], int big, MMDepthBitmap *m)
{
    MMI32 w = signed_word(word(b + 8, big)), h = signed_word(word(b + 10, big));
    if (!layout(w, h, &m->row_bytes, &m->bytes))
        return 0;
    m->bounds.left = signed_word(word(b + 4, big));
    m->bounds.top = signed_word(word(b + 6, big));
    m->bounds.right = m->bounds.left + w;
    m->bounds.bottom = m->bounds.top + h;
    return 1;
}
void *mm_depth_new(MMI32 w, MMI32 h, const void *initial, const MMDepthServices *s)
{
    void *object;
    MMDepthBitmap *m;
    MMI32 row, n;
    if (w <= 0 || h <= 0 || !layout(w, h, &row, &n))
        return NULL;
    object = s->create(s->context);
    if (!object)
        return NULL;
    m = s->state(s->context, object);
    m->bounds.left = m->bounds.top = 0;
    m->bounds.right = w;
    m->bounds.bottom = h;
    m->row_bytes = row;
    m->bytes = n;
    if (!s->allocate(s->context, n, &m->pixels))
    {
        s->release(s->context, object);
        return NULL;
    }
    if (initial)
        memcpy(m->pixels, initial, (size_t)n);
    else
        memset(m->pixels, 255, (size_t)n);
    return object;
}
void mm_depth_dispose(MMDepthBitmap *m, const MMDepthServices *s)
{
    s->free_memory(s->context, &m->pixels);
}
static int read_layout(const unsigned char header[12], MMU32 length, int big, MMDepthBitmap *m)
{
    return mm_depth_header(header, big, m) && length == 12 + (MMU32)m->bytes;
}
int mm_depth_read_stream(const MMReadStream *stream, MMDepthBitmap *out, const MMDepthServices *s)
{
    MMDepthBitmap m = {0};
    unsigned char header[12];
    if (!mm_read_exact(stream, 0, header, sizeof(header)) ||
        !read_layout(header, stream->length, s->host_big_endian, &m) ||
        !s->allocate(s->context, m.bytes, &m.pixels))
        return 0;
    if (!mm_read_exact(stream, sizeof(header), m.pixels, (size_t)m.bytes))
    {
        mm_depth_dispose(&m, s);
        return 0;
    }
    *out = m;
    return 1;
}
static int read_at(void *block, void *data, MMI32 n, MMI32 at, const MMDepthServices *s)
{
    MMI32 bytes = s->size(s->context, block, 0);
    return n >= 0 && at >= 0 && at <= bytes && n <= bytes - at && s->read(s->context, block, data, n, at);
}
void *mm_depth_read(void *block, const MMDepthServices *s)
{
    void *object;
    MMDepthBitmap *m;
    unsigned char header[12];
    if (!s->unpack(s->context, block))
        return NULL;
    object = s->create(s->context);
    if (!object)
        goto fail;
    m = s->state(s->context, object);
    if (!read_at(block, header, 12, 0, s) ||
        !read_layout(header, (MMU32)s->size(s->context, block, 0), s->host_big_endian, m) ||
        !s->allocate(s->context, m->bytes, &m->pixels) ||
        !read_at(block, m->pixels, m->bytes, 12, s))
        goto fail;
    return object;
fail:
    s->warning(s->context);
    if (object)
        s->release(s->context, object);
    return NULL;
}
int mm_depth_fetch(void *block, int want, const MMDepthServices *s)
{
    void *object;
    MMI32 n = s->size(s->context, block, 1);
    if (!want)
    {
        s->publish_fetch(s->context, 1, NULL, n);
        return 1;
    }
    if (!s->unpack(s->context, block))
        goto fail;
    object = mm_depth_read(block, s);
    if (!object)
        goto fail;
    n = s->state(s->context, object)->bytes;
    if (s->object_bytes > MAXIMUM - n)
    {
        s->release(s->context, object);
        goto fail;
    }
    s->publish_fetch(s->context, 1, object, s->object_bytes + n);
    return 1;
fail:
    s->publish_fetch(s->context, 0, NULL, 0);
    return 0;
}
int mm_depth_write(const MMDepthBitmap *m, void *file, MMU32 type, MMU32 *number,
                         const MMDepthServices *s)
{
    unsigned char header[12];
    void *block;
    if (m->bytes < 0 || m->bytes > MAXIMUM - 12)
        return 0;
    put(header, 1, s->host_big_endian);
    put(header + 2, s->system, s->host_big_endian);
    put(header + 4, (MMU32)m->bounds.left, s->host_big_endian);
    put(header + 6, (MMU32)m->bounds.top, s->host_big_endian);
    put(header + 8, (MMU32)m->bounds.right - (MMU32)m->bounds.left, s->host_big_endian);
    put(header + 10, (MMU32)m->bounds.bottom - (MMU32)m->bounds.top, s->host_big_endian);
    block = s->add(s->context, file, type, 12 + m->bytes, number);
    return block && s->write(s->context, block, header, 12, 0) && s->write(s->context, block, m->pixels, m->bytes, 12);
}
static int add(MMI32 a, MMI32 b, MMI32 *out)
{
    if ((b > 0 && a > MAXIMUM - b) || (b < 0 && a < MINIMUM - b))
        return 0;
    *out = a + b;
    return 1;
}
static int empty_rect(const MMDepthRect *rect)
{
    return rect->left >= rect->right || rect->top >= rect->bottom;
}
int mm_depth_draw(const MMDepthBitmap *m, void *pixels, MMI32 row, MMI32 height, MMI32 x,
                        MMI32 y, const MMDepthRect *clip, const MMRegion *region, int half)
{
    MMDepthRect bounds = m->bounds;
    MMRegionCursor cursor = {0};
    MMRegion rectangle = {0};
    MMI32 step = half ? 2 : 1, yp, left, right, sy, bytes, n, src, dst;
    int ok = 0;
    if (!clip && !region)
        return 1;
    if (row < 0 || height < 0 || (row && height / step > MAXIMUM / row) || m->row_bytes < 0 || m->bytes < 0)
        return 0;
    bytes = row * (height / step);
    if (half)
    {
        if (!add(bounds.bottom, bounds.bottom, &bounds.bottom))
            return 0;
    }
    if (!add(bounds.left, x, &bounds.left) || !add(bounds.right, x, &bounds.right) ||
        !add(bounds.top, y, &bounds.top) || !add(bounds.bottom, y, &bounds.bottom))
        return 0;
    if (region)
        mm_pixel_rect_intersect(&bounds, &region->bounds);
    if (clip)
        mm_pixel_rect_intersect(&bounds, clip);
    if (empty_rect(&bounds))
        return 1;
    if (!region)
    {
        rectangle.bounds = bounds;
        region = &rectangle;
    }
    mm_region_cursor_open(&cursor, region, &bounds);
    for (yp = bounds.top; yp < bounds.bottom;)
    {
        while (mm_region_scan_pair(&cursor.scan, &left, &right))
        {
            if (!add(left, bounds.left, &left) || !add(right, bounds.left, &right) || left < bounds.left ||
                right > bounds.right || left < 0 || right < left || right > MAXIMUM / 2 || yp < 0)
                goto done;
            sy = yp / step;
            n = (right - left) * 2;
            if (left > m->row_bytes / 2 || right > m->row_bytes / 2 || right > row / 2 ||
                (m->row_bytes && sy > m->bytes / m->row_bytes) || (row && sy > bytes / row))
                goto done;
            src = sy * m->row_bytes;
            dst = sy * row;
            if (left * 2 > m->bytes - src || n > m->bytes - src - left * 2 || left * 2 > bytes - dst ||
                n > bytes - dst - left * 2 || (n && (!m->pixels || !pixels)))
                goto done;
            if (n)
                memmove((unsigned char *)pixels + dst + left * 2, (const unsigned char *)m->pixels + src + left * 2,
                        (size_t)n);
        }
        if (yp > MAXIMUM - step || yp + step >= bounds.bottom)
            break;
        mm_region_scan_advance(&cursor.scan, step);
        yp += step;
    }
    ok = 1;
done:
    mm_region_cursor_close(&cursor);
    return ok;
}
