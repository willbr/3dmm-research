/* Scene actor/caption editing converted from engine/scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/scene_content.h"
#include "mm/wide.h"
static int insert_start(MMSceneTrack *track, MMI32 index, int kind, void *object)
{
    MMSceneTrackEvent event;
    event.frame = 0;
    event.kind = kind;
    return mm_scene_track_insert(track, index, sizeof(object), &object, &event);
}
static void *start_object(const MMSceneTrack *track, MMI32 index, int kind)
{
    MMSceneTrackEvent event;
    void *object;
    if (!mm_scene_track_get(track, index, &event) || event.kind != kind ||
        !mm_scene_track_get_object(track, index, &object))
        return NULL;
    return object;
}
static void release(void *p, const MMSceneContentServices *s)
{
    s->events.owners.release(s->events.owners.context, p);
}
static void remove_extras(MMI32 id, const MMSceneContentServices *s)
{
    MMSceneOwner *owner = s->events.owner;
    MMI32 i;
    void *entry;
    for (i = mm_scene_member_count(owner, MM_SCENE_SELECTION) - 1; i >= 0; --i)
    {
        entry = mm_scene_member_at(owner, MM_SCENE_SELECTION, i);
        if (entry && s->actor_id(s->context, entry) == id)
            mm_scene_member_remove(owner, MM_SCENE_SELECTION, i);
    }
}
void *mm_scene_actor_find(MMI32 id, const MMSceneContentServices *s)
{
    MMI32 i;
    void *p;
    for (i = 0; i < mm_scene_member_count(s->events.owner, MM_SCENE_ACTORS); ++i)
    {
        p = mm_scene_member_at(s->events.owner, MM_SCENE_ACTORS, i);
        if (s->actor_id(s->context, p) == id)
            return p;
    }
    return NULL;
}
int mm_scene_actor_add(void *actor, const MMSceneContentServices *s)
{
    MMSceneTrack *track = &s->events.owner->tracks[MM_SCENE_STARTS];
    MMI32 i, j;
    void *old;
    int ok;
    for (i = 0; (MMU32)i < track->rows.count; ++i)
    {
        old = start_object(track, i, MM_SCENE_ADD_ACTOR);
        if (!old || s->actor_id(s->context, old) != s->actor_id(s->context, actor))
            continue;
        for (j = 0; j < mm_scene_member_count(s->events.owner, MM_SCENE_ACTORS); ++j)
        {
            old = mm_scene_member_at(s->events.owner, MM_SCENE_ACTORS, j);
            if (s->actor_id(s->context, old) != s->actor_id(s->context, actor))
                continue;
            s->retain(s->context, actor);
            s->events.bind_actor(s->events.context, actor);
            if (!s->events.seek(s->events.context, 0, actor, s->events.data->currentFrame) ||
                !s->actor_ready(s->context, actor) ||
                !mm_scene_track_put(track, i, sizeof(actor), &actor))
            {
                release(actor, s);
                return 0;
            }
            mm_scene_member_replace(s->events.owner, MM_SCENE_ACTORS, j, actor);
            s->select(s->context, 0, actor);
            remove_extras(s->actor_id(s->context, old), s);
            s->hide_actor(s->context, old);
            release(old, s);
            s->invalidate(s->context);
            s->dirty(s->context);
            s->indicator(s->context);
            return 1;
        }
        s->diagnostic(s->context, MM_SCENE_CONTENT_ACTOR_ROLL_MISSING);
    }
    if (!insert_start(track, 0, MM_SCENE_ADD_ACTOR, actor))
        return 0;
    s->retain(s->context, actor);
    if (!s->add_roll(s->context, actor, 1))
    {
        mm_group_delete(&track->rows, 0);
        release(actor, s);
        return 0;
    }
    ok = mm_scene_play_event(0, MM_SCENE_ADD_ACTOR, &actor, s->events.runtime->disabled, &s->events);
    if (ok && !s->actor_ready(s->context, actor))
    {
        mm_scene_member_remove(s->events.owner, MM_SCENE_ACTORS,
                               mm_scene_member_count(s->events.owner, MM_SCENE_ACTORS) - 1);
        ok = 0;
    }
    if (!ok)
    {
        s->remove_roll(s->context, actor, 0);
        mm_group_delete(&track->rows, 0);
        release(actor, s);
        return 0;
    }
    s->invalidate(s->context);
    s->dirty(s->context);
    s->indicator(s->context);
    return 1;
}
int mm_scene_actor_add_onstage(void *actor, const MMSceneContentServices *s)
{
    if (!mm_scene_actor_add(actor, s))
        return 0;
    if (!s->actor_onstage(s->context, actor))
    {
        mm_scene_actor_remove(s->actor_id(s->context, actor), s);
        return 0;
    }
    s->select(s->context, 0, actor);
    return 1;
}
void mm_scene_actor_remove(MMI32 id, const MMSceneContentServices *s)
{
    MMSceneTrack *track = &s->events.owner->tracks[MM_SCENE_STARTS];
    MMI32 i, j;
    void *actor, *removed;
    for (i = 0; (MMU32)i < track->rows.count; ++i)
    {
        actor = start_object(track, i, MM_SCENE_ADD_ACTOR);
        if (!actor || s->actor_id(s->context, actor) != id)
            continue;
        removed = actor;
        if (mm_group_delete(&track->rows, i) != MM_STORAGE_OK)
            return;
        for (j = 0; j < mm_scene_member_count(s->events.owner, MM_SCENE_ACTORS); ++j)
        {
            actor = mm_scene_member_at(s->events.owner, MM_SCENE_ACTORS, j);
            if (s->actor_id(s->context, actor) != id)
                continue;
            mm_scene_member_remove(s->events.owner, MM_SCENE_ACTORS, j);
            if (s->selected(s->context, 0) == actor)
            {
                s->set_selected(s->context, 0, NULL);
                s->end_placement(s->context);
            }
            remove_extras(id, s);
            s->remove_roll(s->context, actor, 0);
            s->hide_actor(s->context, actor);
            release(actor, s);
            s->dirty(s->context);
            s->indicator(s->context);
            return;
        }
        s->diagnostic(s->context, MM_SCENE_CONTENT_ACTOR_ROLL_MISSING);
        release(removed, s);
        s->dirty(s->context);
        return;
    }
    s->diagnostic(s->context, MM_SCENE_CONTENT_ACTOR_MISSING);
}
int mm_scene_actor_remove_undo(MMI32 id, const MMSceneContentServices *s)
{
    void *actor = mm_scene_actor_find(id, s), *u;
    MMSceneActorUndo *p;
    if (!actor)
    {
        s->diagnostic(s->context, MM_SCENE_CONTENT_ACTOR_MISSING);
        return 0;
    }
    s->retain(s->context, actor);
    u = s->new_undo(s->context, 0);
    if (!u)
    {
        release(actor, s);
        return 0;
    }
    p = s->actor_state(s->context, u);
    p->actor = actor;
    p->operation = MM_SCENE_ACTOR_DELETE;
    if (!s->commit_undo(s->context, u))
    {
        s->release_undo(s->context, u);
        return 0;
    }
    s->release_undo(s->context, u);
    mm_scene_actor_remove(id, s);
    return 1;
}
int mm_scene_actor_paste(void *actor, const MMSceneContentServices *s)
{
    if (!s->actor_paste(s->context, actor, s->events.data->currentFrame) || !mm_scene_actor_add(actor, s))
        return 0;
    s->invalidate(s->context);
    if (!s->events.seek(s->events.context, 0, actor, s->events.data->currentFrame))
    {
        mm_scene_actor_remove(s->actor_id(s->context, actor), s);
        return 0;
    }
    s->dirty(s->context);
    s->update_roll(s->context);
    s->indicator(s->context);
    return 1;
}
int mm_scene_roll_add(const MMSceneContentServices *s)
{
    MMI32 i;
    for (i = 0; i < mm_scene_member_count(s->events.owner, MM_SCENE_ACTORS); ++i)
        if (!s->add_roll(s->context, mm_scene_member_at(s->events.owner, MM_SCENE_ACTORS, i), 0))
        {
            while (i > 0)
                s->remove_roll(s->context, mm_scene_member_at(s->events.owner, MM_SCENE_ACTORS, --i), 0);
            return 0;
        }
    return 1;
}
void mm_scene_roll_remove(int only, const MMSceneContentServices *s)
{
    MMI32 i;
    for (i = 0; i < mm_scene_member_count(s->events.owner, MM_SCENE_ACTORS); ++i)
        s->remove_roll(s->context, mm_scene_member_at(s->events.owner, MM_SCENE_ACTORS, i), only);
}
void *mm_scene_text_at(MMI32 i, const MMSceneContentServices *s)
{
    return mm_scene_member_at(s->events.owner, MM_SCENE_TEXTS, i);
}
int mm_scene_text_add(void *text, const MMSceneContentServices *s)
{
    MMSceneTrack *track = &s->events.owner->tracks[MM_SCENE_STARTS];
    MMI32 index = track->rows.count;
    s->bind_text(s->context, text);
    if (!insert_start(track, index, MM_SCENE_ADD_TEXT, text))
        return 0;
    s->retain(s->context, text);
    if (!mm_scene_play_event(0, MM_SCENE_ADD_TEXT, &text, s->events.runtime->disabled, &s->events))
    {
        mm_group_delete(&track->rows, index);
        release(text, s);
        return 0;
    }
    s->dirty(s->context);
    return 1;
}
int mm_scene_text_remove(void *text, const MMSceneContentServices *s)
{
    MMSceneTrack *track = &s->events.owner->tracks[MM_SCENE_STARTS];
    MMI32 i, j, first, last;
    if (!text)
    {
        s->diagnostic(s->context, MM_SCENE_CONTENT_TEXT_MISSING);
        return 0;
    }
    if (s->selected(s->context, 1) == text)
    {
        s->set_selected(s->context, 1, NULL);
        s->text_select(s->context, text, 0);
    }
    for (i = 0; (MMU32)i < track->rows.count; ++i)
    {
        if (start_object(track, i, MM_SCENE_ADD_TEXT) != text)
            continue;
        if (mm_group_delete(&track->rows, i) != MM_STORAGE_OK)
            return 0;
        for (j = 0; j < mm_scene_member_count(s->events.owner, MM_SCENE_TEXTS); ++j)
            if (mm_scene_member_at(s->events.owner, MM_SCENE_TEXTS, j) == text)
            {
                mm_scene_member_remove(s->events.owner, MM_SCENE_TEXTS, j);
                if (s->text_lifetime(s->context, text, &first, &last) &&
                    !s->events.seek(s->events.context, 1, text, mm_signed_word((MMU32)first - 1U)))
                    s->diagnostic(s->context, MM_SCENE_CONTENT_TEXT_HIDE_FAILED);
                release(text, s);
                s->dirty(s->context);
                return 1;
            }
        s->diagnostic(s->context, MM_SCENE_CONTENT_TEXT_ROLL_MISSING);
        release(text, s);
        return 0;
    }
    s->diagnostic(s->context, MM_SCENE_CONTENT_TEXT_MISSING);
    return 0;
}
static int text_snapshot(void *text, void *u, int add, const MMSceneContentServices *s)
{
    MMSceneTextUndo *p = s->text_state(s->context, u);
    MMI32 first, last;
    s->retain(s->context, text);
    p->text = text;
    p->add = add;
    if (!s->text_lifetime(s->context, text, &first, &last))
    {
        s->diagnostic(s->context, MM_SCENE_CONTENT_LIFETIME);
        return 0;
    }
    p->first = first;
    p->last = last == s->events.data->lastFrame ? 2147483647L : last;
    return 1;
}
int mm_scene_text_add_undo(void *text, const MMSceneContentServices *s)
{
    void *u = s->new_undo(s->context, 1);
    MMSceneTextUndo *p;
    MMI32 i;
    if (!u)
        return 0;
    if (!mm_scene_text_add(text, s) || !text_snapshot(text, u, 1, s))
    {
        s->release_undo(s->context, u);
        return 0;
    }
    p = s->text_state(s->context, u);
    for (i = 0; i < mm_scene_member_count(s->events.owner, MM_SCENE_TEXTS); ++i)
        if (mm_scene_member_at(s->events.owner, MM_SCENE_TEXTS, i) == text)
        {
            p->index = i;
            break;
        }
    if (!s->commit_undo(s->context, u))
    {
        s->not_undoable(s->context);
        s->release_undo(s->context, u);
        return 0;
    }
    s->release_undo(s->context, u);
    return 1;
}
int mm_scene_text_remove_undo(void *text, const MMSceneContentServices *s)
{
    void *u = s->new_undo(s->context, 1);
    if (!u)
        return 0;
    if (!text_snapshot(text, u, 0, s) || !s->commit_undo(s->context, u))
    {
        s->release_undo(s->context, u);
        return 0;
    }
    s->release_undo(s->context, u);
    if (!mm_scene_text_remove(text, s))
    {
        s->clear_undo(s->context);
        return 0;
    }
    return 1;
}
void mm_scene_actor_undo_dispose(MMSceneActorUndo *p, const MMSceneContentServices *s)
{
    if (p->actor)
    {
        release(p->actor, s);
        p->actor = NULL;
    }
}
void mm_scene_text_undo_dispose(MMSceneTextUndo *p, const MMSceneContentServices *s)
{
    if (p->text)
    {
        release(p->text, s);
        p->text = NULL;
    }
}
static int failed(const MMSceneContentUndoServices *s)
{
    s->flush(s->context);
    s->clear(s->context);
    return 0;
}
int mm_scene_actor_undo(MMSceneActorUndo *p, int redo, const MMSceneContentUndoServices *u)
{
    MMSceneContentServices s;
    void *copy, *actor;
    MMI32 id;
    if (!u->switch_scene(u->context) || !u->seek(u->context, u->frame))
        return failed(u);
    s = u->scene(u->context);
    id = s.actor_id(s.context, p->actor);
    if ((p->operation == MM_SCENE_ACTOR_ADD && !redo) || (p->operation == MM_SCENE_ACTOR_DELETE && redo))
        mm_scene_actor_remove(id, &s);
    else if (p->operation == MM_SCENE_ACTOR_DELETE)
    {
        copy = u->duplicate(u->context, p->actor);
        if (!copy)
            return failed(u);
        s.set_actor_id(s.context, copy, id);
        if (!mm_scene_actor_add(copy, &s))
        {
            release(copy, &s);
            return failed(u);
        }
        s.select(s.context, 0, copy);
        release(copy, &s);
    }
    else
    {
        if (p->operation == MM_SCENE_ACTOR_ADD)
        {
            if (!mm_scene_actor_add(p->actor, &s))
                return failed(u);
            s.select(s.context, 0, p->actor);
            actor = p->actor;
        }
        else
        {
            actor = mm_scene_actor_find(id, &s);
            if (!actor)
            {
                s.diagnostic(s.context, MM_SCENE_CONTENT_ACTOR_MISSING);
                return failed(u);
            }
        }
        copy = u->duplicate(u->context, actor);
        if (!copy)
            return failed(u);
        if (p->operation != MM_SCENE_ACTOR_ADD)
        {
            if (!mm_scene_actor_add(p->actor, &s))
            {
                release(copy, &s);
                return failed(u);
            }
            s.select(s.context, 0, p->actor);
        }
        if (p->operation == MM_SCENE_ACTOR_ADD)
            id = s.actor_id(s.context, p->actor);
        s.set_actor_id(s.context, copy, id);
        mm_scene_actor_undo_dispose(p, &s);
        p->actor = copy;
    }
    u->flush(u->context);
    u->update_roll(u->context);
    return 1;
}
int mm_scene_text_undo(MMSceneTextUndo *p, int redo, const MMSceneContentUndoServices *u)
{
    MMSceneContentServices s;
    void *text;
    MMI32 i;
    if (!u->switch_scene(u->context))
        return failed(u);
    s = u->scene(u->context);
    if ((p->add && redo) || (!p->add && !redo))
    {
        if (!u->seek(u->context, p->first))
            return failed(u);
        if (!mm_scene_text_add(p->text, &s))
        {
            if (redo)
                u->seek(u->context, u->frame);
            return failed(u);
        }
        if (p->last < s.events.data->lastFrame)
        {
            if (!s.events.seek(s.events.context, 1, p->text, p->last + 1))
                s.diagnostic(s.context, MM_SCENE_CONTENT_TEXT_HIDE_FAILED);
            s.hide_text(s.context, p->text);
        }
        s.select(s.context, 1, p->text);
        if (u->seek(u->context, u->frame))
        {
            for (i = 0; i < mm_scene_member_count(s.events.owner, MM_SCENE_TEXTS); ++i)
                if (mm_scene_member_at(s.events.owner, MM_SCENE_TEXTS, i) == p->text)
                {
                    p->index = i;
                    break;
                }
            if (i == mm_scene_member_count(s.events.owner, MM_SCENE_TEXTS))
            {
                s.diagnostic(s.context, MM_SCENE_CONTENT_TEXT_MISSING);
                return failed(u);
            }
        }
    }
    else
    {
        if (!u->seek(u->context, u->frame))
            return failed(u);
        text = mm_scene_text_at(p->index, &s);
        if (!mm_scene_text_remove(text, &s))
            return failed(u);
    }
    u->flush(u->context);
    return 1;
}
