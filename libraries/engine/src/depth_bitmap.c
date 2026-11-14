/* Depth bitmap policy converted from bren/zbmp.cpp. */
#include "three_mm/depth_bitmap.h"
#include <string.h>
#define MAXIMUM 2147483647
#define MINIMUM (-2147483647 - 1)
static ThreeMMU32 word(const unsigned char *b, int big)
{
    return big ? ((ThreeMMU32)b[0] << 8) | b[1] : ((ThreeMMU32)b[1] << 8) | b[0];
}
static ThreeMMI32 signed_word(ThreeMMU32 n)
{
    return n < 32768U ? (ThreeMMI32)n : (ThreeMMI32)n - 65536L;
}
static void put(unsigned char *b, ThreeMMU32 n, int big)
{
    b[big ? 1 : 0] = (unsigned char)n;
    b[big ? 0 : 1] = (unsigned char)(n >> 8);
}
static int layout(ThreeMMI32 w, ThreeMMI32 h, ThreeMMI32 *row, ThreeMMI32 *bytes)
{
    if (w < 0 || h < 0 || w > MAXIMUM / 2)
        return 0;
    *row = w * 2;
    if (*row && h > (MAXIMUM - 12) / *row)
        return 0;
    *bytes = *row * h;
    return 1;
}
int three_mm_depth_header(const unsigned char b[12], int big, ThreeMMDepthBitmap *m)
{
    ThreeMMI32 w = signed_word(word(b + 8, big)), h = signed_word(word(b + 10, big));
    if (!layout(w, h, &m->row_bytes, &m->bytes))
        return 0;
    m->bounds.left = signed_word(word(b + 4, big));
    m->bounds.top = signed_word(word(b + 6, big));
    m->bounds.right = m->bounds.left + w;
    m->bounds.bottom = m->bounds.top + h;
    return 1;
}
void *three_mm_depth_new(ThreeMMI32 w, ThreeMMI32 h, const void *initial, const ThreeMMDepthServices *s)
{
    void *object;
    ThreeMMDepthBitmap *m;
    ThreeMMI32 row, n;
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
void three_mm_depth_dispose(ThreeMMDepthBitmap *m, const ThreeMMDepthServices *s)
{
    s->free_memory(s->context, &m->pixels);
}
static int read_at(void *block, void *data, ThreeMMI32 n, ThreeMMI32 at, const ThreeMMDepthServices *s)
{
    ThreeMMI32 bytes = s->size(s->context, block, 0);
    return n >= 0 && at >= 0 && at <= bytes && n <= bytes - at && s->read(s->context, block, data, n, at);
}
void *three_mm_depth_read(void *block, const ThreeMMDepthServices *s)
{
    void *object;
    ThreeMMDepthBitmap *m;
    unsigned char header[12];
    if (!s->unpack(s->context, block))
        return NULL;
    object = s->create(s->context);
    if (!object)
        goto fail;
    m = s->state(s->context, object);
    if (!read_at(block, header, 12, 0, s) || !three_mm_depth_header(header, s->host_big_endian, m) ||
        s->size(s->context, block, 0) != 12 + m->bytes || !s->allocate(s->context, m->bytes, &m->pixels) ||
        !read_at(block, m->pixels, m->bytes, 12, s))
        goto fail;
    return object;
fail:
    s->warning(s->context);
    if (object)
        s->release(s->context, object);
    return NULL;
}
int three_mm_depth_fetch(void *block, int want, const ThreeMMDepthServices *s)
{
    void *object;
    ThreeMMI32 n = s->size(s->context, block, 1);
    if (!want)
    {
        s->publish_fetch(s->context, 1, NULL, n);
        return 1;
    }
    if (!s->unpack(s->context, block))
        goto fail;
    object = three_mm_depth_read(block, s);
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
int three_mm_depth_write(const ThreeMMDepthBitmap *m, void *file, ThreeMMU32 type, ThreeMMU32 *number,
                         const ThreeMMDepthServices *s)
{
    unsigned char header[12];
    void *block;
    if (m->bytes < 0 || m->bytes > MAXIMUM - 12)
        return 0;
    put(header, 1, s->host_big_endian);
    put(header + 2, s->system, s->host_big_endian);
    put(header + 4, (ThreeMMU32)m->bounds.left, s->host_big_endian);
    put(header + 6, (ThreeMMU32)m->bounds.top, s->host_big_endian);
    put(header + 8, (ThreeMMU32)m->bounds.right - (ThreeMMU32)m->bounds.left, s->host_big_endian);
    put(header + 10, (ThreeMMU32)m->bounds.bottom - (ThreeMMU32)m->bounds.top, s->host_big_endian);
    block = s->add(s->context, file, type, 12 + m->bytes, number);
    return block && s->write(s->context, block, header, 12, 0) && s->write(s->context, block, m->pixels, m->bytes, 12);
}
static int add(ThreeMMI32 a, ThreeMMI32 b, ThreeMMI32 *out)
{
    if ((b > 0 && a > MAXIMUM - b) || (b < 0 && a < MINIMUM - b))
        return 0;
    *out = a + b;
    return 1;
}
static int intersect(ThreeMMDepthRect *a, const ThreeMMDepthRect *b)
{
    if (a->left < b->left)
        a->left = b->left;
    if (a->right > b->right)
        a->right = b->right;
    if (a->top < b->top)
        a->top = b->top;
    if (a->bottom > b->bottom)
        a->bottom = b->bottom;
    return a->left < a->right && a->top < a->bottom;
}
int three_mm_depth_draw(const ThreeMMDepthBitmap *m, void *pixels, ThreeMMI32 row, ThreeMMI32 height, ThreeMMI32 x,
                        ThreeMMI32 y, const ThreeMMDepthRect *clip, void *region, int half,
                        const ThreeMMDepthServices *s)
{
    ThreeMMDepthRect bounds = m->bounds, r;
    ThreeMMI32 step = half ? 2 : 1, yp, left, right, sy, bytes, n, src, dst;
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
    {
        if (!s->region_bounds(s->context, region, &r) || !intersect(&bounds, &r))
            return 1;
    }
    if (clip && !intersect(&bounds, clip))
        return 1;
    s->scan_init(s->context, region, clip, &bounds);
    for (yp = bounds.top; yp < bounds.bottom;)
    {
        while (s->scan_pair(s->context, &left, &right))
        {
            if (!add(left, bounds.left, &left) || !add(right, bounds.left, &right) || left < bounds.left ||
                right > bounds.right || left < 0 || right < left || right > MAXIMUM / 2 || yp < 0)
                return 0;
            sy = yp / step;
            n = (right - left) * 2;
            if (left > m->row_bytes / 2 || right > m->row_bytes / 2 || right > row / 2 ||
                (m->row_bytes && sy > m->bytes / m->row_bytes) || (row && sy > bytes / row))
                return 0;
            src = sy * m->row_bytes;
            dst = sy * row;
            if (left * 2 > m->bytes - src || n > m->bytes - src - left * 2 || left * 2 > bytes - dst ||
                n > bytes - dst - left * 2 || (n && (!m->pixels || !pixels)))
                return 0;
            if (n)
                memmove((unsigned char *)pixels + dst + left * 2, (const unsigned char *)m->pixels + src + left * 2,
                        (size_t)n);
        }
        s->scan_next(s->context, step);
        if (yp > MAXIMUM - step || yp + step >= bounds.bottom)
            break;
        yp += step;
    }
    return 1;
}
