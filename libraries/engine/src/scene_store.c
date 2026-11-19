/* Scene tag collection and saving converted from engine/scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/scene_store.h"
#include "mm/background_resource.h"
#include "mm/scene_data.h"
#include "mm/movie_sound.h"
#include "mm/wide.h"
#include <string.h>
static void tag_from_event(MMTag *tag, const MMMovieSceneEvent *event)
{
    tag->sid = mm_signed_word(mm_movie_scene_event_word(event, 0));
    tag->cache = NULL;
    tag->type = mm_movie_scene_event_word(event, 8);
    tag->number = mm_movie_scene_event_word(event, 12);
}
typedef struct CollectActorTags
{
    MMTagList *list;
    const MMSceneCollectServices *services;
} CollectActorTags;

static int insert_actor_tag(void *p, const MMTag *tag)
{
    return mm_tag_list_insert(((CollectActorTags *)p)->list, tag, 1) == MM_STORAGE_OK;
}

static void actor_diagnostic(void *p, int issue)
{
    const MMSceneCollectServices *s = ((const CollectActorTags *)p)->services;
    if (s->actor_diagnostic)
        s->actor_diagnostic(s->context, issue);
}

int mm_scene_collect_tags(MMU32 scene, MMTagList *tags, const MMSceneCollectServices *s)
{
    const MMMovieSceneReader *read = &s->read;
    CollectActorTags actor;
    MMChunkAccess chunks;
    MMMovieSceneGroup group;
    MMMovieSceneEvent event;
    MMTag background, tag;
    MMChunkChild link;
    MMSceneSoundWire sound;
    MMI32 j;
    MMU32 i, child, scene_row;
    int start;
    actor.list = tags;
    actor.services = s;
    s->graph.access(s->graph.context, &chunks);
    if (mm_chunk_find(&chunks, MM_SCENE_CHUNK, scene, &scene_row) != MM_CHUNK_OK)
        return 0;
    background.sid = -1;
    for (start = 1; start >= 0; --start)
    {
        if (read->group(read->context, scene, start, &group) != MM_MOVIE_OK)
            return 0;
        for (i = 0; i < group.count; ++i)
        {
            read->event(read->context, &group, i, &event);
            if (start && event.kind == MM_SCENE_ADD_ACTOR)
            {
                if (event.bytes != 4)
                    goto fail;
                child = mm_movie_scene_event_word(&event, 0);
                if (mm_chunk_child_exact(&chunks, scene_row, child, MM_SCENE_ACTOR_CHUNK, &link) != MM_CHUNK_OK)
                    goto fail;
                if (mm_movie_actor_tags(&s->graph, link.number, &actor, insert_actor_tag, actor_diagnostic) !=
                    MM_MOVIE_OK)
                    goto fail;
            }
            else if (start && event.kind == MM_SCENE_SET_BACKGROUND)
            {
                if (event.bytes != 16)
                    goto fail;
                tag_from_event(&tag, &event);
                /* The movie already provides local backgrounds and their children. */
                if (tag.sid ? !mm_background_tags(tags, &tag)
                            : mm_tag_list_insert(tags, &tag, 0) != MM_STORAGE_OK)
                    goto fail;
                background = tag;
            }
            else if (!start && event.kind == MM_SCENE_CHANGE_CAMERA)
            {
                if (event.bytes != 4)
                    goto fail;
                child = mm_movie_scene_event_word(&event, 0);
                if (background.sid == -1)
                {
                    if (s->diagnostic)
                        s->diagnostic(s->context, 0, 1);
                    goto fail;
                }
                if (background.sid &&
                    mm_tag_list_child(tags, &background, child, MM_BACKGROUND_CAMERA) != MM_STORAGE_OK)
                    goto fail;
            }
            else if (!start && event.kind == MM_SCENE_PLAY_SOUND)
            {
                if (!mm_scene_sound_wire(event.data, event.bytes, event.big_endian, &sound))
                    goto fail;
                for (j = 0; j < sound.header.ctagc; ++j)
                {
                    mm_scene_sound_wire_tag(&sound, j, &child, &tag);
                    if (mm_tag_list_insert(tags, &tag, 1) != MM_STORAGE_OK)
                        goto fail;
                }
            }
            else if (s->diagnostic &&
                     !((start && event.kind == MM_SCENE_ADD_TEXT) || (!start && event.kind == MM_SCENE_PAUSE)))
                s->diagnostic(s->context, start, 0);
        }
        read->close(read->context, &group);
    }
    return 1;
fail:
    read->close(read->context, &group);
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
    MMI32 fixed[2];
    unsigned char wire_tag[16];
    *number = 0xffffffffUL;
    if (!s->add_scene(s->context, number))
        goto fail;
    frames = s->new_group(s->context);
    if (!frames)
        goto fail;
    group = s->owner->owned[MM_SCENE_FRAMES];
    for (i = 0; i < g->count(g->context, group); ++i)
    {
        g->fixed(g->context, group, i, (unsigned char *)fixed);
        frame = fixed[0];
        kind = fixed[1];
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
        g->fixed(g->context, group, i, (unsigned char *)fixed);
        frame = fixed[0];
        kind = fixed[1];
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
            if (!s->read_background(s->context, group, i, &tag) ||
                (!tag.sid && !mm_movie_archive_embed_tag(&s->assets, MM_SCENE_CHUNK, *number, &tag)))
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
