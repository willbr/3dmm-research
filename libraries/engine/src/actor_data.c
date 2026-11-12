/* Actor persistence converted from engine actor_data.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/actor_data.h"
#include "three_mm/wide.h"
#include <string.h>
static void swap16(unsigned char *p)
{
    unsigned char t = p[0];
    p[0] = p[1];
    p[1] = t;
}
static void swap32(unsigned char *p)
{
    unsigned char t = p[0];
    p[0] = p[3];
    p[3] = t;
    t = p[1];
    p[1] = p[2];
    p[2] = t;
}
static void swap_words(void *data, ThreeMMU32 count)
{
    unsigned char *p = (unsigned char *)data;
    ThreeMMU32 i;
    for (i = 0; i < count; ++i)
        swap32(p + i * 4);
}
static ThreeMMU32 native_word(const void *p)
{
    ThreeMMU32 value;
    memcpy(&value, p, 4);
    return value;
}
static void put_word(void *p, ThreeMMU32 value)
{
    memcpy(p, &value, 4);
}
static ThreeMMI32 kind(const void *fixed, int order)
{
    unsigned char bytes[4];
    memcpy(bytes, fixed, 4);
    if (order == THREE_MM_ACTOR_OTHER)
        swap32(bytes);
    return three_mm_signed_word(native_word(bytes));
}
int three_mm_actor_read_header(const ThreeMMReadStream *s, void *header)
{
    unsigned char *p = (unsigned char *)header;
    unsigned short order;
    ThreeMMI32 last = 2147483647L;
    int old;
    if (s->length != 44 && s->length != 40)
        return 0;
    old = s->length == 40;
    if (!three_mm_read_exact(s, 0, p, s->length))
        return 0;
    if (old)
        memmove(p + 28, p + 24, 16);
    memcpy(&order, p, 2);
    if (order == 0x0100U)
    {
        swap16(p);
        swap16(p + 2);
        swap_words(p + 4, 10);
    }
    memcpy(&order, p, 2);
    if (order != 1)
        return 0;
    if (old)
        memcpy(p + 24, &last, 4);
    return 1;
}
int three_mm_actor_adjust_header_id(void *header, ThreeMMI32 delta)
{
    unsigned char *p = (unsigned char *)header;
    ThreeMMI32 id;
    memcpy(&id, p + 16, 4);
    if ((delta > 0 && id > 2147483647L - delta) || (delta < 0 && id < (-2147483647L - 1) - delta))
        return 0;
    id += delta;
    memcpy(p + 16, &id, 4);
    return 1;
}
int three_mm_actor_write_header(ThreeMMU32 number, const void *header, const ThreeMMActorHeaderIO *io)
{
    return io->put(io->context, number, header, 44);
}
int three_mm_actor_adjust_id(ThreeMMU32 number, ThreeMMI32 delta, const ThreeMMActorHeaderIO *io)
{
    ThreeMMReadStream stream;
    ThreeMMActorHeader header;
    if (!io->read(io->context, number, &stream) || !three_mm_actor_read_header(&stream, &header) ||
        !three_mm_actor_adjust_header_id(&header, delta))
        return 0;
    return three_mm_actor_write_header(number, &header, io);
}
void *three_mm_actor_finish_route(void *route, int order, const ThreeMMActorRouteServices *s)
{
    ThreeMMU32 count;
    if (!route)
        return NULL;
    if (s->width(route) != 16 || (order != THREE_MM_ACTOR_NATIVE && order != THREE_MM_ACTOR_OTHER))
    {
        s->release(route);
        return NULL;
    }
    if (order == THREE_MM_ACTOR_OTHER)
    {
        count = s->count(route);
        if (count)
            swap_words(s->data(route), count * 4);
    }
    return route;
}
static ThreeMMI32 payload_size(ThreeMMI32 type, int wire)
{
    switch (type)
    {
    case THREE_MM_ACTOR_ADD:
        return 20;
    case THREE_MM_ACTOR_ACTION:
        return 8;
    case THREE_MM_ACTOR_COSTUME:
        return wire ? 28 : (ThreeMMI32)sizeof(ThreeMMActorCostume);
    case THREE_MM_ACTOR_SOUND:
        return wire ? 44 : (ThreeMMI32)sizeof(ThreeMMActorSound);
    case THREE_MM_ACTOR_ROTATE_FORWARD:
    case THREE_MM_ACTOR_ROTATE_HERE:
        return 48;
    case THREE_MM_ACTOR_STRETCH:
    case THREE_MM_ACTOR_MOVE:
    case THREE_MM_ACTOR_TWEAK:
        return 12;
    case THREE_MM_ACTOR_SIZE:
    case THREE_MM_ACTOR_FREEZE:
    case THREE_MM_ACTOR_STEP:
        return 4;
    case THREE_MM_ACTOR_REMOVE:
        return 0;
    default:
        return -1;
    }
}
int three_mm_actor_valid_events(void *g, int wire, int order, const ThreeMMActorEventServices *s)
{
    ThreeMMU32 i;
    ThreeMMI32 bytes, type;
    if ((order != THREE_MM_ACTOR_NATIVE && order != THREE_MM_ACTOR_OTHER) || s->fixed_width(g) != 20)
        return 0;
    for (i = 0; i < s->count(g); ++i)
    {
        type = kind(s->fixed(g, i), order);
        bytes = payload_size(type, wire);
        if (bytes < 0 || s->size(g, i) != bytes)
            return 0;
    }
    return 1;
}
static void swap_events(void *g, const ThreeMMActorEventServices *s)
{
    ThreeMMU32 i, j;
    ThreeMMI32 type, bytes;
    unsigned char *data;
    void *fixed;
    for (i = 0; i < s->count(g); ++i)
    {
        fixed = s->fixed(g, i);
        swap_words(fixed, 5);
        type = kind(fixed, THREE_MM_ACTOR_NATIVE);
        if (type == THREE_MM_ACTOR_REMOVE)
            continue;
        data = (unsigned char *)s->data(g, i);
        if (type == THREE_MM_ACTOR_ADD)
        {
            swap_words(data, 3);
            for (j = 0; j < 3; ++j)
                swap16(data + 12 + j * 2);
        }
        else
        {
            bytes = payload_size(type, 1);
            swap_words(data, (ThreeMMU32)bytes / 4);
        }
    }
}
static void read_tag(ThreeMMTag *t, const unsigned char *p)
{
    t->sid = three_mm_signed_word(native_word(p));
    t->cache = NULL;
    t->type = native_word(p + 8);
    t->number = native_word(p + 12);
}
static void write_tag(unsigned char *p, const ThreeMMTag *t)
{
    put_word(p, (ThreeMMU32)t->sid);
    put_word(p + 4, 0);
    put_word(p + 8, t->type);
    put_word(p + 12, t->number);
}
void *three_mm_actor_read_events(void *wire, int order, const ThreeMMActorEventServices *s)
{
    void *out = NULL, *fixed;
    const unsigned char *data;
    ThreeMMU32 i, count;
    ThreeMMI32 type;
    ThreeMMActorCostume costume;
    ThreeMMActorSound sound;
    if (!wire)
        return NULL;
    if (!three_mm_actor_valid_events(wire, 1, order, s))
        goto fail;
    if (order == THREE_MM_ACTOR_OTHER)
        swap_events(wire, s);
    out = s->new_group(s->context, 20);
    if (!out)
        goto fail;
    count = s->count(wire);
    for (i = 0; i < count; ++i)
    {
        fixed = s->fixed(wire, i);
        type = kind(fixed, THREE_MM_ACTOR_NATIVE);
        data = (const unsigned char *)s->data(wire, i);
        if (type == THREE_MM_ACTOR_COSTUME)
        {
            memset(&costume, 0, sizeof(costume));
            memcpy(&costume.body_set, data, 4);
            memcpy(&costume.costume, data + 4, 4);
            memcpy(&costume.custom, data + 8, 4);
            read_tag(&costume.tag, data + 12);
            if (!s->insert(out, i, sizeof(costume), &costume, fixed))
                goto fail;
        }
        else if (type == THREE_MM_ACTOR_SOUND)
        {
            memset(&sound, 0, sizeof(sound));
            memcpy(&sound.loop, data, 4);
            memcpy(&sound.queue, data + 4, 4);
            memcpy(&sound.volume, data + 8, 4);
            memcpy(&sound.cel, data + 12, 4);
            memcpy(&sound.style, data + 16, 4);
            memcpy(&sound.no_sound, data + 20, 4);
            memcpy(&sound.child, data + 24, 4);
            read_tag(&sound.tag, data + 28);
            if (!s->insert(out, i, sizeof(sound), &sound, fixed))
                goto fail;
        }
        else if (!s->insert(out, i, (ThreeMMU32)s->size(wire, i), data, fixed))
            goto fail;
    }
    s->release(wire);
    return out;
fail:
    s->release(wire);
    if (out)
        s->release(out);
    return NULL;
}
void *three_mm_actor_write_events(void *runtime, const ThreeMMActorEventServices *s)
{
    void *out, *fixed;
    const void *data;
    ThreeMMU32 i, count;
    ThreeMMI32 type;
    unsigned char bytes[44];
    ThreeMMActorCostume costume;
    ThreeMMActorSound sound;
    if (!three_mm_actor_valid_events(runtime, 0, THREE_MM_ACTOR_NATIVE, s))
        return NULL;
    out = s->new_group(s->context, 20);
    if (!out)
        return NULL;
    count = s->count(runtime);
    for (i = 0; i < count; ++i)
    {
        fixed = s->fixed(runtime, i);
        type = kind(fixed, THREE_MM_ACTOR_NATIVE);
        data = s->data(runtime, i);
        if (type == THREE_MM_ACTOR_COSTUME)
        {
            memcpy(&costume, data, sizeof(costume));
            memcpy(bytes, &costume.body_set, 4);
            memcpy(bytes + 4, &costume.costume, 4);
            memcpy(bytes + 8, &costume.custom, 4);
            write_tag(bytes + 12, &costume.tag);
            if (!s->insert(out, i, 28, bytes, fixed))
                goto fail;
        }
        else if (type == THREE_MM_ACTOR_SOUND)
        {
            memcpy(&sound, data, sizeof(sound));
            memcpy(bytes, &sound.loop, 4);
            memcpy(bytes + 4, &sound.queue, 4);
            memcpy(bytes + 8, &sound.volume, 4);
            memcpy(bytes + 12, &sound.cel, 4);
            memcpy(bytes + 16, &sound.style, 4);
            memcpy(bytes + 20, &sound.no_sound, 4);
            memcpy(bytes + 24, &sound.child, 4);
            write_tag(bytes + 28, &sound.tag);
            if (!s->insert(out, i, 44, bytes, fixed))
                goto fail;
        }
        else if (!s->insert(out, i, (ThreeMMU32)s->size(runtime, i), data, fixed))
            goto fail;
    }
    return out;
fail:
    s->release(out);
    return NULL;
}
