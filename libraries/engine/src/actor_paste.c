/* Actor paste policy converted from engine actredit.cpp.
 * Copyright (c) Microsoft Corporation. Licensed under the MIT License. */
#include "three_mm/actor_paste.h"
int three_mm_actor_paste(void *route,void *events,ThreeMMI32 frame,void *scene,int migrate_text,
    const ThreeMMActorMotionServices *m,const ThreeMMActorPasteServices *s)
{
    ThreeMMActorRuntime r;
    ThreeMMActorEvent event;
    ThreeMMActorRoutePoint point;
    ThreeMMActorEntrance entrance;
    ThreeMMActorSound sound;
    ThreeMMActorPoint placement;
    ThreeMMTag tag;
    ThreeMMI32 delta,i;
    void *cache,*template_object;
    s->read(s->context,&r);
    if (migrate_text && s->is_text(s->context,r.template_object))
    {
        if (r.template_tag.sid!=0) s->diagnostic(s->context,THREE_MM_PASTE_TEXT_NOT_DOCUMENT);
        if (!s->ensure_autosave(s->context,scene,&cache)) return 0;
        if (cache!=r.template_tag.cache)
        {
            tag=r.template_tag; s->duplicate_tag(s->context,&tag);
            if (!s->save_template(s->context,&tag,cache))
            { s->close_tag(s->context,&tag); return 0; }
            template_object=s->fetch_template(s->context,&tag);
            if (!template_object) { s->close_tag(s->context,&tag); return 0; }
            s->close_tag(s->context,&r.template_tag);
            r.template_tag=tag; s->write(s->context,&r);
            s->release_template(s->context,r.template_object);
            r.template_object=template_object; s->write(s->context,&r);
        }
    }
    r.first=frame; r.life_dirty=1; r.state.frame=three_mm_fixed_sub(frame,1);
    s->write(s->context,&r); s->bind_scene(s->context,scene);
    s->read(s->context,&r); s->placement(s->context,&placement);
    if (m->event_count(events)<=0) s->diagnostic(s->context,THREE_MM_PASTE_EMPTY);
    m->event(events,0,&event);
    if (event.aet!=THREE_MM_ACTOR_ADD) return 0;
    delta=three_mm_fixed_sub(event.nfrm,frame);
    m->route_point(route,0,&point);
    r.state.subrouteTranslation.dxr=r.state.subrouteTranslation.dyr=r.state.subrouteTranslation.dzr=0;
    m->payload(events,0,&entrance);
    r.origin.dxr=three_mm_fixed_sub(three_mm_fixed_sub(placement.dxr,point.xyz.dxr),entrance.translation.dxr);
    r.origin.dyr=three_mm_fixed_sub(three_mm_fixed_sub(placement.dyr,point.xyz.dyr),entrance.translation.dyr);
    r.origin.dzr=three_mm_fixed_sub(three_mm_fixed_sub(placement.dzr,point.xyz.dzr),entrance.translation.dzr);
    s->write(s->context,&r);
    for (i=0;i<m->event_count(events);++i)
    {
        m->event(events,i,&event);
        if (delta)
        { event.nfrm=three_mm_fixed_sub(event.nfrm,delta); s->write_event(events,i,&event); }
        if (event.aet!=THREE_MM_ACTOR_SOUND) continue;
        m->payload(events,i,&sound);
        if (sound.tag.sid!=0) continue;
        if (!s->sound_exists(s->context,&sound.tag))
        {
            s->diagnostic(s->context,THREE_MM_PASTE_MISSING_SOUND);
            s->remove_event(s->context,i); --i; continue;
        }
        if (!s->save_sound(s->context,r.scene,&sound.tag))
        {
            s->diagnostic(s->context,THREE_MM_PASTE_SAVE_SOUND_FAILED);
            s->remove_event(s->context,i); --i; continue;
        }
        if (!s->sound_child(s->context,r.scene,sound.tag.number,&sound.child)) return 0;
        s->write_payload(events,i,&sound);
    }
    /* Removal may change state/cursors. Never overwrite those changes with the
     * pre-loop runtime mirror. Query the current template after transfer. */
    s->read(s->context,&r);
    if (s->is_text(s->context,r.template_object))
    {
        m->payload(events,0,&entrance); entrance.y=s->camera_yaw(s->context,r.scene);
        s->write_payload(events,0,&entrance);
    }
    three_mm_actor_state_origin(&r.state,&r.origin); s->write(s->context,&r);
    s->dirty(s->context,r.scene); return 1;
}
