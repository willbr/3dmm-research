/* Scene ownership/lifecycle converted from engine/scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/scene_runtime.h"
#include "mm/collections.h"
#include "mm/scene_sound.h"
#include <string.h>
int mm_scene_owner_init(MMSceneOwner *w, void *movie, unsigned unit, const MMStorageAllocator *allocator)
{
    int i;
    memset(w, 0, sizeof(*w));
    if (!mm_scene_init(&w->data, unit))
        return 0;
    w->movie = movie;
    w->runtime.disabled = MM_SCENE_EFFECT_PAUSES;
    for (i = 0; i < MM_SCENE_MEMBER_LISTS; ++i)
        mm_storage_init(&w->members[i].storage, allocator);
    for (i = 0; i < MM_SCENE_TRACKS; ++i)
        mm_scene_track_init(&w->tracks[i], allocator, 0);
    return 1;
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
    MMSceneSound *p = (MMSceneSound *)*sound;
    MMSceneSoundTag tag;
    MMI32 i, count, style;
    if (!p)
        return;
    count = p->ctagc;
    style = p->sty;
    if (count < 0 || count >= 1000 || style < 0 || style >= 5)
        s->diagnostic(s->context, MM_SCENE_BAD_SOUND);
    else
        for (i = 0; i < count; ++i)
        {
            mm_scene_sound_get_tag(p, i, &tag);
            if (tag.tag.cache)
            {
                s->close(s->context, &tag);
                mm_scene_sound_put_tag(p, i, &tag);
            }
        }
    s->free(s->context, p);
    *sound = NULL;
}
void mm_scene_owner_drop(MMSceneOwner *w, int slot, const MMSceneOwnerServices *s)
{
    void *p = w->owned[slot];
    if (!p)
        return;
    if (slot == MM_SCENE_BACKGROUND_SOUND)
        mm_scene_sound_release(&p, &s->sound);
    else if (slot == MM_SCENE_BACKGROUND)
        s->release_background(s->context, p);
    else
        s->release(s->context, p);
    w->owned[slot] = NULL;
}
void *mm_scene_owner_new(void *movie, const MMSceneOwnerServices *s)
{
    void *scene = s->create_scene(s->context, movie);
    if (scene && s->can_notify(s->context))
        s->loaded(s->context, scene);
    return scene;
}
static void release_object(const MMSceneTrack *track, MMI32 index, const MMSceneOwnerServices *s)
{
    void *object;
    if (!mm_scene_track_get_object(track, index, &object))
        s->diagnostic(s->context, MM_SCENE_BAD_PAYLOAD);
    else
        s->release(s->context, object);
}
static void start_event(const MMSceneTrack *track, MMI32 index, int unwind, const MMSceneOwnerServices *s)
{
    MMSceneTrackEvent event;
    MMI32 kind;
    if (!mm_scene_track_get(track, index, &event))
        return;
    kind = event.kind;
    if (kind == MM_SCENE_ADD_ACTOR || kind == MM_SCENE_ADD_TEXT)
        release_object(track, index, s);
    else if (kind != MM_SCENE_CHANGE_CAMERA && kind != MM_SCENE_SET_BACKGROUND)
    {
        if (unwind)
            return;
        s->diagnostic(s->context, kind == MM_SCENE_PAUSE || kind == MM_SCENE_PLAY_SOUND
                                      ? MM_SCENE_BAD_START
                                      : MM_SCENE_UNKNOWN_EVENT);
    }
}
static void close_frame_sound(const MMSceneTrack *track, MMI32 index, int exact, const MMSceneOwnerServices *s)
{
    MMI32 i, count, bytes = 0, span;
    MMSceneSound *sound = (MMSceneSound *)mm_scene_track_payload(track, index, &bytes);
    MMSceneSoundTag tag;
    span = mm_scene_sound_span(sound, bytes);
    if (!span || (exact && span != bytes))
    {
        s->diagnostic(s->context, MM_SCENE_BAD_PAYLOAD);
        return;
    }
    count = sound->ctagc;
    for (i = 0; i < count; ++i)
    {
        sound = (MMSceneSound *)mm_scene_track_payload(track, index, NULL);
        mm_scene_sound_get_tag(sound, i, &tag);
        s->sound.close(s->sound.context, &tag);
        sound = (MMSceneSound *)mm_scene_track_payload(track, index, NULL);
        mm_scene_sound_put_tag(sound, i, &tag);
    }
}
int mm_scene_owner_erase_frame(MMSceneOwner *w, MMI32 index, const MMSceneOwnerServices *s)
{
    MMSceneTrack *track = &w->tracks[MM_SCENE_FRAMES];
    MMSceneTrackEvent event;
    if (!mm_scene_track_get(track, index, &event))
        return 0;
    if (event.kind == MM_SCENE_PLAY_SOUND)
        close_frame_sound(track, index, 0, s);
    return mm_group_delete(&track->rows, (MMU32)index) == MM_STORAGE_OK;
}
static void frame_event(const MMSceneTrack *track, MMI32 index, int unwind, const MMSceneOwnerServices *s)
{
    MMSceneTrackEvent event;
    MMI32 kind;
    if (!mm_scene_track_get(track, index, &event))
        return;
    kind = event.kind;
    if (kind == MM_SCENE_PLAY_SOUND)
        close_frame_sound(track, index, unwind, s);
    else if (kind == MM_SCENE_ADD_ACTOR || kind == MM_SCENE_ADD_TEXT ||
             (!unwind && kind == MM_SCENE_SET_BACKGROUND))
        s->diagnostic(s->context, MM_SCENE_BAD_FRAME);
    else if (!unwind && kind != MM_SCENE_CHANGE_CAMERA && kind != MM_SCENE_PAUSE)
        s->diagnostic(s->context, MM_SCENE_UNKNOWN_EVENT);
}
void mm_scene_owner_dispose(MMSceneOwner *w, const MMSceneOwnerServices *s)
{
    MMI32 i;
    int slot;
    MMSceneTrack *track = &w->tracks[MM_SCENE_STARTS];
    for (i = 0; (MMU32)i < track->rows.count; ++i)
        start_event(track, i, 0, s);
    mm_scene_track_dispose(track);
    track = &w->tracks[MM_SCENE_FRAMES];
    for (i = 0; (MMU32)i < track->rows.count; ++i)
        frame_event(track, i, 0, s);
    mm_scene_track_dispose(track);
    for (slot = 0; slot < MM_SCENE_MEMBER_LISTS; ++slot)
    {
        mm_storage_dispose(&w->members[slot].storage);
        w->members[slot].count = 0;
    }
    w->selected_actor = NULL;
    w->selected_text = NULL;
    for (slot = 0; slot < MM_SCENE_OWNERS; ++slot)
        mm_scene_owner_drop(w, slot, s);
}
void mm_scene_owner_unwind(MMSceneOwner *w, const MMSceneOwnerServices *s)
{
    MMSceneTrack *track = &w->tracks[MM_SCENE_STARTS];
    MMI32 i;
    for (i = track->rows.count; i > 0;)
        start_event(track, --i, 1, s);
    mm_scene_track_dispose(track);
    track = &w->tracks[MM_SCENE_FRAMES];
    for (i = track->rows.count; i > 0;)
        frame_event(track, --i, 1, s);
    mm_scene_track_dispose(track);
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
