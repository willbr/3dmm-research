/* Scene sound editing/undo converted from engine/scene.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/scene_sound_edit.h"
#include "mm/wide.h"
static MMSceneTrack *frames(const MMSceneSoundEditServices *s)
{
    return &s->events.owner->tracks[MM_SCENE_FRAMES];
}
static MMSceneSound *payload(MMI32 i, const MMSceneSoundEditServices *s)
{
    return (MMSceneSound *)mm_scene_track_payload(frames(s), i, NULL);
}
static MMI32 find(MMI32 style, int *boundary, const MMSceneSoundEditServices *s)
{
    const MMSceneTrack *track = frames(s);
    MMSceneTrackEvent event;
    MMI32 i;
    *boundary = 0;
    for (i = s->events.owner->runtime.next_event - 1; i >= 0; --i)
    {
        if (!mm_scene_track_get(track, i, &event))
            break;
        if (event.frame != s->events.owner->data.currentFrame)
        {
            *boundary = 1;
            break;
        }
        if (event.kind == MM_SCENE_PLAY_SOUND && payload(i, s)->sty == style)
            return i;
    }
    return -1;
}
static void close_event(MMI32 index, const MMSceneSoundEditServices *s)
{
    if (mm_scene_owner_erase_frame(s->events.owner, index, &s->events.owners))
        --s->events.owner->runtime.next_event;
}
int mm_scene_edit_insert(MMI32 frame, int kind, MMI32 bytes, const void *p,
                               const MMSceneSoundEditServices *s)
{
    MMSceneTrackEvent event;
    MMI32 index;
    int ok;
    s->dirty(s->context);
    index = s->events.owner->runtime.next_event++;
    event.frame = frame;
    event.kind = kind;
    ok = mm_scene_track_insert(frames(s), index, bytes, p, &event);
    if (!ok)
        --s->events.owner->runtime.next_event;
    return ok;
}
static int install(MMSceneSound *p, MMI32 old, MMI32 frame, const MMSceneSoundEditServices *s)
{
    if (!mm_scene_edit_insert(frame, MM_SCENE_PLAY_SOUND, mm_scene_sound_size(p->ctagc), p, s))
    {
        s->values.release(s->values.context, p);
        return 0;
    }
    if (old >= 0)
        close_event(old, s);
    return 1;
}
static void finish(MMSceneSound *p, MMI32 frame, MMU32 mask, const MMSceneSoundEditServices *s)
{
    /* Cache/play failure has always been non-fatal after event insertion. */
    mm_scene_play_event(frame, MM_SCENE_PLAY_SOUND, p, mask, &s->events);
    s->free_raw(s->context, p);
    s->dirty(s->context);
    s->sound_frame(s->context, 1);
}
int mm_scene_sound_add(int loop, int queue, MMI32 volume, MMI32 style, MMI32 count, MMTag *tags,
                             const MMSceneSoundEditServices *s)
{
    MMI32 old, base, i, bytes, frame;
    int boundary;
    MMU32 child;
    MMSceneSoundTag entry = {0}, *records;
    MMSceneSound *previous, *p;
    void *sound, *c = s->context;
    const MMSceneSoundPacketServices *v = &s->values.packets;
    if (count <= 0 || (queue && (count != 1 || loop)))
        return 0;
    old = find(style, &boundary, s);
    if (old < 0)
        queue = 0;
    frame = s->events.owner->data.currentFrame;
    if (!queue)
    {
        bytes = mm_scene_sound_size(count);
        if (count <= 0 || !bytes)
            return 0;
        records = s->allocate_records(c, bytes - MM_SCENE_SOUND_HEADER_BYTES);
        if (!records)
            return 0;
        for (i = 0; i < count; ++i)
        {
            const MMTag tag = tags[i];
            if (!tag.sid)
            {
                const MMMovieSoundServices movie = s->movie_sound(c);
                child = 0;
                if (!mm_movie_sound_child(tag.number, &child, &movie))
                {
                    s->free_raw(c, records);
                    return 0;
                }
            }
            else
                child = s->unused_child(c);
            records[i].child = child;
            records[i].tag = tag;
        }
        base = 0;
        p = mm_scene_sound_make(volume, style, loop, count, records, v);
        s->free_raw(c, records);
        if (!p || !install(p, old, frame, s))
            return 0;
    }
    else
    {
        previous = mm_scene_sound_track(frames(s), old, v);
        if (!previous)
            return 0;
        entry.tag = tags[0];
        if (!entry.tag.sid)
        {
            const MMMovieSoundServices movie = s->movie_sound(c);
            child = 0;
            if (!mm_movie_sound_child(entry.tag.number, &child, &movie))
            {
                s->values.release(s->values.context, previous);
                return 0;
            }
        }
        else
            child = s->unused_child(c);
        base = previous->ctagc;
        entry.child = child;
        p = mm_scene_sound_append(previous, &entry, v);
        if (!p)
        {
            s->values.release(s->values.context, previous);
            return 0;
        }
        if (!mm_scene_track_put(frames(s), old, mm_scene_sound_size(p->ctagc), p))
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
        if (!tags[i].sid)
        {
            const MMMovieSoundServices movie = s->movie_sound(c);
            mm_scene_sound_get_tag(p, i + base, &entry);
            if (!mm_movie_sound_resolve(&tags[i], entry.child, MM_SOUND_NO_SCENE, NULL, &movie))
                continue;
        }
        sound = s->fetch_input(c, &tags[i]);
        if (!sound)
            continue;
        v->enqueue(v->context, sound, loop != 0, i != 0, volume, v->priority(v->context, sound, loop != 0), 0);
        v->release(v->context, sound);
    }
    finish(p, frame, s->events.owner->runtime.disabled | MM_SCENE_EFFECT_SOUNDS, s);
    return 1;
}
int mm_scene_sound_restore(int loop, int queue, MMI32 volume, MMI32 style, MMI32 count,
                           const MMSceneSoundTag *records, const MMSceneSoundEditServices *s)
{
    MMI32 old, frame;
    int boundary;
    MMSceneSound *p;
    if (count <= 0 || (queue && (count != 1 || loop)))
        return 0;
    old = find(style, &boundary, s);
    frame = s->events.owner->data.currentFrame;
    if (queue && old >= 0)
    {
        s->diagnostic(s->context, MM_SCENE_SOUND_QUEUE_RESTORE);
        return 0;
    }
    p = mm_scene_sound_make(volume, style, loop, count, records, &s->values.packets);
    if (!p || !install(p, old, frame, s))
        return 0;
    finish(p, frame, s->events.owner->runtime.disabled, s);
    return 1;
}
int mm_scene_sound_get(MMI32 style, int *found, MMSceneSound **out,
                             const MMSceneSoundEditServices *s)
{
    MMI32 i;
    int boundary;
    *found = 0;
    i = find(style, &boundary, s);
    if (i < 0)
        return 1;
    *out = mm_scene_sound_track(frames(s), i, &s->values.packets);
    if (!*out)
        return 0;
    *found = 1;
    return 1;
}
void mm_scene_sound_update_indicator(const MMSceneSoundEditServices *s)
{
    const MMSceneTrack *track = frames(s);
    MMSceneTrackEvent event;
    MMI32 i;
    int has = 0;
    for (i = s->events.owner->runtime.next_event - 1; i >= 0; --i)
    {
        if (!mm_scene_track_get(track, i, &event) || event.frame != s->events.owner->data.currentFrame)
            break;
        if (event.kind == MM_SCENE_PLAY_SOUND)
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
void mm_scene_sound_remove(MMI32 style, const MMSceneSoundEditServices *s)
{
    MMI32 i;
    int boundary;
    i = find(style, &boundary, s);
    if (i < 0)
    {
        s->diagnostic(s->context, boundary ? MM_SCENE_SOUND_STYLE_MISSING : MM_SCENE_SOUND_MISSING);
        return;
    }
    close_event(i, s);
    if (style == MM_SCENE_EVENT_MIDI)
        mm_scene_owner_drop(s->events.owner, MM_SCENE_BACKGROUND_SOUND, &s->events.owners);
    mm_scene_sound_update_indicator(s);
    s->dirty(s->context);
}
int mm_scene_sound_query(MMI32 style, void **list, MMI32 *volume, int *loop,
                               const MMSceneSoundEditServices *s)
{
    MMSceneSound *p = NULL;
    int found;
    MMI32 i;
    MMSceneSoundTag entry;
    *list = NULL;
    if (s->publish_query)
        s->publish_query(s->query_context, NULL, 0, 0, 0);
    if (!mm_scene_sound_get(style, &found, &p, s))
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
        s->diagnostic(s->context, MM_SCENE_SOUND_LIST_RESIZE);
        s->release_list(s->context, *list);
        *list = NULL;
        if (s->publish_query)
            s->publish_query(s->query_context, NULL, 0, 0, 0);
        s->values.release(s->values.context, p);
        return 0;
    }
    for (i = 0; i < p->ctagc; ++i)
    {
        mm_scene_sound_get_tag(p, i, &entry);
        s->put_list_tag(s->context, *list, i, &entry.tag);
    }
    *volume = p->vlm;
    *loop = p->fLoop != 0;
    if (s->publish_query)
        s->publish_query(s->query_context, *list, 1, *volume, *loop);
    s->values.release(s->values.context, p);
    return 1;
}
void mm_scene_sound_volume(MMI32 style, MMI32 volume, const MMSceneSoundEditServices *s)
{
    MMI32 i;
    int boundary;
    i = find(style, &boundary, s);
    if (i >= 0)
    {
        payload(i, s)->vlm = volume;
        s->dirty(s->context);
        return;
    }
    if (boundary)
        s->diagnostic(s->context, MM_SCENE_SOUND_MISSING);
    s->diagnostic(s->context, MM_SCENE_SOUND_MISSING);
}
MMU32 mm_scene_sound_background_offset(MMI32 frame, MMI32 first, int *overflow)
{
    MMI32 signed_frames = mm_signed_word((MMU32)frame - (MMU32)first);
    MMU32 magnitude = signed_frames < 0 ? 0U - (MMU32)signed_frames : (MMU32)signed_frames;
    MMU32 q = magnitude / 6U, r = (magnitude % 6U) * 1000U / 6U,
               limit = signed_frames < 0 ? 2147483648UL : 2147483647UL;
    *overflow = q > limit / 1000U || (q == limit / 1000U && r > limit % 1000U);
    q = q * 1000U + r;
    return signed_frames < 0 ? 0U - q : q;
}
void mm_scene_sound_background_play(const MMSceneSoundEditServices *s)
{
    MMSceneSound *p = (MMSceneSound *)s->events.owner->owned[MM_SCENE_BACKGROUND_SOUND];
    MMU32 start;
    int overflow;
    if (!p)
        return;
    start = mm_scene_sound_background_offset(s->events.owner->data.currentFrame, s->events.owner->runtime.background_frame,
                                                   &overflow);
    if (overflow)
        s->diagnostic(s->context, MM_SCENE_SOUND_OFFSET_OVERFLOW);
    mm_scene_sound_play(p, start, &s->values.packets);
}
void mm_scene_sound_undo_init(MMSceneSoundUndo *p)
{
    p->sound = NULL;
    p->style = 0;
}
void mm_scene_sound_undo_dispose(MMSceneSoundUndo *p, const MMSceneSoundValueServices *v)
{
    if (p->sound)
    {
        v->release(v->context, p->sound);
        p->sound = NULL;
    }
}
int mm_scene_sound_undo_set(MMSceneSoundUndo *p, const MMSceneSound *sound,
                                  const MMSceneSoundValueServices *v)
{
    MMSceneSound *copy;
    if (!sound)
        return 0;
    copy = mm_scene_sound_duplicate(sound, &v->packets);
    if (!copy)
        return 0;
    mm_scene_sound_undo_dispose(p, v);
    p->sound = copy;
    p->style = copy->sty;
    return 1;
}
int mm_scene_sound_add_undo(int loop, int queue, MMI32 volume, MMI32 style, MMTag *tag,
                                  const MMSceneSoundEditServices *s)
{
    void *undo = s->new_undo(s->context);
    MMSceneSound *p = NULL;
    int found;
    MMSceneSoundUndo *state;
    if (!undo)
        return 0;
    state = s->undo_state(s->context, undo);
    state->style = style;
    if (!mm_scene_sound_get(style, &found, &p, s))
    {
        s->release_undo(s->context, undo);
        return 0;
    }
    if (found)
    {
        if (!mm_scene_sound_undo_set(state, p, &s->values))
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
    if (!mm_scene_sound_add(loop, queue, volume, style, 1, tag, s))
    {
        s->clear_undo(s->context);
        return 0;
    }
    return 1;
}
int mm_scene_sound_remove_undo(MMI32 style, const MMSceneSoundEditServices *s)
{
    void *undo = s->new_undo(s->context);
    MMSceneSound *p = NULL;
    int found;
    if (!undo)
        return 0;
    if (!mm_scene_sound_get(style, &found, &p, s))
    {
        s->release_undo(s->context, undo);
        return 0;
    }
    if (!found)
    {
        s->diagnostic(s->context, MM_SCENE_SOUND_STYLE_MISSING);
        s->release_undo(s->context, undo);
        return 0;
    }
    if (!mm_scene_sound_undo_set(s->undo_state(s->context, undo), p, &s->values))
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
    mm_scene_sound_remove(style, s);
    return 1;
}
int mm_scene_sound_undo_swap(MMSceneSoundUndo *state, const MMSceneSoundUndoServices *s)
{
    MMSceneSound *current = NULL;
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
        mm_scene_sound_undo_dispose(state, &s->values);
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
            s->diagnostic(s->context, MM_SCENE_SOUND_MISSING);
            return 0;
        }
        if (!mm_scene_sound_undo_set(state, current, &s->values))
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
