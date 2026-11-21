/* Actor paste policy converted from engine actredit.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "mm/actor_paste.h"
#include "mm/actor_effect.h"

static int transfer_template(MMActorRuntime *r, void *scene, const MMActorPasteServices *s)
{
    const MMActorOwnerServices *owner = &s->create.owner;
    MMTag tag;
    void *cache, *object;
    if (r->template_tag.sid != 0)
        s->diagnostic(s->context, MM_PASTE_TEXT_NOT_DOCUMENT);
    if (!s->ensure_autosave(s->context, scene, &cache))
        return 0;
    if (cache == r->template_tag.cache)
        return 1;

    tag = r->template_tag;
    mm_tag_dup(&tag, owner->tags);
    if (!mm_tag_save(&tag, cache, 1, owner->tags))
    {
        mm_tag_close(&tag, owner->tags);
        return 0;
    }
    object = s->create.fetch_template(owner->context, &tag);
    if (!object)
    {
        mm_tag_close(&tag, owner->tags);
        return 0;
    }
    mm_tag_close(&r->template_tag, owner->tags);
    r->template_tag = tag;
    owner->release(owner->context, r->template_object);
    r->template_object = object;
    return 1;
}

int mm_actor_paste(MMActorOwner *actor, MMI32 frame, void *scene, const MMActorPasteServices *s)
{
    MMActorRuntime *r = &actor->runtime;
    MMActorEventTable *events = actor->track.events;
    MMActorEditServices edits = s->edits;
    const MMActorMotionServices motion = mm_actor_track_motion_services();
    MMActorEvent event;
    MMActorRoutePoint point;
    MMActorEntrance entrance;
    MMActorSound sound;
    MMMovieSoundServices sounds;
    MMActorPoint placement;
    MMI32 delta, i;
    void *bound_scene;

    if (s->migrate_text && s->is_text(s->context, r->template_object) && !transfer_template(r, scene, s))
        return 0;

    r->first = frame;
    r->life_dirty = 1;
    r->state.frame = mm_fixed_sub(frame, 1);
    mm_actor_bind_scene(r, scene, &s->create);
    bound_scene = r->scene;
    s->placement(s->context, &placement);
    if (!mm_actor_events_get_fixed(events, 0, &event))
    {
        s->diagnostic(s->context, MM_PASTE_EMPTY);
        return 0;
    }
    if (event.aet != MM_ACTOR_ADD)
        return 0;
    delta = mm_fixed_sub(event.nfrm, frame);
    mm_actor_path_get(actor->track.route, 0, &point);
    r->state.subrouteTranslation.dxr = r->state.subrouteTranslation.dyr = r->state.subrouteTranslation.dzr = 0;
    mm_actor_events_get_payload(events, 0, &entrance);
    r->origin.dxr = mm_fixed_sub(mm_fixed_sub(placement.dxr, point.xyz.dxr), entrance.translation.dxr);
    r->origin.dyr = mm_fixed_sub(mm_fixed_sub(placement.dyr, point.xyz.dyr), entrance.translation.dyr);
    r->origin.dzr = mm_fixed_sub(mm_fixed_sub(placement.dzr, point.xyz.dzr), entrance.translation.dzr);

    edits.tables = mm_actor_edit_tables(events, s->create.owner.tags);
    for (i = 0; i < mm_actor_events_count(events); ++i)
    {
        mm_actor_events_get_fixed(events, i, &event);
        if (delta)
        {
            event.nfrm = mm_fixed_sub(event.nfrm, delta);
            mm_actor_events_set_fixed(events, i, &event);
        }
        if (event.aet != MM_ACTOR_SOUND)
            continue;
        mm_actor_events_get_payload(events, i, &sound);
        if (sound.tag.sid != 0)
            continue;
        if (!mm_tag_local_exists(&sound.tag, s->create.owner.tags))
        {
            s->diagnostic(s->context, MM_PASTE_MISSING_SOUND);
            mm_actor_edit_remove(&r->state, events, i, 1, &motion, &edits);
            --i;
            continue;
        }
        sounds = s->movie_sound(bound_scene);
        if (!mm_movie_sound_save(&sound.tag, &sounds))
        {
            s->diagnostic(s->context, MM_PASTE_SAVE_SOUND_FAILED);
            mm_actor_edit_remove(&r->state, events, i, 1, &motion, &edits);
            --i;
            continue;
        }
        if (!mm_movie_sound_child(sound.tag.number, &sound.child, &sounds))
            return 0;
        mm_actor_events_set_payload(events, i, &sound);
    }

    if (s->is_text(s->context, r->template_object))
    {
        mm_actor_events_get_payload(events, 0, &entrance);
        entrance.y = s->camera_yaw(s->context, r->scene);
        mm_actor_events_set_payload(events, 0, &entrance);
    }
    mm_actor_state_origin(&r->state, &r->origin);
    s->dirty(s->context, r->scene);
    return 1;
}
