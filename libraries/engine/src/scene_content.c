/* Scene actor/caption editing converted from engine/scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/scene_content.h"
#include "three_mm/wide.h"
static void *group(const ThreeMMSceneContentServices *s)
{
    return s->events.owner->owned[THREE_MM_SCENE_STARTS];
}
static void *list(int text, const ThreeMMSceneContentServices *s)
{
    return s->events.owner->owned[text ? THREE_MM_SCENE_TEXTS : THREE_MM_SCENE_ACTORS];
}
static ThreeMMI32 count(void *p, const ThreeMMSceneContentServices *s)
{
    return s->events.owners.count(s->events.owners.context, p);
}
static void *at(void *p, ThreeMMI32 i, const ThreeMMSceneContentServices *s)
{
    return s->events.owners.list_at(s->events.owners.context, p, i);
}
static int kind(ThreeMMI32 i, const ThreeMMSceneContentServices *s)
{
    ThreeMMI32 frame;
    int type;
    s->events.event(s->events.context, group(s), i, &frame, &type);
    return type;
}
static void *object(ThreeMMI32 i, const ThreeMMSceneContentServices *s)
{
    return s->events.owners.object(s->events.owners.context, group(s), i);
}
static void release(void *p, const ThreeMMSceneContentServices *s)
{
    s->events.owners.release(s->events.owners.context, p);
}
static void remove_extras(ThreeMMI32 id, const ThreeMMSceneContentServices *s)
{
    void *extras = s->events.owner->owned[THREE_MM_SCENE_SELECTION], *entry;
    ThreeMMI32 i;
    if (extras)
        for (i = count(extras, s) - 1; i >= 0; --i)
        {
            entry = at(extras, i, s);
            if (entry && s->actor_id(s->context, entry) == id)
                s->erase(s->context, extras, i);
        }
}
void *three_mm_scene_actor_find(ThreeMMI32 id, const ThreeMMSceneContentServices *s)
{
    ThreeMMI32 i;
    void *p;
    for (i = 0; i < count(list(0, s), s); ++i)
    {
        p = at(list(0, s), i, s);
        if (s->actor_id(s->context, p) == id)
            return p;
    }
    return NULL;
}
int three_mm_scene_actor_add(void *actor, const ThreeMMSceneContentServices *s)
{
    ThreeMMI32 i, j;
    void *old;
    int ok;
    for (i = 0; i < count(group(s), s); ++i)
    {
        if (kind(i, s) != THREE_MM_SCENE_ADD_ACTOR)
            continue;
        old = object(i, s);
        if (s->actor_id(s->context, old) != s->actor_id(s->context, actor))
            continue;
        for (j = 0; j < count(list(0, s), s); ++j)
        {
            old = at(list(0, s), j, s);
            if (s->actor_id(s->context, old) != s->actor_id(s->context, actor))
                continue;
            s->retain(s->context, actor);
            s->events.bind_actor(s->events.context, actor);
            if (!s->events.seek(s->events.context, 0, actor, s->events.data->currentFrame) ||
                !s->actor_ready(s->context, actor))
            {
                release(actor, s);
                return 0;
            }
            s->put_start(s->context, i, actor);
            s->put_list(s->context, list(0, s), j, actor);
            s->select(s->context, 0, actor);
            remove_extras(s->actor_id(s->context, old), s);
            s->hide_actor(s->context, old);
            release(old, s);
            s->invalidate(s->context);
            s->dirty(s->context);
            s->indicator(s->context);
            return 1;
        }
        s->diagnostic(s->context, THREE_MM_SCENE_CONTENT_ACTOR_ROLL_MISSING);
    }
    if (!s->insert_start(s->context, 0, THREE_MM_SCENE_ADD_ACTOR, actor))
        return 0;
    s->retain(s->context, actor);
    if (!s->add_roll(s->context, actor, 1))
    {
        s->erase(s->context, group(s), 0);
        release(actor, s);
        return 0;
    }
    ok = three_mm_scene_play_event(0, THREE_MM_SCENE_ADD_ACTOR, &actor, s->events.runtime->disabled, &s->events);
    if (ok && !s->actor_ready(s->context, actor))
    {
        s->events.remove_last(s->events.context, 0);
        ok = 0;
    }
    if (!ok)
    {
        s->remove_roll(s->context, actor, 0);
        s->erase(s->context, group(s), 0);
        release(actor, s);
        return 0;
    }
    s->invalidate(s->context);
    s->dirty(s->context);
    s->indicator(s->context);
    return 1;
}
int three_mm_scene_actor_add_onstage(void *actor, const ThreeMMSceneContentServices *s)
{
    if (!three_mm_scene_actor_add(actor, s))
        return 0;
    if (!s->actor_onstage(s->context, actor))
    {
        three_mm_scene_actor_remove(s->actor_id(s->context, actor), s);
        return 0;
    }
    s->select(s->context, 0, actor);
    return 1;
}
void three_mm_scene_actor_remove(ThreeMMI32 id, const ThreeMMSceneContentServices *s)
{
    ThreeMMI32 i, j;
    void *actor, *removed;
    for (i = 0; i < count(group(s), s); ++i)
    {
        if (kind(i, s) != THREE_MM_SCENE_ADD_ACTOR)
            continue;
        actor = object(i, s);
        if (s->actor_id(s->context, actor) != id)
            continue;
        removed = actor;
        s->erase(s->context, group(s), i);
        for (j = 0; j < count(list(0, s), s); ++j)
        {
            actor = at(list(0, s), j, s);
            if (s->actor_id(s->context, actor) != id)
                continue;
            s->erase(s->context, list(0, s), j);
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
        s->diagnostic(s->context, THREE_MM_SCENE_CONTENT_ACTOR_ROLL_MISSING);
        release(removed, s);
        s->dirty(s->context);
        return;
    }
    s->diagnostic(s->context, THREE_MM_SCENE_CONTENT_ACTOR_MISSING);
}
int three_mm_scene_actor_remove_undo(ThreeMMI32 id, const ThreeMMSceneContentServices *s)
{
    void *actor = three_mm_scene_actor_find(id, s), *u;
    ThreeMMSceneActorUndo *p;
    if (!actor)
    {
        s->diagnostic(s->context, THREE_MM_SCENE_CONTENT_ACTOR_MISSING);
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
    p->operation = THREE_MM_SCENE_ACTOR_DELETE;
    if (!s->commit_undo(s->context, u))
    {
        s->release_undo(s->context, u);
        return 0;
    }
    s->release_undo(s->context, u);
    three_mm_scene_actor_remove(id, s);
    return 1;
}
int three_mm_scene_actor_paste(void *actor, const ThreeMMSceneContentServices *s)
{
    if (!s->actor_paste(s->context, actor, s->events.data->currentFrame) || !three_mm_scene_actor_add(actor, s))
        return 0;
    s->invalidate(s->context);
    if (!s->events.seek(s->events.context, 0, actor, s->events.data->currentFrame))
    {
        three_mm_scene_actor_remove(s->actor_id(s->context, actor), s);
        return 0;
    }
    s->dirty(s->context);
    s->update_roll(s->context);
    s->indicator(s->context);
    return 1;
}
int three_mm_scene_roll_add(const ThreeMMSceneContentServices *s)
{
    ThreeMMI32 i;
    for (i = 0; i < count(list(0, s), s); ++i)
        if (!s->add_roll(s->context, at(list(0, s), i, s), 0))
        {
            while (i > 0)
                s->remove_roll(s->context, at(list(0, s), --i, s), 0);
            return 0;
        }
    return 1;
}
void three_mm_scene_roll_remove(int only, const ThreeMMSceneContentServices *s)
{
    ThreeMMI32 i;
    for (i = 0; i < count(list(0, s), s); ++i)
        s->remove_roll(s->context, at(list(0, s), i, s), only);
}
void *three_mm_scene_text_at(ThreeMMI32 i, const ThreeMMSceneContentServices *s)
{
    return i >= 0 && i < count(list(1, s), s) ? at(list(1, s), i, s) : NULL;
}
int three_mm_scene_text_add(void *text, const ThreeMMSceneContentServices *s)
{
    s->bind_text(s->context, text);
    if (!s->insert_start(s->context, count(group(s), s), THREE_MM_SCENE_ADD_TEXT, text))
        return 0;
    s->retain(s->context, text);
    if (!three_mm_scene_play_event(0, THREE_MM_SCENE_ADD_TEXT, &text, s->events.runtime->disabled, &s->events))
    {
        s->erase(s->context, group(s), count(group(s), s) - 1);
        release(text, s);
        return 0;
    }
    s->dirty(s->context);
    return 1;
}
int three_mm_scene_text_remove(void *text, const ThreeMMSceneContentServices *s)
{
    ThreeMMI32 i, j, first, last;
    if (!text)
    {
        s->diagnostic(s->context, THREE_MM_SCENE_CONTENT_TEXT_MISSING);
        return 0;
    }
    if (s->selected(s->context, 1) == text)
    {
        s->set_selected(s->context, 1, NULL);
        s->text_select(s->context, text, 0);
    }
    for (i = 0; i < count(group(s), s); ++i)
    {
        if (kind(i, s) != THREE_MM_SCENE_ADD_TEXT || object(i, s) != text)
            continue;
        s->erase(s->context, group(s), i);
        for (j = 0; j < count(list(1, s), s); ++j)
            if (at(list(1, s), j, s) == text)
            {
                s->erase(s->context, list(1, s), j);
                if (s->text_lifetime(s->context, text, &first, &last) &&
                    !s->events.seek(s->events.context, 1, text, three_mm_signed_word((ThreeMMU32)first - 1U)))
                    s->diagnostic(s->context, THREE_MM_SCENE_CONTENT_TEXT_HIDE_FAILED);
                release(text, s);
                s->dirty(s->context);
                return 1;
            }
        s->diagnostic(s->context, THREE_MM_SCENE_CONTENT_TEXT_ROLL_MISSING);
        release(text, s);
        return 0;
    }
    s->diagnostic(s->context, THREE_MM_SCENE_CONTENT_TEXT_MISSING);
    return 0;
}
static int text_snapshot(void *text, void *u, int add, const ThreeMMSceneContentServices *s)
{
    ThreeMMSceneTextUndo *p = s->text_state(s->context, u);
    ThreeMMI32 first, last;
    s->retain(s->context, text);
    p->text = text;
    p->add = add;
    if (!s->text_lifetime(s->context, text, &first, &last))
    {
        s->diagnostic(s->context, THREE_MM_SCENE_CONTENT_LIFETIME);
        return 0;
    }
    p->first = first;
    p->last = last == s->events.data->lastFrame ? 2147483647L : last;
    return 1;
}
int three_mm_scene_text_add_undo(void *text, const ThreeMMSceneContentServices *s)
{
    void *u = s->new_undo(s->context, 1);
    ThreeMMSceneTextUndo *p;
    ThreeMMI32 i;
    if (!u)
        return 0;
    if (!three_mm_scene_text_add(text, s) || !text_snapshot(text, u, 1, s))
    {
        s->release_undo(s->context, u);
        return 0;
    }
    p = s->text_state(s->context, u);
    for (i = 0; i < count(list(1, s), s); ++i)
        if (at(list(1, s), i, s) == text)
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
int three_mm_scene_text_remove_undo(void *text, const ThreeMMSceneContentServices *s)
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
    if (!three_mm_scene_text_remove(text, s))
    {
        s->clear_undo(s->context);
        return 0;
    }
    return 1;
}
void three_mm_scene_content_bind_movie(void *movie, const ThreeMMSceneContentServices *s)
{
    s->events.owner->movie = movie;
    s->events.owners.publish(s->events.owners.context, s->events.scene, s->events.owner);
}
void three_mm_scene_actor_undo_dispose(ThreeMMSceneActorUndo *p, const ThreeMMSceneContentServices *s)
{
    if (p->actor)
    {
        release(p->actor, s);
        p->actor = NULL;
    }
}
void three_mm_scene_text_undo_dispose(ThreeMMSceneTextUndo *p, const ThreeMMSceneContentServices *s)
{
    if (p->text)
    {
        release(p->text, s);
        p->text = NULL;
    }
}
static int failed(const ThreeMMSceneContentUndoServices *s)
{
    s->flush(s->context);
    s->clear(s->context);
    return 0;
}
int three_mm_scene_actor_undo(ThreeMMSceneActorUndo *p, int redo, const ThreeMMSceneContentUndoServices *u)
{
    ThreeMMSceneContentServices s;
    void *copy, *actor;
    ThreeMMI32 id;
    if (!u->switch_scene(u->context) || !u->seek(u->context, u->frame))
        return failed(u);
    s = u->scene(u->context);
    id = s.actor_id(s.context, p->actor);
    if ((p->operation == THREE_MM_SCENE_ACTOR_ADD && !redo) || (p->operation == THREE_MM_SCENE_ACTOR_DELETE && redo))
        three_mm_scene_actor_remove(id, &s);
    else if (p->operation == THREE_MM_SCENE_ACTOR_DELETE)
    {
        copy = u->duplicate(u->context, p->actor);
        if (!copy)
            return failed(u);
        s.set_actor_id(s.context, copy, id);
        if (!three_mm_scene_actor_add(copy, &s))
        {
            release(copy, &s);
            return failed(u);
        }
        s.select(s.context, 0, copy);
        release(copy, &s);
    }
    else
    {
        if (p->operation == THREE_MM_SCENE_ACTOR_ADD)
        {
            if (!three_mm_scene_actor_add(p->actor, &s))
                return failed(u);
            s.select(s.context, 0, p->actor);
            actor = p->actor;
        }
        else
        {
            actor = three_mm_scene_actor_find(id, &s);
            if (!actor)
            {
                s.diagnostic(s.context, THREE_MM_SCENE_CONTENT_ACTOR_MISSING);
                return failed(u);
            }
        }
        copy = u->duplicate(u->context, actor);
        if (!copy)
            return failed(u);
        if (p->operation != THREE_MM_SCENE_ACTOR_ADD)
        {
            if (!three_mm_scene_actor_add(p->actor, &s))
            {
                release(copy, &s);
                return failed(u);
            }
            s.select(s.context, 0, p->actor);
        }
        if (p->operation == THREE_MM_SCENE_ACTOR_ADD)
            id = s.actor_id(s.context, p->actor);
        s.set_actor_id(s.context, copy, id);
        three_mm_scene_actor_undo_dispose(p, &s);
        p->actor = copy;
    }
    u->flush(u->context);
    u->update_roll(u->context);
    return 1;
}
int three_mm_scene_text_undo(ThreeMMSceneTextUndo *p, int redo, const ThreeMMSceneContentUndoServices *u)
{
    ThreeMMSceneContentServices s;
    void *text;
    ThreeMMI32 i;
    if (!u->switch_scene(u->context))
        return failed(u);
    s = u->scene(u->context);
    if ((p->add && redo) || (!p->add && !redo))
    {
        if (!u->seek(u->context, p->first))
            return failed(u);
        if (!three_mm_scene_text_add(p->text, &s))
        {
            if (redo)
                u->seek(u->context, u->frame);
            return failed(u);
        }
        if (p->last < s.events.data->lastFrame)
        {
            if (!s.events.seek(s.events.context, 1, p->text, p->last + 1))
                s.diagnostic(s.context, THREE_MM_SCENE_CONTENT_TEXT_HIDE_FAILED);
            s.hide_text(s.context, p->text);
        }
        s.select(s.context, 1, p->text);
        if (u->seek(u->context, u->frame))
        {
            for (i = 0; i < count(list(1, &s), &s); ++i)
                if (at(list(1, &s), i, &s) == p->text)
                {
                    p->index = i;
                    break;
                }
            if (i == count(list(1, &s), &s))
            {
                s.diagnostic(s.context, THREE_MM_SCENE_CONTENT_TEXT_MISSING);
                return failed(u);
            }
        }
    }
    else
    {
        if (!u->seek(u->context, u->frame))
            return failed(u);
        text = three_mm_scene_text_at(p->index, &s);
        if (!three_mm_scene_text_remove(text, &s))
            return failed(u);
    }
    u->flush(u->context);
    return 1;
}
