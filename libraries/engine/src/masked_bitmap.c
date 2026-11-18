/* Masked bitmap policy converted from Kauai mbmp.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/masked_bitmap.h"
#include "mm/wide.h"
#include <string.h>
#define MAXIMUM 2147483647
#define MINIMUM (-2147483647 - 1)
static MMU32 word(const unsigned char *p, unsigned n, int big)
{
    MMU32 v = 0;
    unsigned i;
    for (i = 0; i < n; ++i)
        v = (v << 8) | p[big ? i : n - i - 1];
    return v;
}
static void put(unsigned char *p, unsigned n, MMU32 v, int big)
{
    unsigned i;
    for (i = 0; i < n; ++i)
    {
        p[big ? n - i - 1 : i] = (unsigned char)v;
        v >>= 8;
    }
}
static int subtract(MMI32 a, MMI32 b, MMI32 *out)
{
    if ((b > 0 && a < MINIMUM + b) || (b < 0 && a > MAXIMUM + b))
        return 0;
    *out = a - b;
    return 1;
}
static int row_encode(const unsigned char *src, MMI32 width, unsigned char transparent, int mask,
                      unsigned char *out)
{
    MMI32 i = 0, run = 0, n = 0, j;
    int trans = 1, opaque = 0;
    for (j = 0; j < width; ++j)
        if (src[j] != transparent)
        {
            opaque = 1;
            break;
        }
    if (!opaque)
        return 0;
    for (;;)
    {
        if (i == width || trans != (src[i] == transparent) || run == 255)
        {
            if (trans && i == width)
                break;
            if (n == 32767)
                return -1;
            if (out)
                out[n] = (unsigned char)run;
            ++n;
            if (!trans && !mask)
            {
                if (run > 32767 - n)
                    return -1;
                if (out && run)
                    memcpy(out + n, src + i - run, (size_t)run);
                n += run;
            }
            if (i == width)
                break;
            run = 0;
            trans = !trans;
        }
        else
        {
            ++run;
            ++i;
        }
    }
    return (int)n;
}
static void canonical(unsigned char *b, MMI32 n, const MMMaskedServices *s)
{
    put(b, 2, 1, s->host_big_endian);
    put(b + 2, 2, s->system, s->host_big_endian);
    put(b + 6, 2, 0, s->host_big_endian);
    put(b + 24, 4, (MMU32)n, s->host_big_endian);
}
void mm_masked_dispose(MMMaskedBitmap *m, const MMMaskedServices *s)
{
    s->free_memory(s->context, &m->data);
    m->bytes = m->row_bytes = 0;
}
int mm_masked_init(MMMaskedBitmap *m, const unsigned char *pixels, MMI32 row, MMI32 height,
                         const MMMaskedRect *extract, MMI32 xref, MMI32 yref, unsigned char transparent,
                         unsigned flags, unsigned char fill, const MMMaskedServices *s)
{
    MMMaskedRect r;
    MMI32 x, y, sy, n, table, at, left = MAXIMUM, top = MAXIMUM, right = 0, bottom = 0;
    int count, mask = (flags & 2) != 0;
    unsigned char *b;
    void *data = NULL;
    if (!pixels || !extract || row <= 0 || height <= 0 || height > MAXIMUM / row || extract->left < 0 ||
        extract->top < 0 || extract->left >= extract->right || extract->top >= extract->bottom ||
        extract->right > row || extract->bottom > height)
        return 0;
    for (y = extract->top; y < extract->bottom; ++y)
    {
        sy = (flags & 1) ? height - y - 1 : y;
        for (x = extract->left; x < extract->right; ++x)
            if (pixels[sy * row + x] != transparent)
            {
                if (x < left)
                    left = x;
                if (x + 1 > right)
                    right = x + 1;
                if (y < top)
                    top = y;
                bottom = y + 1;
            }
    }
    if (left == MAXIMUM)
    {
        left = top = right = bottom = 0;
        s->diagnostic(s->context, MM_MASKED_EMPTY);
    }
    if (!subtract(left, xref, &r.left) || !subtract(right, xref, &r.right) || !subtract(top, yref, &r.top) ||
        !subtract(bottom, yref, &r.bottom))
        return 0;
    if (bottom - top > (MAXIMUM - 28) / 2)
        return 0;
    table = (bottom - top) * 2;
    n = 28 + table;
    for (y = top; y < bottom; ++y)
    {
        sy = (flags & 1) ? height - y - 1 : y;
        count = row_encode(pixels + sy * row + left, right - left, transparent, mask, NULL);
        if (count < 0 || count > MAXIMUM - n)
            return 0;
        n += count;
    }
    if (!s->allocate(s->context, n, &data))
        return 0;
    b = (unsigned char *)data;
    memset(b, 0, 28);
    canonical(b, n, s);
    b[4] = (unsigned char)mask;
    b[5] = fill;
    put(b + 8, 4, (MMU32)r.left, s->host_big_endian);
    put(b + 12, 4, (MMU32)r.top, s->host_big_endian);
    put(b + 16, 4, (MMU32)r.right, s->host_big_endian);
    put(b + 20, 4, (MMU32)r.bottom, s->host_big_endian);
    at = 28 + table;
    for (y = top; y < bottom; ++y)
    {
        sy = (flags & 1) ? height - y - 1 : y;
        count = row_encode(pixels + sy * row + left, right - left, transparent, mask, b + at);
        put(b + 28 + (y - top) * 2, 2, (MMU32)count, s->host_big_endian);
        at += count;
    }
    mm_masked_dispose(m, s);
    m->data = data;
    m->bytes = n;
    m->row_bytes = table;
    return 1;
}
void *mm_masked_new(const unsigned char *pixels, MMI32 row, MMI32 height, const MMMaskedRect *r,
                          MMI32 x, MMI32 y, unsigned char transparent, unsigned flags, unsigned char fill,
                          const MMMaskedServices *s)
{
    void *o = s->create(s->context);
    if (o && !mm_masked_init(s->state(s->context, o), pixels, row, height, r, x, y, transparent, flags, fill, s))
    {
        s->release(s->context, o);
        o = NULL;
    }
    return o;
}
void mm_masked_bounds(const MMMaskedBitmap *m, int big, MMMaskedRect *r)
{
    const unsigned char *b = (const unsigned char *)m->data;
    r->left = mm_signed_word(word(b + 8, 4, big));
    r->top = mm_signed_word(word(b + 12, 4, big));
    r->right = mm_signed_word(word(b + 16, 4, big));
    r->bottom = mm_signed_word(word(b + 20, 4, big));
}
int mm_masked_validate(MMMaskedBitmap *m, int big)
{
    unsigned char *b = (unsigned char *)m->data;
    MMMaskedRect r;
    MMU32 marker, width, height, i, offset;
    MMI32 n = m->bytes, rows, at, end, pos, dxp, k, len;
    int source = big;
    if (n < 28 || !b)
        return 0;
    marker = word(b, 2, big);
    if (marker == 256)
        source = !big;
    else if (marker != 1)
        return 0;
    if (word(b + 6, 2, source) || word(b + 24, 4, source) != (MMU32)n)
        return 0;
    if (source != big)
    {
        for (i = 0; i < 4; i += 2)
            put(b + i, 2, word(b + i, 2, source), big);
        for (i = 8; i < 28; i += 4)
            put(b + i, 4, word(b + i, 4, source), big);
        put(b, 2, 1, big);
    }
    mm_masked_bounds(m, big, &r);
    if (r.bottom == r.top)
    {
        if (n != 28)
            return 0;
        put(b + 16, 4, (MMU32)r.left, big);
        m->row_bytes = 0;
        return 1;
    }
    if (r.bottom < r.top || r.right <= r.left)
        return 0;
    width = (MMU32)r.right - (MMU32)r.left;
    height = (MMU32)r.bottom - (MMU32)r.top;
    if (width > MAXIMUM || height > (MMU32)(n - 28) / 2)
        return 0;
    rows = (MMI32)height * 2;
    at = 28 + rows;
    for (offset = 28; offset < (MMU32)(28 + rows); offset += 2)
    {
        len = (MMI32)word(b + offset, 2, source);
        if (source != big)
            put(b + offset, 2, (MMU32)len, big);
        if (len > 32767 || len > n - at)
            return 0;
        end = at + len;
        pos = at;
        dxp = 0;
        while (pos < end)
        {
            k = b[pos++];
            if ((MMU32)k > width - (MMU32)dxp)
                return 0;
            dxp += k;
            if (pos == end)
                break;
            k = b[pos++];
            if ((MMU32)k > width - (MMU32)dxp)
                return 0;
            dxp += k;
            if (!b[4])
            {
                if (k > end - pos)
                    return 0;
                pos += k;
            }
        }
        at = end;
    }
    if (at != n)
        return 0;
    m->row_bytes = rows;
    return 1;
}
int mm_masked_hit(const MMMaskedBitmap *m, int big, MMI32 x, MMI32 y)
{
    const unsigned char *b = (const unsigned char *)m->data;
    MMMaskedRect r;
    MMI32 row, at, end, len, pos, skip, count;
    if (!b || m->bytes < 28 || m->row_bytes < 0 || m->row_bytes > m->bytes - 28)
        return 0;
    mm_masked_bounds(m, big, &r);
    if (x < r.left || x >= r.right || y < r.top || y >= r.bottom || !subtract(x, r.left, &x) ||
        !subtract(y, r.top, &y) || y >= m->row_bytes / 2)
        return 0;
    at = 28 + m->row_bytes;
    for (row = 0; row <= y; ++row)
    {
        len = (MMI32)word(b + 28 + row * 2, 2, big);
        if (len > 32767 || len > m->bytes - at)
            return 0;
        if (row == y)
            break;
        at += len;
    }
    end = at + len;
    pos = at;
    while (pos < end)
    {
        skip = b[pos++];
        if (x < skip || pos == end)
            return 0;
        x -= skip;
        count = b[pos++];
        if (!b[4] && count > end - pos)
            return 0;
        if (x < count)
            return 1;
        x -= count;
        if (!b[4])
            pos += count;
    }
    return 0;
}
static int visible_span(MMI32 origin, MMI32 extent, MMI32 clip_min, MMI32 clip_max, MMI32 *first, MMI32 *end)
{
    MMU32 start, stop;
    if (origin >= clip_max || clip_min >= clip_max)
        return 0;
    start = origin < clip_min ? (MMU32)clip_min - (MMU32)origin : 0;
    stop = (MMU32)clip_max - (MMU32)origin;
    if (start >= (MMU32)extent)
        return 0;
    *first = (MMI32)start;
    *end = stop < (MMU32)extent ? (MMI32)stop : extent;
    return 1;
}

static void fill_bits(unsigned char *line, MMI32 left, MMI32 right, int on)
{
    MMI32 first = left / 8, last = (right - 1) / 8;
    unsigned char leading = (unsigned char)(0xffU >> (left & 7));
    unsigned char trailing = (unsigned char)(0xffU << (7 - ((right - 1) & 7)));
    if (first == last)
        leading &= trailing;
    else
    {
        if (last > first + 1)
            memset(line + first + 1, on ? 255 : 0, (size_t)(last - first - 1));
        line[last] = (unsigned char)(on ? line[last] | trailing : line[last] & ~trailing);
    }
    line[first] = (unsigned char)(on ? line[first] | leading : line[first] & ~leading);
}

static int next_span(const MMMaskedSpans *spans, MMI32 origin, MMI32 *left, MMI32 *right)
{
    if (!spans || !spans->next(spans->context, left, right))
        return 0;
    *left -= origin;
    *right -= origin;
    return 1;
}

static void blit_row(const unsigned char *run, const unsigned char *row_end, unsigned char *line,
                     MMI32 origin, MMI32 left, MMI32 right, int solid, unsigned char fill, int mask,
                     const MMMaskedSpans *spans)
{
    MMI32 x = 0, count, run_end, start, end;
    if (spans && !next_span(spans, origin, &left, &right))
        return;
    if (mask)
        fill_bits(line, origin + left, origin + right, 0);
    while (run < row_end)
    {
        x += *run++;
        if (run == row_end)
            break;
        count = *run++;
        run_end = x + count;
        while (left < run_end)
        {
            start = x < left ? left : x;
            end = run_end < right ? run_end : right;
            if (end > start)
            {
                if (mask)
                    fill_bits(line, origin + start, origin + end, 1);
                else if (solid)
                    memset(line + (origin + start), fill, (size_t)(end - start));
                else
                    memcpy(line + (origin + start), run + (start - x), (size_t)(end - start));
            }
            if (right > run_end)
                break;
            if (!next_span(spans, origin, &left, &right))
                return;
        }
        if (!solid)
            run += count;
        x = run_end;
    }
}

static void blit(const MMMaskedBitmap *m, int big, unsigned char *pixels, MMI32 stride, MMI32 height,
                 MMI32 left, MMI32 top, const MMMaskedRect *clip, int mask, const MMMaskedSpans *spans)
{
    const unsigned char *b = (const unsigned char *)m->data;
    MMMaskedRect r, target;
    const unsigned char *encoded, *row_end;
    unsigned char *line;
    MMI32 x0, x1, y0, y1, row;
    if (stride <= 0 || height <= 0 || (size_t)stride != (MMU32)stride || (size_t)height != (MMU32)height ||
        (size_t)height > (size_t)-1 / (size_t)stride || (mask && stride > MAXIMUM / 8))
        return;
    target.left = target.top = 0;
    target.right = mask ? stride * 8 : stride;
    target.bottom = height;
    if (clip)
    {
        if (clip->left > target.left)
            target.left = clip->left;
        if (clip->top > target.top)
            target.top = clip->top;
        if (clip->right < target.right)
            target.right = clip->right;
        if (clip->bottom < target.bottom)
            target.bottom = clip->bottom;
    }
    mm_masked_bounds(m, big, &r);
    if (!visible_span(left, r.right - r.left, target.left, target.right, &x0, &x1) ||
        !visible_span(top, r.bottom - r.top, target.top, target.bottom, &y0, &y1))
        return;
    if (spans)
    {
        target.left = left + x0;
        target.right = left + x1;
        target.top = top + y0;
        target.bottom = top + y1;
        spans->begin(spans->context, &target);
    }
    encoded = b + 28 + m->row_bytes;
    for (row = 0; row < y0; ++row)
        encoded += word(b + 28 + row * 2, 2, big);
    for (; row < y1; ++row)
    {
        line = pixels + (size_t)(top + row) * (size_t)stride;
        row_end = encoded + word(b + 28 + row * 2, 2, big);
        blit_row(encoded, row_end, line, left, x0, x1, b[4], b[5], mask, spans);
        encoded = row_end;
        if (spans && row + 1 < y1)
            spans->advance(spans->context);
    }
}

void mm_masked_blit8(const MMMaskedBitmap *m, int big, unsigned char *pixels, MMI32 stride, MMI32 height,
                     MMI32 left, MMI32 top, const MMMaskedRect *clip, const MMMaskedSpans *spans)
{
    blit(m, big, pixels, stride, height, left, top, clip, 0, spans);
}

void mm_masked_blit_mask(const MMMaskedBitmap *m, int big, unsigned char *pixels, MMI32 stride, MMI32 height,
                         MMI32 left, MMI32 top, const MMMaskedRect *clip)
{
    blit(m, big, pixels, stride, height, left, top, clip, 1, NULL);
}

int mm_masked_read_stream(const MMReadStream *stream, MMMaskedBitmap *out, const MMMaskedServices *s)
{
    MMMaskedBitmap m;
    memset(&m, 0, sizeof(m));
    if (stream->length < 28 || stream->length > MAXIMUM || (size_t)stream->length != stream->length)
        return 0;
    m.bytes = (MMI32)stream->length;
    if (!s->allocate(s->context, m.bytes, &m.data))
        return 0;
    if (!mm_read_exact(stream, 0, m.data, (size_t)m.bytes) || !mm_masked_validate(&m, s->host_big_endian))
    {
        mm_masked_dispose(&m, s);
        return 0;
    }
    *out = m;
    return 1;
}

typedef struct MaskedBlockStream
{
    void *block;
    const MMMaskedServices *services;
} MaskedBlockStream;

static int read_block(void *context, MMU32 offset, void *data, size_t bytes)
{
    MaskedBlockStream *source = (MaskedBlockStream *)context;
    const MMMaskedServices *s = source->services;
    if (offset || !s->read(s->context, source->block, data, (MMI32)bytes))
        return 0;
    if (s->is_memory(s->context, source->block))
        s->consume(s->context, source->block);
    return 1;
}

void *mm_masked_read(void *block, const MMMaskedServices *s)
{
    MMMaskedBitmap m = {0};
    MaskedBlockStream source;
    MMReadStream stream;
    MMI32 bytes;
    void *o;
    if (!s->unpack(s->context, block))
        return NULL;
    bytes = s->size(s->context, block, 0);
    if (bytes < 0)
        return NULL;
    source.block = block;
    source.services = s;
    stream.context = &source;
    stream.read_at = read_block;
    stream.length = (MMU32)bytes;
    if (!mm_masked_read_stream(&stream, &m, s))
        return NULL;
    o = s->create(s->context);
    if (!o)
    {
        mm_masked_dispose(&m, s);
        return NULL;
    }
    *s->state(s->context, o) = m;
    return o;
}
int mm_masked_fetch(void *block, int want, const MMMaskedServices *s)
{
    MMI32 n = s->size(s->context, block, 1);
    void *o;
    if (!want)
    {
        s->publish_fetch(s->context, 1, NULL, n);
        return 1;
    }
    if (!s->unpack(s->context, block))
        goto fail;
    n = s->size(s->context, block, 0);
    o = mm_masked_read(block, s);
    if (!o)
        goto fail;
    s->publish_fetch(s->context, 1, o, n);
    return 1;
fail:
    s->publish_fetch(s->context, 0, NULL, 0);
    return 0;
}
int mm_masked_write(MMMaskedBitmap *m, void *block, const MMMaskedServices *s)
{
    if (!m->data || m->bytes < 28)
        return 0;
    canonical((unsigned char *)m->data, m->bytes, s);
    if (m->bytes != s->size(s->context, block, 0))
    {
        s->diagnostic(s->context, MM_MASKED_WRONG_SIZE);
        return 0;
    }
    return s->write(s->context, block, m->data, m->bytes);
}
void *mm_masked_import(void *filename, unsigned char transparent, MMI32 x, MMI32 y, unsigned flags,
                             unsigned char fill, const MMMaskedServices *s)
{
    MMMaskedSource input;
    MMMaskedRect r;
    void *o = NULL;
    memset(&input, 0, sizeof(input));
    if (!s->bitmap_read(s->context, filename, transparent, &input))
        return NULL;
    r.left = r.top = 0;
    r.right = input.width;
    r.bottom = input.height;
    if (input.width > 0 && input.width <= MAXIMUM - 3)
        o = mm_masked_new((const unsigned char *)input.pixels, (input.width + 3) & ~3, input.height, &r, x, y,
                                transparent, input.upside_down ? flags : flags ^ 1U, fill, s);
    s->free_memory(s->context, &input.pixels);
    return o;
}
