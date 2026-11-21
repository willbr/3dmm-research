/* Scene loading and sound resolution converted from engine/scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/scene_load.h"
#include "mm/movie_sound.h"
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
    MMSceneSoundTag sound_tag;
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
            {
                mm_scene_sound_get_tag(sound, j, &sound_tag);
                ok = mm_tag_open(&sound_tag.tag, s->resources, s->source_file, s->tags);
                mm_scene_sound_put_tag(sound, j, &sound_tag);
                if (!ok)
                    break;
            }
            ok = j == sound->ctagc && mm_scene_track_insert(group, i,
                         mm_scene_sound_size(sound->ctagc), sound, &record);
            if (!ok)
                while (j > 0)
                {
                    mm_scene_sound_get_tag(sound, --j, &sound_tag);
                    mm_tag_close(&sound_tag.tag, s->tags);
                    mm_scene_sound_put_tag(sound, j, &sound_tag);
                }
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
                {
                    s->tags->retain(s->resources);
                    owner->owned[MM_SCENE_BACKGROUND_CACHE] = s->resources;
                }
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
void *mm_scene_load(void *movie, MMU32 number, const MMSceneLoadServices *s)
{
    MMMovieSessionScene header;
    const MMSceneOwnerServices *o = &s->owners;
    MMSceneOwner *owner;
    MMU32 child;
    void *scene;
    if (s->read.header(s->read.context, number, &header) != MM_MOVIE_OK)
        return NULL;
    scene = o->create_scene(o->context, movie);
    if (!scene)
        return NULL;
    owner = o->state(o->context, scene);
    owner->data.firstFrame = header.first_frame;
    owner->data.lastFrame = header.last_frame;
    owner->data.transition = header.transition;
    owner->data.currentFrame = header.first_frame - 1;
    if (s->read.child(s->read.context, number, 0, MM_SCENE_THUMBNAIL_CHUNK, &child))
        owner->owned[MM_SCENE_THUMBNAIL] = s->thumbnail(s->context, child);
    if (!load_group(scene, owner, number, 0, s) || !load_group(scene, owner, number, 1, s))
        goto unwind;
    mm_chunk_title_read(&owner->data.name, s->title_file, MM_SCENE_CHUNK, number, &s->titles);
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
    MMSceneSoundTag tag;
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
            if (!mm_scene_sound_span(sound, bytes))
                goto done;
            for (j = 0; j < sound->ctagc; ++j)
            {
                mm_scene_sound_get_tag(sound, j, &tag);
                if (!tag.tag.sid)
                {
                    const MMMovieSoundServices movie = s->movie_sound(s->context);
                    const int resolved = mm_movie_sound_resolve(&tag.tag, tag.child, scene, NULL, &movie);
                    mm_scene_sound_put_tag(sound, j, &tag);
                    if (!resolved)
                        goto done;
                }
            }
        }
    }
    ok = 1;
done:
    return mm_storage_unlock(&track->storage, 0) == MM_STORAGE_OK && ok;
}
