/* Background resources converted from engine/bkgd.cpp and preview_background.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/background_resource.h"
#include "mm/chunky_file.h"
#include "mm/collections.h"
#include "mm/wide.h"
#include <string.h>
#define MAXIMUM 2147483647
static MMU32 word(const unsigned char *p, int n, int big)
{
    MMU32 value = 0;
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
int mm_background_header_read(const MMReadStream *r, MMBackgroundHeader *out)
{
    unsigned char bytes[8];
    MMBackgroundHeader d;
    int big;
    if (r->length != 8 || !mm_read_exact(r, 0, bytes, 8) || (big = order(bytes)) < 0)
        return 0;
    d.system = (unsigned)word(bytes + 2, 2, big);
    d.palette_base = bytes[4];
    *out = d;
    return 1;
}
int mm_background_sound_read(const MMReadStream *r, MMBackgroundSound *out)
{
    unsigned char bytes[28];
    MMBackgroundSound d;
    int big;
    if (r->length != 28 || !mm_read_exact(r, 0, bytes, 28) || (big = order(bytes)) < 0)
        return 0;
    memset(&d, 0, sizeof(d));
    d.system = (unsigned)word(bytes + 2, 2, big);
    d.volume = mm_signed_word(word(bytes + 4, 4, big));
    d.loop = word(bytes + 8, 4, big) != 0;
    d.tag.sid = mm_signed_word(word(bytes + 12, 4, big));
    d.tag.cache = NULL;
    d.tag.type = word(bytes + 20, 4, big);
    d.tag.number = word(bytes + 24, 4, big);
    *out = d;
    return 1;
}
void mm_background_light_decode(const unsigned char *b, int big, MMBackgroundLight *out)
{
    int i;
    for (i = 0; i < 12; ++i)
        out->matrix.m[i / 3][i % 3] = mm_signed_word(word(b + i * 4, 4, big));
    out->intensity = mm_signed_word(word(b + 48, 4, big));
    out->type = mm_signed_word(word(b + 52, 4, big));
}
void mm_background_resource_init(MMBackgroundResource *b)
{
    memset(b, 0, sizeof(*b));
    mm_storage_init(&b->palette, NULL);
}
static MMI32 cameras(MMU32 type, MMU32 number, int profile, const MMBackgroundServices *s)
{
    MMI32 count = 0, i, total = 0;
    MMU32 child, child_type;
    while (s->child(s->context, type, number, (MMU32)count, MM_BACKGROUND_CAMERA, &child))
    {
        if (count == MAXIMUM || (profile == MM_BACKGROUND_PREVIEW && !s->append_camera(s->context, child)))
            return -1;
        ++count;
    }
    if (s->debug_checks && profile == MM_BACKGROUND_STUDIO)
    {
        for (i = 0; s->child_at(s->context, type, number, i, &child_type); ++i)
        {
            if (child_type == MM_BACKGROUND_CAMERA)
            {
                if (total == MAXIMUM)
                    return -1;
                ++total;
            }
            if (i == MAXIMUM)
                return -1;
        }
        if (total != count)
            s->diagnostic(s->context, MM_BACKGROUND_CAMERA_GAPS);
    }
    return count;
}
static int read_header(void *context, const MMReadStream *stream)
{
    return mm_background_header_read(stream, (MMBackgroundHeader *)context);
}
static int read_sound(void *context, const MMReadStream *stream)
{
    return mm_background_sound_read(stream, (MMBackgroundSound *)context);
}
static int read_record(const MMBackgroundServices *s, MMU32 type, MMU32 number, MMReadReceiver read, void *record)
{
    int decoded;
    return s->payload(s->context, type, number, read, record, &decoded) == MM_CHUNK_OK && decoded;
}

typedef struct BackgroundArray
{
    MMCollectionStorage *storage;
    MMCollectionHeader header;
} BackgroundArray;

static int read_array(void *context, const MMReadStream *stream)
{
    BackgroundArray *array = (BackgroundArray *)context;
    MMCollectionHeader *header = &array->header;
    return mm_collection_header_read(stream, MM_COLLECTION_ARRAY, 0, header) == MM_STORAGE_OK &&
           mm_storage_read(array->storage, stream, header->header_bytes, header->first_bytes, 0) == MM_STORAGE_OK;
}

static int palette(MMBackgroundResource *b, MMU32 type, MMU32 number, unsigned base, int profile,
                   const MMBackgroundServices *s)
{
    BackgroundArray array;
    unsigned char *bytes, t;
    MMU32 child, i;
    int decoded, status;
    if (!s->child(s->context, type, number, 0, MM_BACKGROUND_COLORS, &child))
        return 1;
    array.storage = &b->palette;
    status = s->payload(s->context, MM_BACKGROUND_COLORS, child, read_array, &array, &decoded);
    if (status == MM_CHUNK_NOT_FOUND)
        return 0;
    if (status != MM_CHUNK_OK || !decoded)
    {
        mm_storage_dispose(&b->palette);
        if (profile == MM_BACKGROUND_STUDIO)
            b->palette_base = base;
        return profile == MM_BACKGROUND_STUDIO;
    }
    if (array.header.width != 4 || (profile == MM_BACKGROUND_PREVIEW && array.header.count > 256U - base))
    {
        mm_storage_dispose(&b->palette);
        return 0;
    }
    bytes = b->palette.buffers[0].data;
    for (i = 0; i < array.header.count; ++i, bytes += 4)
    {
        if (array.header.big_endian != s->host_big_endian)
        {
            t = bytes[0];
            bytes[0] = bytes[3];
            bytes[3] = t;
            t = bytes[1];
            bytes[1] = bytes[2];
            bytes[2] = t;
        }
        if (profile == MM_BACKGROUND_PREVIEW)
            s->palette_color(s->context, (MMI32)(base + i), bytes);
    }
    if (profile == MM_BACKGROUND_PREVIEW)
        mm_storage_dispose(&b->palette);
    else
        b->palette_count = array.header.count;
    b->palette_base = base;
    return 1;
}
int mm_background_read(MMBackgroundResource *b, MMBackgroundCameraState *camera, MMU32 type,
                             MMU32 number, int profile, const MMBackgroundServices *s)
{
    MMBackgroundHeader header;
    MMBackgroundLight light;
    MMCollectionStorage lights;
    BackgroundArray array;
    const unsigned char *bytes;
    MMU32 child;
    MMI32 i;
    mm_storage_init(&lights, NULL);
    array.storage = &lights;
    if (profile == MM_BACKGROUND_STUDIO)
    {
        camera->count = cameras(type, number, profile, s);
        camera->current = -1;
        if (camera->count < 0)
            goto fail;
    }
    if (!read_record(s, type, number, read_header, &header))
        goto fail;
    if (profile == MM_BACKGROUND_STUDIO)
    {
        if (!s->name(s->context, type, number))
            goto fail;
        if (s->child(s->context, type, number, 0, MM_BACKGROUND_SOUND, &child))
        {
            if (!read_record(s, MM_BACKGROUND_SOUND, child, read_sound, &b->sound))
                goto fail;
        }
        else
            b->sound.tag.sid = -1;
    }
    /* Only a present palette publishes the stored base in Studio. */
    if (!palette(b, type, number, header.palette_base, profile, s))
        goto fail;
    if (profile == MM_BACKGROUND_PREVIEW)
        s->resolve_palette(s->context);
    if (!s->child(s->context, type, number, 0, MM_BACKGROUND_LIGHTS, &child) ||
        !read_record(s, MM_BACKGROUND_LIGHTS, child, read_array, &array))
        goto fail;
    b->light_count = (MMI32)array.header.count;
    if (array.header.width != 56 ||
        (profile == MM_BACKGROUND_PREVIEW && b->light_count > 16))
        goto fail;
    if (profile == MM_BACKGROUND_STUDIO)
    {
        if (s->actor_bytes <= 0 || s->light_bytes <= 0 || b->light_count > MAXIMUM / s->actor_bytes ||
            b->light_count > MAXIMUM / s->light_bytes ||
            !s->allocate(s->context, b->light_count * s->actor_bytes, &b->actors) ||
            !s->allocate(s->context, b->light_count * s->light_bytes, &b->lights))
            goto fail;
    }
    bytes = lights.buffers[0].data;
    for (i = 0; i < b->light_count; ++i, bytes += 56)
    {
        mm_background_light_decode(bytes, array.header.big_endian, &light);
        if (!s->configure_light(s->context, b, i, &light))
            goto fail;
    }
    mm_storage_dispose(&lights);
    if (profile == MM_BACKGROUND_PREVIEW)
    {
        camera->count = cameras(type, number, profile, s);
        return camera->count > 0 && s->select_camera(s->context, 0);
    }
    return 1;
fail:
    if (profile == MM_BACKGROUND_STUDIO)
        s->diagnostic(s->context, MM_BACKGROUND_READ_ERROR);
    mm_storage_dispose(&lights);
    return 0;
}
void mm_background_lights_on(MMBackgroundResource *b, const MMBackgroundServices *s)
{
    MMI32 i;
    if (b->enabled)
        return;
    for (i = 0; i < b->light_count; ++i)
    {
        s->add_light(s->context, b, i);
        s->enable_light(s->context, b, i);
    }
    b->enabled = 1;
}
void mm_background_lights_off(MMBackgroundResource *b, const MMBackgroundServices *s)
{
    MMI32 i;
    if (!b->enabled || b->leave_on)
        return;
    for (i = 0; i < b->light_count; ++i)
    {
        s->disable_light(s->context, b, i);
        s->remove_light(s->context, b, i);
    }
    b->enabled = 0;
}
void mm_background_resource_dispose(MMBackgroundResource *b, const MMBackgroundServices *s)
{
    if (b->actors && b->lights)
        mm_background_lights_off(b, s);
    s->free_memory(s->context, &b->actors);
    s->free_memory(s->context, &b->lights);
    mm_storage_dispose(&b->palette);
    b->palette_count = 0;
}
const unsigned char *mm_background_palette(const MMBackgroundResource *b, MMU32 *count, MMI32 *base)
{
    *count = b->palette_count;
    *base = (MMI32)b->palette_base;
    return b->palette.buffers[0].data;
}
int mm_background_factory(MMU32 type, MMU32 number, int materialize,
                                const MMBackgroundFactoryServices *s)
{
    void *object;
    MMI32 count, unit;
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
static const MMU32 children[4] = {MM_BACKGROUND_SOUND, MM_BACKGROUND_LIGHTS,
                                       MM_BACKGROUND_COLORS, MM_BACKGROUND_CAMERA};
int mm_background_tags(const MMBackgroundTagServices *s)
{
    int i;
    if (!s->insert(s->context, 1, 0))
        return 0;
    for (i = 0; i < 4; ++i)
        if (!s->insert(s->context, 0, children[i]))
            return 0;
    return 1;
}
int mm_background_cache(const MMTag *parent, const MMBackgroundTagServices *s)
{
    MMTag tags[4];
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
