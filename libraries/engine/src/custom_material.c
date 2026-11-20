/* Custom material ownership converted from engine mtrl.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/material_resource.h"
#include "mm/template_runtime.h"
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
static int valid_count(MMI32 count, const MMMaterialServices *s)
{
    return count >= 0 && (MMU32)count <= (0x7fffffffUL - (MMU32)s->custom_bytes) / (2 * sizeof(void *));
}
static int arrays(MMCustomMaterial *m, const MMMaterialServices *s)
{
    void *p = NULL;
    if (!valid_count(m->count, s) || !s->allocate(s->context, (MMI32)(m->count * sizeof(void *)), &p))
        return 0;
    m->materials = (void **)p;
    p = NULL;
    if (!s->allocate(s->context, (MMI32)(m->count * sizeof(void *)), &p))
        return 0;
    m->models = (void **)p;
    return 1;
}
void mm_custom_material_dispose(MMCustomMaterial *m, const MMMaterialServices *s)
{
    MMI32 i;
    void *p;
    if (m->materials)
    {
        for (i = 0; i < m->count; ++i)
            if (m->materials[i])
            {
                s->release(s->context, m->materials[i]);
                m->materials[i] = NULL;
            }
        p = m->materials;
        s->free_memory(s->context, &p);
        m->materials = NULL;
    }
    if (m->models)
    {
        for (i = 0; i < m->count; ++i)
            if (m->models[i])
            {
                s->release(s->context, m->models[i]);
                m->models[i] = NULL;
            }
        p = m->models;
        s->free_memory(s->context, &p);
        m->models = NULL;
    }
}
void *mm_custom_material_new(MMI32 set, MMI32 count, const void *materials,
                                   const MMMaterialServices *s)
{
    void *object;
    MMCustomMaterial *m;
    MMI32 i;
    if (!valid_count(count, s))
        return NULL;
    object = s->create(s->context, MM_CUSTOM_MATERIAL);
    if (!object)
        return NULL;
    m = s->custom_state(s->context, object);
    m->set = set;
    m->count = count;
    if (!arrays(m, s))
    {
        s->release(s->context, object);
        return NULL;
    }
    for (i = 0; i < count; ++i)
    {
        m->materials[i] = s->input_material(s->context, materials, i);
        s->retain(s->context, m->materials[i]);
    }
    return object;
}
MMI32 mm_custom_material_read_set(const unsigned char b[8], int big, int *bad_order)
{
    if (word(b, 2, big) == 256)
        big = !big;
    if (bad_order)
        *bad_order = word(b, 2, big) != 1;
    return mm_signed_word(word(b + 4, 4, big));
}

static int part_child(const MMChunkAccess *access, MMU32 parent, MMU32 slot, MMU32 type,
                      MMMaterialChild *out)
{
    MMChunkChild child;
    MMU32 position;
    int status = mm_chunk_child_exact(access, parent, slot, type, &child);
    if (status == MM_CHUNK_NOT_FOUND)
        return MM_CHUNK_OK;
    if (status != MM_CHUNK_OK)
        return status;
    status = mm_chunk_find(access, child.type, child.number, &position);
    if (status != MM_CHUNK_OK)
        return status == MM_CHUNK_NOT_FOUND ? MM_CHUNK_BAD_FILE : status;
    out->type = child.type;
    out->number = child.number;
    out->id = child.slot;
    return MM_CHUNK_OK;
}

int mm_custom_material_part(const MMChunkAccess *access, MMU32 position, MMU32 slot,
                            MMCustomMaterialPart *out)
{
    MMCustomMaterialPart part;
    int status;
    memset(&part, 0, sizeof(part));
    status = part_child(access, position, slot, MM_MATERIAL_CHUNK, &part.material);
    if (status == MM_CHUNK_OK)
        status = part_child(access, position, slot, MM_TEMPLATE_BMDL, &part.model);
    if (status == MM_CHUNK_OK)
        *out = part;
    return status;
}

static int read_custom(void *object, void *cache, MMU32 type, MMU32 number, void *block,
                       const MMMaterialServices *s)
{
    MMCustomMaterial *m = s->custom_state(s->context, object);
    unsigned char b[8];
    int bad_order;
    MMI32 i, id, set;
    MMU32 position, ordinal;
    MMChunkAccess access;
    MMChunkEntry entry;
    MMChunkChild child;
    MMCustomMaterialPart part;
    if (!block)
        block = s->find(s->context, cache, type, number);
    if (!block || !s->unpack(s->context, block))
        return 0;
    if (s->size(s->context, block) != 8)
    {
        s->diagnostic(s->context, MM_MATERIAL_BAD_CUSTOM_SIZE);
        return 0;
    }
    if (!s->read(s->context, block, b, 8))
        return 0;
    set = mm_custom_material_read_set(b, s->host_big_endian, &bad_order);
    if (bad_order)
        s->diagnostic(s->context, MM_MATERIAL_BAD_CUSTOM_ORDER);
    m->set = set;
    m->count = 0;
    s->chunks(s->context, cache, &access);
    if (mm_chunk_find(&access, type, number, &position) != MM_CHUNK_OK ||
        mm_chunk_at(&access, position, &entry) != MM_CHUNK_OK)
        return 0;
    for (ordinal = 0; ordinal < entry.child_count; ++ordinal)
    {
        if (mm_chunk_child_at(&access, position, ordinal, &child) != MM_CHUNK_OK)
            return 0;
        id = mm_signed_word(child.slot);
        if (id > m->count - 1)
        {
            if (id == 2147483647)
                return 0;
            m->count = id + 1;
        }
    }
    if (!arrays(m, s))
        return 0;
    for (i = 0; i < m->count; ++i)
    {
        if (mm_custom_material_part(&access, position, (MMU32)i, &part) != MM_CHUNK_OK)
            return 0;
        if (part.material.type)
        {
            m->materials[i] = s->fetch(s->context, cache, &part.material, MM_MATERIAL);
            if (!m->materials[i])
                return 0;
        }
        if (part.model.type)
        {
            m->models[i] = s->fetch(s->context, cache, &part.model, MM_MATERIAL_MODEL);
            if (!m->models[i])
                return 0;
        }
    }
    return 1;
}
int mm_custom_material_read(void *object, void *cache, MMU32 type, MMU32 number, void *block,
                                  const MMMaterialServices *s)
{
    int ok = read_custom(object, cache, type, number, block, s);
    s->close_block(s->context);
    return ok;
}
int mm_custom_material_fetch(void *cache, MMU32 type, MMU32 number, void *block, void *out, void *size,
                                   const MMMaterialServices *s)
{
    void *object;
    MMI32 bytes = s->custom_bytes;
    MMCustomMaterial *m;
    memcpy(size, &bytes, 4);
    if (!out)
        return 1;
    object = s->create(s->context, MM_CUSTOM_MATERIAL);
    if (!object || !mm_custom_material_read(object, cache, type, number, block, s))
    {
        if (object)
            s->release(s->context, object);
        s->invalidate(s->context, out, sizeof(object));
        s->invalidate(s->context, size, 4);
        return 0;
    }
    m = s->custom_state(s->context, object);
    bytes += (MMI32)(m->count * (2 * sizeof(void *)));
    memcpy(out, &object, sizeof(object));
    memcpy(size, &bytes, 4);
    return 1;
}
int mm_custom_material_has_models(const MMCustomMaterial *m)
{
    MMI32 i;
    for (i = 0; i < m->count; ++i)
        if (m->models[i])
            return 1;
    return 0;
}
void *mm_custom_material_model(const MMCustomMaterial *m, MMI32 index)
{
    return m->models[index];
}
void *mm_custom_material_handle(const MMCustomMaterial *m, MMI32 index,
                                      const MMMaterialServices *s)
{
    return s->material_handle(s->context, m->materials[index]);
}
