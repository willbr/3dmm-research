/* Material resources converted from engine mtrl.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/material_resource.h"
#include "mm/chunk_storage.h"
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
struct MMMaterialOwner
{
    MMResourceObject binding;
    MMReference references;
    int kind;
    const MMMaterialOwnerHost *host;
};

static MMU32 owner_bytes(int kind)
{
    return sizeof(MMMaterialOwner) + (kind == MM_MATERIAL ? sizeof(MMMaterialResource) : sizeof(MMCustomMaterial));
}

MMMaterialOwner *mm_material_owner_create(const MMMaterialOwnerHost *host, int kind)
{
    const MMStorageAllocator *allocator = host->objects;
    MMU32 bytes = owner_bytes(kind);
    MMMaterialOwner *owner = allocator ? allocator->resize(allocator->context, NULL, 0, bytes, MM_STORAGE_NORMAL) :
        malloc(bytes);
    if (owner)
    {
        memset(owner, 0, bytes);
        mm_reference_init(&owner->references);
        owner->kind = kind;
        owner->host = host;
    }
    return owner;
}

const MMMaterialOwnerHost *mm_material_owner_host(const MMMaterialOwner *owner) { return owner->host; }
const MMResourceObject *mm_material_owner_binding(const MMMaterialOwner *owner) { return &owner->binding; }
int mm_material_owner_kind(const MMMaterialOwner *owner) { return owner->kind; }
MMI32 mm_material_owner_references(const MMMaterialOwner *owner) { return owner->references.count; }
MMMaterialResource *mm_material_owner_resource(MMMaterialOwner *owner) { return (MMMaterialResource *)(owner + 1); }
MMCustomMaterial *mm_material_owner_custom(MMMaterialOwner *owner) { return (MMCustomMaterial *)(owner + 1); }

static MMResourceObject *binding(void *object) { return &((MMMaterialOwner *)object)->binding; }
static MMReference *references(void *object) { return &((MMMaterialOwner *)object)->references; }
static void dispose_material(MMMaterialResource *, const MMMaterialServices *);

static void destroy(void *object)
{
    MMMaterialOwner *owner = object;
    const MMMaterialOwnerHost *host = owner->host;
    const MMStorageAllocator *allocator = host->objects;
    MMU32 bytes = owner_bytes(owner->kind);
    if (owner->kind == MM_MATERIAL)
        dispose_material(mm_material_owner_resource(owner), host->services);
    else
        mm_custom_material_dispose(mm_material_owner_custom(owner), host->services);
    mm_resource_object_dispose(owner, mm_material_owner_services());
    if (allocator)
        allocator->release(allocator->context, owner, bytes);
    else
        free(owner);
}

static void mark(void *object)
{
    MMMaterialOwner *owner = object;
    const MMMaterialOwnerHost *host = owner->host;
    const MMCustomMaterial *costume;
    void *texture;
    MMI32 i;
    if (!host->mark_memory)
        return;
    host->mark_memory(owner);
    if (host->mark_cache && !owner->binding.attached && owner->binding.cache)
        host->mark_cache(owner->binding.cache);
    if (owner->kind == MM_MATERIAL)
    {
        texture = mm_material_texture(mm_material_owner_resource(owner), host->services);
        if (texture && host->mark_texture)
            host->mark_texture(texture);
        return;
    }
    costume = mm_material_owner_custom(owner);
    if (costume->materials)
        host->mark_memory(costume->materials);
    if (costume->models)
        host->mark_memory(costume->models);
    for (i = 0; i < costume->count; ++i)
    {
        if (costume->materials && costume->materials[i] && host->services->material_objects &&
            host->services->material_objects->mark_memory)
            host->services->material_objects->mark_memory(costume->materials[i]);
        if (costume->models && costume->models[i] && host->mark_model)
            host->mark_model(costume->models[i]);
    }
}

const MMResourceObjectServices *mm_material_owner_services(void)
{
    static const MMResourceObjectServices services = {binding, references, destroy, NULL, mark};
    return &services;
}

void mm_material_owner_retain(MMMaterialOwner *owner)
{
    mm_reference_add(&owner->references);
}

void mm_material_owner_release(MMMaterialOwner *owner)
{
    if (owner)
        mm_resource_object_release(owner, mm_material_owner_services());
}

static void *allocate(MMMaterialOwner *owner, const MMMaterialServices *s)
{
    MMMaterialResource *m = mm_material_owner_resource(owner);
    m->handle = s->allocate_material(s->context);
    if (m->handle)
        s->identify(s->context, m->handle, owner);
    return m->handle;
}

MMMaterialOwner *mm_material_new(MMI32 base, MMI32 range, const MMMaterialOwnerHost *host)
{
    const MMMaterialServices *s = host->services;
    MMMaterialOwner *owner = mm_material_owner_create(host, MM_MATERIAL);
    void *handle;
    if (!owner)
        return NULL;
    handle = allocate(owner, s);
    if (!handle)
    {
        mm_material_owner_release(owner);
        return NULL;
    }
    defaults(handle, base, range, s);
    s->add_material(s->context, handle);
    return owner;
}
void *mm_material_texture(const MMMaterialResource *m, const MMMaterialServices *s)
{
    return m->handle ? s->texture(s->context, m->handle) : NULL;
}
static void release_texture(void *texture, const MMMaterialServices *s)
{
    if (s->texture_objects)
        mm_resource_object_release(texture, s->texture_objects);
    else
        s->release(s->context, texture);
}

static void dispose_material(MMMaterialResource *m, const MMMaterialServices *s)
{
    void *texture = mm_material_texture(m, s);
    if (texture)
    {
        release_texture(texture, s);
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
static int read_material(MMMaterialOwner *owner, MMResourceCache *cache, MMU32 type, MMU32 number, MMOwnedBlock *block,
                         MMOwnedBlock *scratch, const MMMaterialShade *shade, const MMMaterialServices *s)
{
    MMMaterialResource *m = mm_material_owner_resource(owner);
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
    if (!allocate(owner, s))
        return 0;
    d.opacity = 255;
    d.flags = s->light_flag | s->smooth_flag;
    s->configure(s->context, m->handle, &d);
    graph = mm_resource_cache_chunks(cache);
    if (mm_chunk_link_number(&graph, type, number, 0, s->texture_type, &child) == MM_CHUNK_OK)
    {
        texture = mm_resource_cache_fetch_owned_chunk(cache, s->texture_type, child,
                                                      s->texture_reader, s->texture_objects);
        if (!texture)
            return 0;
        attach(m->handle, texture, NULL, shade, s);
        graph = mm_resource_cache_chunks(cache);
        if (mm_chunk_link_number(&graph, type, number, 0, s->transform_type, &child) == MM_CHUNK_OK)
        {
            if (mm_resource_cache_read_block(cache, s->transform_type, child, scratch) != MM_CHUNK_OK ||
                !read_header(scratch, b, 28, s))
            {
                release_texture(texture, s);
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
int mm_material_read(MMMaterialOwner *owner, MMResourceCache *cache, MMU32 type, MMU32 number, MMOwnedBlock *block)
{
    const MMMaterialOwnerHost *host = owner->host;
    MMOwnedBlock scratch;
    int ok = 0;
    if (mm_owned_block_init(&scratch, cache->services.handles, cache->services.block_files) != MM_DATA_OK)
        return 0;
    if (block || mm_resource_cache_read_block(cache, type, number, &scratch) == MM_CHUNK_OK)
        ok = read_material(owner, cache, type, number, block ? block : &scratch, &scratch, host->shade, host->services);
    mm_owned_block_clear(&scratch);
    return ok;
}

int mm_material_fetch(MMResourceCache *cache, MMU32 type, MMU32 number, MMOwnedBlock *block, void **out, MMI32 *bytes,
                      const MMMaterialOwnerHost *host)
{
    const MMMaterialServices *s = host->services;
    MMMaterialOwner *owner;
    *bytes = s->material_bytes;
    if (!out)
        return 1;
    owner = mm_material_owner_create(host, MM_MATERIAL);
    if (!owner || !mm_material_read(owner, cache, type, number, block))
    {
        s->invalidate(s->context, out, sizeof(*out));
        s->invalidate(s->context, bytes, sizeof(*bytes));
        mm_material_owner_release(owner);
        return 0;
    }
    *out = owner;
    return 1;
}
static int within(float value, float low, float high)
{
    return value >= low && value <= high;
}

static int valid_components(const MMMaterialAppearance *v)
{
    unsigned i;
    if (!within(v->ambient, 0, 1) || !within(v->diffuse, 0, 1) || !within(v->specular, 0, 1) ||
        !within(v->shininess, 0, 32700) || v->index_base > 255 || v->index_range > 255)
        return 0;
    for (i = 0; i < 6; ++i)
        if (!within(v->mapping[i], -32700, 32700))
            return 0;
    return 1;
}

int mm_material_appearance_valid(const MMMaterialAppearance *v)
{
    return valid_components(v) && v->colour <= 0xffffffUL && v->index_base + v->index_range <= 255;
}

static int close_component(float a, float b)
{
    const float delta = a - b;
    return delta > -.00002f && delta < .00002f;
}

int mm_material_appearance_equal(const MMMaterialAppearance *a, const MMMaterialAppearance *b)
{
    unsigned i;
    if (a->colour != b->colour || a->index_base != b->index_base || a->index_range != b->index_range ||
        !close_component(a->ambient, b->ambient) || !close_component(a->diffuse, b->diffuse) ||
        !close_component(a->specular, b->specular) || !close_component(a->shininess, b->shininess))
        return 0;
    for (i = 0; i < 6; ++i)
        if (!close_component(a->mapping[i], b->mapping[i]))
            return 0;
    return 1;
}

void mm_material_read_view(const MMMaterialResource *source, MMMaterialAppearance *out, const MMMaterialServices *s)
{
    MMMaterialDescription material;
    MMI32 mapping[6];
    unsigned i;
    s->describe(s->context, source->handle, &material);
    out->textured = s->read_mapping(s->context, source->handle, mapping);
    out->colour = material.colour;
    out->index_base = material.index_base;
    out->index_range = material.index_range;
    out->ambient = material.ambient / 65535.f;
    out->diffuse = material.diffuse / 65535.f;
    out->specular = material.specular / 65535.f;
    out->shininess = material.power / 65536.f;
    for (i = 0; i < 6; ++i)
        out->mapping[i] = mapping[i] / 65536.f;
}

static MMU32 fraction(float gain)
{
    const float scaled = gain * 65535.f;
    return (MMU32)((double)scaled + .5);
}

int mm_material_write_view(const MMMaterialResource *source, const MMArchiveWriter *writer,
                           MMU32 type, MMU32 *number, const MMMaterialAppearance *v, const MMMaterialServices *s)
{
    MMMaterialDescription material = {0};
    MMI32 mapping[6];
    unsigned i;
    int textured;
    if (!valid_components(v))
        return 0;
    material.colour = v->colour;
    material.index_base = v->index_base;
    material.index_range = v->index_range;
    material.ambient = fraction(v->ambient);
    material.diffuse = fraction(v->diffuse);
    material.specular = fraction(v->specular);
    material.power = (MMI32)(v->shininess * 65536.f);
    textured = s->read_mapping(s->context, source->handle, mapping);
    if (textured)
        for (i = 0; i < 6; ++i)
            mapping[i] = (MMI32)(v->mapping[i] * 65536.f);
    return mm_material_write_appearance(source, &writer->edit, writer->payloads->files, type, number, &material,
                                        textured ? mapping : NULL, s);
}

int mm_material_write_appearance(const MMMaterialResource *m, const MMChunkEdit *edit, const MMBlockFileServices *files,
                                 MMU32 type, MMU32 *number, const MMMaterialDescription *appearance, const MMI32 mapping[6],
                                 const MMMaterialServices *s)
{
    MMMaterialDescription d;
    MMChunkChild child;
    unsigned char b[28];
    unsigned i;
    void *texture;
    if (!appearance)
    {
        s->describe(s->context, m->handle, &d);
        appearance = &d;
    }
    mm_material_header_write(b, s->host_big_endian, s->system, appearance);
    if (mm_chunk_add_bytes(edit, files, type, b, 20, number) != MM_CHUNK_OK)
    {
        s->invalidate(s->context, number, sizeof(*number));
        return 0;
    }
    texture = mm_material_texture(m, s);
    child.slot = 0;
    if (texture)
    {
        child.type = s->texture_type;
        child.number = 0;
        if (!s->write_texture(s->context, texture, edit, files, child.type, &child.number))
        {
            mm_chunk_delete(edit, type, *number);
            return 0;
        }
        if (mm_chunk_adopt(edit, type, *number, &child, 1) != MM_CHUNK_OK)
        {
            mm_chunk_delete(edit, child.type, child.number);
            mm_chunk_delete(edit, type, *number);
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
        if (mm_chunk_add_bytes(edit, files, child.type, b, 28, &child.number) != MM_CHUNK_OK)
        {
            mm_chunk_delete(edit, type, *number);
            return 0;
        }
        if (mm_chunk_adopt(edit, type, *number, &child, 1) != MM_CHUNK_OK)
        {
            mm_chunk_delete(edit, child.type, child.number);
            mm_chunk_delete(edit, type, *number);
            return 0;
        }
    }
    return 1;
}
int mm_material_write(const MMMaterialResource *m, const MMChunkEdit *edit, const MMBlockFileServices *files,
                      MMU32 type, MMU32 *number, const MMMaterialServices *s)
{
    return mm_material_write_appearance(m, edit, files, type, number, NULL, NULL, s);
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
        release_texture(m->texture, s);
        m->texture = NULL;
    }
}
MMMaterialOwner *mm_material_import(void *source, void *palette, int bitmap, const MMMaterialOwnerHost *host)
{
    const MMMaterialServices *s = host->services;
    MMMaterialOwner *owner;
    void *handle, *texture, *raw = NULL;
    MMMaterialDescription d;
    owner = bitmap ? mm_material_new(-1, -1, host) : mm_material_owner_create(host, MM_MATERIAL);
    if (!owner)
        return NULL;
    handle = bitmap ? mm_material_owner_resource(owner)->handle : allocate(owner, s);
    if (!handle)
        goto fail;
    if (bitmap)
        texture = s->read_bitmap(s->context, source, palette);
    else
    {
        defaults(handle, -1, -1, s);
        s->describe(s->context, handle, &d);
        d.colour = 0;
        s->configure(s->context, handle, &d);
        raw = s->read_pixelmap(s->context, source);
        if (!raw)
            goto fail;
        texture = s->wrap_pixelmap(s->context, raw);
        /* Do not expose an unwrapped pixelmap to disposal as a texture owner. */
        if (!texture)
        {
            s->free_pixelmap(s->context, raw);
            goto fail;
        }
    }
    if (!texture)
        goto fail;
    attach(handle, texture, raw, host->shade, s);
    return owner;
fail:
    mm_material_owner_release(owner);
    return NULL;
}
