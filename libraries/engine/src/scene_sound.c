/* Scene sound packets converted from engine/scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/scene_sound.h"
#include <string.h>
typedef char scene_sound_header_must_be_16_bytes[sizeof(MMSceneSound) == 16 ? 1 : -1];
MMI32 mm_scene_sound_size(MMI32 count, size_t stride)
{
    MMU32 bytes;
    if (count < 0 || !stride || stride > 2147483631UL || (MMU32)count > (2147483631UL / stride))
        return 0;
    bytes = 16U + (MMU32)count * (MMU32)stride;
    return (size_t)bytes == bytes ? (MMI32)bytes : 0;
}
static void *record(MMSceneSound *p, MMI32 i, const MMSceneSoundPacketServices *s)
{
    return (unsigned char *)p + 16 + (size_t)i * s->tag_bytes;
}
MMSceneSound *mm_scene_sound_allocate(MMI32 count, const MMSceneSoundPacketServices *s)
{
    MMI32 bytes = mm_scene_sound_size(count, s->tag_bytes);
    return bytes ? (MMSceneSound *)s->allocate(s->context, bytes) : NULL;
}
MMSceneSound *mm_scene_sound_make(MMI32 volume, MMI32 style, int loop, MMI32 count,
                                             const void *records, const MMSceneSoundPacketServices *s)
{
    MMSceneSound *p = mm_scene_sound_allocate(count, s);
    MMI32 i;
    void *out;
    if (!p)
        return NULL;
    p->vlm = volume;
    p->sty = style;
    p->fLoop = loop != 0;
    p->ctagc = count;
    for (i = 0; i < count; ++i)
    {
        out = record(p, i, s);
        s->copy_record(s->context, out, (const unsigned char *)records + (size_t)i * s->tag_bytes);
        s->retain(s->context, out);
    }
    return p;
}
MMSceneSound *mm_scene_sound_duplicate(const MMSceneSound *source,
                                                  const MMSceneSoundPacketServices *s)
{
    MMI32 bytes = mm_scene_sound_size(source->ctagc, s->tag_bytes), i;
    MMSceneSound *p;
    if (!bytes)
        return NULL;
    p = (MMSceneSound *)s->allocate(s->context, bytes);
    if (!p)
        return NULL;
    memcpy(p, source, (size_t)bytes);
    for (i = 0; i < p->ctagc; ++i)
        s->retain(s->context, record(p, i, s));
    return p;
}
MMSceneSound *mm_scene_sound_group(void *group, MMI32 index, int retain_tags,
                                              const MMSceneSoundPacketServices *s)
{
    MMSceneSound *p, *source;
    MMI32 size = s->group_size(s->context, group, index), bytes, i;
    void *tag;
    if (size < 16)
        return NULL;
    source = (MMSceneSound *)s->group_data(s->context, group, index);
    if (!source || source->ctagc <= 0)
        return NULL;
    bytes = mm_scene_sound_size(source->ctagc, s->tag_bytes);
    if (!bytes || bytes > size)
        return NULL;
    p = (MMSceneSound *)s->allocate(s->context, bytes);
    if (!p)
        return NULL;
    memcpy(p, s->group_data(s->context, group, index), (size_t)bytes);
    for (i = 0; i < p->ctagc; ++i)
    {
        tag = record(p, i, s);
        if (!retain_tags)
            s->clear_cache(s->context, tag);
        else if (!s->sid(s->context, tag))
            s->retain(s->context, tag);
    }
    return p;
}
static MMSceneSound *append_storage(const MMSceneSound *source, const MMSceneSoundPacketServices *s)
{
    MMSceneSound *p;
    MMI32 bytes = mm_scene_sound_size(source->ctagc, s->tag_bytes);
    if (!bytes || source->ctagc == 2147483647)
        return NULL;
    p = mm_scene_sound_allocate(source->ctagc + 1, s);
    if (!p)
        return NULL;
    memcpy(p, source, (size_t)bytes);
    return p;
}
static void retain_appended(MMSceneSound *p, const MMSceneSoundPacketServices *s)
{
    ++p->ctagc;
    s->retain(s->context, record(p, p->ctagc - 1, s));
}
MMSceneSound *mm_scene_sound_append(const MMSceneSound *source, const void *tag,
                                               const MMSceneSoundPacketServices *s)
{
    MMSceneSound *p = append_storage(source, s);
    if (!p)
        return NULL;
    s->copy_record(s->context, record(p, p->ctagc, s), tag);
    retain_appended(p, s);
    return p;
}
MMSceneSound *mm_scene_sound_append_tag(const MMSceneSound *source, MMU32 child,
                                                   const MMTag *tag, const MMSceneSoundPacketServices *s)
{
    MMSceneSound *p = append_storage(source, s);
    if (!p)
        return NULL;
    s->write_tag(s->context, record(p, p->ctagc, s), child, tag);
    retain_appended(p, s);
    return p;
}
static MMU32 reverse(MMU32 n)
{
    return ((n & 255U) << 24) | ((n & 65280U) << 8) | ((n >> 8) & 65280U) | (n >> 24);
}
static MMU32 word(const unsigned char *p)
{
    MMU32 n;
    memcpy(&n, p, 4);
    return n;
}
static void put(unsigned char *p, MMU32 n)
{
    memcpy(p, &n, 4);
}
MMSceneSound *mm_scene_sound_decode(void *bytes, MMI32 size, int swap,
                                               const MMSceneSoundPacketServices *s)
{
    unsigned char *b = (unsigned char *)bytes, *q;
    MMU32 count, n;
    MMI32 width, i;
    MMTag tag;
    MMSceneSound *p;
    if (!b || size < 16)
        return NULL;
    count = word(b + 12);
    if (swap)
        count = reverse(count);
    if (!count || count > 2147483647UL)
        return NULL;
    width = mm_scene_sound_size((MMI32)count, 20);
    if (!width || width > size || !mm_scene_sound_size((MMI32)count, s->tag_bytes))
        return NULL;
    if (swap)
        for (i = 0; i < width; i += 4)
            put(b + i, reverse(word(b + i)));
    p = mm_scene_sound_allocate((MMI32)count, s);
    if (!p)
        return NULL;
    memcpy(p, b, 16);
    for (i = 0; i < p->ctagc; ++i)
    {
        q = b + 16 + (size_t)i * 20;
        n = word(q + 4);
        memcpy(&tag.sid, &n, 4);
        tag.cache = NULL;
        tag.type = word(q + 12);
        tag.number = word(q + 16);
        s->write_tag(s->context, record(p, i, s), word(q), &tag);
    }
    return p;
}
void *mm_scene_sound_encode(const MMSceneSound *p, MMI32 *size,
                                  const MMSceneSoundPacketServices *s)
{
    MMI32 width = mm_scene_sound_size(p->ctagc, 20), i;
    unsigned char *b, *q;
    MMU32 child;
    MMTag tag;
    *size = 0;
    if (!width || !mm_scene_sound_size(p->ctagc, s->tag_bytes))
        return NULL;
    b = (unsigned char *)s->allocate(s->context, width);
    if (!b)
        return NULL;
    memcpy(b, p, 16);
    for (i = 0; i < p->ctagc; ++i)
    {
        s->read_tag(s->context, (const unsigned char *)p + 16 + (size_t)i * s->tag_bytes, &child, &tag);
        q = b + 16 + (size_t)i * 20;
        put(q, child);
        put(q + 4, (MMU32)tag.sid);
        put(q + 8, 0);
        put(q + 12, tag.type);
        put(q + 16, tag.number);
    }
    *size = width;
    return b;
}
void mm_scene_sound_play(MMSceneSound *p, MMU32 start, const MMSceneSoundPacketServices *s)
{
    MMI32 i, priority;
    int tool = p->fLoop != 0;
    void *tag, *sound;
    for (i = 0; i < p->ctagc; ++i)
    {
        tag = record(p, i, s);
        if (!s->sid(s->context, tag) && !s->resolve(s->context, tag))
            continue;
        sound = s->fetch(s->context, tag);
        if (!sound)
            return;
        priority = s->priority(s->context, sound, tool);
        s->enqueue(s->context, sound, p->fLoop != 0, i != 0, p->vlm, priority, i == 0 ? start : 0);
        s->release(s->context, sound);
    }
}
