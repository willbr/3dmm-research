/* Scene chopping and snapshot undo converted from engine/scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/scene_chop.h"
#include <stddef.h>
static void *group(int slot, const MMSceneChopServices *s)
{
    return s->events.owner->owned[slot];
}
static MMI32 count(int slot, const MMSceneChopServices *s)
{
    return s->events.owners.count(s->events.owners.context, group(slot, s));
}
static int kind(MMI32 i, MMI32 *frame, const MMSceneChopServices *s)
{
    int k;
    s->events.event(s->events.context, group(MM_SCENE_FRAMES, s), i, frame, &k);
    return k;
}
static void erase(MMI32 i, const MMSceneChopServices *s)
{
    const MMSceneSoundServices *v = &s->events.owners.sound;
    MMI32 frame, j, n;
    void *payload;
    if (kind(i, &frame, s) == MM_SCENE_PLAY_SOUND)
    {
        payload = s->events.payload(s->events.context, group(MM_SCENE_FRAMES, s), i);
        n = v->count(v->context, payload);
        for (j = 0; j < n; ++j)
        {
            payload = s->events.payload(s->events.context, group(MM_SCENE_FRAMES, s), i);
            v->close(v->context, payload, j);
        }
    }
    s->erase_frame(s->context, i);
}
int mm_scene_chop(int back, const MMSceneChopServices *s)
{
    MMSceneState *d = s->data;
    MMSceneOwner *owner = s->events.owner;
    MMSceneRuntime *r = s->events.runtime;
    const MMSceneOwnerServices *o = &s->events.owners;
    MMI32 i, first, last, frame;
    void *p;
    int k, copy_camera = 1;
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
        while (r->next_event < count(MM_SCENE_FRAMES, s))
            erase(r->next_event, s);
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
            k = kind(r->next_event, &frame, s);
            if (k == MM_SCENE_CHANGE_CAMERA && copy_camera)
            {
                copy_camera = 0;
                s->set_frame(s->context, r->next_event, d->currentFrame);
            }
            else if (frame != d->currentFrame)
                erase(r->next_event, s);
        }
        for (r->next_event = 0; r->next_event < count(MM_SCENE_FRAMES, s); ++r->next_event)
        {
            kind(r->next_event, &frame, s);
            if (frame != d->currentFrame)
                break;
        }
        if (d->firstFrame != d->currentFrame)
        {
            d->firstFrame = d->currentFrame;
            s->dirty(s->context);
        }
        if (group(MM_SCENE_BACKGROUND_SOUND, s) && r->background_frame < d->firstFrame)
        {
            mm_scene_owner_drop(s->events.owner, s->events.scene, MM_SCENE_BACKGROUND_SOUND, o);
            s->discard_background_frame(s->context);
        }
    }
    return 1;
}
int mm_scene_chop_edit(int back, const MMSceneChopServices *s)
{
    void *u;
    if (s->data->currentFrame == (back ? s->data->firstFrame : s->data->lastFrame))
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
    void *file = s->new_file(s->context);
    int ok;
    if (!file)
        return 0;
    p->resource = s->new_resource(s->context, file);
    s->release(s->context, file);
    if (!p->resource)
        return 0;
    s->begin(s->context);
    ok = s->write(s->context, scene, p->resource, &p->number);
    s->end(s->context);
    return ok;
}
int mm_scene_chop_swap(MMSceneChopUndo *p, MMI32 index, MMI32 frame,
                             const MMSceneSnapshotServices *s)
{
    void *loaded, *old;
    s->begin(s->context);
    loaded = s->read(s->context, p->resource, p->number);
    if (!loaded)
        goto fail;
    if (!s->start(s->context, loaded) || !s->switch_scene(s->context, index))
    {
        s->close(s->context, loaded);
        goto fail;
    }
    old = s->current(s->context);
    s->retain(s->context, old);
    if (!s->insert(s->context, index, loaded))
    {
        s->release(s->context, old);
        s->close(s->context, loaded);
        goto fail;
    }
    s->close(s->context, loaded);
    s->delete_chunk(s->context, p->resource, p->number);
    s->rewind(s->context, old);
    if (!s->write(s->context, old, p->resource, &p->number))
    {
        s->remove(s->context, index + 1);
        s->close(s->context, old);
        goto fail;
    }
    s->close(s->context, old);
    if (!s->remove(s->context, index + 1))
        goto fail;
    if (s->switch_scene(s->context, index))
    {
        s->seek(s->context, frame);
        s->invalidate(s->context);
    }
    s->end(s->context);
    return 1;
fail:
    s->flush(s->context);
    s->clear(s->context);
    s->end(s->context);
    return 0;
}
void mm_scene_chop_dispose(MMSceneChopUndo *p, const MMSceneSnapshotServices *s)
{
    if (p->resource)
    {
        s->release(s->context, p->resource);
        p->resource = NULL;
    }
}
