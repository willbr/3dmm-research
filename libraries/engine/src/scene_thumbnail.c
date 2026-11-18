/* Scene thumbnails/dirty propagation converted from engine/scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/scene_thumbnail.h"
#include "mm/collections.h"
#include <stdlib.h>
#include <string.h>

int mm_scene_thumbnail_link(const MMChunkAccess *access, MMU32 scene, MMU32 *number)
{
    return mm_chunk_link_number(access, 0x5343454eUL, scene, 0, MM_SCENE_THUMBNAIL_TYPE, number);
}

static int allocate_bitmap(void *context, MMI32 bytes, void **out)
{
    (void)context;
    *out = malloc((size_t)bytes);
    return *out != NULL;
}
static void dispose_bitmap(void *context, void **data)
{
    (void)context;
    free(*data);
    *data = NULL;
}

typedef struct ThumbnailPalette
{
    unsigned char bytes[256 * 4];
    int big;
} ThumbnailPalette;

static int read_thumbnail_palette(void *context, const MMReadStream *stream)
{
    ThumbnailPalette *palette = (ThumbnailPalette *)context;
    MMCollectionHeader header;
    unsigned char *p, t;
    unsigned i;
    if (mm_collection_header_read(stream, MM_COLLECTION_ARRAY, 0, &header) != MM_STORAGE_OK ||
        header.width != 4 || header.count != 256 ||
        !mm_read_exact(stream, header.header_bytes, palette->bytes, sizeof(palette->bytes)))
        return 0;
    if (header.big_endian != palette->big)
    {
        p = palette->bytes;
        for (i = 0; i < 256; ++i, p += 4)
        {
            t = p[0]; p[0] = p[3]; p[3] = t;
            t = p[1]; p[1] = p[2]; p[2] = t;
        }
    }
    return 1;
}

static int read_thumbnail(void *context, const MMReadStream *stream)
{
    const MMSceneThumbnailPreview *preview = (const MMSceneThumbnailPreview *)context;
    const unsigned short one = 1;
    MMMaskedServices services;
    MMMaskedBitmap bitmap;
    MMMaskedRect bounds;
    ThumbnailPalette palette;
    const unsigned char *colours, *colour;
    unsigned char *indices, *rgba;
    MMU32 width, height, rgba_bytes;
    size_t pixels, i;
    int decoded, result;
    memset(&services, 0, sizeof(services));
    memset(&bitmap, 0, sizeof(bitmap));
    services.host_big_endian = *(const unsigned char *)&one == 0;
    services.allocate = allocate_bitmap;
    services.free_memory = dispose_bitmap;
    if (!mm_masked_read_stream(stream, &bitmap, &services))
        return MM_SCENE_THUMBNAIL_DECODE;
    mm_masked_bounds(&bitmap, services.host_big_endian, &bounds);
    width = (MMU32)bounds.right - (MMU32)bounds.left;
    height = (MMU32)bounds.bottom - (MMU32)bounds.top;
    result = MM_SCENE_THUMBNAIL_DIMENSIONS;
    if (!width || !height || width > 1024 || height > 1024)
        goto done;
    result = MM_SCENE_THUMBNAIL_MEMORY;
    rgba_bytes = width * height * 4;
    if ((size_t)rgba_bytes != rgba_bytes)
        goto done;
    pixels = (size_t)(width * height);
    indices = (unsigned char *)calloc(pixels, 1);
    if (!indices)
        goto done;
    rgba = preview->image(preview->context, width, height);
    if (rgba)
    {
        mm_masked_blit8(&bitmap, services.host_big_endian, indices, (MMI32)width, (MMI32)height, 0, 0, NULL, NULL);
        colours = preview->palette;
        palette.big = services.host_big_endian;
        if (preview->resources &&
            mm_chunk_read_payload(preview->payload, preview->resources, 0x474c4352UL, 0x20001UL,
                                  read_thumbnail_palette, &palette, &decoded) == MM_CHUNK_OK && decoded)
            colours = palette.bytes;
        for (i = 0; i < pixels; ++i, rgba += 4)
        {
            colour = colours + 4 * (size_t)indices[i];
            rgba[0] = colour[2];
            rgba[1] = colour[1];
            rgba[2] = colour[0];
            rgba[3] = 255;
        }
        result = MM_SCENE_THUMBNAIL_OK;
    }
    free(indices);
done:
    mm_masked_dispose(&bitmap, &services);
    return result;
}

int mm_scene_thumbnail_preview(const MMChunkAccess *access, MMU32 scene, const MMSceneThumbnailPreview *preview)
{
    MMChunkEntry chunk;
    MMU32 number, position;
    int status, result;
    if (mm_scene_thumbnail_link(access, scene, &number) != MM_CHUNK_OK)
        return MM_SCENE_THUMBNAIL_OK;
    if (mm_chunk_find(access, MM_SCENE_THUMBNAIL_TYPE, number, &position) != MM_CHUNK_OK ||
        mm_chunk_at(access, position, &chunk) != MM_CHUNK_OK || chunk.bytes > 1024UL * 1024UL)
        return MM_SCENE_THUMBNAIL_READ;
    status = mm_chunk_read_payload(preview->payload, preview->archive, MM_SCENE_THUMBNAIL_TYPE, number,
                                   read_thumbnail, (void *)preview, &result);
    if (status == MM_CHUNK_NOT_FOUND)
        return MM_SCENE_THUMBNAIL_READ;
    return status == MM_CHUNK_OK ? result : MM_SCENE_THUMBNAIL_DECODE;
}

void mm_scene_mark_dirty(const MMSceneOwner *owner, int dirty, const MMSceneDirtyServices *s)
{
    if (dirty && owner->movie)
        s->mark(s->context, owner->movie);
}
void mm_scene_update_thumbnail(const MMSceneThumbnailServices *s)
{
    MMMaskedRect full = {0, 0, 0, 0},
                      thumb = {0, 0, MM_SCENE_THUMBNAIL_WIDTH, MM_SCENE_THUMBNAIL_HEIGHT};
    void *text = s->selected(s->context, 1), *actor = s->selected(s->context, 0);
    MMI32 time, frame, stride;
    MMU32 disabled;
    void *view, *source, *target, *pixels, *bitmap;
    int mode;
    time = s->sound_time(s->context);
    s->mute(s->context);
    view = s->view(s->context);
    if (!s->owner->owned[MM_SCENE_BACKGROUND] || !view || !s->dirty(s->context))
        goto resume;
    s->dimensions(s->context, &full.right, &full.bottom);
    source = s->new_port(s->context, &full);
    if (!source)
        goto resume;
    target = s->new_port(s->context, &thumb);
    if (!target)
    {
        s->release_port(s->context, source);
        goto resume;
    }
    s->palette(s->context, target);
    disabled = s->runtime->disabled;
    s->runtime->disabled |= MM_SCENE_EFFECT_PAUSES | MM_SCENE_EFFECT_SOUNDS;
    frame = s->data->currentFrame;
    if (s->data->currentFrame != s->data->firstFrame && !s->seek(s->context, s->data->firstFrame))
    {
        s->release_port(s->context, source);
        s->release_port(s->context, target);
        s->runtime->disabled = disabled;
        goto resume;
    }
    mode = s->text_mode(s->context, view);
    s->select(s->context, mode, NULL);
    s->draw(s->context, view, source, &full);
    mode = s->text_mode(s->context, view);
    s->select(s->context, mode, mode ? text : actor);
    s->copy(s->copy_context, source, target, &full, &thumb);
    mm_scene_owner_drop(s->owner, s->scene, MM_SCENE_THUMBNAIL, &s->owners);
    stride = s->row_bytes(s->context, target);
    pixels = s->lock(s->context, target);
    bitmap = s->bitmap(s->context, pixels, stride, &thumb, MM_SCENE_THUMBNAIL_TRANSPARENT);
    mm_scene_owner_set(s->owner, s->scene, MM_SCENE_THUMBNAIL, bitmap, &s->owners);
    s->unlock(s->context, target);
    s->release_port(s->context, source);
    s->release_port(s->context, target);
    if (frame > s->data->firstFrame && frame <= s->data->lastFrame)
        s->seek(s->context, frame);
    s->runtime->disabled = disabled;
    s->end_copy(s->copy_context);
resume:
    s->resume(s->context, time);
}
void *mm_scene_thumbnail(const MMSceneThumbnailServices *s)
{
    mm_scene_update_thumbnail(s);
    return s->owner->owned[MM_SCENE_THUMBNAIL];
}
