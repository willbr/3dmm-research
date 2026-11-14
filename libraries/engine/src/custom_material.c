/* Custom material ownership converted from engine mtrl.cpp.
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
static int valid_count(ThreeMMI32 count, const ThreeMMMaterialServices *s)
{
    return count >= 0 && (ThreeMMU32)count <= (0x7fffffffUL - (ThreeMMU32)s->custom_bytes) / (2 * sizeof(void *));
}
static int arrays(ThreeMMCustomMaterial *m, const ThreeMMMaterialServices *s)
{
    void *p = NULL;
    if (!valid_count(m->count, s) || !s->allocate(s->context, (ThreeMMI32)(m->count * sizeof(void *)), &p))
        return 0;
    m->materials = (void **)p;
    p = NULL;
    if (!s->allocate(s->context, (ThreeMMI32)(m->count * sizeof(void *)), &p))
        return 0;
    m->models = (void **)p;
    return 1;
}
void three_mm_custom_material_dispose(ThreeMMCustomMaterial *m, const ThreeMMMaterialServices *s)
{
    ThreeMMI32 i;
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
void *three_mm_custom_material_new(ThreeMMI32 set, ThreeMMI32 count, const void *materials,
                                   const ThreeMMMaterialServices *s)
{
    void *object;
    ThreeMMCustomMaterial *m;
    ThreeMMI32 i;
    if (!valid_count(count, s))
        return NULL;
    object = s->create(s->context, THREE_MM_CUSTOM_MATERIAL);
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
static int read_custom(void *object, void *cache, ThreeMMU32 type, ThreeMMU32 number, const ThreeMMMaterialServices *s)
{
    ThreeMMCustomMaterial *m = s->custom_state(s->context, object);
    void *file = s->file(s->context, cache), *block;
    unsigned char b[8];
    int big = s->host_big_endian;
    ThreeMMU32 bo;
    ThreeMMI32 i, id;
    ThreeMMMaterialChild child;
    block = s->find(s->context, file, type, number);
    if (!block || !s->unpack(s->context, block))
        return 0;
    if (s->size(s->context, block) != 8)
    {
        s->diagnostic(s->context, THREE_MM_MATERIAL_BAD_CUSTOM_SIZE);
        return 0;
    }
    if (!s->read(s->context, block, b, 8))
        return 0;
    bo = word(b, 2, big);
    if (bo == 256)
        big = !big;
    if (word(b, 2, big) != 1)
        s->diagnostic(s->context, THREE_MM_MATERIAL_BAD_CUSTOM_ORDER);
    m->set = three_mm_signed_word(word(b + 4, 4, big));
    m->count = 0;
    for (i = 0; s->child_at(s->context, file, type, number, i, &child); ++i)
    {
        id = three_mm_signed_word(child.id);
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
        if (s->child(s->context, file, type, number, (ThreeMMU32)i, s->material_type, &child))
        {
            m->materials[i] = s->fetch(s->context, cache, &child, THREE_MM_MATERIAL);
            if (!m->materials[i])
                return 0;
        }
        if (s->child(s->context, file, type, number, (ThreeMMU32)i, s->model_type, &child))
        {
            m->models[i] = s->fetch(s->context, cache, &child, THREE_MM_MATERIAL_MODEL);
            if (!m->models[i])
                return 0;
        }
    }
    return 1;
}
int three_mm_custom_material_read(void *object, void *cache, ThreeMMU32 type, ThreeMMU32 number,
                                  const ThreeMMMaterialServices *s)
{
    int ok = read_custom(object, cache, type, number, s);
    s->close_block(s->context);
    return ok;
}
int three_mm_custom_material_fetch(void *cache, ThreeMMU32 type, ThreeMMU32 number, void *out, void *size,
                                   const ThreeMMMaterialServices *s)
{
    void *object;
    ThreeMMI32 bytes = s->custom_bytes;
    ThreeMMCustomMaterial *m;
    memcpy(size, &bytes, 4);
    if (!out)
        return 1;
    object = s->create(s->context, THREE_MM_CUSTOM_MATERIAL);
    if (!object || !three_mm_custom_material_read(object, cache, type, number, s))
    {
        if (object)
            s->release(s->context, object);
        s->invalidate(s->context, out, sizeof(object));
        s->invalidate(s->context, size, 4);
        return 0;
    }
    m = s->custom_state(s->context, object);
    bytes += (ThreeMMI32)(m->count * (2 * sizeof(void *)));
    memcpy(out, &object, sizeof(object));
    memcpy(size, &bytes, 4);
    return 1;
}
int three_mm_custom_material_has_models(const ThreeMMCustomMaterial *m)
{
    ThreeMMI32 i;
    for (i = 0; i < m->count; ++i)
        if (m->models[i])
            return 1;
    return 0;
}
void *three_mm_custom_material_model(const ThreeMMCustomMaterial *m, ThreeMMI32 index)
{
    return m->models[index];
}
void *three_mm_custom_material_handle(const ThreeMMCustomMaterial *m, ThreeMMI32 index,
                                      const ThreeMMMaterialServices *s)
{
    return s->material_handle(s->context, m->materials[index]);
}
