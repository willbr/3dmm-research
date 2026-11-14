/* Scene loading and sound resolution converted from engine/scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/scene_load.h"
#include <string.h>
static ThreeMMU32 word(const void *p, int swap)
{
    ThreeMMU32 value;
    memcpy(&value, p, 4);
    return swap ? ((value & 255U) << 24) | ((value & 65280U) << 8) | ((value >> 8) & 65280U) | (value >> 24) : value;
}
static ThreeMMI32 signed_word(const void *p, int swap)
{
    ThreeMMU32 value = word(p, swap);
    ThreeMMI32 result;
    memcpy(&result, &value, 4);
    return result;
}
static int load_group(void *scene, ThreeMMSceneOwner *owner, ThreeMMU32 number, int start, ThreeMMI32 *completed,
                      const ThreeMMSceneLoadServices *s)
{
    const ThreeMMSceneOwnerServices *o = &s->owners;
    const ThreeMMSceneSoundPacketServices *v = &s->packets;
    void *wire, *group, *data, *object;
    ThreeMMSceneSound *sound;
    ThreeMMTag tag;
    ThreeMMU32 child;
    ThreeMMI32 i, j, bytes, frame;
    int swap, kind, ok, slot = start ? THREE_MM_SCENE_STARTS : THREE_MM_SCENE_FRAMES;
    unsigned char fixed[8], background[16];
    if (!s->child(s->context, number, slot, start ? 1U : 0U, &child))
        return 0;
    wire = s->read_group(s->context, child, start, &swap);
    if (!wire)
        return 0;
    group = o->create_slot(o->context, slot);
    three_mm_scene_owner_set(owner, scene, slot, group, o);
    if (!group)
        goto fail;
    for (i = 0; i < o->count(o->context, wire); ++i)
    {
        s->fixed(s->context, wire, i, fixed);
        frame = signed_word(fixed, swap);
        kind = signed_word(fixed + 4, swap);
        if (!start && kind == THREE_MM_SCENE_PLAY_SOUND)
        {
            data = o->data(o->context, wire, i, &bytes);
            sound = three_mm_scene_sound_decode(data, bytes, swap, v);
            if (!sound)
                goto fail;
            for (j = 0; j < sound->ctagc; ++j)
                if (!s->open_tag(s->context, sound, j))
                    break;
            ok = j == sound->ctagc && s->insert(s->context, group, i, frame, kind,
                                                three_mm_scene_sound_size(sound->ctagc, v->tag_bytes), sound);
            if (!ok)
                while (j > 0)
                    o->sound.close(o->sound.context, sound, --j);
            o->sound.free(o->sound.context, sound);
            if (!ok)
                goto fail;
        }
        else if (start && (kind == THREE_MM_SCENE_ADD_ACTOR || kind == THREE_MM_SCENE_ADD_TEXT))
        {
            data = o->data(o->context, wire, i, &bytes);
            child = word(data, swap);
            if (!s->child(s->context, number,
                          kind == THREE_MM_SCENE_ADD_ACTOR ? THREE_MM_SCENE_ACTORS : THREE_MM_SCENE_TEXTS, child,
                          &child))
                goto fail;
            object = s->object(s->context, child, kind, scene);
            if (!object)
                goto fail;
            if (!s->insert(s->context, group, i, frame, kind, o->pointer_bytes, &object))
            {
                o->release(o->context, object);
                goto fail;
            }
        }
        else if (start && kind == THREE_MM_SCENE_SET_BACKGROUND)
        {
            /* Copy before a callback can allocate/relocate the source group. */
            data = o->data(o->context, wire, i, &bytes);
            memcpy(background, data, 16);
            tag.sid = signed_word(background, swap);
            tag.cache = NULL;
            tag.type = word(background + 8, swap);
            tag.number = word(background + 12, swap);
            if (!s->insert_tag(s->context, group, i, frame, &tag))
                goto fail;
        }
        else if (kind == THREE_MM_SCENE_CHANGE_CAMERA || (!start && kind == THREE_MM_SCENE_PAUSE))
        {
            data = o->data(o->context, wire, i, &bytes);
            if (!s->insert(s->context, group, i, frame, kind, bytes, data))
                goto fail;
        }
        else
        {
            o->diagnostic(o->context, start ? THREE_MM_SCENE_BAD_START : THREE_MM_SCENE_BAD_FRAME);
            goto fail; /* Validated wire groups cannot contain these kinds. */
        }
        ++*completed;
    }
    o->release(o->context, wire);
    return 1;
fail:
    o->release(o->context, wire);
    return 0;
}
void *three_mm_scene_load(void *movie, ThreeMMU32 number, unsigned unit, const ThreeMMSceneLoadServices *s)
{
    ThreeMMSceneState metadata;
    const ThreeMMSceneOwnerServices *o = &s->owners;
    ThreeMMSceneOwner *owner;
    ThreeMMI32 starts = 0, frames = 0;
    ThreeMMU32 child;
    void *scene, *p;
    int slot;
    if (!three_mm_scene_init(&metadata, unit) || !s->metadata(s->context, number, &metadata))
        return NULL;
    scene = o->create_scene(o->context, movie);
    if (!scene)
        return NULL;
    owner = o->state(o->context, scene);
    o->reset_cursor(o->context, scene);
    for (slot = THREE_MM_SCENE_ACTORS; slot <= THREE_MM_SCENE_TEXTS; ++slot)
    {
        p = o->create_slot(o->context, slot);
        three_mm_scene_owner_set(owner, scene, slot, p, o);
        if (!p)
            goto fail;
    }
    metadata.currentFrame = metadata.firstFrame - 1;
    s->publish_metadata(s->context, scene, &metadata);
    if (s->child(s->context, number, THREE_MM_SCENE_THUMBNAIL, 0, &child))
        three_mm_scene_owner_set(owner, scene, THREE_MM_SCENE_THUMBNAIL, s->thumbnail(s->context, child), o);
    if (!load_group(scene, owner, number, 0, &frames, s))
        goto unwind;
    three_mm_scene_owner_drop(owner, scene, THREE_MM_SCENE_STARTS, o);
    if (!load_group(scene, owner, number, 1, &starts, s))
        goto unwind;
    s->load_name(s->context, scene, number);
    return scene;
unwind:
    three_mm_scene_owner_unwind(owner, scene, starts, frames, o);
fail:
    o->release(o->context, scene);
    return NULL;
}
int three_mm_scene_resolve_sounds(ThreeMMU32 scene, const ThreeMMSceneResolveServices *s)
{
    void *group = s->owner->owned[THREE_MM_SCENE_ACTORS], *entry;
    ThreeMMI32 i, j, count = s->count(s->context, group);
    ThreeMMSceneSound *sound;
    for (i = 0; i < count; ++i)
        if (!s->actor(s->context, i, scene))
            return 0;
    group = s->owner->owned[THREE_MM_SCENE_FRAMES];
    s->lock(s->context, group);
    count = s->count(s->context, group);
    for (i = 0; i < count; ++i)
        if (s->kind(s->context, group, i) == THREE_MM_SCENE_PLAY_SOUND)
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
