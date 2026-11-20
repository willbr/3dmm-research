/* Scene sound packets converted from engine/scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/scene_sound.h"
#include "mm/scene_track.h"
#include "mm/wide.h"
#include <string.h>
typedef char scene_sound_header_must_be_16_bytes[sizeof(MMSceneSound) == 16 ? 1 : -1];
static MMI32 packet_size(MMI32 count, MMU32 stride)
{
    MMU32 bytes;
    if (count < 0 || (MMU32)count > (2147483631UL / stride))
        return 0;
    bytes = MM_SCENE_SOUND_HEADER_BYTES + (MMU32)count * stride;
    return (size_t)bytes == bytes ? (MMI32)bytes : 0;
}
MMI32 mm_scene_sound_size(MMI32 count)
{
    return packet_size(count, sizeof(MMSceneSoundTag));
}
MMI32 mm_scene_sound_wire_size(MMI32 count)
{
    return packet_size(count, MM_SCENE_SOUND_WIRE_TAG_BYTES);
}
MMI32 mm_scene_sound_span(const void *data, MMI32 bytes)
{
    MMSceneSound header;
    MMI32 span;
    if (!data || bytes < MM_SCENE_SOUND_HEADER_BYTES)
        return 0;
    memcpy(&header, data, sizeof(header));
    span = mm_scene_sound_size(header.ctagc);
    return span <= bytes ? span : 0;
}
static size_t record_offset(MMI32 i)
{
    return MM_SCENE_SOUND_HEADER_BYTES + (size_t)i * sizeof(MMSceneSoundTag);
}
void mm_scene_sound_get_tag(const MMSceneSound *p, MMI32 i, MMSceneSoundTag *tag)
{
    memcpy(tag, (const unsigned char *)p + record_offset(i), sizeof(*tag));
}
void mm_scene_sound_put_tag(MMSceneSound *p, MMI32 i, const MMSceneSoundTag *tag)
{
    memcpy((unsigned char *)p + record_offset(i), tag, sizeof(*tag));
}
MMSceneSound *mm_scene_sound_allocate(MMI32 count, const MMSceneSoundPacketServices *s)
{
    MMI32 bytes = mm_scene_sound_size(count);
    return bytes ? (MMSceneSound *)s->allocate(s->context, bytes) : NULL;
}
MMSceneSound *mm_scene_sound_make(MMI32 volume, MMI32 style, int loop, MMI32 count,
                                  const MMSceneSoundTag *records, const MMSceneSoundPacketServices *s)
{
    MMSceneSound *p = mm_scene_sound_allocate(count, s);
    MMI32 i;
    if (!p)
        return NULL;
    p->vlm = volume;
    p->sty = style;
    p->fLoop = loop != 0;
    p->ctagc = count;
    for (i = 0; i < count; ++i)
    {
        mm_scene_sound_put_tag(p, i, records + i);
        s->retain(s->context, records + i);
    }
    return p;
}
MMSceneSound *mm_scene_sound_duplicate(const MMSceneSound *source,
                                                  const MMSceneSoundPacketServices *s)
{
    MMI32 bytes = mm_scene_sound_size(source->ctagc), i;
    MMSceneSound *p;
    MMSceneSoundTag tag;
    if (!bytes)
        return NULL;
    p = (MMSceneSound *)s->allocate(s->context, bytes);
    if (!p)
        return NULL;
    memcpy(p, source, (size_t)bytes);
    for (i = 0; i < p->ctagc; ++i)
    {
        mm_scene_sound_get_tag(p, i, &tag);
        s->retain(s->context, &tag);
    }
    return p;
}
static void copy_group_sound(MMSceneSound *p, const void *source, MMI32 bytes, int retain_tags,
                             const MMSceneSoundPacketServices *s)
{
    MMI32 i;
    MMSceneSoundTag tag;
    memcpy(p, source, (size_t)bytes);
    for (i = 0; i < p->ctagc; ++i)
    {
        mm_scene_sound_get_tag(p, i, &tag);
        if (!retain_tags)
        {
            tag.tag.cache = NULL;
            mm_scene_sound_put_tag(p, i, &tag);
        }
        else if (!tag.tag.sid)
            s->retain(s->context, &tag);
    }
}
MMSceneSound *mm_scene_sound_group(void *group, MMI32 index, int retain_tags,
                                              const MMSceneSoundPacketServices *s)
{
    MMSceneSound *p;
    const void *source;
    MMI32 size = s->group_size(s->context, group, index), bytes;
    if (size < MM_SCENE_SOUND_HEADER_BYTES)
        return NULL;
    source = s->group_data(s->context, group, index);
    bytes = mm_scene_sound_span(source, size);
    if (bytes <= MM_SCENE_SOUND_HEADER_BYTES)
        return NULL;
    p = (MMSceneSound *)s->allocate(s->context, bytes);
    if (p)
        copy_group_sound(p, s->group_data(s->context, group, index), bytes, retain_tags, s);
    return p;
}
MMSceneSound *mm_scene_sound_track(const MMSceneTrack *track, MMI32 index,
                                  const MMSceneSoundPacketServices *s)
{
    MMSceneSound *p;
    MMI32 size = 0, bytes;
    const void *source = mm_scene_track_payload(track, index, &size);
    bytes = mm_scene_sound_span(source, size);
    if (bytes <= MM_SCENE_SOUND_HEADER_BYTES)
        return NULL;
    p = (MMSceneSound *)s->allocate(s->context, bytes);
    if (p)
        copy_group_sound(p, mm_scene_track_payload(track, index, NULL), bytes, 1, s);
    return p;
}
MMSceneSound *mm_scene_sound_append(const MMSceneSound *source, const MMSceneSoundTag *tag,
                                    const MMSceneSoundPacketServices *s)
{
    MMSceneSound *p;
    MMI32 bytes = mm_scene_sound_size(source->ctagc);
    if (!bytes)
        return NULL;
    p = mm_scene_sound_allocate(source->ctagc + 1, s);
    if (!p)
        return NULL;
    memcpy(p, source, (size_t)bytes);
    mm_scene_sound_put_tag(p, p->ctagc, tag);
    ++p->ctagc;
    s->retain(s->context, tag);
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
    width = mm_scene_sound_wire_size(count);
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
    MMI32 width, i;
    MMSceneSoundTag tag = {0};
    MMSceneSound *p;
    if (!mm_scene_sound_wire(bytes, size, (*(const unsigned char *)&one == 0) != (swap != 0), &wire) ||
        !mm_scene_sound_size(wire.header.ctagc))
        return NULL;
    if (swap)
    {
        width = mm_scene_sound_wire_size(wire.header.ctagc);
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
        mm_scene_sound_wire_tag(&wire, i, &tag.child, &tag.tag);
        mm_scene_sound_put_tag(p, i, &tag);
    }
    return p;
}
void *mm_scene_sound_encode(const MMSceneSound *p, MMI32 *size,
                                  const MMSceneSoundPacketServices *s)
{
    MMI32 width = mm_scene_sound_wire_size(p->ctagc), i;
    unsigned char *b, *q;
    MMSceneSoundTag tag;
    *size = 0;
    if (!width || !mm_scene_sound_size(p->ctagc))
        return NULL;
    b = (unsigned char *)s->allocate(s->context, width);
    if (!b)
        return NULL;
    memcpy(b, p, 16);
    for (i = 0; i < p->ctagc; ++i)
    {
        mm_scene_sound_get_tag(p, i, &tag);
        q = b + MM_SCENE_SOUND_HEADER_BYTES + (size_t)i * MM_SCENE_SOUND_WIRE_TAG_BYTES;
        put(q, tag.child);
        put(q + 4, (MMU32)tag.tag.sid);
        put(q + 8, 0);
        put(q + 12, tag.tag.type);
        put(q + 16, tag.tag.number);
    }
    *size = width;
    return b;
}
void mm_scene_sound_play(MMSceneSound *p, MMU32 start, const MMSceneSoundPacketServices *s)
{
    MMI32 i, priority;
    int tool = p->fLoop != 0;
    void *sound;
    MMSceneSoundTag tag;
    for (i = 0; i < p->ctagc; ++i)
    {
        mm_scene_sound_get_tag(p, i, &tag);
        if (!tag.tag.sid)
        {
            int ready = s->resolve(s->context, &tag);
            mm_scene_sound_put_tag(p, i, &tag);
            if (!ready)
                continue;
        }
        sound = s->fetch(s->context, &tag);
        if (!sound)
            return;
        priority = s->priority(s->context, sound, tool);
        s->enqueue(s->context, sound, p->fLoop != 0, i != 0, p->vlm, priority, i == 0 ? start : 0);
        s->release(s->context, sound);
    }
}
