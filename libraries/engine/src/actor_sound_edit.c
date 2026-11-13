/* Actor sound editing converted from engine actrsnd.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/actor_sound_edit.h"
#include <string.h>
static int cel(const ThreeMMActorState *state, ThreeMMI32 *out, const ThreeMMActorSoundEditServices *s)
{
    ThreeMMI32 count = 0;
    return s->runtime.cel_count(s->runtime.context, state->action, &count) &&
           three_mm_actor_sound_cel(state->cel, count, out);
}
static void remove_event(ThreeMMActorState *state, void *events, ThreeMMI32 i,
                         const ThreeMMActorMotionServices *m, const ThreeMMActorSoundEditServices *s)
{
    three_mm_actor_edit_remove(state, events, i, 1, m, &s->edits);
}
void three_mm_actor_sound_remove_motion(ThreeMMActorState *state, void *events, ThreeMMI32 action,
                                        const ThreeMMActorMotionServices *m, const ThreeMMActorSoundEditServices *s)
{
    ThreeMMI32 i, value[2];
    ThreeMMActorEvent event;
    ThreeMMActorSound sound;
    for (i = state->nextEvent; i < m->event_count(events); ++i)
    {
        m->event(events, i, &event);
        if (event.aet == THREE_MM_ACTOR_ADD) break;
        if (event.aet == THREE_MM_ACTOR_ACTION)
        {
            m->payload(events, i, value);
            if (value[0] != action) break;
            continue;
        }
        if (event.aet != THREE_MM_ACTOR_SOUND) continue;
        m->payload(events, i, &sound);
        if (sound.cel == -1) continue;
        remove_event(state, events, i, m, s);
        --i;
    }
}
int three_mm_actor_sound_volume(ThreeMMActorState *state, ThreeMMActorSounds *list, void *events,
                                ThreeMMI32 style, int motion, ThreeMMI32 volume,
                                const ThreeMMActorMotionServices *m, const ThreeMMActorSoundEditServices *s)
{
    ThreeMMI32 frame, i;
    ThreeMMU32 j;
    ThreeMMActorEvent event;
    ThreeMMActorSound sound;
    ThreeMMActorMotionSound record;
    int action_this_frame = 0;
    if (!cel(state, &frame, s)) return 0;
    for (i = state->firstEventInFrame; i < state->nextEvent; ++i)
    {
        m->event(events, i, &event);
        if (event.aet == THREE_MM_ACTOR_ACTION) action_this_frame = 1;
        if (event.aet != THREE_MM_ACTOR_SOUND) continue;
        m->payload(events, i, &sound);
        if ((sound.cel != -1) != (motion != 0) || sound.style != style) continue;
        sound.volume = volume;
        s->write_sound(s->context, i, &sound);
    }
    if (motion && !action_this_frame)
        for (i = state->firstEventInFrame - 1; i > 0; --i)
        {
            m->event(events, i, &event);
            if (event.aet == THREE_MM_ACTOR_ACTION) break;
            if (event.aet != THREE_MM_ACTOR_SOUND) continue;
            m->payload(events, i, &sound);
            if (sound.style != style || sound.cel != frame) continue;
            sound.volume = volume;
            s->write_sound(s->context, i, &sound);
            break;
        }
    if (motion)
        for (j = 0; j < list->count; ++j)
        {
            three_mm_actor_sounds_get(list, j, &record);
            if (record.sound.cel != frame || record.sound.style != style) continue;
            record.sound.volume = volume;
            three_mm_actor_sounds_put(list, j, &record);
        }
    s->dirty(s->context);
    return 1;
}
static int append(void *query, ThreeMMActorSound *sound, ThreeMMActorSoundQuery *out,
                   const ThreeMMActorSoundEditServices *s)
{
    if (!sound->tag.sid && !s->runtime.resolve(s->runtime.context, &sound->tag, sound->child)) return 0;
    if (!s->query_add(s->context, query, &sound->tag)) return 0;
    out->volume = sound->volume;
    out->loop = sound->loop != 0;
    if (out->publish) out->publish(out->context, out->volume, out->loop);
    return 1;
}
int three_mm_actor_sound_query(const ThreeMMActorState *state, const ThreeMMActorSounds *list, void *events,
                               ThreeMMI32 style, int motion, ThreeMMActorSoundQuery *out,
                               const ThreeMMActorMotionServices *m, const ThreeMMActorSoundEditServices *s)
{
    void *query = s->query_new(s->context);
    ThreeMMActorSound sound;
    ThreeMMActorMotionSound record;
    ThreeMMActorEvent event;
    ThreeMMI32 i, frame;
    ThreeMMU32 j, count = 0;
    if (!query) return 0;
    if (!motion)
        for (i = state->firstEventInFrame; i < state->nextEvent; ++i)
        {
            m->event(events, i, &event);
            if (event.aet != THREE_MM_ACTOR_SOUND) continue;
            m->payload(events, i, &sound);
            if (sound.style != style || sound.cel != -1) continue;
            if (!append(query, &sound, out, s)) goto fail;
            ++count;
        }
    if (motion)
    {
        if (!cel(state, &frame, s)) goto fail;
        for (j = 0; j < list->count; ++j)
        {
            three_mm_actor_sounds_get(list, j, &record);
            if (record.sound.style != style || record.sound.cel != frame) continue;
            if (!append(query, &record.sound, out, s)) goto fail;
            ++count;
            break;
        }
    }
    if (!count) { s->query_release(s->context, query); query = 0; }
    out->tags = query;
    return 1;
fail:
    s->query_release(s->context, query);
    return 0;
}
int three_mm_actor_sound_delete(ThreeMMActorState *state, ThreeMMActorSounds *list, void *events,
                                ThreeMMI32 style, int motion, const ThreeMMActorMotionServices *m,
                                const ThreeMMActorSoundEditServices *s)
{
    ThreeMMI32 frame, first, i, last_frame = -1;
    ThreeMMU32 j;
    ThreeMMActorEvent event;
    ThreeMMActorSound sound;
    ThreeMMActorMotionSound record;
    if (!cel(state, &frame, s)) return 0;
    first = motion ? state->actionEvent : state->firstEventInFrame;
    for (i = state->nextEvent - 1; i >= first; --i)
    {
        m->event(events, i, &event);
        if (motion && last_frame != -1 && last_frame > event.nfrm) break;
        if (event.aet != THREE_MM_ACTOR_SOUND) continue;
        m->payload(events, i, &sound);
        if (sound.style != style || ((sound.cel != -1) != (motion != 0))) continue;
        if (motion && sound.cel != frame) continue;
        remove_event(state, events, i, m, s);
        last_frame = event.nfrm;
    }
    if (motion)
    {
        if (!cel(state, &frame, s)) return 0;
        j = 0;
        while (j < list->count)
        {
            three_mm_actor_sounds_get(list, j, &record);
            if (record.sound.style == style && record.sound.cel == frame)
                three_mm_actor_sounds_delete(list, j);
            else ++j;
        }
    }
    s->dirty(s->context);
    return 1;
}
int three_mm_actor_sound_in_frame(const ThreeMMActorState *state, void *events, ThreeMMI32 scene_frame,
                                  const ThreeMMActorMotionServices *m)
{
    ThreeMMActorEvent event;
    ThreeMMI32 i;
    if (scene_frame != state->frame) return 0;
    for (i = state->firstEventInFrame; i < state->nextEvent; ++i)
    {
        m->event(events, i, &event);
        if (event.aet == THREE_MM_ACTOR_SOUND) return 1;
    }
    return 0;
}
int three_mm_actor_sound_resolve_all(void *events, ThreeMMU32 scene, const ThreeMMActorMotionServices *m,
                                     const ThreeMMActorSoundEditServices *s)
{
    ThreeMMI32 i;
    ThreeMMActorEvent event;
    ThreeMMActorSound sound;
    int success = 1;
    for (i = 0; i < m->event_count(events); ++i)
    {
        m->event(events, i, &event);
        if (event.aet != THREE_MM_ACTOR_SOUND) continue;
        m->payload(events, i, &sound);
        if (sound.tag.sid) continue;
        if (!s->resolve_scene(s->context, &sound.tag, sound.child, scene)) { success = 0; continue; }
        s->write_sound(s->context, i, &sound);
    }
    return success;
}
int three_mm_actor_sound_set(ThreeMMActorState *state, ThreeMMActorSounds *list, void *events, ThreeMMI32 actor,
                             const ThreeMMTag *tag, ThreeMMI32 loop, ThreeMMI32 queue, int motion,
                             ThreeMMI32 volume, ThreeMMI32 style, ThreeMMU32 external_child,
                             const ThreeMMActorMotionServices *m, const ThreeMMActorSoundEditServices *s)
{
    ThreeMMActorSoundInfo info;
    ThreeMMActorSound sound, later;
    ThreeMMActorEvent event;
    ThreeMMI32 i, number;
    void *resource = s->runtime.fetch(s->runtime.context, tag);
    if (!resource) return 0;
    s->sound_info(s->context, resource, &info);
    if (info.style == 4) { s->midi_error(s->context); goto fail; }
    if (!s->reserve_events(s->context, 2, 4 + sizeof(ThreeMMActorSound))) goto fail;
    memset(&sound, 0, sizeof(sound));
    sound.loop = loop; sound.queue = queue;
    sound.volume = volume == -1 ? info.volume : volume;
    sound.style = style == 0 ? info.style : style;
    sound.no_sound = info.no_sound;
    sound.tag = *tag;
    sound.child = external_child;
    if (!tag->sid && !s->sound_child(s->context, tag->number, &sound.child)) goto fail;
    sound.cel = -1;
    if (motion && !cel(state, &sound.cel, s)) goto fail;
    if (!s->insert_apply(s->context, state, &sound)) goto fail;
    s->runtime.release(s->runtime.context, resource);
    number = three_mm_sound_queue_number(sound.style, actor, 1);
    for (i = state->nextEvent; i < m->event_count(events); ++i)
    {
        m->event(events, i, &event);
        if (event.aet == THREE_MM_ACTOR_ADD) break;
        if (event.aet != THREE_MM_ACTOR_SOUND) continue;
        m->payload(events, i, &later);
        if (three_mm_sound_queue_number(later.style, actor, 1) != number) continue;
        if (!later.no_sound) break;
        remove_event(state, events, i, m, s); --i;
    }
    if (!s->sounds_suppressed(s->context))
        three_mm_actor_sound_motion(list, state->action, state->cel, &s->runtime);
    s->set_sound_frame(s->context);
    return 1;
fail:
    s->runtime.release(s->runtime.context, resource);
    return 0;
}
int three_mm_actor_sound_defaults(ThreeMMActorState *state, ThreeMMActorSounds *list, void *events, ThreeMMI32 actor,
                                  ThreeMMI32 action, ThreeMMU32 external_child, const ThreeMMActorMotionServices *m,
                                  const ThreeMMActorSoundEditServices *s)
{
    ThreeMMI32 i, count = 0;
    ThreeMMTag tag;
    ThreeMMActorSoundInfo info;
    void *resource;
    int exists, success;
    if (!s->runtime.cel_count(s->runtime.context, action, &count)) return 0;
    for (i = 0; i < count; ++i)
    {
        if (!s->default_sound(s->context, action, i, &exists, &tag) || !exists) continue;
        resource = s->runtime.fetch(s->runtime.context, &tag);
        if (!resource) continue;
        s->sound_info(s->context, resource, &info);
        /* Original positional call: queue=volume, ordinary, volume=style,
         * style=tYes. Do not silently correct this while migrating policy. */
        success = three_mm_actor_sound_set(state, list, events, actor, &tag, 0, info.volume, 0, info.style, 1,
                                           external_child, m, s);
        s->runtime.release(s->runtime.context, resource);
        if (!success) return 0;
    }
    return 1;
}
