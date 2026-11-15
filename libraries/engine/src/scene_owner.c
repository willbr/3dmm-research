/* Scene ownership/lifecycle converted from engine/scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/scene_owner.h"
#include <string.h>
void mm_scene_owner_init(MMSceneOwner *w, void *movie)
{
    memset(w, 0, sizeof(*w));
    w->movie = movie;
}
void mm_scene_sound_release(void **sound, const MMSceneSoundServices *s)
{
    void *p = *sound;
    MMI32 i, count, style;
    if (!p)
        return;
    count = s->count(s->context, p);
    style = s->style(s->context, p);
    if (count < 0 || count >= 1000 || style < 0 || style >= 5)
        s->diagnostic(s->context, MM_SCENE_BAD_SOUND);
    else
        for (i = 0; i < count; ++i)
            if (s->open(s->context, p, i))
                s->close(s->context, p, i);
    s->free(s->context, p);
    *sound = NULL;
}
void mm_scene_owner_set(MMSceneOwner *w, void *scene, int slot, void *p, const MMSceneOwnerServices *s)
{
    w->owned[slot] = p;
    s->publish(s->context, scene, w);
}
void mm_scene_owner_drop(MMSceneOwner *w, void *scene, int slot, const MMSceneOwnerServices *s)
{
    void *p = w->owned[slot];
    if (!p)
        return;
    if (slot == MM_SCENE_BACKGROUND_SOUND)
        mm_scene_sound_release(&p, &s->sound);
    else
        s->release(s->context, p);
    w->owned[slot] = NULL;
    s->publish(s->context, scene, w);
}
void *mm_scene_owner_new(void *movie, const MMSceneOwnerServices *s)
{
    void *scene = s->create_scene(s->context, movie), *p;
    MMSceneOwner *w;
    int slot;
    if (!scene)
        return NULL;
    w = s->state(s->context, scene);
    for (slot = MM_SCENE_FRAMES; slot <= MM_SCENE_TEXTS; ++slot)
    {
        p = s->create_slot(s->context, slot);
        if (!p)
        {
            s->release(s->context, scene);
            return NULL;
        }
        mm_scene_owner_set(w, scene, slot, p, s);
        if (slot == MM_SCENE_FRAMES)
            s->reset_cursor(s->context, scene);
    }
    if (s->can_notify(s->context))
        s->loaded(s->context, scene);
    return scene;
}
static void release_object(void *group, MMI32 index, const MMSceneOwnerServices *s)
{
    MMI32 bytes;
    s->data(s->context, group, index, &bytes);
    if (bytes != s->pointer_bytes)
        s->diagnostic(s->context, MM_SCENE_BAD_PAYLOAD);
    else
        s->release(s->context, s->object(s->context, group, index));
}
static void start_event(void *group, MMI32 index, int unwind, const MMSceneOwnerServices *s)
{
    MMI32 kind = s->kind(s->context, group, index);
    if (kind == MM_SCENE_ADD_ACTOR || kind == MM_SCENE_ADD_TEXT)
        release_object(group, index, s);
    else if (kind != MM_SCENE_CHANGE_CAMERA && kind != MM_SCENE_SET_BACKGROUND)
    {
        if (unwind)
            return;
        s->diagnostic(s->context, kind == MM_SCENE_PAUSE || kind == MM_SCENE_PLAY_SOUND
                                      ? MM_SCENE_BAD_START
                                      : MM_SCENE_UNKNOWN_EVENT);
    }
}
static void frame_event(void *group, MMI32 index, int unwind, const MMSceneOwnerServices *s)
{
    MMI32 kind = s->kind(s->context, group, index), count, i, bytes;
    void *sound;
    if (kind == MM_SCENE_PLAY_SOUND)
    {
        sound = s->data(s->context, group, index, &bytes);
        if (bytes < s->sound_header_bytes || s->sound_tag_bytes <= 0)
        {
            s->diagnostic(s->context, MM_SCENE_BAD_PAYLOAD);
            return;
        }
        count = s->sound.count(s->sound.context, sound);
        if (count < 0 || count > (bytes - s->sound_header_bytes) / s->sound_tag_bytes ||
            (unwind && count * s->sound_tag_bytes != bytes - s->sound_header_bytes))
        {
            s->diagnostic(s->context, MM_SCENE_BAD_PAYLOAD);
            return;
        }
        for (i = 0; i < count; ++i)
        {
            sound = s->data(s->context, group, index, &bytes);
            s->sound.close(s->sound.context, sound, i);
        }
    }
    else if (kind == MM_SCENE_ADD_ACTOR || kind == MM_SCENE_ADD_TEXT ||
             (!unwind && kind == MM_SCENE_SET_BACKGROUND))
        s->diagnostic(s->context, MM_SCENE_BAD_FRAME);
    else if (!unwind && kind != MM_SCENE_CHANGE_CAMERA && kind != MM_SCENE_PAUSE)
        s->diagnostic(s->context, MM_SCENE_UNKNOWN_EVENT);
}
void mm_scene_owner_dispose(MMSceneOwner *w, void *scene, const MMSceneOwnerServices *s)
{
    MMI32 i;
    int slot;
    void *group = w->owned[MM_SCENE_STARTS];
    if (group)
        for (i = 0; i < s->count(s->context, group); ++i)
            start_event(group, i, 0, s);
    mm_scene_owner_drop(w, scene, MM_SCENE_STARTS, s);
    group = w->owned[MM_SCENE_FRAMES];
    if (group)
        for (i = 0; i < s->count(s->context, group); ++i)
            frame_event(group, i, 0, s);
    mm_scene_owner_drop(w, scene, MM_SCENE_FRAMES, s);
    for (slot = MM_SCENE_ACTORS; slot < MM_SCENE_OWNERS; ++slot)
        mm_scene_owner_drop(w, scene, slot, s);
}
void mm_scene_owner_unwind(MMSceneOwner *w, void *scene, MMI32 starts, MMI32 frames,
                                 const MMSceneOwnerServices *s)
{
    while (starts > 0)
        start_event(w->owned[MM_SCENE_STARTS], --starts, 1, s);
    mm_scene_owner_drop(w, scene, MM_SCENE_STARTS, s);
    while (frames > 0)
        frame_event(w->owned[MM_SCENE_FRAMES], --frames, 1, s);
    mm_scene_owner_drop(w, scene, MM_SCENE_FRAMES, s);
}
void mm_scene_owner_actors(MMSceneOwner *w, int show, const MMSceneOwnerServices *s)
{
    MMI32 i;
    void *list = w->owned[MM_SCENE_ACTORS];
    for (i = 0; i < s->count(s->context, list); ++i)
        s->actor_visibility(s->context, s->list_at(s->context, list, i), show);
}
void mm_scene_owner_hide_texts(MMSceneOwner *w, const MMSceneOwnerServices *s)
{
    MMI32 i, first;
    void *list = w->owned[MM_SCENE_TEXTS], *text;
    for (i = 0; i < s->count(s->context, list); ++i)
    {
        text = s->list_at(s->context, list, i);
        if (s->text_lifetime(s->context, text, &first))
            if (first == (-2147483647 - 1) || !s->text_seek(s->context, text, first - 1))
                s->diagnostic(s->context, MM_SCENE_TEXT_HIDE_FAILED);
    }
}
void mm_scene_owner_close(void **scene, const MMSceneOwnerServices *s)
{
    void *p = *scene;
    MMSceneOwner *w;
    if (!p)
        return;
    w = s->state(s->context, p);
    if (w->owned[MM_SCENE_BACKGROUND])
        s->lights_off(s->context, w->owned[MM_SCENE_BACKGROUND]);
    mm_scene_owner_actors(w, 0, s);
    mm_scene_owner_hide_texts(w, s);
    s->release(s->context, p);
    *scene = NULL;
}
