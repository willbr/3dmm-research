/* Scene tag collection and saving converted from engine/scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/scene_store.h"
#include "mm/scene_data.h"
#include "mm/movie_sound.h"
#include <string.h>
static MMU32 word(const void *p, int swap)
{
    MMU32 n;
    memcpy(&n, p, 4);
    return swap ? ((n & 255U) << 24) | ((n & 65280U) << 8) | ((n >> 8) & 65280U) | (n >> 24) : n;
}
static MMI32 signed_word(const void *p, int swap)
{
    MMU32 n = word(p, swap);
    MMI32 result;
    memcpy(&result, &n, 4);
    return result;
}
static void tag_from_wire(MMTag *tag, const void *p, int swap)
{
    const unsigned char *b = (const unsigned char *)p;
    tag->sid = signed_word(b, swap);
    tag->cache = NULL;
    tag->type = word(b + 8, swap);
    tag->number = word(b + 12, swap);
}
int mm_scene_collect_tags(MMU32 scene, const MMSceneCollectServices *s)
{
    const MMSceneStoreGroups *g = &s->groups;
    const MMSceneSoundPacketServices *v = &s->packets;
    MMTag background, tag;
    MMChunkChild link;
    MMSceneSound *sound;
    void *group = NULL, *list, *data;
    MMI32 i, j, kind, bytes;
    MMU32 child, scene_row;
    unsigned char fixed[8];
    int start, swap, error, ok;
    if (mm_chunk_find(&s->chunks, MM_SCENE_CHUNK, scene, &scene_row) != MM_CHUNK_OK)
        return 0;
    background.sid = -1;
    for (start = 1; start >= 0; --start)
    {
        if (mm_chunk_child_exact(&s->chunks, scene_row, start,
                                 start ? MM_SCENE_START_GROUP : MM_SCENE_FRAME_GROUP, &link) != MM_CHUNK_OK)
            return 0;
        group = s->read_group(s->context, link.type, link.number, &swap);
        if (!group)
            return 0;
        for (i = 0; i < g->count(g->context, group); ++i)
        {
            g->fixed(g->context, group, i, fixed);
            kind = signed_word(fixed + 4, swap);
            if (start && kind == MM_SCENE_ADD_ACTOR)
            {
                data = g->data(g->context, group, i, &bytes);
                if (bytes != 4)
                    goto fail;
                child = word(data, swap);
                if (mm_chunk_child_exact(&s->chunks, scene_row, child, MM_SCENE_ACTOR_CHUNK, &link) != MM_CHUNK_OK)
                    goto fail;
                list = s->actor_tags(s->context, link.number, &error);
                if (error)
                {
                    if (list)
                        g->release(g->context, list);
                    goto fail;
                }
                if (list)
                {
                    for (j = 0; j < g->count(g->context, list); ++j)
                    {
                        s->list_tag(s->context, list, j, &tag);
                        if (!s->insert_tag(s->context, &tag))
                        {
                            g->release(g->context, list);
                            goto fail;
                        }
                    }
                    g->release(g->context, list);
                }
            }
            else if (start && kind == MM_SCENE_SET_BACKGROUND)
            {
                data = g->data(g->context, group, i, &bytes);
                if (bytes != 16)
                    goto fail;
                tag_from_wire(&tag, data, swap);
                if (!s->background(s->context, &tag))
                    goto fail;
                background = tag;
            }
            else if (!start && kind == MM_SCENE_CHANGE_CAMERA)
            {
                data = g->data(g->context, group, i, &bytes);
                if (bytes != 4)
                    goto fail;
                child = word(data, swap);
                if (background.sid == -1)
                {
                    s->diagnostic(s->context, 0, 1);
                    goto fail;
                }
                if (!s->insert_child(s->context, &background, child, s->camera_type))
                    goto fail;
            }
            else if (!start && kind == MM_SCENE_PLAY_SOUND)
            {
                data = g->data(g->context, group, i, &bytes);
                sound = mm_scene_sound_decode(data, bytes, swap, v);
                if (!sound)
                    goto fail;
                ok = 1;
                for (j = 0; j < sound->ctagc; ++j)
                {
                    v->read_tag(v->context, (unsigned char *)sound + 16 + (size_t)j * v->tag_bytes, &child, &tag);
                    if (!s->insert_tag(s->context, &tag))
                    {
                        ok = 0;
                        break;
                    }
                }
                g->free_raw(g->context, sound);
                if (!ok)
                    goto fail;
            }
            else if (!((start && kind == MM_SCENE_ADD_TEXT) || (!start && kind == MM_SCENE_PAUSE)))
                s->diagnostic(s->context, start, 0);
        }
        g->release(g->context, group);
        group = NULL;
    }
    return 1;
fail:
    g->release(g->context, group);
    return 0;
}
static int store_sound(void *frames, MMI32 index, MMI32 frame, MMU32 scene,
                       const MMSceneStoreServices *s)
{
    const MMSceneSoundPacketServices *v = &s->values.packets;
    MMSceneSound *sound = mm_scene_sound_group(s->owner->owned[MM_SCENE_FRAMES], index, 1, v);
    MMChunkAccess chunks;
    MMTag tag;
    MMU32 child, match;
    MMI32 i, bytes;
    void *wire;
    int ok = 0;
    if (!sound)
        return 0;
    for (i = 0; i < sound->ctagc; ++i)
    {
        v->read_tag(v->context, (unsigned char *)sound + 16 + (size_t)i * v->tag_bytes, &child, &tag);
        if (tag.sid != 0)
            continue;
        /* A previous adoption can shift rows or add this slot to the scene. */
        chunks = s->chunks(s->context);
        if (mm_chunk_link_number(&chunks, MM_SCENE_CHUNK, scene, child, MM_SOUND_CHUNK, &match) == MM_CHUNK_OK ||
            mm_chunk_find(&chunks, MM_SOUND_CHUNK, tag.number, &match) != MM_CHUNK_OK)
            continue;
        if (!s->adopt_sound(s->context, scene, tag.number, child))
            goto done;
    }
    wire = mm_scene_sound_encode(sound, &bytes, v);
    if (wire)
    {
        ok = s->insert(s->context, frames, index, frame, MM_SCENE_PLAY_SOUND, bytes, wire);
        s->groups.free_raw(s->groups.context, wire);
    }
done:
    s->values.release(s->values.context, sound);
    return ok;
}
int mm_scene_store(MMU32 *number, const MMSceneStoreServices *s)
{
    const MMSceneStoreGroups *g = &s->groups;
    void *frames = NULL, *starts = NULL, *group, *data, *object, *thumbnail;
    MMI32 i, frame, kind, bytes;
    MMU32 actor = 0, text = 0, child, count;
    MMTag tag;
    unsigned char fixed[8], wire_tag[16];
    *number = 0xffffffffUL;
    if (!s->add_scene(s->context, number))
        goto fail;
    frames = s->new_group(s->context);
    if (!frames)
        goto fail;
    group = s->owner->owned[MM_SCENE_FRAMES];
    for (i = 0; i < g->count(g->context, group); ++i)
    {
        g->fixed(g->context, group, i, fixed);
        frame = signed_word(fixed, 0);
        kind = signed_word(fixed + 4, 0);
        if (kind == MM_SCENE_PLAY_SOUND)
        {
            if (!store_sound(frames, i, frame, *number, s))
                goto fail;
        }
        else if (kind == MM_SCENE_CHANGE_CAMERA || kind == MM_SCENE_PAUSE)
        {
            data = g->data(g->context, group, i, &bytes);
            if (!s->insert(s->context, frames, i, frame, kind, kind == MM_SCENE_PAUSE ? 8 : 4, data))
                goto fail;
        }
        else
            s->diagnostic(s->context, 0);
    }
    starts = s->new_group(s->context);
    if (!starts)
        goto fail;
    group = s->owner->owned[MM_SCENE_STARTS];
    for (i = 0; i < g->count(g->context, group); ++i)
    {
        g->fixed(g->context, group, i, fixed);
        frame = signed_word(fixed, 0);
        kind = signed_word(fixed + 4, 0);
        if (kind == MM_SCENE_ADD_ACTOR || kind == MM_SCENE_ADD_TEXT)
        {
            count = kind == MM_SCENE_ADD_ACTOR ? actor : text;
            if (!s->add_child(s->context, *number, kind, count, &child))
                goto fail;
            object = s->object(s->context, group, i);
            if (!s->write_object(s->context, object, kind, child, *number))
                goto fail;
            if (!s->insert(s->context, starts, i, frame, kind, 4, &count))
                goto fail;
            if (kind == MM_SCENE_ADD_ACTOR)
                ++actor;
            else
                ++text;
        }
        else if (kind == MM_SCENE_SET_BACKGROUND)
        {
            if (!s->save_background(s->context, group, i, &tag))
                goto fail;
            memcpy(wire_tag, &tag.sid, 4);
            memset(wire_tag + 4, 0, 4);
            memcpy(wire_tag + 8, &tag.type, 4);
            memcpy(wire_tag + 12, &tag.number, 4);
            if (!s->insert(s->context, starts, i, frame, kind, 16, wire_tag))
                goto fail;
        }
        else if (kind == MM_SCENE_CHANGE_CAMERA)
        {
            data = g->data(g->context, group, i, &bytes);
            if (!s->insert(s->context, starts, i, frame, kind, 4, data))
                goto fail;
        }
        else
            s->diagnostic(s->context, 1);
    }
    if (!s->write_events(s->context, *number, frames, starts))
        goto fail;
    s->update_thumbnail(s->context);
    thumbnail = s->owner->owned[MM_SCENE_THUMBNAIL];
    if (thumbnail)
    {
        bytes = s->thumbnail_size(s->context, thumbnail);
        if (!s->add_thumbnail(s->context, bytes, &child))
            goto fail;
        if (!s->write_thumbnail(s->context, thumbnail) || !s->adopt_thumbnail(s->context, *number, child))
        {
            s->delete_thumbnail(s->context, child);
            goto fail;
        }
        s->clear_loner(s->context, child);
    }
    if (!s->write_metadata(s->context, *number))
        goto fail;
    g->release(g->context, frames);
    g->release(g->context, starts);
    return 1;
fail:
    if (*number != 0xffffffffUL)
        s->delete_scene(s->context, *number);
    if (starts)
        g->release(g->context, starts);
    if (frames)
        g->release(g->context, frames);
    return 0;
}
