/* Scene sound packets converted from engine/scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/scene_sound.h"
#include <string.h>
typedef char scene_sound_header_must_be_16_bytes[sizeof(ThreeMMSceneSound) == 16 ? 1 : -1];
ThreeMMI32 three_mm_scene_sound_size(ThreeMMI32 count, size_t stride)
{
    ThreeMMU32 bytes;
    if (count < 0 || !stride || stride > 2147483631UL || (ThreeMMU32)count > (2147483631UL / stride))
        return 0;
    bytes = 16U + (ThreeMMU32)count * (ThreeMMU32)stride;
    return (size_t)bytes == bytes ? (ThreeMMI32)bytes : 0;
}
static void *record(ThreeMMSceneSound *p, ThreeMMI32 i, const ThreeMMSceneSoundPacketServices *s)
{
    return (unsigned char *)p + 16 + (size_t)i * s->tag_bytes;
}
ThreeMMSceneSound *three_mm_scene_sound_allocate(ThreeMMI32 count, const ThreeMMSceneSoundPacketServices *s)
{
    ThreeMMI32 bytes = three_mm_scene_sound_size(count, s->tag_bytes);
    return bytes ? (ThreeMMSceneSound *)s->allocate(s->context, bytes) : NULL;
}
ThreeMMSceneSound *three_mm_scene_sound_make(ThreeMMI32 volume, ThreeMMI32 style, int loop, ThreeMMI32 count,
                                             const void *records, const ThreeMMSceneSoundPacketServices *s)
{
    ThreeMMSceneSound *p = three_mm_scene_sound_allocate(count, s);
    ThreeMMI32 i;
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
ThreeMMSceneSound *three_mm_scene_sound_duplicate(const ThreeMMSceneSound *source,
                                                  const ThreeMMSceneSoundPacketServices *s)
{
    ThreeMMI32 bytes = three_mm_scene_sound_size(source->ctagc, s->tag_bytes), i;
    ThreeMMSceneSound *p;
    if (!bytes)
        return NULL;
    p = (ThreeMMSceneSound *)s->allocate(s->context, bytes);
    if (!p)
        return NULL;
    memcpy(p, source, (size_t)bytes);
    for (i = 0; i < p->ctagc; ++i)
        s->retain(s->context, record(p, i, s));
    return p;
}
ThreeMMSceneSound *three_mm_scene_sound_group(void *group, ThreeMMI32 index, int retain_tags,
                                              const ThreeMMSceneSoundPacketServices *s)
{
    ThreeMMSceneSound *p, *source;
    ThreeMMI32 size = s->group_size(s->context, group, index), bytes, i;
    void *tag;
    if (size < 16)
        return NULL;
    source = (ThreeMMSceneSound *)s->group_data(s->context, group, index);
    if (!source || source->ctagc <= 0)
        return NULL;
    bytes = three_mm_scene_sound_size(source->ctagc, s->tag_bytes);
    if (!bytes || bytes > size)
        return NULL;
    p = (ThreeMMSceneSound *)s->allocate(s->context, bytes);
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
ThreeMMSceneSound *three_mm_scene_sound_append(const ThreeMMSceneSound *source, const void *tag,
                                               const ThreeMMSceneSoundPacketServices *s)
{
    ThreeMMSceneSound *p;
    ThreeMMI32 bytes = three_mm_scene_sound_size(source->ctagc, s->tag_bytes);
    if (!bytes || source->ctagc == 2147483647)
        return NULL;
    p = three_mm_scene_sound_allocate(source->ctagc + 1, s);
    if (!p)
        return NULL;
    memcpy(p, source, (size_t)bytes);
    s->copy_record(s->context, record(p, p->ctagc, s), tag);
    ++p->ctagc;
    s->retain(s->context, record(p, p->ctagc - 1, s));
    return p;
}
static ThreeMMU32 reverse(ThreeMMU32 n)
{
    return ((n & 255U) << 24) | ((n & 65280U) << 8) | ((n >> 8) & 65280U) | (n >> 24);
}
static ThreeMMU32 word(const unsigned char *p)
{
    ThreeMMU32 n;
    memcpy(&n, p, 4);
    return n;
}
static void put(unsigned char *p, ThreeMMU32 n)
{
    memcpy(p, &n, 4);
}
ThreeMMSceneSound *three_mm_scene_sound_decode(void *bytes, ThreeMMI32 size, int swap,
                                               const ThreeMMSceneSoundPacketServices *s)
{
    unsigned char *b = (unsigned char *)bytes, *q;
    ThreeMMU32 count, n;
    ThreeMMI32 width, i;
    ThreeMMTag tag;
    ThreeMMSceneSound *p;
    if (!b || size < 16)
        return NULL;
    count = word(b + 12);
    if (swap)
        count = reverse(count);
    if (!count || count > 2147483647UL)
        return NULL;
    width = three_mm_scene_sound_size((ThreeMMI32)count, 20);
    if (!width || width > size || !three_mm_scene_sound_size((ThreeMMI32)count, s->tag_bytes))
        return NULL;
    if (swap)
        for (i = 0; i < width; i += 4)
            put(b + i, reverse(word(b + i)));
    p = three_mm_scene_sound_allocate((ThreeMMI32)count, s);
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
void *three_mm_scene_sound_encode(const ThreeMMSceneSound *p, ThreeMMI32 *size,
                                  const ThreeMMSceneSoundPacketServices *s)
{
    ThreeMMI32 width = three_mm_scene_sound_size(p->ctagc, 20), i;
    unsigned char *b, *q;
    ThreeMMU32 child;
    ThreeMMTag tag;
    *size = 0;
    if (!width || !three_mm_scene_sound_size(p->ctagc, s->tag_bytes))
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
        put(q + 4, (ThreeMMU32)tag.sid);
        put(q + 8, 0);
        put(q + 12, tag.type);
        put(q + 16, tag.number);
    }
    *size = width;
    return b;
}
void three_mm_scene_sound_play(ThreeMMSceneSound *p, ThreeMMU32 start, const ThreeMMSceneSoundPacketServices *s)
{
    ThreeMMI32 i, priority;
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
