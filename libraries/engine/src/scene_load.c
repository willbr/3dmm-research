/* Scene loading and sound resolution converted from engine/scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/scene_load.h"
#include "mm/text_store.h"

static int load_group(void *scene, MMSceneOwner *owner, MMU32 number, int start, const MMSceneLoadServices *s)
{
    const MMSceneOwnerServices *o = &s->owners;
    const MMSceneSoundPacketServices *v = &s->packets;
    const MMMovieSceneReader *read = &s->read;
    const unsigned short endian = 1;
    const int native_big = *(const unsigned char *)&endian == 0;
    MMMovieSceneGroup wire;
    MMMovieSceneEvent event;
    MMSceneTrackEvent record;
    MMSceneTrack *group = &owner->tracks[start ? MM_SCENE_STARTS : MM_SCENE_FRAMES];
    void *object;
    MMSceneSound *sound;
    MMTag tag;
    MMU32 i, child;
    MMI32 j;
    int ok;
    if (read->group(read->context, number, start, &wire) != MM_MOVIE_OK)
        return 0;
    for (i = 0; i < wire.count; ++i)
    {
        read->event(read->context, &wire, i, &event);
        record.frame = event.frame;
        record.kind = event.kind;
        if (!start && event.kind == MM_SCENE_PLAY_SOUND)
        {
            sound = mm_scene_sound_decode(event.data, event.bytes, event.big_endian != native_big, v);
            if (!sound)
                goto fail;
            for (j = 0; j < sound->ctagc; ++j)
                if (!s->open_tag(s->context, sound, j))
                    break;
            ok = j == sound->ctagc && mm_scene_track_insert(group, i,
                         mm_scene_sound_size(sound->ctagc, v->tag_bytes), sound, &record);
            if (!ok)
                while (j > 0)
                    o->sound.close(o->sound.context, sound, --j);
            o->sound.free(o->sound.context, sound);
            if (!ok)
                goto fail;
        }
        else if (start && (event.kind == MM_SCENE_ADD_ACTOR || event.kind == MM_SCENE_ADD_TEXT))
        {
            if (!read->child(read->context, number, mm_movie_scene_event_word(&event, 0),
                             event.kind == MM_SCENE_ADD_ACTOR ? MM_SCENE_ACTOR_CHUNK : MM_TEXT_BOX, &child))
                goto fail;
            object = s->object(s->context, child, event.kind, scene);
            if (!object)
                goto fail;
            if (!mm_scene_track_insert(group, i, sizeof(object), &object, &record))
            {
                o->release(o->context, object);
                goto fail;
            }
        }
        else if (start && event.kind == MM_SCENE_SET_BACKGROUND)
        {
            mm_movie_scene_event_tag(&event, &tag);
            if (!tag.sid)
            {
                if (!owner->owned[MM_SCENE_BACKGROUND_CACHE])
                    owner->owned[MM_SCENE_BACKGROUND_CACHE] = s->retain_background_cache(s->context);
                tag.cache = owner->owned[MM_SCENE_BACKGROUND_CACHE];
            }
            if (!mm_scene_track_insert(group, i, sizeof(tag), &tag, &record))
                goto fail;
        }
        else if (event.kind == MM_SCENE_CHANGE_CAMERA || (!start && event.kind == MM_SCENE_PAUSE))
        {
            if (!mm_scene_track_insert(group, i, event.bytes, event.data, &record))
                goto fail;
        }
        else
        {
            o->diagnostic(o->context, start ? MM_SCENE_BAD_START : MM_SCENE_BAD_FRAME);
            goto fail; /* Validated wire groups cannot contain these kinds. */
        }
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
    MMU32 child;
    void *scene;
    if (!mm_scene_init(&metadata, unit) || s->read.header(s->read.context, number, &header) != MM_MOVIE_OK)
        return NULL;
    scene = o->create_scene(o->context, movie);
    if (!scene)
        return NULL;
    owner = o->state(o->context, scene);
    metadata.firstFrame = header.first_frame;
    metadata.lastFrame = header.last_frame;
    metadata.transition = header.transition;
    metadata.currentFrame = metadata.firstFrame - 1;
    s->publish_metadata(s->context, scene, &metadata);
    if (s->read.child(s->read.context, number, 0, MM_SCENE_THUMBNAIL_CHUNK, &child))
        owner->owned[MM_SCENE_THUMBNAIL] = s->thumbnail(s->context, child);
    if (!load_group(scene, owner, number, 0, s) || !load_group(scene, owner, number, 1, s))
        goto unwind;
    s->load_name(s->context, scene, number);
    return scene;
unwind:
    mm_scene_owner_unwind(owner, o);
    o->release(o->context, scene);
    return NULL;
}
int mm_scene_resolve_sounds(MMU32 scene, const MMSceneResolveServices *s)
{
    MMSceneTrack *track = &s->owner->tracks[MM_SCENE_FRAMES];
    MMSceneTrackEvent event;
    void *entry;
    MMI32 i, j, bytes = 0, count = mm_scene_member_count(s->owner, MM_SCENE_ACTORS);
    MMSceneSound *sound;
    int ok = 0;
    for (i = 0; i < count; ++i)
        if (!s->actor(s->context, mm_scene_member_at(s->owner, MM_SCENE_ACTORS, i), scene))
            return 0;
    if (mm_storage_lock(&track->storage, 0) != MM_STORAGE_OK)
        return 0;
    count = track->rows.count;
    for (i = 0; i < count; ++i)
    {
        if (!mm_scene_track_get(track, i, &event))
            goto done;
        if (event.kind == MM_SCENE_PLAY_SOUND)
        {
            sound = (MMSceneSound *)mm_scene_track_payload(track, i, &bytes);
            if (!mm_scene_sound_span(sound, bytes, s->packets.tag_bytes))
                goto done;
            for (j = 0; j < sound->ctagc; ++j)
            {
                entry = (unsigned char *)sound + MM_SCENE_SOUND_HEADER_BYTES + (size_t)j * s->packets.tag_bytes;
                if (s->packets.sid(s->packets.context, entry) == 0 && !s->resolve(s->context, entry, scene))
                    goto done;
            }
        }
    }
    ok = 1;
done:
    return mm_storage_unlock(&track->storage, 0) == MM_STORAGE_OK && ok;
}
