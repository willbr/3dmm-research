/* Scene sound editing/undo converted from engine/scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/scene_sound_edit.h"
#include "three_mm/wide.h"
static void *frames(const ThreeMMSceneSoundEditServices *s)
{
    return s->events.owner->owned[THREE_MM_SCENE_FRAMES];
}
static ThreeMMSceneSound *payload(ThreeMMI32 i, const ThreeMMSceneSoundEditServices *s)
{
    return (ThreeMMSceneSound *)s->events.payload(s->events.context, frames(s), i);
}
static ThreeMMI32 find(ThreeMMI32 style, int *boundary, const ThreeMMSceneSoundEditServices *s)
{
    ThreeMMI32 i, frame;
    int kind;
    *boundary = 0;
    for (i = s->events.runtime->next_event - 1; i >= 0; --i)
    {
        s->events.event(s->events.context, frames(s), i, &frame, &kind);
        if (frame != s->events.data->currentFrame)
        {
            *boundary = 1;
            break;
        }
        if (kind == THREE_MM_SCENE_PLAY_SOUND && payload(i, s)->sty == style)
            return i;
    }
    return -1;
}
static void close_event(ThreeMMI32 index, const ThreeMMSceneSoundEditServices *s)
{
    ThreeMMSceneSound *p = payload(index, s);
    ThreeMMI32 i;
    const ThreeMMSceneSoundServices *v = &s->events.owners.sound;
    for (i = 0; i < p->ctagc; ++i)
        v->close(v->context, p, i);
    s->erase(s->context, index);
    --s->events.runtime->next_event;
}
int three_mm_scene_edit_insert(ThreeMMI32 frame, int kind, ThreeMMI32 bytes, const void *p,
                               const ThreeMMSceneSoundEditServices *s)
{
    ThreeMMI32 index;
    int ok;
    s->dirty(s->context);
    index = s->events.runtime->next_event++;
    ok = s->insert(s->context, index, frame, kind, bytes, p);
    if (!ok)
        --s->events.runtime->next_event;
    return ok;
}
static ThreeMMI32 packet_bytes(const ThreeMMSceneSound *p, const ThreeMMSceneSoundEditServices *s)
{
    return three_mm_scene_sound_size(p->ctagc, s->values.packets.tag_bytes);
}
static int install(ThreeMMSceneSound *p, ThreeMMI32 old, ThreeMMI32 frame, const ThreeMMSceneSoundEditServices *s)
{
    if (!three_mm_scene_edit_insert(frame, THREE_MM_SCENE_PLAY_SOUND, packet_bytes(p, s), p, s))
    {
        s->values.release(s->values.context, p);
        return 0;
    }
    if (old >= 0)
        close_event(old, s);
    return 1;
}
static void finish(ThreeMMSceneSound *p, ThreeMMI32 frame, ThreeMMU32 mask, const ThreeMMSceneSoundEditServices *s)
{
    /* Cache/play failure has always been non-fatal after event insertion. */
    three_mm_scene_play_event(frame, THREE_MM_SCENE_PLAY_SOUND, p, mask, &s->events);
    s->free_raw(s->context, p);
    s->dirty(s->context);
    s->sound_frame(s->context, 1);
}
int three_mm_scene_sound_add(int loop, int queue, ThreeMMI32 volume, ThreeMMI32 style, ThreeMMI32 count, void *tags,
                             const ThreeMMSceneSoundEditServices *s)
{
    ThreeMMI32 old, base, i, bytes, frame;
    int boundary;
    ThreeMMU32 child;
    ThreeMMTag tag;
    ThreeMMSceneSound *previous, *p;
    void *records, *sound, *c = s->context;
    const ThreeMMSceneSoundPacketServices *v = &s->values.packets;
    if (count <= 0 || (queue && (count != 1 || loop)))
        return 0;
    old = find(style, &boundary, s);
    if (old < 0)
        queue = 0;
    frame = s->events.data->currentFrame;
    if (!queue)
    {
        bytes = three_mm_scene_sound_size(count, v->tag_bytes);
        if (count <= 0 || !bytes)
            return 0;
        records = s->allocate_records(c, bytes - 16);
        if (!records)
            return 0;
        for (i = 0; i < count; ++i)
        {
            s->input_tag(c, tags, i, &tag);
            if (!tag.sid)
            {
                child = 0;
                if (!s->child(c, tag.number, &child))
                {
                    s->free_raw(c, records);
                    return 0;
                }
            }
            else
                child = s->unused_child(c);
            v->write_tag(v->context, (unsigned char *)records + (size_t)i * v->tag_bytes, child, &tag);
        }
        base = 0;
        p = three_mm_scene_sound_make(volume, style, loop, count, records, v);
        s->free_raw(c, records);
        if (!p || !install(p, old, frame, s))
            return 0;
    }
    else
    {
        previous = three_mm_scene_sound_group(frames(s), old, 1, v);
        if (!previous)
            return 0;
        s->input_tag(c, tags, 0, &tag);
        if (!tag.sid)
        {
            child = 0;
            if (!s->child(c, tag.number, &child))
            {
                s->values.release(s->values.context, previous);
                return 0;
            }
        }
        else
            child = s->unused_child(c);
        base = previous->ctagc;
        p = three_mm_scene_sound_append_tag(previous, child, &tag, v);
        if (!p)
        {
            s->values.release(s->values.context, previous);
            return 0;
        }
        if (!s->put(c, old, packet_bytes(p, s), p))
        {
            /* append transferred previous's prefix to p: dispose that prefix once. */
            s->free_raw(c, previous);
            s->values.release(s->values.context, p);
            return 0;
        }
        s->values.release(s->values.context, previous);
    }
    for (i = 0; i < count; ++i)
    {
        s->input_tag(c, tags, i, &tag);
        if (!tag.sid)
        {
            v->read_tag(v->context, (unsigned char *)p + 16 + (size_t)(i + base) * v->tag_bytes, &child, &tag);
            if (!s->resolve_input(c, tags, i, child))
                continue;
        }
        sound = s->fetch_input(c, tags, i);
        if (!sound)
            continue;
        v->enqueue(v->context, sound, loop != 0, i != 0, volume, v->priority(v->context, sound, loop != 0), 0);
        v->release(v->context, sound);
    }
    finish(p, frame, s->events.runtime->disabled | THREE_MM_SCENE_EFFECT_SOUNDS, s);
    return 1;
}
int three_mm_scene_sound_restore(int loop, int queue, ThreeMMI32 volume, ThreeMMI32 style, ThreeMMI32 count,
                                 const void *records, const ThreeMMSceneSoundEditServices *s)
{
    ThreeMMI32 old, frame;
    int boundary;
    ThreeMMSceneSound *p;
    if (count <= 0 || (queue && (count != 1 || loop)))
        return 0;
    old = find(style, &boundary, s);
    frame = s->events.data->currentFrame;
    if (queue && old >= 0)
    {
        s->diagnostic(s->context, THREE_MM_SCENE_SOUND_QUEUE_RESTORE);
        return 0;
    }
    p = three_mm_scene_sound_make(volume, style, loop, count, records, &s->values.packets);
    if (!p || !install(p, old, frame, s))
        return 0;
    finish(p, frame, s->events.runtime->disabled, s);
    return 1;
}
int three_mm_scene_sound_get(ThreeMMI32 style, int *found, ThreeMMSceneSound **out,
                             const ThreeMMSceneSoundEditServices *s)
{
    ThreeMMI32 i;
    int boundary;
    *found = 0;
    i = find(style, &boundary, s);
    if (i < 0)
        return 1;
    *out = three_mm_scene_sound_group(frames(s), i, 1, &s->values.packets);
    if (!*out)
        return 0;
    *found = 1;
    return 1;
}
void three_mm_scene_sound_update_indicator(const ThreeMMSceneSoundEditServices *s)
{
    ThreeMMI32 i, frame;
    int kind, has = 0;
    for (i = s->events.runtime->next_event - 1; i >= 0; --i)
    {
        s->events.event(s->events.context, frames(s), i, &frame, &kind);
        if (frame != s->events.data->currentFrame)
            break;
        if (kind == THREE_MM_SCENE_PLAY_SOUND)
        {
            has = 1;
            break;
        }
    }
    if (!has)
        for (i = 0; i < s->actor_count(s->context); ++i)
            if (s->actor_sound(s->context, i))
            {
                has = 1;
                break;
            }
    s->sound_frame(s->context, has);
}
void three_mm_scene_sound_remove(ThreeMMI32 style, const ThreeMMSceneSoundEditServices *s)
{
    ThreeMMI32 i;
    int boundary;
    i = find(style, &boundary, s);
    if (i < 0)
    {
        s->diagnostic(s->context, boundary ? THREE_MM_SCENE_SOUND_STYLE_MISSING : THREE_MM_SCENE_SOUND_MISSING);
        return;
    }
    close_event(i, s);
    if (style == THREE_MM_SCENE_EVENT_MIDI)
        three_mm_scene_owner_drop(s->events.owner, s->events.scene, THREE_MM_SCENE_BACKGROUND_SOUND, &s->events.owners);
    three_mm_scene_sound_update_indicator(s);
    s->dirty(s->context);
}
int three_mm_scene_sound_query(ThreeMMI32 style, void **list, ThreeMMI32 *volume, int *loop,
                               const ThreeMMSceneSoundEditServices *s)
{
    ThreeMMSceneSound *p = NULL;
    int found;
    ThreeMMI32 i;
    ThreeMMU32 child;
    ThreeMMTag tag;
    const ThreeMMSceneSoundPacketServices *v = &s->values.packets;
    *list = NULL;
    if (s->publish_query)
        s->publish_query(s->query_context, NULL, 0, 0, 0);
    if (!three_mm_scene_sound_get(style, &found, &p, s))
        return 0;
    if (!found)
        return 1;
    *list = s->create_tag_list(s->context, p->ctagc);
    if (s->publish_query)
        s->publish_query(s->query_context, *list, 0, 0, 0);
    if (!*list)
    {
        s->values.release(s->values.context, p);
        return 0;
    }
    if (!s->resize_tag_list(s->context, *list, p->ctagc))
    {
        s->diagnostic(s->context, THREE_MM_SCENE_SOUND_LIST_RESIZE);
        s->release_list(s->context, *list);
        *list = NULL;
        if (s->publish_query)
            s->publish_query(s->query_context, NULL, 0, 0, 0);
        s->values.release(s->values.context, p);
        return 0;
    }
    for (i = 0; i < p->ctagc; ++i)
    {
        v->read_tag(v->context, (unsigned char *)p + 16 + (size_t)i * v->tag_bytes, &child, &tag);
        s->put_list_tag(s->context, *list, i, &tag);
    }
    *volume = p->vlm;
    *loop = p->fLoop != 0;
    if (s->publish_query)
        s->publish_query(s->query_context, *list, 1, *volume, *loop);
    s->values.release(s->values.context, p);
    return 1;
}
void three_mm_scene_sound_volume(ThreeMMI32 style, ThreeMMI32 volume, const ThreeMMSceneSoundEditServices *s)
{
    ThreeMMI32 i;
    int boundary;
    i = find(style, &boundary, s);
    if (i >= 0)
    {
        payload(i, s)->vlm = volume;
        s->dirty(s->context);
        return;
    }
    if (boundary)
        s->diagnostic(s->context, THREE_MM_SCENE_SOUND_MISSING);
    s->diagnostic(s->context, THREE_MM_SCENE_SOUND_MISSING);
}
ThreeMMU32 three_mm_scene_sound_background_offset(ThreeMMI32 frame, ThreeMMI32 first, int *overflow)
{
    ThreeMMI32 signed_frames = three_mm_signed_word((ThreeMMU32)frame - (ThreeMMU32)first);
    ThreeMMU32 magnitude = signed_frames < 0 ? 0U - (ThreeMMU32)signed_frames : (ThreeMMU32)signed_frames;
    ThreeMMU32 q = magnitude / 6U, r = (magnitude % 6U) * 1000U / 6U,
               limit = signed_frames < 0 ? 2147483648UL : 2147483647UL;
    *overflow = q > limit / 1000U || (q == limit / 1000U && r > limit % 1000U);
    q = q * 1000U + r;
    return signed_frames < 0 ? 0U - q : q;
}
void three_mm_scene_sound_background_play(const ThreeMMSceneSoundEditServices *s)
{
    ThreeMMSceneSound *p = (ThreeMMSceneSound *)s->events.owner->owned[THREE_MM_SCENE_BACKGROUND_SOUND];
    ThreeMMU32 start;
    int overflow;
    if (!p)
        return;
    start = three_mm_scene_sound_background_offset(s->events.data->currentFrame, s->events.runtime->background_frame,
                                                   &overflow);
    if (overflow)
        s->diagnostic(s->context, THREE_MM_SCENE_SOUND_OFFSET_OVERFLOW);
    three_mm_scene_sound_play(p, start, &s->values.packets);
}
void three_mm_scene_sound_undo_init(ThreeMMSceneSoundUndo *p)
{
    p->sound = NULL;
    p->style = 0;
}
void three_mm_scene_sound_undo_dispose(ThreeMMSceneSoundUndo *p, const ThreeMMSceneSoundValueServices *v)
{
    if (p->sound)
    {
        v->release(v->context, p->sound);
        p->sound = NULL;
    }
}
int three_mm_scene_sound_undo_set(ThreeMMSceneSoundUndo *p, const ThreeMMSceneSound *sound,
                                  const ThreeMMSceneSoundValueServices *v)
{
    ThreeMMSceneSound *copy;
    if (!sound)
        return 0;
    copy = three_mm_scene_sound_duplicate(sound, &v->packets);
    if (!copy)
        return 0;
    three_mm_scene_sound_undo_dispose(p, v);
    p->sound = copy;
    p->style = copy->sty;
    return 1;
}
int three_mm_scene_sound_add_undo(int loop, int queue, ThreeMMI32 volume, ThreeMMI32 style, void *tag,
                                  const ThreeMMSceneSoundEditServices *s)
{
    void *undo = s->new_undo(s->context);
    ThreeMMSceneSound *p = NULL;
    int found;
    ThreeMMSceneSoundUndo *state;
    if (!undo)
        return 0;
    state = s->undo_state(s->context, undo);
    state->style = style;
    if (!three_mm_scene_sound_get(style, &found, &p, s))
    {
        s->release_undo(s->context, undo);
        return 0;
    }
    if (found)
    {
        if (!three_mm_scene_sound_undo_set(state, p, &s->values))
        {
            s->values.release(s->values.context, p);
            s->release_undo(s->context, undo);
            return 0;
        }
        s->values.release(s->values.context, p);
    }
    if (!s->commit_undo(s->context, undo))
    {
        s->release_undo(s->context, undo);
        return 0;
    }
    s->release_undo(s->context, undo);
    if (!three_mm_scene_sound_add(loop, queue, volume, style, 1, tag, s))
    {
        s->clear_undo(s->context);
        return 0;
    }
    return 1;
}
int three_mm_scene_sound_remove_undo(ThreeMMI32 style, const ThreeMMSceneSoundEditServices *s)
{
    void *undo = s->new_undo(s->context);
    ThreeMMSceneSound *p = NULL;
    int found;
    if (!undo)
        return 0;
    if (!three_mm_scene_sound_get(style, &found, &p, s))
    {
        s->release_undo(s->context, undo);
        return 0;
    }
    if (!found)
    {
        s->diagnostic(s->context, THREE_MM_SCENE_SOUND_STYLE_MISSING);
        s->release_undo(s->context, undo);
        return 0;
    }
    if (!three_mm_scene_sound_undo_set(s->undo_state(s->context, undo), p, &s->values))
    {
        s->values.release(s->values.context, p);
        s->release_undo(s->context, undo);
        return 0;
    }
    s->values.release(s->values.context, p);
    if (!s->commit_undo(s->context, undo))
    {
        s->release_undo(s->context, undo);
        return 0;
    }
    s->release_undo(s->context, undo);
    three_mm_scene_sound_remove(style, s);
    return 1;
}
int three_mm_scene_sound_undo_swap(ThreeMMSceneSoundUndo *state, const ThreeMMSceneSoundUndoServices *s)
{
    ThreeMMSceneSound *current = NULL;
    int found;
    if (!s->switch_scene(s->context) || !s->seek_frame(s->context))
    {
        s->clear(s->context);
        return 0;
    }
    s->flush(s->context);
    if (!s->get(s->context, state->style, &found, &current))
    {
        s->clear(s->context);
        return 0;
    }
    if (state->sound)
    {
        if (found)
            s->remove(s->context, current->sty);
        if (!s->restore(s->context, state->sound))
        {
            if (current)
                s->values.release(s->values.context, current);
            s->clear(s->context);
            return 0;
        }
        three_mm_scene_sound_undo_dispose(state, &s->values);
        if (found)
        {
            state->sound = current;
            state->style = current->sty;
        }
    }
    else
    {
        if (!found)
        {
            s->diagnostic(s->context, THREE_MM_SCENE_SOUND_MISSING);
            return 0;
        }
        if (!three_mm_scene_sound_undo_set(state, current, &s->values))
        {
            s->values.release(s->values.context, current);
            s->clear(s->context);
            return 0;
        }
        s->values.release(s->values.context, current);
        s->remove(s->context, state->style);
    }
    s->play(s->context);
    return 1;
}
