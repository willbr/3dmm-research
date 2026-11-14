/* Model resource policy converted from engine modl.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/model_resource.h"
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
static ThreeMMI32 signed16(ThreeMMU32 n)
{
    return n < 32768U ? (ThreeMMI32)n : (ThreeMMI32)n - 65536L;
}
int three_mm_model_header(const unsigned char b[48], int big, ThreeMMModelDescription *d, int *bad)
{
    ThreeMMU32 bo = word(b, 2, big);
    unsigned i;
    if (bo == 256U)
    {
        big = !big;
        bo = word(b, 2, big);
    }
    if (bad)
        *bad = bo != 1U;
    d->vertices = signed16(word(b + 4, 2, big));
    d->faces = signed16(word(b + 6, 2, big));
    d->radius = three_mm_signed_word(word(b + 8, 4, big));
    for (i = 0; i < 3; ++i)
    {
        d->bounds.min[i] = three_mm_signed_word(word(b + 12 + i * 4, 4, big));
        d->bounds.max[i] = three_mm_signed_word(word(b + 24 + i * 4, 4, big));
        d->pivot[i] = three_mm_signed_word(word(b + 36 + i * 4, 4, big));
    }
    return d->vertices >= 0 && d->faces >= 0;
}
void three_mm_model_face_read(const unsigned char *b, int big, ThreeMMModelFace *f)
{
    unsigned i;
    for (i = 0; i < 3; ++i)
    {
        f->vertices[i] = word(b + i * 2, 2, big);
        f->edges[i] = word(b + 6 + i * 2, 2, big);
        f->normal[i] = signed16(word(b + 20 + i * 2, 2, big));
    }
    f->smoothing = word(b + 16, 2, big);
    f->flags = b[18];
    f->pad = b[19];
    f->distance = three_mm_signed_word(word(b + 28, 4, big));
}
void three_mm_model_face_write(unsigned char *b, int big, const ThreeMMModelFace *f)
{
    unsigned i;
    memset(b, 0, 32);
    for (i = 0; i < 3; ++i)
    {
        put(b + i * 2, 2, f->vertices[i], big);
        put(b + 6 + i * 2, 2, f->edges[i], big);
        put(b + 20 + i * 2, 2, (ThreeMMU32)f->normal[i], big);
    }
    put(b + 16, 2, f->smoothing, big);
    b[18] = (unsigned char)f->flags;
    b[19] = (unsigned char)f->pad;
    put(b + 28, 4, (ThreeMMU32)f->distance, big);
}
void three_mm_model_init(ThreeMMModelResource *m)
{
    m->handle = NULL;
}
void three_mm_model_dispose(ThreeMMModelResource *m, const ThreeMMModelServices *s)
{
    if (m->handle)
    {
        s->remove(s->context, m->handle);
        s->free_model(s->context, m->handle);
        m->handle = NULL;
    }
}
void *three_mm_model_new(ThreeMMI32 vertices, const void *v, ThreeMMI32 faces, const void *f,
                         const ThreeMMModelServices *s)
{
    void *object;
    ThreeMMModelResource *m;
    if (vertices < 0 || vertices >= 65535L || faces < 0 || faces >= 65535L)
        return NULL;
    object = s->create(s->context);
    if (!object)
        return NULL;
    m = s->state(s->context, object);
    m->handle = s->allocate_model(s->context, vertices, faces);
    if (!m->handle)
    {
        s->release(s->context, object);
        return NULL;
    }
    s->identify(s->context, m->handle, object);
    s->copy_input(s->context, m->handle, v, f, vertices, faces);
    s->add(s->context, m->handle);
    return object;
}
int three_mm_model_fetch(void *block, void *out, void *size, const ThreeMMModelServices *s)
{
    void *object = NULL;
    ThreeMMI32 bytes = s->size(s->context, block, 1);
    memcpy(size, &bytes, 4);
    if (!out)
        return 1;
    if (!s->unpack(s->context, block))
        goto fail;
    bytes = s->size(s->context, block, 0);
    memcpy(size, &bytes, 4);
    object = s->create(s->context);
    if (!object || !three_mm_model_read(object, block, s))
        goto fail;
    memcpy(out, &object, sizeof(object));
    return 1;
fail:
    if (object)
        s->release(s->context, object);
    s->invalidate(s->context, out, sizeof(object));
    s->invalidate(s->context, size, 4);
    return 0;
}
static int read_at(void *block, void *buffer, ThreeMMI32 n, ThreeMMI32 at, const ThreeMMModelServices *s)
{
    ThreeMMI32 size = s->size(s->context, block, 0);
    if (n < 0 || at < 0 || at > size || n > size - at)
        return 0;
    return s->read(s->context, block, buffer, n, at);
}
static int faces(void *block, void *handle, int prepared, ThreeMMI32 count, ThreeMMI32 offset,
                 const ThreeMMModelServices *s)
{
    void *memory = NULL;
    ThreeMMModelFace face;
    ThreeMMI32 i;
    int ok = 0;
    if (!count)
        return 1;
    if (!s->allocate(s->context, count * 32, 0, &memory))
        goto done;
    if (!read_at(block, memory, count * 32, offset, s))
        goto done;
    for (i = 0; i < count; ++i)
    {
        three_mm_model_face_read((const unsigned char *)memory + i * 32, s->host_big_endian, &face);
        s->set_face(s->context, handle, prepared, i, &face);
    }
    ok = 1;
done:
    s->free_memory(s->context, &memory);
    return ok;
}
int three_mm_model_read(void *object, void *block, const ThreeMMModelServices *s)
{
    ThreeMMModelResource *m = s->state(s->context, object);
    ThreeMMModelDescription d;
    unsigned char header[48];
    ThreeMMI32 vb;
    int prepared, bad, valid;
    if (!s->unpack(s->context, block) || !read_at(block, header, 48, 0, s))
        return 0;
    valid = three_mm_model_header(header, s->host_big_endian, &d, &bad);
    if (bad && s->diagnostic)
        s->diagnostic(s->context, THREE_MM_MODEL_BAD_ORDER);
    if (!valid)
    {
        if (s->diagnostic)
            s->diagnostic(s->context, THREE_MM_MODEL_BAD_COUNT);
        return 0;
    }
    prepared = d.radius != 0;
    vb = d.vertices * 32;
    m->handle = s->allocate_model(s->context, prepared ? 0 : d.vertices, prepared ? 0 : d.faces);
    if (!m->handle)
        return 0;
    s->identify(s->context, m->handle, object);
    if (prepared)
    {
        if (!s->component(s->context, m->handle, THREE_MM_MODEL_VERTICES, vb) ||
            !s->component(s->context, m->handle, THREE_MM_MODEL_FACES, d.faces * s->face_bytes))
            return 0;
    }
    if (!read_at(block, s->vertex_data(s->context, m->handle, prepared), vb, 48, s) ||
        !faces(block, m->handle, prepared, d.faces, 48 + vb, s))
        return 0;
    /* Native normalized the order marker before testing it here, so neither
     * prepared nor unprepared payloads were swapped. Preserve that policy. */
    if (prepared)
    {
        s->publish_prepared(s->context, m->handle, &d, s->prepared_flag, 1, 1);
        if (!s->component(s->context, m->handle, THREE_MM_MODEL_VERTEX_GROUP, s->vertex_group_bytes))
            return 0;
        s->bind_group(s->context, m->handle, 0);
        if (!s->component(s->context, m->handle, THREE_MM_MODEL_FACE_GROUP, s->face_group_bytes))
            return 0;
        s->bind_group(s->context, m->handle, 1);
        s->geometry(s->context, m->handle, &d);
    }
    s->add(s->context, m->handle);
    return 1;
}
int three_mm_model_write(const ThreeMMModelResource *m, void *file, ThreeMMU32 type, ThreeMMU32 number,
                         const ThreeMMModelServices *s)
{
    ThreeMMModelDescription d;
    ThreeMMModelFace face;
    void *memory = NULL;
    unsigned char *b;
    ThreeMMI32 i, vb, bytes;
    int ok = 0;
    s->describe(s->context, m->handle, &d);
    if (d.vertices < 0 || d.vertices > 65535L || d.faces < 0 || d.faces > 65535L)
        goto done;
    vb = d.vertices * 32;
    bytes = 48 + vb + d.faces * 32;
    if (!s->allocate(s->context, bytes, 1, &memory))
        goto done;
    b = (unsigned char *)memory;
    put(b, 2, 1, s->host_big_endian);
    put(b + 2, 2, s->system, s->host_big_endian);
    put(b + 4, 2, (ThreeMMU32)d.vertices, s->host_big_endian);
    put(b + 6, 2, (ThreeMMU32)d.faces, s->host_big_endian);
    put(b + 8, 4, (ThreeMMU32)d.radius, s->host_big_endian);
    for (i = 0; i < 3; ++i)
    {
        put(b + 12 + i * 4, 4, (ThreeMMU32)d.bounds.min[i], s->host_big_endian);
        put(b + 24 + i * 4, 4, (ThreeMMU32)d.bounds.max[i], s->host_big_endian);
        put(b + 36 + i * 4, 4, (ThreeMMU32)d.pivot[i], s->host_big_endian);
    }
    if (vb)
        memcpy(b + 48, s->vertex_data(s->context, m->handle, 1), (size_t)vb);
    for (i = 0; i < d.faces; ++i)
    {
        s->get_face(s->context, m->handle, i, &face);
        three_mm_model_face_write(b + 48 + vb + i * 32, s->host_big_endian, &face);
    }
    ok = s->put(s->context, file, type, number, b, bytes);
done:
    if (!ok && s->save_failed)
        s->save_failed(s->context);
    s->free_memory(s->context, &memory);
    return ok;
}
void *three_mm_model_dat(void *filename, const ThreeMMModelServices *s)
{
    void *object = s->create(s->context);
    const void *path;
    ThreeMMModelResource *m;
    if (!object)
        return NULL;
    path = s->path(s->context, filename);
    m = s->state(s->context, object);
    m->handle = s->load_dat(s->context, path);
    if (!m->handle)
    {
        s->release(s->context, object);
        return NULL;
    }
    s->or_flags(s->context, m->handle, s->keep_original_flag);
    s->prepare(s->context, m->handle, s->prepare_all);
    s->identify(s->context, m->handle, object);
    return object;
}
ThreeMMI32 three_mm_model_extent(const ThreeMMModelResource *m, unsigned axis, const ThreeMMModelServices *s)
{
    ThreeMMModelDescription d;
    s->describe(s->context, m->handle, &d);
    return three_mm_fixed_sub(d.bounds.max[axis], d.bounds.min[axis]);
}
static ThreeMMI32 half_floor(ThreeMMI32 n)
{
    return n < 0 ? -1 - ((-1 - n) / 2) : n / 2;
}
void three_mm_model_adjust_glyph(const ThreeMMModelResource *m, const ThreeMMModelServices *s)
{
    ThreeMMModelDescription d;
    ThreeMMI32 i, x, z, spacing, p[3];
    s->describe(s->context, m->handle, &d);
    spacing = d.bounds.min[0];
    x = three_mm_fixed_add(half_floor(three_mm_fixed_sub(d.bounds.max[0], d.bounds.min[0])), spacing);
    z = half_floor(three_mm_fixed_sub(d.bounds.max[2], d.bounds.min[2]));
    for (i = 0; i < d.vertices; ++i)
    {
        s->position(s->context, m->handle, i, p);
        p[0] = three_mm_fixed_sub(p[0], x);
        p[2] = three_mm_fixed_sub(p[2], z);
        s->set_position(s->context, m->handle, i, p);
    }
    d.bounds.min[0] = three_mm_fixed_sub(d.bounds.min[0], x);
    d.bounds.max[0] = three_mm_fixed_sub(d.bounds.max[0], x);
    d.pivot[0] = three_mm_fixed_sub(d.pivot[0], x);
    d.bounds.min[0] = three_mm_fixed_sub(d.bounds.min[0], spacing);
    if (spacing < -655)
        spacing = 32768;
    d.bounds.max[0] = three_mm_fixed_add(d.bounds.max[0], spacing);
    d.bounds.min[2] = three_mm_fixed_sub(d.bounds.min[2], z);
    d.bounds.max[2] = three_mm_fixed_sub(d.bounds.max[2], z);
    d.pivot[2] = three_mm_fixed_sub(d.pivot[2], z);
    s->geometry(s->context, m->handle, &d);
}
