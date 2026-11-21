/* Scene thumbnails/dirty propagation converted from engine/scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/scene_thumbnail.h"
#include "mm/indexed_display.h"
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

static int read_thumbnail_palette(MMArchiveReader *reader, int poster, unsigned char palette[1024])
{
    static const MMU32 numbers[] = {0x20001UL, 0};
    const MMChunkAccess access = mm_archive_reader_access(reader);
    MMCollectionHeader header;
    MMReadStream stream;
    MMU32 position, i, count = poster ? 2 : 1;
    for (i = 0; i < count; ++i)
    {
        if (mm_chunk_find(&access, 0x474c4352UL, numbers[i], &position) != MM_CHUNK_OK ||
            mm_archive_reader_payload(reader, position, &stream) != MM_ARCHIVE_READ_OK)
            continue;
        if (poster && (stream.length > 8192 ||
                       mm_collection_header_read(&stream, MM_COLLECTION_ARRAY, 0, &header) != MM_STORAGE_OK))
            continue;
        return mm_palette_read_256(&stream, palette);
    }
    return 0;
}

static int read_thumbnail(const MMSceneThumbnailPreview *preview, const MMReadStream *stream, int poster)
{
    const unsigned short one = 1;
    MMMaskedServices services;
    MMMaskedBitmap bitmap;
    MMMaskedRect bounds;
    unsigned char palette[1024];
    const unsigned char *colours, *colour;
    unsigned char *indices, *rgba;
    MMU32 width, height, rgba_bytes;
    size_t pixels, i;
    int result;
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
    colours = poster ? NULL : preview->palette;
    if (preview->resources && read_thumbnail_palette(preview->resources, poster, palette))
        colours = palette;
    if (!colours)
    {
        result = MM_SCENE_THUMBNAIL_OK;
        goto done;
    }
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

static int thumbnail_chunk(MMU32 number, const MMSceneThumbnailPreview *preview, int poster)
{
    const MMChunkAccess access = mm_archive_reader_access(preview->archive);
    MMChunkEntry chunk;
    MMReadStream stream;
    MMU32 position;
    int status;
    if (mm_chunk_find(&access, MM_SCENE_THUMBNAIL_TYPE, number, &position) != MM_CHUNK_OK ||
        mm_chunk_at(&access, position, &chunk) != MM_CHUNK_OK || (!poster && chunk.bytes > 1024UL * 1024UL))
        return MM_SCENE_THUMBNAIL_READ;
    status = mm_archive_reader_payload(preview->archive, position, &stream);
    if (status == MM_ARCHIVE_MISSING_PAYLOAD)
        return MM_SCENE_THUMBNAIL_READ;
    if (status != MM_ARCHIVE_READ_OK)
        return MM_SCENE_THUMBNAIL_DECODE;
    if (poster && stream.length > 1024UL * 1024UL)
        return MM_SCENE_THUMBNAIL_READ;
    return read_thumbnail(preview, &stream, poster);
}

int mm_scene_thumbnail_preview(MMU32 scene, const MMSceneThumbnailPreview *preview)
{
    const MMChunkAccess access = mm_archive_reader_access(preview->archive);
    MMU32 number;
    return mm_scene_thumbnail_link(&access, scene, &number) == MM_CHUNK_OK ?
               thumbnail_chunk(number, preview, 0) : MM_SCENE_THUMBNAIL_OK;
}

int mm_scene_thumbnail_poster(MMU32 number, const MMSceneThumbnailPreview *preview)
{
    return thumbnail_chunk(number, preview, 1);
}

void mm_scene_mark_dirty(const MMSceneOwner *owner, int dirty, const MMSceneDirtyServices *s)
{
    if (dirty && owner->movie)
        s->mark(s->context, owner->movie);
}
static void select_for_view(void *view, void *actor, void *text, const MMSceneThumbnailServices *s)
{
    if (s->text_mode(s->context, view))
        mm_scene_select_text(text, &s->selection);
    else
        mm_scene_select_actor(actor, &s->selection);
}
void mm_scene_update_thumbnail(const MMSceneThumbnailServices *s)
{
    MMMaskedRect full = {0, 0, 0, 0},
                      thumb = {0, 0, MM_SCENE_THUMBNAIL_WIDTH, MM_SCENE_THUMBNAIL_HEIGHT};
    MMSceneOwner *owner = s->owner;
    const MMSceneState *data = &owner->data;
    MMSceneRuntime *runtime = &owner->runtime;
    void *actor = owner->selected_actor, *text = owner->selected_text;
    MMI32 time, frame, stride;
    MMU32 disabled;
    void *view, *source, *target, *pixels, *bitmap;
    time = s->sound_time(s->context);
    s->mute(s->context);
    view = s->view(s->context);
    if (!owner->owned[MM_SCENE_BACKGROUND] || !view || !s->dirty(s->context))
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
    disabled = runtime->disabled;
    runtime->disabled |= MM_SCENE_EFFECT_PAUSES | MM_SCENE_EFFECT_SOUNDS;
    frame = data->currentFrame;
    if (data->currentFrame != data->firstFrame && !s->seek(s->context, data->firstFrame))
    {
        s->release_port(s->context, source);
        s->release_port(s->context, target);
        runtime->disabled = disabled;
        goto resume;
    }
    select_for_view(view, NULL, NULL, s);
    s->draw(s->context, view, source, &full);
    select_for_view(view, actor, text, s);
    s->copy(s->copy_context, source, target, &full, &thumb);
    mm_scene_owner_drop(owner, MM_SCENE_THUMBNAIL, &s->owners);
    stride = s->row_bytes(s->context, target);
    pixels = s->lock(s->context, target);
    bitmap = s->bitmap(s->context, pixels, stride, &thumb, MM_SCENE_THUMBNAIL_TRANSPARENT);
    owner->owned[MM_SCENE_THUMBNAIL] = bitmap;
    s->unlock(s->context, target);
    s->release_port(s->context, source);
    s->release_port(s->context, target);
    if (frame > data->firstFrame && frame <= data->lastFrame)
        s->seek(s->context, frame);
    runtime->disabled = disabled;
    s->end_copy(s->copy_context);
resume:
    s->resume(s->context, time);
}
void *mm_scene_thumbnail(const MMSceneThumbnailServices *s)
{
    mm_scene_update_thumbnail(s);
    return s->owner->owned[MM_SCENE_THUMBNAIL];
}
