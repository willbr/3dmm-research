/* Scene thumbnails/dirty propagation converted from engine/scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/scene_thumbnail.h"
#include <stddef.h>
void three_mm_scene_mark_dirty(const ThreeMMSceneOwner *owner, int dirty, const ThreeMMSceneDirtyServices *s)
{
    if (dirty && owner->movie)
        s->mark(s->context, owner->movie);
}
void three_mm_scene_update_thumbnail(const ThreeMMSceneThumbnailServices *s)
{
    ThreeMMMaskedRect full = {0, 0, 0, 0},
                      thumb = {0, 0, THREE_MM_SCENE_THUMBNAIL_WIDTH, THREE_MM_SCENE_THUMBNAIL_HEIGHT};
    void *text = s->selected(s->context, 1), *actor = s->selected(s->context, 0);
    ThreeMMI32 time, frame, stride;
    ThreeMMU32 disabled;
    void *view, *source, *target, *pixels, *bitmap;
    int mode;
    time = s->sound_time(s->context);
    s->mute(s->context);
    view = s->view(s->context);
    if (!s->owner->owned[THREE_MM_SCENE_BACKGROUND] || !view || !s->dirty(s->context))
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
    s->runtime->disabled |= THREE_MM_SCENE_EFFECT_PAUSES | THREE_MM_SCENE_EFFECT_SOUNDS;
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
    three_mm_scene_owner_drop(s->owner, s->scene, THREE_MM_SCENE_THUMBNAIL, &s->owners);
    stride = s->row_bytes(s->context, target);
    pixels = s->lock(s->context, target);
    bitmap = s->bitmap(s->context, pixels, stride, &thumb, THREE_MM_SCENE_THUMBNAIL_TRANSPARENT);
    three_mm_scene_owner_set(s->owner, s->scene, THREE_MM_SCENE_THUMBNAIL, bitmap, &s->owners);
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
void *three_mm_scene_thumbnail(const ThreeMMSceneThumbnailServices *s)
{
    three_mm_scene_update_thumbnail(s);
    return s->owner->owned[THREE_MM_SCENE_THUMBNAIL];
}
