/* Texture map policy converted from bren/tmap.cpp. */
#include "mm/texture_resource.h"
#include <string.h>
static MMU32 word(const unsigned char *p, unsigned n, int big)
{
    MMU32 v = 0;
    unsigned i;
    for (i = 0; i < n; ++i)
        v = (v << 8) | p[big ? i : n - 1 - i];
    return v;
}
static void put(unsigned char *p, unsigned n, MMU32 v, int big)
{
    unsigned i;
    for (i = 0; i < n; ++i)
    {
        p[big ? n - 1 - i : i] = (unsigned char)v;
        v >>= 8;
    }
}
static MMI32 signed16(MMU32 n)
{
    return n < 32768U ? (MMI32)n : (MMI32)n - 65536L;
}
int mm_texture_payload(const MMTextureDescription *d, MMI32 *n)
{
    if (d->row_bytes < 0 || d->row_bytes > 32767 || d->width < 0 || d->width > 65535L || d->height < 0 ||
        d->height > 65535L || (d->row_bytes && (MMU32)d->height > 0x7fffffebUL / (MMU32)d->row_bytes))
        return 0;
    *n = d->row_bytes * d->height;
    return 1;
}
int mm_texture_header_read(const unsigned char *b, int big, MMTextureDescription *d, int *bad)
{
    MMI32 n;
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
    return d->height <= 32767 && mm_texture_payload(d, &n);
}
void mm_texture_header_write(unsigned char *b, int big, MMU32 system, const MMTextureDescription *d)
{
    put(b, 2, 1, big);
    put(b + 2, 2, system, big);
    put(b + 4, 2, (MMU32)d->row_bytes, big);
    b[6] = (unsigned char)d->type;
    b[7] = (unsigned char)d->flags;
    put(b + 8, 2, (MMU32)d->base_x, big);
    put(b + 10, 2, (MMU32)d->base_y, big);
    put(b + 12, 2, (MMU32)d->width, big);
    put(b + 14, 2, (MMU32)d->height, big);
    put(b + 16, 2, (MMU32)d->origin_x, big);
    put(b + 18, 2, (MMU32)d->origin_y, big);
}
MMI32 mm_texture_size(const MMTextureResource *m)
{
    MMI32 n;
    return mm_texture_payload(&m->description, &n) ? 20 + n : -1;
}
void *mm_texture_new(void *pixels, MMI32 width, MMI32 height, const MMTextureServices *s)
{
    MMTextureDescription d;
    MMI32 n;
    MMTextureResource *m;
    void *object;
    memset(&d, 0, sizeof(d));
    d.row_bytes = width;
    d.width = width;
    d.height = height;
    d.type = s->indexed_type;
    d.flags = s->linear_flag;
    if (!mm_texture_payload(&d, &n) || (n && !pixels))
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
void *mm_texture_wrap(void *owner, const MMTextureServices *s)
{
    void *object;
    MMTextureResource *m;
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
void mm_texture_dispose(MMTextureResource *m, const MMTextureServices *s)
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
typedef struct TextureBlock
{
    void *block;
    const MMTextureServices *services;
} TextureBlock;

static int read_block(void *context, MMU32 offset, void *bytes, size_t count)
{
    const TextureBlock *source = (const TextureBlock *)context;
    const MMTextureServices *s = source->services;
    return s->read(s->context, source->block, bytes, (MMI32)count, (MMI32)offset);
}
static int load(void *object, const MMReadStream *stream, const MMTextureServices *s)
{
    MMTextureResource *m;
    unsigned char b[20];
    MMTextureDescription d;
    MMI32 n;
    int bad, valid;
    if (!mm_read_exact(stream, 0, b, sizeof(b)))
        return 0;
    valid = mm_texture_header_read(b, s->host_big_endian, &d, &bad);
    if (bad)
        s->diagnostic(s->context, MM_TEXTURE_ORDER);
    if (!valid || !mm_texture_payload(&d, &n))
        return 0;
    m = s->state(s->context, object);
    if (!s->allocate(s->context, n, 1, &m->pixels))
        return 0;
    m->description = d;
    s->publish(s->context, object, m);
    return mm_read_exact(stream, sizeof(b), m->pixels, (size_t)n);
}
void *mm_texture_read_stream(const MMReadStream *stream, const MMTextureServices *s)
{
    void *object = s->create(s->context);
    if (object && !load(object, stream, s))
    {
        s->release(s->context, object);
        object = NULL;
    }
    return object;
}
void *mm_texture_read(void *file, MMU32 type, MMU32 number, const MMTextureServices *s)
{
    void *object = s->create(s->context);
    TextureBlock source;
    MMReadStream stream;
    MMI32 length;
    if (!object)
        goto done;
    source.services = s;
    source.block = s->find(s->context, file, type, number);
    if (!source.block || !s->unpack(s->context, source.block))
        goto fail;
    length = s->size(s->context, source.block, 0);
    if (length < 0)
        goto fail;
    stream.context = &source;
    stream.length = (MMU32)length;
    stream.read_at = read_block;
    if (load(object, &stream, s))
        goto done;
fail:
    s->release(s->context, object);
    object = NULL;
done:
    s->close_block(s->context);
    return object;
}
int mm_texture_fetch(void *cache, MMU32 type, MMU32 number, void *block, void *out, void *size,
                           const MMTextureServices *s)
{
    MMI32 n = s->size(s->context, block, 1);
    void *object;
    memcpy(size, &n, 4);
    if (!out)
        return 1;
    object = mm_texture_read(s->file(s->context, cache), type, number, s);
    if (!object)
    {
        s->invalidate(s->context, out, sizeof(object));
        s->invalidate(s->context, size, 4);
        return 0;
    }
    memcpy(out, &object, sizeof(object));
    return 1;
}
int mm_texture_write(const MMTextureResource *m, void *block, const MMTextureServices *s)
{
    unsigned char b[20];
    MMI32 n;
    if (!mm_texture_payload(&m->description, &n))
        return 0;
    mm_texture_header_write(b, s->host_big_endian, s->system, &m->description);
    if (s->size(s->context, block, 0) < 20 || !s->write(s->context, block, b, 20, 0))
        return 0;
    return n <= s->size(s->context, block, 0) - 20 && s->write(s->context, block, m->pixels, n, 20);
}
int mm_texture_write_chunk(const MMTextureResource *m, void *file, MMU32 type, MMU32 *number,
                                 const MMTextureServices *s)
{
    void *block;
    MMI32 bytes = mm_texture_size(m);
    int ok = 0;
    if (bytes < 0)
        return 0;
    block = s->add(s->context, file, type, bytes, number);
    if (block)
        ok = mm_texture_write(m, block, s);
    s->close_block(s->context);
    return ok;
}
int mm_texture_standalone(const MMTextureResource *m, void *filename, int compress, void *sink,
                                const MMTextureServices *s)
{
    void *file = s->create_file(s->context, filename), *block;
    MMI32 bytes = mm_texture_size(m);
    MMU32 signature = s->unpacked_signature;
    unsigned char b[20];
    int error = MM_TEXTURE_FILE_CREATE, ok = 0;
    if (!file)
        goto done;
    error = MM_TEXTURE_FILE_MEMORY;
    if (bytes < 0)
        goto done;
    if (compress)
    {
        block = s->temp_block(s->context, bytes);
        if (!block || !mm_texture_write(m, block, s))
            goto done;
        if (s->pack(s->context, block))
        {
            signature = s->packed_signature;
            bytes = s->size(s->context, block, 1);
        }
        put(b, 4, signature, s->host_big_endian);
        error = MM_TEXTURE_FILE_WRITE;
        if (!s->write_file(s->context, file, b, 4, 0) || !s->copy_to_file(s->context, block, file, 4, bytes))
            goto done;
    }
    else
    {
        put(b, 4, signature, s->host_big_endian);
        error = MM_TEXTURE_FILE_WRITE;
        if (!s->write_file(s->context, file, b, 4, 0))
            goto done;
        mm_texture_header_write(b, s->host_big_endian, s->system, &m->description);
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
