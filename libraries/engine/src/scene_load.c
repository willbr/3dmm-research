/* Scene loading and sound resolution converted from engine/scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/scene_load.h"
#include "mm/wide.h"

static int load_group(void *scene, MMSceneOwner *owner, MMU32 number, int start, MMI32 *completed,
                      const MMSceneLoadServices *s)
{
    const MMSceneOwnerServices *o = &s->owners;
    const MMSceneSoundPacketServices *v = &s->packets;
    const MMMovieSceneReader *read = &s->read;
    const unsigned short endian = 1;
    const int native_big = *(const unsigned char *)&endian == 0;
    MMMovieSceneGroup wire;
    MMMovieSceneEvent event;
    void *group, *object;
    MMSceneSound *sound;
    MMTag tag;
    MMU32 i, child;
    MMI32 j;
    int ok, slot = start ? MM_SCENE_STARTS : MM_SCENE_FRAMES;
    if (read->group(read->context, number, start, &wire) != MM_MOVIE_OK)
        return 0;
    group = o->create_slot(o->context, slot);
    mm_scene_owner_set(owner, scene, slot, group, o);
    if (!group)
        goto fail;
    for (i = 0; i < wire.count; ++i)
    {
        read->event(read->context, &wire, i, &event);
        if (!start && event.kind == MM_SCENE_PLAY_SOUND)
        {
            sound = mm_scene_sound_decode(event.data, event.bytes, event.big_endian != native_big, v);
            if (!sound)
                goto fail;
            for (j = 0; j < sound->ctagc; ++j)
                if (!s->open_tag(s->context, sound, j))
                    break;
            ok = j == sound->ctagc && s->insert(s->context, group, i, event.frame, event.kind,
                                                mm_scene_sound_size(sound->ctagc, v->tag_bytes), sound);
            if (!ok)
                while (j > 0)
                    o->sound.close(o->sound.context, sound, --j);
            o->sound.free(o->sound.context, sound);
            if (!ok)
                goto fail;
        }
        else if (start && (event.kind == MM_SCENE_ADD_ACTOR || event.kind == MM_SCENE_ADD_TEXT))
        {
            child = mm_movie_scene_event_word(&event, 0);
            if (!s->child(s->context, number,
                          event.kind == MM_SCENE_ADD_ACTOR ? MM_SCENE_ACTORS : MM_SCENE_TEXTS, child,
                          &child))
                goto fail;
            object = s->object(s->context, child, event.kind, scene);
            if (!object)
                goto fail;
            if (!s->insert(s->context, group, i, event.frame, event.kind, o->pointer_bytes, &object))
            {
                o->release(o->context, object);
                goto fail;
            }
        }
        else if (start && event.kind == MM_SCENE_SET_BACKGROUND)
        {
            tag.sid = mm_signed_word(mm_movie_scene_event_word(&event, 0));
            tag.cache = NULL;
            if (!tag.sid)
            {
                if (!owner->owned[MM_SCENE_BACKGROUND_CACHE])
                    mm_scene_owner_set(owner, scene, MM_SCENE_BACKGROUND_CACHE,
                                       s->retain_background_cache(s->context), o);
                tag.cache = owner->owned[MM_SCENE_BACKGROUND_CACHE];
            }
            tag.type = mm_movie_scene_event_word(&event, 8);
            tag.number = mm_movie_scene_event_word(&event, 12);
            if (!s->insert_tag(s->context, group, i, event.frame, &tag))
                goto fail;
        }
        else if (event.kind == MM_SCENE_CHANGE_CAMERA || (!start && event.kind == MM_SCENE_PAUSE))
        {
            if (!s->insert(s->context, group, i, event.frame, event.kind, event.bytes, event.data))
                goto fail;
        }
        else
        {
            o->diagnostic(o->context, start ? MM_SCENE_BAD_START : MM_SCENE_BAD_FRAME);
            goto fail; /* Validated wire groups cannot contain these kinds. */
        }
        ++*completed;
    }
    read->close(read->context, &wire);
    return 1;
fail:
    read->close(read->context, &wire);
    return 0;
}
void *mm_scene_load(void *movie, MMU32 number, unsigned unit, const MMSceneLoadServices *s)
{
    MMSceneState metadata;
    MMMovieSessionScene header;
    const MMSceneOwnerServices *o = &s->owners;
    MMSceneOwner *owner;
    MMI32 starts = 0, frames = 0;
    MMU32 child;
    void *scene, *p;
    int slot;
    if (!mm_scene_init(&metadata, unit) || s->read.header(s->read.context, number, &header) != MM_MOVIE_OK)
        return NULL;
    scene = o->create_scene(o->context, movie);
    if (!scene)
        return NULL;
    owner = o->state(o->context, scene);
    o->reset_cursor(o->context, scene);
    for (slot = MM_SCENE_ACTORS; slot <= MM_SCENE_TEXTS; ++slot)
    {
        p = o->create_slot(o->context, slot);
        mm_scene_owner_set(owner, scene, slot, p, o);
        if (!p)
            goto fail;
    }
    metadata.firstFrame = header.first_frame;
    metadata.lastFrame = header.last_frame;
    metadata.transition = header.transition;
    metadata.currentFrame = metadata.firstFrame - 1;
    s->publish_metadata(s->context, scene, &metadata);
    if (s->child(s->context, number, MM_SCENE_THUMBNAIL, 0, &child))
        mm_scene_owner_set(owner, scene, MM_SCENE_THUMBNAIL, s->thumbnail(s->context, child), o);
    if (!load_group(scene, owner, number, 0, &frames, s))
        goto unwind;
    mm_scene_owner_drop(owner, scene, MM_SCENE_STARTS, o);
    if (!load_group(scene, owner, number, 1, &starts, s))
        goto unwind;
    s->load_name(s->context, scene, number);
    return scene;
unwind:
    mm_scene_owner_unwind(owner, scene, starts, frames, o);
fail:
    o->release(o->context, scene);
    return NULL;
}
int mm_scene_resolve_sounds(MMU32 scene, const MMSceneResolveServices *s)
{
    void *group = s->owner->owned[MM_SCENE_ACTORS], *entry;
    MMI32 i, j, count = s->count(s->context, group);
    MMSceneSound *sound;
    for (i = 0; i < count; ++i)
        if (!s->actor(s->context, i, scene))
            return 0;
    group = s->owner->owned[MM_SCENE_FRAMES];
    s->lock(s->context, group);
    count = s->count(s->context, group);
    for (i = 0; i < count; ++i)
        if (s->kind(s->context, group, i) == MM_SCENE_PLAY_SOUND)
        {
            sound = s->sound(s->context, group, i);
            for (j = 0; j < sound->ctagc; ++j)
            {
                entry = (unsigned char *)sound + 16 + (size_t)j * s->packets.tag_bytes;
                if (s->packets.sid(s->packets.context, entry) == 0 && !s->resolve(s->context, entry, scene))
                {
                    s->unlock(s->context, group);
                    return 0;
                }
            }
        }
    s->unlock(s->context, group);
    return 1;
}
