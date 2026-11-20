/* Material resources converted from engine mtrl.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/material_resource.h"
#include "mm/wide.h"
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
int mm_material_header_read(const unsigned char *b, int big, MMU32 windows, MMMaterialDescription *d)
{
    MMU32 bo = word(b, 2, big);
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
    d->power = mm_signed_word(word(b + 16, 4, big));
    return 1;
}
void mm_material_header_write(unsigned char *b, int big, MMU32 system, const MMMaterialDescription *d)
{
    put(b, 2, 1, big);
    put(b + 2, 2, system, big);
    put(b + 4, 4, d->colour, big);
    put(b + 8, 2, d->ambient, big);
    put(b + 10, 2, d->diffuse, big);
    put(b + 12, 2, d->specular, big);
    b[14] = (unsigned char)d->index_base;
    b[15] = (unsigned char)d->index_range;
    put(b + 16, 4, (MMU32)d->power, big);
}
static void defaults(void *handle, MMI32 base, MMI32 range, const MMMaterialServices *s)
{
    MMMaterialDescription d;
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
static void *allocate(void *object, const MMMaterialServices *s)
{
    MMMaterialResource *m = s->material_state(s->context, object);
    m->handle = s->allocate_material(s->context);
    if (m->handle)
        s->identify(s->context, m->handle, object);
    return m->handle;
}
void *mm_material_new(MMI32 base, MMI32 range, const MMMaterialServices *s)
{
    void *object = s->create(s->context, MM_MATERIAL), *handle;
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
void *mm_material_texture(const MMMaterialResource *m, const MMMaterialServices *s)
{
    return m->handle ? s->texture(s->context, m->handle) : NULL;
}
void mm_material_dispose(MMMaterialResource *m, const MMMaterialServices *s)
{
    void *texture = mm_material_texture(m, s);
    if (texture)
    {
        s->release(s->context, texture);
        s->map(s->context, m->handle, NULL, NULL);
    }
    if (m->handle)
    {
        s->remove_material(s->context, m->handle);
        s->free_material(s->context, m->handle);
        m->handle = NULL;
    }
}
static void attach(void *handle, void *texture, void *raw, const MMMaterialShade *shade,
                   const MMMaterialServices *s)
{
    MMMaterialDescription d;
    void *colour, *index_shade;
    s->describe(s->context, handle, &d);
    colour = raw ? raw : s->texture_pixelmap(s->context, texture);
    index_shade = s->texture_pixelmap(s->context, shade->texture);
    s->map(s->context, handle, colour, index_shade);
    d.flags |= s->map_flag;
    d.index_base = 0;
    d.index_range = (unsigned)(s->height(s->context, index_shade) - 1) & 255U;
    s->configure(s->context, handle, &d);
}
static int read_header(MMOwnedBlock *block, void *bytes, MMU32 size, const MMMaterialServices *s)
{
    MMBlockIO io;
    return mm_owned_block_code(block, 0, s->context, s->decode, NULL) == MM_DATA_OK &&
           block->view.length >= size && mm_owned_block_io(block, &io) == MM_DATA_OK &&
           mm_block_read(&io, 0, bytes, size, 0) == MM_DATA_OK;
}
static int read_material(void *object, MMResourceCache *cache, MMU32 type, MMU32 number, MMOwnedBlock *block,
                         MMOwnedBlock *scratch, const MMMaterialShade *shade, const MMMaterialServices *s)
{
    MMMaterialResource *m = s->material_state(s->context, object);
    MMMaterialDescription d;
    MMChunkAccess graph;
    MMU32 child;
    unsigned char b[28];
    MMI32 matrix[6];
    unsigned i;
    int big;
    void *texture;
    if (!read_header(block, b, 20, s))
        return 0;
    if (!mm_material_header_read(b, s->host_big_endian, s->windows_system, &d))
    {
        s->diagnostic(s->context, MM_MATERIAL_BAD_FILE);
        return 0;
    }
    if (!allocate(object, s))
        return 0;
    d.opacity = 255;
    d.flags = s->light_flag | s->smooth_flag;
    s->configure(s->context, m->handle, &d);
    graph = mm_resource_cache_chunks(cache);
    if (mm_chunk_link_number(&graph, type, number, 0, s->texture_type, &child) == MM_CHUNK_OK)
    {
        texture = mm_resource_cache_fetch_chunk(cache, s->texture_type, child, s->texture_reader);
        if (!texture)
            return 0;
        attach(m->handle, texture, NULL, shade, s);
        graph = mm_resource_cache_chunks(cache);
        if (mm_chunk_link_number(&graph, type, number, 0, s->transform_type, &child) == MM_CHUNK_OK)
        {
            if (mm_resource_cache_read_block(cache, s->transform_type, child, scratch) != MM_CHUNK_OK ||
                !read_header(scratch, b, 28, s))
            {
                s->release(s->context, texture);
                s->map(s->context, m->handle, NULL, NULL);
                s->free_material(s->context, m->handle);
                m->handle = NULL;
                return 0;
            }
            big = s->host_big_endian;
            if (word(b, 2, big) != 1)
                big = !big;
            if (word(b, 2, big) != 1)
                s->diagnostic(s->context, MM_MATERIAL_BAD_TRANSFORM_ORDER);
            for (i = 0; i < 6; ++i)
                matrix[i] = mm_signed_word(word(b + 4 + i * 4, 4, big));
            s->transform(s->context, m->handle, matrix);
        }
    }
    s->add_material(s->context, m->handle);
    return 1;
}
int mm_material_read(void *object, MMResourceCache *cache, MMU32 type, MMU32 number, MMOwnedBlock *block,
                     const MMMaterialShade *shade, const MMMaterialServices *s)
{
    MMOwnedBlock scratch;
    int ok = 0;
    if (mm_owned_block_init(&scratch, cache->services.handles, cache->services.block_files) != MM_DATA_OK)
        return 0;
    if (block || mm_resource_cache_read_block(cache, type, number, &scratch) == MM_CHUNK_OK)
        ok = read_material(object, cache, type, number, block ? block : &scratch, &scratch, shade, s);
    mm_owned_block_clear(&scratch);
    return ok;
}
int mm_material_fetch(MMResourceCache *cache, MMU32 type, MMU32 number, MMOwnedBlock *block, void *out, void *size,
                      const MMMaterialShade *shade, const MMMaterialServices *s)
{
    void *object;
    memcpy(size, &s->material_bytes, 4);
    if (!out)
        return 1;
    object = s->create(s->context, MM_MATERIAL);
    if (!object || !mm_material_read(object, cache, type, number, block, shade, s))
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
int mm_material_write_appearance(const MMMaterialResource *m, void *file, MMU32 type, MMU32 *number,
                                 const MMMaterialDescription *appearance, const MMI32 mapping[6],
                                 const MMMaterialServices *s)
{
    MMMaterialDescription d;
    MMMaterialChild child;
    unsigned char b[28];
    unsigned i;
    void *texture;
    if (!appearance)
    {
        s->describe(s->context, m->handle, &d);
        appearance = &d;
    }
    mm_material_header_write(b, s->host_big_endian, s->system, appearance);
    if (!s->add_chunk(s->context, file, type, b, 20, number))
        return 0;
    texture = mm_material_texture(m, s);
    child.id = 0;
    if (texture)
    {
        child.type = s->texture_type;
        child.number = 0;
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
    if (mapping)
    {
        child.type = s->transform_type;
        child.number = 0;
        put(b, 2, 1, s->host_big_endian);
        put(b + 2, 2, s->system, s->host_big_endian);
        for (i = 0; i < 6; ++i)
            put(b + 4 + i * 4, 4, (MMU32)mapping[i], s->host_big_endian);
        if (!s->add_chunk(s->context, file, child.type, b, 28, &child.number))
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
int mm_material_write(const MMMaterialResource *m, void *file, MMU32 type, MMU32 *number,
                      const MMMaterialServices *s)
{
    return mm_material_write_appearance(m, file, type, number, NULL, NULL, s);
}
int mm_material_shade_set(MMMaterialShade *m, void *replacement, const MMMaterialServices *s)
{
    if (!replacement)
        return 0;
    mm_material_shade_dispose(m, s);
    m->texture = replacement;
    return 1;
}
void mm_material_shade_dispose(MMMaterialShade *m, const MMMaterialServices *s)
{
    if (m->texture)
    {
        s->release(s->context, m->texture);
        m->texture = NULL;
    }
}
void *mm_material_import(void *filename, void *palette, int bitmap, const MMMaterialShade *shade,
                               const MMMaterialServices *s)
{
    void *object, *handle, *texture, *raw = NULL;
    MMMaterialDescription d;
    object = bitmap ? mm_material_new(-1, -1, s) : s->create(s->context, MM_MATERIAL);
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
        raw = s->read_pixelmap(s->context, s->pixelmap_path(s->context, filename));
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
