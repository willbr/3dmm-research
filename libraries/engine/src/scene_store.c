/* Scene tag collection and saving converted from engine/scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/scene_store.h"
#include "mm/actor_archive.h"
#include "mm/background_resource.h"
#include "mm/chunk_copy.h"
#include "mm/material_resource.h"
#include "mm/scene_data.h"
#include "mm/movie_sound.h"
#include "mm/private_archive.h"
#include "mm/scene_snapshot.h"
#include "mm/template_create.h"
#include "mm/text_store.h"
#include <string.h>

int mm_scene_snapshot_save(MMSceneSnapshot *p, void *scene, const MMSceneSnapshotServices *s)
{
    int ok;
    p->archive = s->open(s->context);
    if (!p->archive)
        return 0;
    s->begin(s->context);
    ok = s->write(s->context, scene, p->archive, &p->number);
    s->end(s->context);
    return ok;
}

void mm_scene_snapshot_dispose(MMSceneSnapshot *p)
{
    mm_private_archive_close(p->archive);
    p->archive = NULL;
}

typedef struct SnapshotLinks
{
    const MMChunkAccess *source;
    const MMMovieArchiveEditor *editor;
    MMU32 owner_type, original, copied;
} SnapshotLinks;

static int map_snapshot_tag(void *p, const MMTag *tag, MMU32 *number, int *created)
{
    const SnapshotLinks *links = p;
    const MMChunkAccess destination = mm_chunk_edit_access(&links->editor->edit);
    MMU32 row, i;
    MMChunkChild child;
    if (mm_chunk_find(links->source, links->owner_type, links->original, &row) != MM_CHUNK_OK)
        return 0;
    for (i = 0; mm_chunk_child_at(links->source, row, i, &child) == MM_CHUNK_OK; ++i)
        if (child.type == tag->type && child.number == tag->number)
        {
            *created = 0;
            return mm_chunk_link_number(&destination, links->owner_type, links->copied, child.slot, child.type,
                                        number) == MM_CHUNK_OK;
        }
    return 0;
}

static int reuse_snapshot_asset(MMU32 type)
{
    return type == MM_BACKGROUND || type == MM_MATERIAL_CHUNK || type == MM_SOUND_CHUNK || type == MM_TMPL;
}

static int rebind_snapshot(const MMChunkAccess *source, MMU32 original, MMU32 scene,
                           const MMMovieArchiveEditor *editor)
{
    SnapshotLinks links;
    MMMovieEmbeddedArchive assets = {0};
    MMChunkChild child;
    MMChunkEntry entry;
    MMU32 row, i;
    links.source = source;
    links.editor = editor;
    links.owner_type = MM_SCENE_CHUNK;
    links.original = original;
    links.copied = scene;
    assets.context = &links;
    assets.copy_reference = map_snapshot_tag;
    assets.edit = editor->edit;
    if (!mm_movie_archive_rebind_backgrounds(editor, &assets, scene) ||
        !mm_movie_archive_rebind_scene_sounds(editor, scene) ||
        mm_chunk_find(source, MM_SCENE_CHUNK, original, &row) != MM_CHUNK_OK ||
        mm_chunk_at(source, row, &entry) != MM_CHUNK_OK)
        return 0;
    links.owner_type = MM_ACTOR_CHUNK;
    for (i = 0; i < entry.child_count; ++i)
    {
        MMChunkAccess current;
        if (mm_chunk_child_at(source, row, i, &child) != MM_CHUNK_OK)
            return 0;
        if (child.type != MM_ACTOR_CHUNK)
            continue;
        current = mm_chunk_edit_access(&editor->edit);
        links.original = child.number;
        if (mm_chunk_link_number(&current, MM_SCENE_CHUNK, scene, child.slot, child.type, &links.copied) != MM_CHUNK_OK ||
            !mm_actor_archive_rebind_snapshot(editor, scene, links.copied, &assets))
            return 0;
    }
    return 1;
}

int mm_scene_snapshot_copy(const MMSceneSnapshot *p, const MMChunkEndpoint *destination,
                           const MMMovieArchiveEditor *editor, MMU32 *number)
{
    MMChunkFileCopy binding;
    MMChunkEndpoint source;
    MMChunkAccess chunks;
    MMU32 copied;
    if (!p->archive)
        return 0;
    source = mm_private_archive_copy_endpoint(p->archive, &binding);
    if (mm_chunk_copy_filtered(&source, destination, MM_SCENE_CHUNK, p->number, reuse_snapshot_asset, &copied) != MM_CHUNK_OK)
        return 0;
    chunks = mm_chunk_edit_access(&source.edit);
    if (!rebind_snapshot(&chunks, p->number, copied, editor))
    {
        mm_chunk_delete(&editor->edit, MM_SCENE_CHUNK, copied);
        return 0;
    }
    *number = copied;
    return 1;
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
    MMSceneSound *sound = mm_scene_sound_track(&s->owner->tracks[MM_SCENE_FRAMES], index, v);
    MMSceneSoundTag tag;
    MMI32 i, bytes;
    unsigned char *wire;
    int ok = 0;
    if (!sound)
        return 0;
    wire = mm_scene_sound_encode(sound, &bytes, v);
    if (!wire)
        goto done;
    for (i = 0; i < sound->ctagc; ++i)
    {
        const size_t end = MM_SCENE_SOUND_HEADER_BYTES + ((size_t)i + 1) * MM_SCENE_SOUND_WIRE_TAG_BYTES;
        mm_scene_sound_get_tag(sound, i, &tag);
        if (!mm_movie_archive_embed_sound(&s->assets, scene, tag.child, &tag.tag))
            goto done;
        memcpy(wire + end - sizeof(tag.tag.number), &tag.tag.number, sizeof(tag.tag.number));
    }
    ok = mm_scene_track_insert(frames, index, bytes, wire, event);
done:
    if (wire)
        s->free_sound_bytes(s->context, wire);
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
