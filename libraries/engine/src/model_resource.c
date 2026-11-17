/* Model resource policy converted from engine modl.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/model_resource.h"
#include "mm/wide.h"
#include <stdlib.h>
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
int mm_model_header(const unsigned char b[48], int big, MMModelDescription *d, int *bad)
{
    MMU32 bo = word(b, 2, big);
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
    d->radius = mm_signed_word(word(b + 8, 4, big));
    for (i = 0; i < 3; ++i)
    {
        d->bounds.min[i] = mm_signed_word(word(b + 12 + i * 4, 4, big));
        d->bounds.max[i] = mm_signed_word(word(b + 24 + i * 4, 4, big));
        d->pivot[i] = mm_signed_word(word(b + 36 + i * 4, 4, big));
    }
    return d->vertices >= 0 && d->faces >= 0;
}
void mm_model_face_read(const unsigned char *b, int big, MMModelFace *f)
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
    f->distance = mm_signed_word(word(b + 28, 4, big));
}
void mm_model_face_write(unsigned char *b, int big, const MMModelFace *f)
{
    unsigned i;
    memset(b, 0, 32);
    for (i = 0; i < 3; ++i)
    {
        put(b + i * 2, 2, f->vertices[i], big);
        put(b + 6 + i * 2, 2, f->edges[i], big);
        put(b + 20 + i * 2, 2, (MMU32)f->normal[i], big);
    }
    put(b + 16, 2, f->smoothing, big);
    b[18] = (unsigned char)f->flags;
    b[19] = (unsigned char)f->pad;
    put(b + 28, 4, (MMU32)f->distance, big);
}

static void vertex_read(const unsigned char *bytes, int big, MMModelVertex *v)
{
    unsigned i;
    for (i = 0; i < 3; ++i)
    {
        v->position[i] = mm_signed_word(word(bytes + i * 4, 4, big));
        v->normal[i] = signed16(word(bytes + 26 + i * 2, 2, big));
    }
    for (i = 0; i < 2; ++i)
        v->mapping[i] = mm_signed_word(word(bytes + 12 + i * 4, 4, big));
    v->index = bytes[20];
    v->red = bytes[21];
    v->green = bytes[22];
    v->blue = bytes[23];
    v->reserved = word(bytes + 24, 2, big);
}

void mm_model_mesh_dispose(MMModelMesh *mesh)
{
    if (mesh)
    {
        free(mesh->vertices);
        free(mesh->faces);
        memset(mesh, 0, sizeof(*mesh));
    }
}

static int mesh_arrays(const MMReadStream *stream, MMModelMesh *mesh, int big)
{
    unsigned char bytes[32 * 128];
    MMI32 kind, i, n, j, count;
    MMU32 offset = 48;
    MMModelFace *face;
    for (kind = MM_MODEL_VERTICES; kind <= MM_MODEL_FACES; ++kind)
    {
        count = kind == MM_MODEL_VERTICES ? mesh->description.vertices : mesh->description.faces;
        for (i = 0; i < count; i += n)
        {
            n = count - i;
            if (n > 128)
                n = 128;
            if (!mm_read_exact(stream, offset, bytes, (size_t)n * 32))
                return MM_MODEL_MESH_IO;
            if (kind == MM_MODEL_VERTICES)
                for (j = 0; j < n; ++j)
                    vertex_read(bytes + j * 32, big, &mesh->vertices[i + j]);
            else
                for (j = 0; j < n; ++j)
                {
                    face = &mesh->faces[i + j];
                    mm_model_face_read(bytes + j * 32, big, face);
                    if (face->vertices[0] >= (MMU32)mesh->description.vertices ||
                        face->vertices[1] >= (MMU32)mesh->description.vertices ||
                        face->vertices[2] >= (MMU32)mesh->description.vertices)
                        return MM_MODEL_MESH_BAD_FILE;
                }
            offset += (MMU32)n * 32;
        }
    }
    return MM_MODEL_MESH_OK;
}

int mm_model_mesh_read(const MMReadStream *stream, MMModelMesh *out)
{
    const unsigned short one = 1;
    int big = *(const unsigned char *)&one == 0;
    unsigned char header[48];
    MMModelMesh mesh;
    int status;
    if (!stream || !out || stream->length < sizeof(header))
        return MM_MODEL_MESH_BAD_FILE;
    if (!mm_read_exact(stream, 0, header, sizeof(header)))
        return MM_MODEL_MESH_IO;
    memset(&mesh, 0, sizeof(mesh));
    if (!mm_model_header(header, big, &mesh.description, NULL) ||
        (MMU32)(mesh.description.vertices + mesh.description.faces) > (stream->length - sizeof(header)) / 32)
        return MM_MODEL_MESH_BAD_FILE;
    if ((mesh.description.vertices && (size_t)-1 / (size_t)mesh.description.vertices < sizeof(*mesh.vertices)) ||
        (mesh.description.faces && (size_t)-1 / (size_t)mesh.description.faces < sizeof(*mesh.faces)))
        return MM_MODEL_MESH_MEMORY;
    if (mesh.description.vertices)
        mesh.vertices = (MMModelVertex *)malloc((size_t)mesh.description.vertices * sizeof(*mesh.vertices));
    if (mesh.description.faces)
        mesh.faces = (MMModelFace *)malloc((size_t)mesh.description.faces * sizeof(*mesh.faces));
    if ((mesh.description.vertices && !mesh.vertices) || (mesh.description.faces && !mesh.faces))
        status = MM_MODEL_MESH_MEMORY;
    else
        status = mesh_arrays(stream, &mesh, big);
    if (status == MM_MODEL_MESH_OK)
        *out = mesh;
    else
        mm_model_mesh_dispose(&mesh);
    return status;
}
void mm_model_init(MMModelResource *m)
{
    m->handle = NULL;
}
void mm_model_dispose(MMModelResource *m, const MMModelServices *s)
{
    if (m->handle)
    {
        s->remove(s->context, m->handle);
        s->free_model(s->context, m->handle);
        m->handle = NULL;
    }
}
void *mm_model_new(MMI32 vertices, const void *v, MMI32 faces, const void *f,
                         const MMModelServices *s)
{
    void *object;
    MMModelResource *m;
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
int mm_model_fetch(void *block, void *out, void *size, const MMModelServices *s)
{
    void *object = NULL;
    MMI32 bytes = s->size(s->context, block, 1);
    memcpy(size, &bytes, 4);
    if (!out)
        return 1;
    if (!s->unpack(s->context, block))
        goto fail;
    bytes = s->size(s->context, block, 0);
    memcpy(size, &bytes, 4);
    object = s->create(s->context);
    if (!object || !mm_model_read(object, block, s))
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
static int read_at(void *block, void *buffer, MMI32 n, MMI32 at, const MMModelServices *s)
{
    MMI32 size = s->size(s->context, block, 0);
    if (n < 0 || at < 0 || at > size || n > size - at)
        return 0;
    return s->read(s->context, block, buffer, n, at);
}
static int faces(void *block, void *handle, int prepared, MMI32 count, MMI32 offset,
                 const MMModelServices *s)
{
    void *memory = NULL;
    MMModelFace face;
    MMI32 i;
    int ok = 0;
    if (!count)
        return 1;
    if (!s->allocate(s->context, count * 32, 0, &memory))
        goto done;
    if (!read_at(block, memory, count * 32, offset, s))
        goto done;
    for (i = 0; i < count; ++i)
    {
        mm_model_face_read((const unsigned char *)memory + i * 32, s->host_big_endian, &face);
        s->set_face(s->context, handle, prepared, i, &face);
    }
    ok = 1;
done:
    s->free_memory(s->context, &memory);
    return ok;
}
int mm_model_read(void *object, void *block, const MMModelServices *s)
{
    MMModelResource *m = s->state(s->context, object);
    MMModelDescription d;
    unsigned char header[48];
    MMI32 vb;
    int prepared, bad, valid;
    if (!s->unpack(s->context, block) || !read_at(block, header, 48, 0, s))
        return 0;
    valid = mm_model_header(header, s->host_big_endian, &d, &bad);
    if (bad && s->diagnostic)
        s->diagnostic(s->context, MM_MODEL_BAD_ORDER);
    if (!valid)
    {
        if (s->diagnostic)
            s->diagnostic(s->context, MM_MODEL_BAD_COUNT);
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
        if (!s->component(s->context, m->handle, MM_MODEL_VERTICES, vb) ||
            !s->component(s->context, m->handle, MM_MODEL_FACES, d.faces * s->face_bytes))
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
        if (!s->component(s->context, m->handle, MM_MODEL_VERTEX_GROUP, s->vertex_group_bytes))
            return 0;
        s->bind_group(s->context, m->handle, 0);
        if (!s->component(s->context, m->handle, MM_MODEL_FACE_GROUP, s->face_group_bytes))
            return 0;
        s->bind_group(s->context, m->handle, 1);
        s->geometry(s->context, m->handle, &d);
    }
    s->add(s->context, m->handle);
    return 1;
}
int mm_model_write(const MMModelResource *m, void *file, MMU32 type, MMU32 number,
                         const MMModelServices *s)
{
    MMModelDescription d;
    MMModelFace face;
    void *memory = NULL;
    unsigned char *b;
    MMI32 i, vb, bytes;
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
    put(b + 4, 2, (MMU32)d.vertices, s->host_big_endian);
    put(b + 6, 2, (MMU32)d.faces, s->host_big_endian);
    put(b + 8, 4, (MMU32)d.radius, s->host_big_endian);
    for (i = 0; i < 3; ++i)
    {
        put(b + 12 + i * 4, 4, (MMU32)d.bounds.min[i], s->host_big_endian);
        put(b + 24 + i * 4, 4, (MMU32)d.bounds.max[i], s->host_big_endian);
        put(b + 36 + i * 4, 4, (MMU32)d.pivot[i], s->host_big_endian);
    }
    if (vb)
        memcpy(b + 48, s->vertex_data(s->context, m->handle, 1), (size_t)vb);
    for (i = 0; i < d.faces; ++i)
    {
        s->get_face(s->context, m->handle, i, &face);
        mm_model_face_write(b + 48 + vb + i * 32, s->host_big_endian, &face);
    }
    ok = s->put(s->context, file, type, number, b, bytes);
done:
    if (!ok && s->save_failed)
        s->save_failed(s->context);
    s->free_memory(s->context, &memory);
    return ok;
}
void *mm_model_dat(void *filename, const MMModelServices *s)
{
    void *object = s->create(s->context);
    const void *path;
    MMModelResource *m;
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
MMI32 mm_model_extent(const MMModelResource *m, unsigned axis, const MMModelServices *s)
{
    MMModelDescription d;
    s->describe(s->context, m->handle, &d);
    return mm_fixed_sub(d.bounds.max[axis], d.bounds.min[axis]);
}
static MMI32 half_floor(MMI32 n)
{
    return n < 0 ? -1 - ((-1 - n) / 2) : n / 2;
}
void mm_model_adjust_glyph(const MMModelResource *m, const MMModelServices *s)
{
    MMModelDescription d;
    MMI32 i, x, z, spacing, p[3];
    s->describe(s->context, m->handle, &d);
    spacing = d.bounds.min[0];
    x = mm_fixed_add(half_floor(mm_fixed_sub(d.bounds.max[0], d.bounds.min[0])), spacing);
    z = half_floor(mm_fixed_sub(d.bounds.max[2], d.bounds.min[2]));
    for (i = 0; i < d.vertices; ++i)
    {
        s->position(s->context, m->handle, i, p);
        p[0] = mm_fixed_sub(p[0], x);
        p[2] = mm_fixed_sub(p[2], z);
        s->set_position(s->context, m->handle, i, p);
    }
    d.bounds.min[0] = mm_fixed_sub(d.bounds.min[0], x);
    d.bounds.max[0] = mm_fixed_sub(d.bounds.max[0], x);
    d.pivot[0] = mm_fixed_sub(d.pivot[0], x);
    d.bounds.min[0] = mm_fixed_sub(d.bounds.min[0], spacing);
    if (spacing < -655)
        spacing = 32768;
    d.bounds.max[0] = mm_fixed_add(d.bounds.max[0], spacing);
    d.bounds.min[2] = mm_fixed_sub(d.bounds.min[2], z);
    d.bounds.max[2] = mm_fixed_sub(d.bounds.max[2], z);
    d.pivot[2] = mm_fixed_sub(d.pivot[2], z);
    s->geometry(s->context, m->handle, &d);
}
