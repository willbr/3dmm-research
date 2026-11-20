/* Actor sound editing converted from engine actrsnd.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/actor_sound_edit.h"
#include <string.h>
static int cel(const MMActorState *state, MMI32 *out, const MMActorSoundEditServices *s)
{
    MMI32 count = 0;
    return s->runtime.cel_count(s->runtime.context, state->action, &count) &&
           mm_actor_sound_cel(state->cel, count, out);
}
static void remove_event(MMActorState *state, void *events, MMI32 i,
                         const MMActorMotionServices *m, const MMActorSoundEditServices *s)
{
    mm_actor_edit_remove(state, events, i, 1, m, &s->edits);
}
void mm_actor_sound_remove_motion(MMActorState *state, void *events, MMI32 action,
                                        const MMActorMotionServices *m, const MMActorSoundEditServices *s)
{
    MMI32 i, value[2];
    MMActorEvent event;
    MMActorSound sound;
    for (i = state->nextEvent; i < m->event_count(events); ++i)
    {
        m->event(events, i, &event);
        if (event.aet == MM_ACTOR_ADD) break;
        if (event.aet == MM_ACTOR_ACTION)
        {
            m->payload(events, i, value);
            if (value[0] != action) break;
            continue;
        }
        if (event.aet != MM_ACTOR_SOUND) continue;
        m->payload(events, i, &sound);
        if (sound.cel == -1) continue;
        remove_event(state, events, i, m, s);
        --i;
    }
}
int mm_actor_sound_volume(MMActorState *state, MMActorSounds *list, void *events,
                                MMI32 style, int motion, MMI32 volume,
                                const MMActorMotionServices *m, const MMActorSoundEditServices *s)
{
    MMI32 frame, i;
    MMU32 j;
    MMActorEvent event;
    MMActorSound sound;
    MMActorMotionSound record;
    const MMActorEditTables *tables = &s->edits.tables;
    int action_this_frame = 0;
    if (!cel(state, &frame, s)) return 0;
    for (i = state->firstEventInFrame; i < state->nextEvent; ++i)
    {
        m->event(events, i, &event);
        if (event.aet == MM_ACTOR_ACTION) action_this_frame = 1;
        if (event.aet != MM_ACTOR_SOUND) continue;
        m->payload(events, i, &sound);
        if ((sound.cel != -1) != (motion != 0) || sound.style != style) continue;
        sound.volume = volume;
        tables->write_payload(tables->context, i, &sound);
    }
    if (motion && !action_this_frame)
        for (i = state->firstEventInFrame - 1; i > 0; --i)
        {
            m->event(events, i, &event);
            if (event.aet == MM_ACTOR_ACTION) break;
            if (event.aet != MM_ACTOR_SOUND) continue;
            m->payload(events, i, &sound);
            if (sound.style != style || sound.cel != frame) continue;
            sound.volume = volume;
            tables->write_payload(tables->context, i, &sound);
            break;
        }
    if (motion)
        for (j = 0; j < list->count; ++j)
        {
            mm_actor_sounds_get(list, j, &record);
            if (record.sound.cel != frame || record.sound.style != style) continue;
            record.sound.volume = volume;
            mm_actor_sounds_put(list, j, &record);
        }
    s->dirty(s->context);
    return 1;
}
static int append(void *query, MMActorSound *sound, MMActorSoundQuery *out,
                   const MMActorSoundEditServices *s)
{
    if (!sound->tag.sid && !s->runtime.resolve(s->runtime.context, &sound->tag, sound->child)) return 0;
    if (!s->query_add(s->context, query, &sound->tag)) return 0;
    out->volume = sound->volume;
    out->loop = sound->loop != 0;
    if (out->publish) out->publish(out->context, out->volume, out->loop);
    return 1;
}
int mm_actor_sound_query(const MMActorState *state, const MMActorSounds *list, void *events,
                               MMI32 style, int motion, MMActorSoundQuery *out,
                               const MMActorMotionServices *m, const MMActorSoundEditServices *s)
{
    void *query = s->query_new(s->context);
    MMActorSound sound;
    MMActorMotionSound record;
    MMActorEvent event;
    MMI32 i, frame;
    MMU32 j, count = 0;
    if (!query) return 0;
    if (!motion)
        for (i = state->firstEventInFrame; i < state->nextEvent; ++i)
        {
            m->event(events, i, &event);
            if (event.aet != MM_ACTOR_SOUND) continue;
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
            mm_actor_sounds_get(list, j, &record);
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
int mm_actor_sound_delete(MMActorState *state, MMActorSounds *list, void *events,
                                MMI32 style, int motion, const MMActorMotionServices *m,
                                const MMActorSoundEditServices *s)
{
    MMI32 frame, first, i, last_frame = -1;
    MMU32 j;
    MMActorEvent event;
    MMActorSound sound;
    MMActorMotionSound record;
    if (!cel(state, &frame, s)) return 0;
    first = motion ? state->actionEvent : state->firstEventInFrame;
    for (i = state->nextEvent - 1; i >= first; --i)
    {
        m->event(events, i, &event);
        if (motion && last_frame != -1 && last_frame > event.nfrm) break;
        if (event.aet != MM_ACTOR_SOUND) continue;
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
            mm_actor_sounds_get(list, j, &record);
            if (record.sound.style == style && record.sound.cel == frame)
                mm_actor_sounds_delete(list, j);
            else ++j;
        }
    }
    s->dirty(s->context);
    return 1;
}
int mm_actor_sound_in_frame(const MMActorState *state, void *events, MMI32 scene_frame,
                                  const MMActorMotionServices *m)
{
    MMActorEvent event;
    MMI32 i;
    if (scene_frame != state->frame) return 0;
    for (i = state->firstEventInFrame; i < state->nextEvent; ++i)
    {
        m->event(events, i, &event);
        if (event.aet == MM_ACTOR_SOUND) return 1;
    }
    return 0;
}
int mm_actor_sound_resolve_all(void *events, MMU32 scene, const MMActorMotionServices *m,
                                     const MMActorSoundEditServices *s)
{
    MMI32 i;
    MMActorEvent event;
    MMActorSound sound;
    const MMActorEditTables *tables = &s->edits.tables;
    int success = 1;
    for (i = 0; i < m->event_count(events); ++i)
    {
        m->event(events, i, &event);
        if (event.aet != MM_ACTOR_SOUND) continue;
        m->payload(events, i, &sound);
        if (sound.tag.sid) continue;
        if (!s->resolve_scene(s->context, &sound.tag, sound.child, scene)) { success = 0; continue; }
        tables->write_payload(tables->context, i, &sound);
    }
    return success;
}
int mm_actor_sound_set(MMActorState *state, MMActorSounds *list, void *events, MMI32 actor,
                             const MMTag *tag, MMI32 loop, MMI32 queue, int motion,
                             MMI32 volume, MMI32 style, MMU32 external_child,
                             const MMActorMotionServices *m, const MMActorSoundEditServices *s)
{
    MMActorSoundInfo info;
    MMActorSound sound, later;
    MMActorEvent event;
    MMI32 i, number;
    void *resource = s->runtime.fetch(s->runtime.context, tag);
    if (!resource) return 0;
    s->sound_info(s->context, resource, &info);
    if (info.style == 4) { s->midi_error(s->context); goto fail; }
    if (!mm_actor_edit_reserve(&s->edits, 2, 4 + sizeof(MMActorSound))) goto fail;
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
    if (!mm_actor_edit_add_apply(state,list,events,actor,MM_ACTOR_SOUND,sizeof(sound),&sound,m,&s->edits)) goto fail;
    s->runtime.release(s->runtime.context, resource);
    number = mm_sound_queue_number(sound.style, actor, 1);
    for (i = state->nextEvent; i < m->event_count(events); ++i)
    {
        m->event(events, i, &event);
        if (event.aet == MM_ACTOR_ADD) break;
        if (event.aet != MM_ACTOR_SOUND) continue;
        m->payload(events, i, &later);
        if (mm_sound_queue_number(later.style, actor, 1) != number) continue;
        if (!later.no_sound) break;
        remove_event(state, events, i, m, s); --i;
    }
    if (!s->sounds_suppressed(s->context))
        mm_actor_sound_motion(list, state->action, state->cel, &s->runtime);
    s->set_sound_frame(s->context);
    return 1;
fail:
    s->runtime.release(s->runtime.context, resource);
    return 0;
}
int mm_actor_sound_defaults(MMActorState *state, MMActorSounds *list, void *events, MMI32 actor,
                                  MMI32 action, MMU32 external_child, const MMActorMotionServices *m,
                                  const MMActorSoundEditServices *s)
{
    MMI32 i, count = 0;
    MMTag tag;
    MMActorSoundInfo info;
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
        success = mm_actor_sound_set(state, list, events, actor, &tag, 0, info.volume, 0, info.style, 1,
                                           external_child, m, s);
        s->runtime.release(s->runtime.context, resource);
        if (!success) return 0;
    }
    return 1;
}
