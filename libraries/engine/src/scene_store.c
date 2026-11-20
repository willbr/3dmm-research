/* Scene tag collection and saving converted from engine/scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/scene_store.h"
#include "mm/background_resource.h"
#include "mm/scene_data.h"
#include "mm/movie_sound.h"
#include "mm/text_store.h"
#include <string.h>
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
                mm_movie_scene_event_tag(&event, &tag);
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
static int store_sound(MMSceneTrack *frames, MMI32 index, const MMSceneTrackEvent *event,
                       MMU32 scene, const MMSceneStoreServices *s)
{
    const MMSceneSoundPacketServices *v = &s->values.packets;
    const MMMovieArchiveEditor *editor = s->editor;
    MMSceneSound *sound = mm_scene_sound_track(&s->owner->tracks[MM_SCENE_FRAMES], index, v);
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
        chunks = editor->chunks(editor->context);
        if (mm_chunk_link_number(&chunks, MM_SCENE_CHUNK, scene, child, MM_SOUND_CHUNK, &match) == MM_CHUNK_OK ||
            mm_chunk_find(&chunks, MM_SOUND_CHUNK, tag.number, &match) != MM_CHUNK_OK)
            continue;
        if (!editor->adopt(editor->context, MM_SCENE_CHUNK, scene, MM_SOUND_CHUNK, tag.number, child))
            goto done;
    }
    wire = mm_scene_sound_encode(sound, &bytes, v);
    if (wire)
    {
        ok = mm_scene_track_insert(frames, index, bytes, wire, event);
        s->free_sound_bytes(s->context, wire);
    }
done:
    s->values.release(s->values.context, sound);
    return ok;
}

static int copy_event(MMSceneTrack *out, const MMSceneTrack *source, MMI32 index,
                      const MMSceneTrackEvent *event, MMI32 bytes)
{
    MMI32 available;
    const void *data = mm_scene_track_payload(source, index, &available);
    return data && available >= bytes && mm_scene_track_insert(out, index, bytes, data, event);
}

static int store_object(const MMSceneStoreServices *s, MMU32 scene, MMI32 kind, MMU32 slot,
                        const MMSceneTrack *starts, MMI32 index)
{
    const MMMovieArchiveEditor *editor = s->editor;
    MMU32 child, type = kind == MM_SCENE_ADD_ACTOR ? MM_SCENE_ACTOR_CHUNK : MM_TEXT_BOX;
    MMI32 bytes;
    const void *data;
    void *object;
    if (!editor->add(editor->context, type, 0, &child, NULL))
        return 0;
    if (!editor->adopt(editor->context, MM_SCENE_CHUNK, scene, type, child, slot))
    {
        editor->discard(editor->context, type, child);
        return 0;
    }
    data = mm_scene_track_payload(starts, index, &bytes);
    if (!data || bytes < (MMI32)sizeof(object))
        return 0;
    memcpy(&object, data, sizeof(object));
    return s->write_object(s->context, object, kind, child, scene);
}

static int store_thumbnail(const MMSceneStoreServices *s, MMU32 scene)
{
    const MMMovieArchiveEditor *editor = s->editor;
    MMStorageWriter writer;
    void *thumbnail;
    MMU32 child;
    MMI32 bytes;
    s->update_thumbnail(s->context);
    thumbnail = s->owner->owned[MM_SCENE_THUMBNAIL];
    if (!thumbnail)
        return 1;
    bytes = s->thumbnail_size(s->context, thumbnail);
    if (bytes < 0 || !editor->add(editor->context, MM_SCENE_THUMBNAIL_CHUNK, (MMU32)bytes, &child, &writer))
        return 0;
    if (s->write_thumbnail(s->context, thumbnail, &writer) &&
        editor->adopt(editor->context, MM_SCENE_CHUNK, scene, MM_SCENE_THUMBNAIL_CHUNK, child, 0))
        return 1;
    editor->discard(editor->context, MM_SCENE_THUMBNAIL_CHUNK, child);
    return 0;
}

int mm_scene_store(MMU32 *number, const MMSceneStoreServices *s)
{
    const MMMovieArchiveEditor *editor = s->editor;
    const MMSceneTrack *live_frames = &s->owner->tracks[MM_SCENE_FRAMES];
    const MMSceneTrack *live_starts = &s->owner->tracks[MM_SCENE_STARTS];
    MMSceneTrack frames, starts;
    MMSceneTrackEvent event;
    const void *data;
    MMI32 i, bytes;
    MMU32 actor = 0, text = 0, *slot;
    int have_frames = 0, have_starts = 0;
    MMTag tag;
    unsigned char wire_tag[16];
    *number = 0xffffffffUL;
    if (!editor->add(editor->context, MM_SCENE_CHUNK, 0, number, NULL))
        goto fail;
    have_frames = mm_scene_track_init(&frames, &live_frames->storage.allocator, 0);
    if (!have_frames)
        goto fail;
    for (i = 0; (MMU32)i < live_frames->rows.count; ++i)
    {
        if (!mm_scene_track_get(live_frames, i, &event))
            goto fail;
        if (event.kind == MM_SCENE_PLAY_SOUND)
        {
            if (!store_sound(&frames, i, &event, *number, s))
                goto fail;
        }
        else if (event.kind == MM_SCENE_CHANGE_CAMERA || event.kind == MM_SCENE_PAUSE)
        {
            if (!copy_event(&frames, live_frames, i, &event, event.kind == MM_SCENE_PAUSE ? 8 : 4))
                goto fail;
        }
        else
            s->diagnostic(s->context, 0);
    }
    have_starts = mm_scene_track_init(&starts, &live_starts->storage.allocator, 0);
    if (!have_starts)
        goto fail;
    for (i = 0; (MMU32)i < live_starts->rows.count; ++i)
    {
        if (!mm_scene_track_get(live_starts, i, &event))
            goto fail;
        if (event.kind == MM_SCENE_ADD_ACTOR || event.kind == MM_SCENE_ADD_TEXT)
        {
            slot = event.kind == MM_SCENE_ADD_ACTOR ? &actor : &text;
            if (!store_object(s, *number, event.kind, *slot, live_starts, i) ||
                !mm_scene_track_insert(&starts, i, 4, slot, &event))
                goto fail;
            ++*slot;
        }
        else if (event.kind == MM_SCENE_SET_BACKGROUND)
        {
            data = mm_scene_track_payload(live_starts, i, &bytes);
            if (!data || bytes < (MMI32)sizeof(tag))
                goto fail;
            memcpy(&tag, data, sizeof(tag));
            if (!tag.sid && !mm_movie_archive_embed_tag(&s->assets, MM_SCENE_CHUNK, *number, &tag))
                goto fail;
            memcpy(wire_tag, &tag.sid, 4);
            memset(wire_tag + 4, 0, 4);
            memcpy(wire_tag + 8, &tag.type, 4);
            memcpy(wire_tag + 12, &tag.number, 4);
            if (!mm_scene_track_insert(&starts, i, 16, wire_tag, &event))
                goto fail;
        }
        else if (event.kind == MM_SCENE_CHANGE_CAMERA)
        {
            if (!copy_event(&starts, live_starts, i, &event, 4))
                goto fail;
        }
        else
            s->diagnostic(s->context, 1);
    }
    if (!mm_scene_archive_write_tracks(editor, *number, &frames, &starts) ||
        !store_thumbnail(s, *number) || !mm_scene_archive_write(editor, *number, s->metadata))
        goto fail;
    mm_scene_track_dispose(&frames);
    mm_scene_track_dispose(&starts);
    return 1;
fail:
    if (*number != 0xffffffffUL)
        editor->discard(editor->context, MM_SCENE_CHUNK, *number);
    if (have_starts)
        mm_scene_track_dispose(&starts);
    if (have_frames)
        mm_scene_track_dispose(&frames);
    return 0;
}
