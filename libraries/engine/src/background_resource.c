/* Background resources converted from engine/bkgd.cpp and preview_background.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/background_resource.h"
#include "three_mm/wide.h"
#include <string.h>
#define MAXIMUM 2147483647
static ThreeMMU32 word(const unsigned char *p, int n, int big)
{
    ThreeMMU32 value = 0;
    int i;
    for (i = 0; i < n; ++i)
        value = (value << 8) | p[big ? i : n - 1 - i];
    return value;
}
static int order(const unsigned char *p)
{
    if (p[0] == 1 && !p[1])
        return 0;
    if (!p[0] && p[1] == 1)
        return 1;
    return -1;
}
int three_mm_background_header_read(const ThreeMMReadStream *r, ThreeMMBackgroundHeader *out)
{
    unsigned char bytes[8];
    ThreeMMBackgroundHeader d;
    int big;
    if (r->length != 8 || !three_mm_read_exact(r, 0, bytes, 8) || (big = order(bytes)) < 0)
        return 0;
    d.system = (unsigned)word(bytes + 2, 2, big);
    d.palette_base = bytes[4];
    *out = d;
    return 1;
}
int three_mm_background_sound_read(const ThreeMMReadStream *r, ThreeMMBackgroundSound *out)
{
    unsigned char bytes[28];
    ThreeMMBackgroundSound d;
    int big;
    if (r->length != 28 || !three_mm_read_exact(r, 0, bytes, 28) || (big = order(bytes)) < 0)
        return 0;
    memset(&d, 0, sizeof(d));
    d.system = (unsigned)word(bytes + 2, 2, big);
    d.volume = three_mm_signed_word(word(bytes + 4, 4, big));
    d.loop = word(bytes + 8, 4, big) != 0;
    d.tag.sid = three_mm_signed_word(word(bytes + 12, 4, big));
    d.tag.cache = NULL;
    d.tag.type = word(bytes + 20, 4, big);
    d.tag.number = word(bytes + 24, 4, big);
    *out = d;
    return 1;
}
void three_mm_background_light_decode(const unsigned char *b, int big, ThreeMMBackgroundLight *out)
{
    int i;
    for (i = 0; i < 12; ++i)
        out->matrix.m[i / 3][i % 3] = three_mm_signed_word(word(b + i * 4, 4, big));
    out->intensity = three_mm_signed_word(word(b + 48, 4, big));
    out->type = three_mm_signed_word(word(b + 52, 4, big));
}
void three_mm_background_resource_init(ThreeMMBackgroundResource *b)
{
    memset(b, 0, sizeof(*b));
}
static ThreeMMI32 cameras(ThreeMMU32 type, ThreeMMU32 number, int profile, const ThreeMMBackgroundServices *s)
{
    ThreeMMI32 count = 0, i, total = 0;
    ThreeMMU32 child, child_type;
    while (s->child(s->context, type, number, (ThreeMMU32)count, THREE_MM_BACKGROUND_CAMERA, &child))
    {
        if (count == MAXIMUM || (profile == THREE_MM_BACKGROUND_PREVIEW && !s->append_camera(s->context, child)))
            return -1;
        ++count;
    }
    if (s->debug_checks && profile == THREE_MM_BACKGROUND_STUDIO)
    {
        for (i = 0; s->child_at(s->context, type, number, i, &child_type); ++i)
        {
            if (child_type == THREE_MM_BACKGROUND_CAMERA)
            {
                if (total == MAXIMUM)
                    return -1;
                ++total;
            }
            if (i == MAXIMUM)
                return -1;
        }
        if (total != count)
            s->diagnostic(s->context, THREE_MM_BACKGROUND_CAMERA_GAPS);
    }
    return count;
}
static int palette(ThreeMMBackgroundResource *b, ThreeMMU32 type, ThreeMMU32 number, unsigned base, int profile,
                   const ThreeMMBackgroundServices *s)
{
    ThreeMMU32 child;
    ThreeMMI32 count, i;
    unsigned char bytes[4], t;
    int big;
    void *list;
    if (!s->child(s->context, type, number, 0, THREE_MM_BACKGROUND_COLORS, &child))
    {
        b->palette = NULL;
        return 1;
    }
    if (!s->find(s->context, THREE_MM_BACKGROUND_COLORS, child, 0))
        return 0;
    list = s->read_list(s->context, &big);
    if (profile == THREE_MM_BACKGROUND_STUDIO)
        b->palette = list;
    if (!list)
    {
        if (profile == THREE_MM_BACKGROUND_STUDIO)
            b->palette_base = base;
        return profile == THREE_MM_BACKGROUND_STUDIO;
    }
    count = s->count(s->context, list);
    if (s->stride(s->context, list) != 4 || count < 0 ||
        (profile == THREE_MM_BACKGROUND_PREVIEW && (ThreeMMU32)count > 256U - base))
    {
        if (profile == THREE_MM_BACKGROUND_PREVIEW)
            s->release(s->context, list);
        return 0;
    }
    for (i = 0; i < count; ++i)
    {
        s->get(s->context, list, i, bytes, 4);
        if (big != s->host_big_endian)
        {
            t = bytes[0];
            bytes[0] = bytes[3];
            bytes[3] = t;
            t = bytes[1];
            bytes[1] = bytes[2];
            bytes[2] = t;
        }
        if (profile == THREE_MM_BACKGROUND_STUDIO)
        {
            if (big != s->host_big_endian)
                s->put(s->context, list, i, bytes, 4);
        }
        else
            s->palette_color(s->context, (ThreeMMI32)base + i, bytes);
    }
    if (profile == THREE_MM_BACKGROUND_PREVIEW)
        s->release(s->context, list);
    b->palette_base = base;
    return 1;
}
int three_mm_background_read(ThreeMMBackgroundResource *b, ThreeMMBackgroundCameraState *camera, ThreeMMU32 type,
                             ThreeMMU32 number, int profile, const ThreeMMBackgroundServices *s)
{
    ThreeMMBackgroundHeader header;
    ThreeMMBackgroundLight light;
    ThreeMMReadStream stream;
    ThreeMMU32 child;
    unsigned char bytes[56];
    void *list = NULL;
    ThreeMMI32 i;
    int big;
    if (profile == THREE_MM_BACKGROUND_STUDIO)
    {
        camera->count = cameras(type, number, profile, s);
        camera->current = -1;
        if (camera->count < 0)
            goto fail;
    }
    if (!s->find(s->context, type, number, 1))
        goto fail;
    stream = s->stream(s->context);
    if (!three_mm_background_header_read(&stream, &header))
        goto fail;
    if (profile == THREE_MM_BACKGROUND_STUDIO)
    {
        if (!s->name(s->context, type, number))
            goto fail;
        if (s->child(s->context, type, number, 0, THREE_MM_BACKGROUND_SOUND, &child))
        {
            if (!s->find(s->context, THREE_MM_BACKGROUND_SOUND, child, 1))
                goto fail;
            stream = s->stream(s->context);
            if (!three_mm_background_sound_read(&stream, &b->sound))
                goto fail;
        }
        else
            b->sound.tag.sid = -1;
    }
    /* Only a present palette publishes the stored base in Studio. */
    if (!palette(b, type, number, header.palette_base, profile, s))
        goto fail;
    if (profile == THREE_MM_BACKGROUND_PREVIEW)
        s->resolve_palette(s->context);
    if (!s->child(s->context, type, number, 0, THREE_MM_BACKGROUND_LIGHTS, &child) ||
        !s->find(s->context, THREE_MM_BACKGROUND_LIGHTS, child, 0) || !(list = s->read_list(s->context, &big)))
        goto fail;
    b->light_count = s->count(s->context, list);
    if (s->stride(s->context, list) != 56 || b->light_count < 0 ||
        (profile == THREE_MM_BACKGROUND_PREVIEW && b->light_count > 16))
        goto fail;
    if (profile == THREE_MM_BACKGROUND_STUDIO)
    {
        if (s->actor_bytes <= 0 || s->light_bytes <= 0 || b->light_count > MAXIMUM / s->actor_bytes ||
            b->light_count > MAXIMUM / s->light_bytes ||
            !s->allocate(s->context, b->light_count * s->actor_bytes, &b->actors) ||
            !s->allocate(s->context, b->light_count * s->light_bytes, &b->lights))
            goto fail;
    }
    for (i = 0; i < b->light_count; ++i)
    {
        s->get(s->context, list, i, bytes, 56);
        three_mm_background_light_decode(bytes, big, &light);
        if (!s->configure_light(s->context, b, i, &light))
            goto fail;
    }
    s->release(s->context, list);
    list = NULL;
    if (profile == THREE_MM_BACKGROUND_PREVIEW)
    {
        camera->count = cameras(type, number, profile, s);
        return camera->count > 0 && s->select_camera(s->context, 0);
    }
    return 1;
fail:
    if (profile == THREE_MM_BACKGROUND_STUDIO)
        s->diagnostic(s->context, THREE_MM_BACKGROUND_READ_ERROR);
    if (list)
        s->release(s->context, list);
    return 0;
}
void three_mm_background_lights_on(ThreeMMBackgroundResource *b, const ThreeMMBackgroundServices *s)
{
    ThreeMMI32 i;
    if (b->enabled)
        return;
    for (i = 0; i < b->light_count; ++i)
    {
        s->add_light(s->context, b, i);
        s->enable_light(s->context, b, i);
    }
    b->enabled = 1;
}
void three_mm_background_lights_off(ThreeMMBackgroundResource *b, const ThreeMMBackgroundServices *s)
{
    ThreeMMI32 i;
    if (!b->enabled || b->leave_on)
        return;
    for (i = 0; i < b->light_count; ++i)
    {
        s->disable_light(s->context, b, i);
        s->remove_light(s->context, b, i);
    }
    b->enabled = 0;
}
void three_mm_background_resource_dispose(ThreeMMBackgroundResource *b, const ThreeMMBackgroundServices *s)
{
    if (b->actors && b->lights)
        three_mm_background_lights_off(b, s);
    s->free_memory(s->context, &b->actors);
    s->free_memory(s->context, &b->lights);
    if (b->palette)
    {
        s->release(s->context, b->palette);
        b->palette = NULL;
    }
}
void *three_mm_background_palette(const ThreeMMBackgroundResource *b, ThreeMMI32 *base,
                                  const ThreeMMBackgroundServices *s)
{
    *base = (ThreeMMI32)b->palette_base;
    return b->palette ? s->duplicate_palette(s->context, b->palette) : s->new_palette(s->context);
}
int three_mm_background_factory(ThreeMMU32 type, ThreeMMU32 number, int materialize,
                                const ThreeMMBackgroundFactoryServices *s)
{
    void *object;
    ThreeMMI32 count, unit;
    if (s->actor_bytes < 0 || s->light_bytes < 0 || s->object_bytes < 0 || s->actor_bytes > MAXIMUM - s->light_bytes)
        return 0;
    unit = s->actor_bytes + s->light_bytes;
    if (s->object_bytes > MAXIMUM - unit)
        return 0;
    s->size(s->context, s->object_bytes + unit);
    if (!materialize)
        return 1;
    object = s->create(s->context);
    if (!object || !s->initialize(s->context, object, type, number))
        goto fail;
    count = s->lights(s->context, object);
    if (count < 0 || (unit && count > (MAXIMUM - s->object_bytes) / unit))
        goto fail;
    s->publish(s->context, object);
    s->size(s->context, s->object_bytes + count * unit);
    return 1;
fail:
    s->failure(s->context);
    if (object)
        s->release(s->context, object);
    return 0;
}
static const ThreeMMU32 children[4] = {THREE_MM_BACKGROUND_SOUND, THREE_MM_BACKGROUND_LIGHTS,
                                       THREE_MM_BACKGROUND_COLORS, THREE_MM_BACKGROUND_CAMERA};
int three_mm_background_tags(const ThreeMMBackgroundTagServices *s)
{
    int i;
    if (!s->insert(s->context, 1, 0))
        return 0;
    for (i = 0; i < 4; ++i)
        if (!s->insert(s->context, 0, children[i]))
            return 0;
    return 1;
}
int three_mm_background_cache(const ThreeMMTag *parent, const ThreeMMBackgroundTagServices *s)
{
    ThreeMMTag tags[4];
    int i;
    for (i = 0; i < 4; ++i)
        if (!s->child(s->context, children[i], &tags[i]))
            return 0;
    if (!s->cache(s->context, parent, 0))
        return 0;
    for (i = 0; i < 4; ++i)
        if (!s->cache(s->context, &tags[i], 1))
            return 0;
    return 1;
}
