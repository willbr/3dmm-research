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
    MMChunkChild child = {MM_SOUND_CHUNK, 0, 0};
    MMSceneSoundTag tag;
    MMU32 match;
    MMI32 i, bytes;
    void *wire;
    int ok = 0;
    if (!sound)
        return 0;
    for (i = 0; i < sound->ctagc; ++i)
    {
        mm_scene_sound_get_tag(sound, i, &tag);
        if (tag.tag.sid != 0)
            continue;
        /* A previous adoption can shift rows or add this slot to the scene. */
        chunks = editor->chunks(editor->context);
        if (mm_chunk_link_number(&chunks, MM_SCENE_CHUNK, scene, tag.child, MM_SOUND_CHUNK, &match) == MM_CHUNK_OK ||
            mm_chunk_find(&chunks, MM_SOUND_CHUNK, tag.tag.number, &match) != MM_CHUNK_OK)
            continue;
        child.number = tag.tag.number;
        child.slot = tag.child;
        if (mm_chunk_adopt(&editor->edit, MM_SCENE_CHUNK, scene, &child, 1) != MM_CHUNK_OK)
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
    MMChunkChild child;
    MMI32 bytes;
    const void *data;
    void *object;
    child.type = kind == MM_SCENE_ADD_ACTOR ? MM_SCENE_ACTOR_CHUNK : MM_TEXT_BOX;
    child.slot = slot;
    if (!editor->add(editor->context, child.type, 0, &child.number, NULL))
        return 0;
    if (mm_chunk_adopt(&editor->edit, MM_SCENE_CHUNK, scene, &child, 1) != MM_CHUNK_OK)
    {
        mm_chunk_delete(&editor->edit, child.type, child.number);
        return 0;
    }
    data = mm_scene_track_payload(starts, index, &bytes);
    if (!data || bytes < (MMI32)sizeof(object))
        return 0;
    memcpy(&object, data, sizeof(object));
    return s->write_object(s->context, object, kind, child.number, scene);
}

static int store_thumbnail(const MMSceneStoreServices *s, MMU32 scene)
{
    const MMMovieArchiveEditor *editor = s->editor;
    MMStorageWriter writer;
    void *thumbnail;
    MMChunkChild child = {MM_SCENE_THUMBNAIL_CHUNK, 0, 0};
    MMI32 bytes;
    s->update_thumbnail(s->context);
    thumbnail = s->owner->owned[MM_SCENE_THUMBNAIL];
    if (!thumbnail)
        return 1;
    bytes = s->thumbnail_size(s->context, thumbnail);
    if (bytes < 0 || !editor->add(editor->context, child.type, (MMU32)bytes, &child.number, &writer))
        return 0;
    if (s->write_thumbnail(s->context, thumbnail, &writer) &&
        mm_chunk_adopt(&editor->edit, MM_SCENE_CHUNK, scene, &child, 1) == MM_CHUNK_OK)
        return 1;
    mm_chunk_delete(&editor->edit, child.type, child.number);
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
        !store_thumbnail(s, *number) || !mm_scene_archive_write(editor, *number, &s->owner->data))
        goto fail;
    mm_scene_track_dispose(&frames);
    mm_scene_track_dispose(&starts);
    return 1;
fail:
    if (*number != 0xffffffffUL)
        mm_chunk_delete(&editor->edit, MM_SCENE_CHUNK, *number);
    if (have_starts)
        mm_scene_track_dispose(&starts);
    if (have_frames)
        mm_scene_track_dispose(&frames);
    return 0;
}
