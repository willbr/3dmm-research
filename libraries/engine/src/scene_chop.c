/* Scene chopping and snapshot undo converted from engine/scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/scene_chop.h"
#include <stddef.h>
int mm_scene_chop(int back, const MMSceneChopServices *s)
{
    MMSceneOwner *owner = s->events.owner;
    MMSceneState *d = &owner->data;
    MMSceneTrack *frames = &owner->tracks[MM_SCENE_FRAMES];
    MMSceneTrackEvent event;
    MMSceneRuntime *r = &owner->runtime;
    const MMSceneOwnerServices *o = &s->events.owners;
    MMI32 i, first, last;
    void *p;
    int copy_camera = 1;
    if (d->currentFrame == (back ? d->firstFrame : d->lastFrame))
        return 1;
    for (i = 0; i < mm_scene_member_count(owner, MM_SCENE_ACTORS);)
    {
        p = mm_scene_member_at(owner, MM_SCENE_ACTORS, i);
        if (!s->trim_actor(s->context, p, back))
        {
            s->remove_actor(s->context, p);
            if (i < mm_scene_member_count(owner, MM_SCENE_ACTORS) && mm_scene_member_at(owner, MM_SCENE_ACTORS, i) == p)
                ++i;
        }
        else
            ++i;
    }
    for (i = 0; i < mm_scene_member_count(owner, MM_SCENE_TEXTS);)
    {
        p = mm_scene_member_at(owner, MM_SCENE_TEXTS, i);
        if (!s->lifetime(s->context, p, &first, &last))
        {
            s->bad_lifetime(s->context);
            ++i;
            continue;
        }
        if ((back && last < d->currentFrame) || (!back && first > d->currentFrame))
        {
            s->remove_text(s->context, p);
            if (i < mm_scene_member_count(owner, MM_SCENE_TEXTS) && mm_scene_member_at(owner, MM_SCENE_TEXTS, i) == p)
                ++i;
        }
        else
        {
            if (back)
            {
                if (first < d->currentFrame)
                    s->start_text(s->context, p, d->currentFrame);
            }
            else if (last >= d->currentFrame && s->events.seek(s->events.context, 1, p, 2147483646L))
            {
                s->show_text(s->context, p);
                s->events.seek(s->events.context, 1, p, d->currentFrame);
            }
            ++i;
        }
    }
    if (!back)
    {
        while ((MMU32)r->next_event < frames->rows.count)
            if (!mm_scene_owner_erase_frame(owner, r->next_event, o))
                return 0;
        if (d->lastFrame != d->currentFrame)
        {
            d->lastFrame = d->currentFrame;
            s->dirty(s->context);
        }
    }
    else
    {
        while (r->next_event > 0)
        {
            --r->next_event;
            if (!mm_scene_track_get(frames, r->next_event, &event))
                return 0;
            if (event.kind == MM_SCENE_CHANGE_CAMERA && copy_camera)
            {
                copy_camera = 0;
                event.frame = d->currentFrame;
                if (!mm_scene_track_set(frames, r->next_event, &event))
                    return 0;
            }
            else if (event.frame != d->currentFrame && !mm_scene_owner_erase_frame(owner, r->next_event, o))
                return 0;
        }
        for (r->next_event = 0; (MMU32)r->next_event < frames->rows.count; ++r->next_event)
            if (!mm_scene_track_get(frames, r->next_event, &event) || event.frame != d->currentFrame)
                break;
        if (d->firstFrame != d->currentFrame)
        {
            d->firstFrame = d->currentFrame;
            s->dirty(s->context);
        }
        if (owner->owned[MM_SCENE_BACKGROUND_SOUND] && r->background_frame < d->firstFrame)
        {
            mm_scene_owner_drop(owner, MM_SCENE_BACKGROUND_SOUND, o);
            s->discard_background_frame(s->context);
        }
    }
    return 1;
}
int mm_scene_chop_edit(int back, const MMSceneChopServices *s)
{
    const MMSceneState *d = &s->events.owner->data;
    void *u;
    if (d->currentFrame == (back ? d->firstFrame : d->lastFrame))
        return 1;
    u = s->new_undo(s->context);
    if (!u || !s->save_undo(s->context, u) || !s->commit_undo(s->context, u))
    {
        if (u)
            s->release_undo(s->context, u);
        return 0;
    }
    s->release_undo(s->context, u);
    if (!mm_scene_chop(back, s))
    {
        s->clear_undo(s->context);
        return 0;
    }
    return 1;
}
int mm_scene_chop_save(MMSceneChopUndo *p, void *scene, const MMSceneSnapshotServices *s)
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
int mm_scene_chop_swap(MMSceneChopUndo *p, MMI32 index, MMI32 frame,
                       const MMSceneSnapshotServices *s, const MMMovieSceneServices *scenes)
{
    MMArchiveWriter writer;
    void *loaded, *old;
    s->begin(s->context);
    loaded = s->read(s->context, p->archive, p->number);
    if (!loaded)
        goto fail;
    if (!scenes->start(loaded) || !mm_movie_scene_switch(index, scenes))
    {
        scenes->close(loaded);
        goto fail;
    }
    old = scenes->state->open;
    scenes->retain(old);
    if (!mm_movie_scene_insert(index, loaded, scenes))
    {
        scenes->release(old);
        scenes->close(loaded);
        goto fail;
    }
    scenes->close(loaded);
    writer = mm_private_archive_writer(p->archive);
    if (mm_chunk_delete(&writer.edit, MM_SCENE_CHUNK, p->number) != MM_CHUNK_OK && s->delete_failed)
        s->delete_failed(s->context);
    s->rewind(s->context, old);
    if (!s->write(s->context, old, p->archive, &p->number))
    {
        mm_movie_scene_remove(index + 1, scenes);
        scenes->close(old);
        goto fail;
    }
    scenes->close(old);
    if (!mm_movie_scene_remove(index + 1, scenes))
        goto fail;
    if (mm_movie_scene_switch(index, scenes))
    {
        s->seek(scenes->state->open, frame);
        scenes->invalidate(scenes->context);
    }
    s->end(s->context);
    return 1;
fail:
    scenes->flush_sound(scenes->context);
    scenes->clear_undo(scenes->context);
    s->end(s->context);
    return 0;
}
void mm_scene_chop_dispose(MMSceneChopUndo *p)
{
    mm_private_archive_close(p->archive);
    p->archive = NULL;
}
