/* Scene sound packets converted from engine/scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/scene_sound.h"
#include "mm/wide.h"
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
static MMU32 wire_word(const unsigned char *p, int swap)
{
    MMU32 n = word(p);
    return swap ? reverse(n) : n;
}
int mm_scene_sound_wire(const void *bytes, MMI32 size, int big_endian, MMSceneSoundWire *out)
{
    const unsigned short one = 1;
    const unsigned char *b = (const unsigned char *)bytes;
    const int swap = (*(const unsigned char *)&one == 0) != big_endian;
    MMI32 count, width;
    if (!b || size < MM_SCENE_SOUND_HEADER_BYTES)
        return 0;
    count = mm_signed_word(wire_word(b + 12, swap));
    if (count <= 0)
        return 0;
    width = mm_scene_sound_size(count, MM_SCENE_SOUND_WIRE_TAG_BYTES);
    if (!width || width > size)
        return 0;
    out->header.vlm = mm_signed_word(wire_word(b, swap));
    out->header.sty = mm_signed_word(wire_word(b + 4, swap));
    out->header.fLoop = mm_signed_word(wire_word(b + 8, swap));
    out->header.ctagc = count;
    out->records = b + MM_SCENE_SOUND_HEADER_BYTES;
    out->swap = swap;
    return 1;
}
void mm_scene_sound_wire_tag(const MMSceneSoundWire *wire, MMI32 i, MMU32 *child, MMTag *tag)
{
    const unsigned char *row = wire->records + (size_t)i * MM_SCENE_SOUND_WIRE_TAG_BYTES;
    *child = wire_word(row, wire->swap);
    tag->sid = mm_signed_word(wire_word(row + 4, wire->swap));
    tag->cache = NULL;
    tag->type = wire_word(row + 12, wire->swap);
    tag->number = wire_word(row + 16, wire->swap);
}
MMSceneSound *mm_scene_sound_decode(void *bytes, MMI32 size, int swap,
                                               const MMSceneSoundPacketServices *s)
{
    const unsigned short one = 1;
    unsigned char *b = (unsigned char *)bytes;
    MMSceneSoundWire wire;
    MMU32 child;
    MMI32 width, i;
    MMTag tag;
    MMSceneSound *p;
    if (!mm_scene_sound_wire(bytes, size, (*(const unsigned char *)&one == 0) != (swap != 0), &wire) ||
        !mm_scene_sound_size(wire.header.ctagc, s->tag_bytes))
        return NULL;
    if (swap)
    {
        width = mm_scene_sound_size(wire.header.ctagc, MM_SCENE_SOUND_WIRE_TAG_BYTES);
        for (i = 0; i < width; i += 4)
            put(b + i, reverse(word(b + i)));
        wire.swap = 0;
    }
    p = mm_scene_sound_allocate(wire.header.ctagc, s);
    if (!p)
        return NULL;
    *p = wire.header;
    for (i = 0; i < p->ctagc; ++i)
    {
        mm_scene_sound_wire_tag(&wire, i, &child, &tag);
        s->write_tag(s->context, record(p, i, s), child, &tag);
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
