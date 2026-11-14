/* Material resources converted from engine mtrl.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/material_resource.h"
#include "three_mm/wide.h"
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
int three_mm_material_header_read(const unsigned char *b, int big, ThreeMMU32 windows, ThreeMMMaterialDescription *d)
{
    ThreeMMU32 bo = word(b, 2, big);
    if (bo == 256)
    {
        big = !big;
        bo = word(b, 2, big);
    }
    if (bo != 1 && !(bo == 0 && word(b + 2, 2, big) == windows))
        return 0;
    d->colour = word(b + 4, 4, big);
    d->ambient = word(b + 8, 2, big);
    d->diffuse = word(b + 10, 2, big);
    d->specular = word(b + 12, 2, big);
    d->index_base = b[14];
    d->index_range = b[15];
    d->power = three_mm_signed_word(word(b + 16, 4, big));
    return 1;
}
void three_mm_material_header_write(unsigned char *b, int big, ThreeMMU32 system, const ThreeMMMaterialDescription *d)
{
    put(b, 2, 1, big);
    put(b + 2, 2, system, big);
    put(b + 4, 4, d->colour, big);
    put(b + 8, 2, d->ambient, big);
    put(b + 10, 2, d->diffuse, big);
    put(b + 12, 2, d->specular, big);
    b[14] = (unsigned char)d->index_base;
    b[15] = (unsigned char)d->index_range;
    put(b + 16, 4, (ThreeMMU32)d->power, big);
}
static void defaults(void *handle, ThreeMMI32 base, ThreeMMI32 range, const ThreeMMMaterialServices *s)
{
    ThreeMMMaterialDescription d;
    s->describe(s->context, handle, &d);
    d.ambient = 6553;
    d.diffuse = d.specular = 39321;
    d.power = 3276800;
    d.opacity = 255;
    d.index_base = base == -1 ? 15 : (unsigned)base;
    d.index_range = range == -1 ? 15 : (unsigned)range;
    d.flags = s->light_flag | s->gouraud_flag;
    s->configure(s->context, handle, &d);
}
static void *allocate(void *object, const ThreeMMMaterialServices *s)
{
    ThreeMMMaterialResource *m = s->material_state(s->context, object);
    m->handle = s->allocate_material(s->context);
    if (m->handle)
        s->identify(s->context, m->handle, object);
    return m->handle;
}
void *three_mm_material_new(ThreeMMI32 base, ThreeMMI32 range, const ThreeMMMaterialServices *s)
{
    void *object = s->create(s->context, THREE_MM_MATERIAL), *handle;
    if (!object)
        return NULL;
    handle = allocate(object, s);
    if (!handle)
    {
        s->release(s->context, object);
        return NULL;
    }
    defaults(handle, base, range, s);
    s->add_material(s->context, handle);
    return object;
}
void *three_mm_material_texture(const ThreeMMMaterialResource *m, const ThreeMMMaterialServices *s)
{
    return m->handle ? s->texture(s->context, m->handle) : NULL;
}
void three_mm_material_dispose(ThreeMMMaterialResource *m, const ThreeMMMaterialServices *s)
{
    void *texture = three_mm_material_texture(m, s);
    if (texture)
    {
        s->release(s->context, texture);
        s->map(s->context, m->handle, NULL, NULL, NULL);
    }
    if (m->handle)
    {
        s->remove_material(s->context, m->handle);
        s->free_material(s->context, m->handle);
        m->handle = NULL;
    }
}
static void attach(void *handle, void *texture, void *raw, const ThreeMMMaterialShade *shade,
                   const ThreeMMMaterialServices *s)
{
    ThreeMMMaterialDescription d;
    s->describe(s->context, handle, &d);
    s->map(s->context, handle, texture, raw, shade->texture);
    d.flags |= s->map_flag;
    d.index_base = 0;
    d.index_range = (unsigned)(s->height(s->context, shade->texture) - 1) & 255U;
    s->configure(s->context, handle, &d);
}
static int read_header(void *file, ThreeMMU32 type, ThreeMMU32 number, void *b, ThreeMMI32 n, int exact,
                       const ThreeMMMaterialServices *s)
{
    void *block = s->find(s->context, file, type, number);
    ThreeMMI32 bytes;
    if (!block || !s->unpack(s->context, block))
        return 0;
    bytes = s->size(s->context, block);
    if (exact ? bytes != n : bytes < n)
        return 0;
    return s->read(s->context, block, b, n);
}
static int read_material(void *object, void *cache, ThreeMMU32 type, ThreeMMU32 number,
                         const ThreeMMMaterialShade *shade, const ThreeMMMaterialServices *s)
{
    ThreeMMMaterialResource *m = s->material_state(s->context, object);
    ThreeMMMaterialDescription d;
    ThreeMMMaterialChild child;
    unsigned char b[28];
    ThreeMMI32 matrix[6];
    unsigned i;
    int big;
    void *file = s->file(s->context, cache), *texture;
    if (!read_header(file, type, number, b, 20, 0, s))
        return 0;
    if (!three_mm_material_header_read(b, s->host_big_endian, s->windows_system, &d))
    {
        s->diagnostic(s->context, THREE_MM_MATERIAL_BAD_FILE);
        return 0;
    }
    if (!allocate(object, s))
        return 0;
    d.opacity = 255;
    d.flags = s->light_flag | s->smooth_flag;
    s->configure(s->context, m->handle, &d);
    if (s->child(s->context, file, type, number, 0, s->texture_type, &child))
    {
        texture = s->fetch(s->context, cache, &child, THREE_MM_MATERIAL_TEXTURE);
        if (!texture)
            return 0;
        attach(m->handle, texture, NULL, shade, s);
        if (s->child(s->context, file, type, number, 0, s->transform_type, &child))
        {
            if (!read_header(file, child.type, child.number, b, 28, 0, s))
            {
                s->release(s->context, texture);
                s->map(s->context, m->handle, NULL, NULL, NULL);
                s->free_material(s->context, m->handle);
                m->handle = NULL;
                return 0;
            }
            big = s->host_big_endian;
            if (word(b, 2, big) != 1)
                big = !big;
            if (word(b, 2, big) != 1)
                s->diagnostic(s->context, THREE_MM_MATERIAL_BAD_TRANSFORM_ORDER);
            for (i = 0; i < 6; ++i)
                matrix[i] = three_mm_signed_word(word(b + 4 + i * 4, 4, big));
            s->transform(s->context, m->handle, matrix);
        }
    }
    s->add_material(s->context, m->handle);
    return 1;
}
int three_mm_material_read(void *object, void *cache, ThreeMMU32 type, ThreeMMU32 number,
                           const ThreeMMMaterialShade *shade, const ThreeMMMaterialServices *s)
{
    int ok = read_material(object, cache, type, number, shade, s);
    s->close_block(s->context);
    return ok;
}
int three_mm_material_fetch(void *cache, ThreeMMU32 type, ThreeMMU32 number, void *out, void *size,
                            const ThreeMMMaterialShade *shade, const ThreeMMMaterialServices *s)
{
    void *object;
    memcpy(size, &s->material_bytes, 4);
    if (!out)
        return 1;
    object = s->create(s->context, THREE_MM_MATERIAL);
    if (!object || !three_mm_material_read(object, cache, type, number, shade, s))
    {
        s->invalidate(s->context, out, sizeof(object));
        s->invalidate(s->context, size, 4);
        if (object)
            s->release(s->context, object);
        return 0;
    }
    memcpy(out, &object, sizeof(object));
    return 1;
}
int three_mm_material_write(const ThreeMMMaterialResource *m, void *file, ThreeMMU32 type, ThreeMMU32 *number,
                            const ThreeMMMaterialServices *s)
{
    ThreeMMMaterialDescription d;
    ThreeMMMaterialChild child;
    unsigned char b[20];
    void *texture;
    s->describe(s->context, m->handle, &d);
    three_mm_material_header_write(b, s->host_big_endian, s->system, &d);
    if (!s->add_chunk(s->context, file, type, b, 20, number))
        return 0;
    texture = three_mm_material_texture(m, s);
    if (texture)
    {
        child.type = s->texture_type;
        child.id = 0;
        if (!s->write_texture(s->context, texture, file, child.type, &child.number))
        {
            s->delete_chunk(s->context, file, type, *number);
            return 0;
        }
        if (!s->adopt(s->context, file, type, *number, &child))
        {
            s->delete_chunk(s->context, file, child.type, child.number);
            s->delete_chunk(s->context, file, type, *number);
            return 0;
        }
    }
    return 1;
}
int three_mm_material_shade_set(ThreeMMMaterialShade *m, void *file, ThreeMMU32 type, ThreeMMU32 number,
                                const ThreeMMMaterialServices *s)
{
    void *replacement = s->read_texture(s->context, file, type, number);
    if (!replacement)
        return 0;
    three_mm_material_shade_dispose(m, s);
    m->texture = replacement;
    return 1;
}
void three_mm_material_shade_dispose(ThreeMMMaterialShade *m, const ThreeMMMaterialServices *s)
{
    if (m->texture)
    {
        s->release(s->context, m->texture);
        m->texture = NULL;
    }
}
void *three_mm_material_import(void *filename, void *palette, int bitmap, const ThreeMMMaterialShade *shade,
                               const ThreeMMMaterialServices *s)
{
    void *object, *handle, *texture, *raw = NULL;
    ThreeMMMaterialDescription d;
    object = bitmap ? three_mm_material_new(-1, -1, s) : s->create(s->context, THREE_MM_MATERIAL);
    if (!object)
        return NULL;
    handle = bitmap ? s->material_state(s->context, object)->handle : allocate(object, s);
    if (!handle)
        goto fail;
    if (bitmap)
        texture = s->read_bitmap(s->context, filename, palette);
    else
    {
        defaults(handle, -1, -1, s);
        s->describe(s->context, handle, &d);
        d.colour = 0;
        s->configure(s->context, handle, &d);
        raw = s->read_pixelmap(s->context, filename);
        if (!raw)
            goto fail;
        texture = s->wrap_pixelmap(s->context, raw);
        /* Do not expose an unwrapped pixelmap to disposal as a TextureMap. */
        if (!texture)
        {
            s->free_pixelmap(s->context, raw);
            goto fail;
        }
    }
    if (!texture)
        goto fail;
    attach(handle, texture, raw, shade, s);
    return object;
fail:
    s->release(s->context, object);
    return NULL;
}
