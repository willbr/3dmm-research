/* Texture map policy converted from bren/tmap.cpp. */
#include "three_mm/texture_resource.h"
#include <string.h>
static ThreeMMU32 word(const unsigned char *p, unsigned n, int big)
{
    ThreeMMU32 v = 0;
    unsigned i;
    for (i = 0; i < n; ++i)
        v = (v << 8) | p[big ? i : n - 1 - i];
    return v;
}
static void put(unsigned char *p, unsigned n, ThreeMMU32 v, int big)
{
    unsigned i;
    for (i = 0; i < n; ++i)
    {
        p[big ? n - 1 - i : i] = (unsigned char)v;
        v >>= 8;
    }
}
static ThreeMMI32 signed16(ThreeMMU32 n)
{
    return n < 32768U ? (ThreeMMI32)n : (ThreeMMI32)n - 65536L;
}
int three_mm_texture_payload(const ThreeMMTextureDescription *d, ThreeMMI32 *n)
{
    if (d->row_bytes < 0 || d->row_bytes > 32767 || d->width < 0 || d->width > 65535L || d->height < 0 ||
        d->height > 65535L || (d->row_bytes && (ThreeMMU32)d->height > 0x7fffffebUL / (ThreeMMU32)d->row_bytes))
        return 0;
    *n = d->row_bytes * d->height;
    return 1;
}
int three_mm_texture_header_read(const unsigned char *b, int big, ThreeMMTextureDescription *d, int *bad)
{
    ThreeMMI32 n;
    if (word(b, 2, big) != 1)
        big = !big;
    if (bad)
        *bad = word(b, 2, big) != 1;
    d->row_bytes = signed16(word(b + 4, 2, big));
    d->type = b[6];
    d->flags = b[7];
    d->base_x = word(b + 8, 2, big);
    d->base_y = word(b + 10, 2, big);
    d->width = word(b + 12, 2, big);
    d->height = word(b + 14, 2, big);
    d->origin_x = signed16(word(b + 16, 2, big));
    d->origin_y = signed16(word(b + 18, 2, big));
    return d->height <= 32767 && three_mm_texture_payload(d, &n);
}
void three_mm_texture_header_write(unsigned char *b, int big, ThreeMMU32 system, const ThreeMMTextureDescription *d)
{
    put(b, 2, 1, big);
    put(b + 2, 2, system, big);
    put(b + 4, 2, (ThreeMMU32)d->row_bytes, big);
    b[6] = (unsigned char)d->type;
    b[7] = (unsigned char)d->flags;
    put(b + 8, 2, (ThreeMMU32)d->base_x, big);
    put(b + 10, 2, (ThreeMMU32)d->base_y, big);
    put(b + 12, 2, (ThreeMMU32)d->width, big);
    put(b + 14, 2, (ThreeMMU32)d->height, big);
    put(b + 16, 2, (ThreeMMU32)d->origin_x, big);
    put(b + 18, 2, (ThreeMMU32)d->origin_y, big);
}
ThreeMMI32 three_mm_texture_size(const ThreeMMTextureResource *m)
{
    ThreeMMI32 n;
    return three_mm_texture_payload(&m->description, &n) ? 20 + n : -1;
}
void *three_mm_texture_new(void *pixels, ThreeMMI32 width, ThreeMMI32 height, const ThreeMMTextureServices *s)
{
    ThreeMMTextureDescription d;
    ThreeMMI32 n;
    ThreeMMTextureResource *m;
    void *object;
    memset(&d, 0, sizeof(d));
    d.row_bytes = width;
    d.width = width;
    d.height = height;
    d.type = s->indexed_type;
    d.flags = s->linear_flag;
    if (!three_mm_texture_payload(&d, &n) || (n && !pixels))
        return NULL;
    object = s->create(s->context);
    if (!object)
        return NULL;
    m = s->state(s->context, object);
    m->pixels = pixels;
    m->description = d;
    s->publish(s->context, object, m);
    return object;
}
void *three_mm_texture_wrap(void *owner, const ThreeMMTextureServices *s)
{
    void *object;
    ThreeMMTextureResource *m;
    if (!owner)
        return NULL;
    object = s->create(s->context);
    if (!object)
        return NULL;
    m = s->state(s->context, object);
    m->imported_owner = owner;
    s->describe_import(s->context, owner, &m->description, &m->pixels);
    s->publish(s->context, object, m);
    return object;
}
void three_mm_texture_dispose(ThreeMMTextureResource *m, const ThreeMMTextureServices *s)
{
    if (m->imported_owner)
    {
        s->free_import(s->context, m->imported_owner);
        m->imported_owner = NULL;
        m->pixels = NULL;
    }
    else
        s->free_memory(s->context, &m->pixels);
}
static int read_at(void *block, void *data, ThreeMMI32 n, ThreeMMI32 offset, const ThreeMMTextureServices *s)
{
    ThreeMMI32 size = s->size(s->context, block, 0);
    return n >= 0 && offset >= 0 && offset <= size && n <= size - offset && s->read(s->context, block, data, n, offset);
}
void *three_mm_texture_read(void *file, ThreeMMU32 type, ThreeMMU32 number, const ThreeMMTextureServices *s)
{
    void *object = s->create(s->context), *block;
    ThreeMMTextureResource *m;
    unsigned char b[20];
    ThreeMMTextureDescription d;
    ThreeMMI32 n;
    int bad, valid;
    if (!object)
        goto done;
    block = s->find(s->context, file, type, number);
    if (!block || !s->unpack(s->context, block) || !read_at(block, b, 20, 0, s))
        goto fail;
    valid = three_mm_texture_header_read(b, s->host_big_endian, &d, &bad);
    if (bad)
        s->diagnostic(s->context, THREE_MM_TEXTURE_ORDER);
    if (!valid || !three_mm_texture_payload(&d, &n))
        goto fail;
    m = s->state(s->context, object);
    if (!s->allocate(s->context, n, 1, &m->pixels))
        goto fail;
    m->description = d;
    s->publish(s->context, object, m);
    if (!read_at(block, m->pixels, n, 20, s))
        goto fail;
    goto done;
fail:
    s->release(s->context, object);
    object = NULL;
done:
    s->close_block(s->context);
    return object;
}
int three_mm_texture_fetch(void *cache, ThreeMMU32 type, ThreeMMU32 number, void *block, void *out, void *size,
                           const ThreeMMTextureServices *s)
{
    ThreeMMI32 n = s->size(s->context, block, 1);
    void *object;
    memcpy(size, &n, 4);
    if (!out)
        return 1;
    object = three_mm_texture_read(s->file(s->context, cache), type, number, s);
    if (!object)
    {
        s->invalidate(s->context, out, sizeof(object));
        s->invalidate(s->context, size, 4);
        return 0;
    }
    memcpy(out, &object, sizeof(object));
    return 1;
}
int three_mm_texture_write(const ThreeMMTextureResource *m, void *block, const ThreeMMTextureServices *s)
{
    unsigned char b[20];
    ThreeMMI32 n;
    if (!three_mm_texture_payload(&m->description, &n))
        return 0;
    three_mm_texture_header_write(b, s->host_big_endian, s->system, &m->description);
    if (s->size(s->context, block, 0) < 20 || !s->write(s->context, block, b, 20, 0))
        return 0;
    return n <= s->size(s->context, block, 0) - 20 && s->write(s->context, block, m->pixels, n, 20);
}
int three_mm_texture_write_chunk(const ThreeMMTextureResource *m, void *file, ThreeMMU32 type, ThreeMMU32 *number,
                                 const ThreeMMTextureServices *s)
{
    void *block;
    ThreeMMI32 bytes = three_mm_texture_size(m);
    int ok = 0;
    if (bytes < 0)
        return 0;
    block = s->add(s->context, file, type, bytes, number);
    if (block)
        ok = three_mm_texture_write(m, block, s);
    s->close_block(s->context);
    return ok;
}
int three_mm_texture_standalone(const ThreeMMTextureResource *m, void *filename, int compress, void *sink,
                                const ThreeMMTextureServices *s)
{
    void *file = s->create_file(s->context, filename), *block;
    ThreeMMI32 bytes = three_mm_texture_size(m);
    ThreeMMU32 signature = s->unpacked_signature;
    unsigned char b[20];
    int error = THREE_MM_TEXTURE_FILE_CREATE, ok = 0;
    if (!file)
        goto done;
    error = THREE_MM_TEXTURE_FILE_MEMORY;
    if (bytes < 0)
        goto done;
    if (compress)
    {
        block = s->temp_block(s->context, bytes);
        if (!block || !three_mm_texture_write(m, block, s))
            goto done;
        if (s->pack(s->context, block))
        {
            signature = s->packed_signature;
            bytes = s->size(s->context, block, 1);
        }
        put(b, 4, signature, s->host_big_endian);
        error = THREE_MM_TEXTURE_FILE_WRITE;
        if (!s->write_file(s->context, file, b, 4, 0) || !s->copy_to_file(s->context, block, file, 4, bytes))
            goto done;
    }
    else
    {
        put(b, 4, signature, s->host_big_endian);
        error = THREE_MM_TEXTURE_FILE_WRITE;
        if (!s->write_file(s->context, file, b, 4, 0))
            goto done;
        three_mm_texture_header_write(b, s->host_big_endian, s->system, &m->description);
        if (!s->write_file(s->context, file, b, 20, 4) || !s->write_file(s->context, file, m->pixels, bytes - 20, 24))
            goto done;
    }
    ok = 1;
done:
    s->close_block(s->context);
    if (!ok && sink)
        s->report(s->context, sink, error);
    if (file)
    {
        if (!ok)
            s->make_temporary(s->context, file);
        s->release(s->context, file);
    }
    return ok;
}
