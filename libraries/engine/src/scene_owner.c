/* Scene ownership/lifecycle converted from engine/scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/scene_owner.h"
#include "mm/collections.h"
#include <string.h>
void mm_scene_owner_init(MMSceneOwner *w, void *movie, const MMStorageAllocator *allocator)
{
    int i;
    memset(w, 0, sizeof(*w));
    w->movie = movie;
    for (i = 0; i < MM_SCENE_MEMBER_LISTS; ++i)
        mm_storage_init(&w->members[i].storage, allocator);
    for (i = MM_SCENE_FRAMES; i <= MM_SCENE_STARTS; ++i)
        mm_scene_track_init(&w->tracks[i], allocator, 0);
}
MMI32 mm_scene_member_count(const MMSceneOwner *w, int list)
{
    return (MMI32)w->members[list].count;
}
void *mm_scene_member_at(const MMSceneOwner *w, int list, MMI32 index)
{
    const MMSceneMemberList *members = &w->members[list];
    void *object;
    if (index < 0 || (MMU32)index >= members->count)
        return NULL;
    memcpy(&object, members->storage.buffers[0].data + (size_t)index * sizeof(object), sizeof(object));
    return object;
}
static MMArray member_array(MMSceneMemberList *members)
{
    MMArray array = {NULL, sizeof(void *), 0, -1, 0, 0};
    array.storage = &members->storage;
    array.count = members->count;
    return array;
}
int mm_scene_member_append(MMSceneOwner *w, int list, void *object)
{
    MMSceneMemberList *members = &w->members[list];
    MMArray array = member_array(members);
    if (mm_array_add(&array, &object, NULL) != MM_STORAGE_OK)
        return 0;
    members->count = array.count;
    return 1;
}
void mm_scene_member_replace(MMSceneOwner *w, int list, MMI32 index, void *object)
{
    MMSceneMemberList *members = &w->members[list];
    if (index >= 0 && (MMU32)index < members->count)
        memcpy(members->storage.buffers[0].data + (size_t)index * sizeof(object), &object, sizeof(object));
}
void mm_scene_member_remove(MMSceneOwner *w, int list, MMI32 index)
{
    MMSceneMemberList *members = &w->members[list];
    MMArray array = member_array(members);
    if (mm_array_delete(&array, (MMU32)index, 1) == MM_STORAGE_OK)
        members->count = array.count;
}
void mm_scene_member_clear(MMSceneOwner *w, int list)
{
    w->members[list].count = 0;
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
    else if (slot <= MM_SCENE_STARTS && p == &w->tracks[slot])
        mm_scene_track_dispose(&w->tracks[slot]);
    else
        s->release(s->context, p);
    w->owned[slot] = NULL;
    s->publish(s->context, scene, w);
}
void *mm_scene_owner_open_events(MMSceneOwner *w, void *scene, int slot, const MMSceneOwnerServices *s)
{
    void *track = s->create_slot ? s->create_slot(s->context, slot) : &w->tracks[slot];
    if (track)
        mm_scene_owner_set(w, scene, slot, track, s);
    return track;
}
void *mm_scene_owner_new(void *movie, const MMSceneOwnerServices *s)
{
    void *scene = s->create_scene(s->context, movie);
    MMSceneOwner *w;
    int slot;
    if (!scene)
        return NULL;
    w = s->state(s->context, scene);
    for (slot = MM_SCENE_FRAMES; slot <= MM_SCENE_STARTS; ++slot)
    {
        if (!mm_scene_owner_open_events(w, scene, slot, s))
        {
            s->release(s->context, scene);
            return NULL;
        }
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
    for (slot = 0; slot < MM_SCENE_MEMBER_LISTS; ++slot)
    {
        mm_storage_dispose(&w->members[slot].storage);
        w->members[slot].count = 0;
    }
    for (slot = MM_SCENE_BACKGROUND; slot < MM_SCENE_OWNERS; ++slot)
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
    for (i = 0; i < mm_scene_member_count(w, MM_SCENE_ACTORS); ++i)
        s->actor_visibility(s->context, mm_scene_member_at(w, MM_SCENE_ACTORS, i), show);
}
void mm_scene_owner_hide_texts(MMSceneOwner *w, const MMSceneOwnerServices *s)
{
    MMI32 i, first;
    void *text;
    for (i = 0; i < mm_scene_member_count(w, MM_SCENE_TEXTS); ++i)
    {
        text = mm_scene_member_at(w, MM_SCENE_TEXTS, i);
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
